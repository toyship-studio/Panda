#pragma once

#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Runtime/AssetPack.hpp>
#include <Panda/Runtime/RuntimeManifest.hpp>
#include <Rain/Coders/BinaryEncoder.hpp>
#include <Rain/Rain.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Panda {

class AssetHandlerEditor;

struct ExportSettings {
    path_t runtimeDataDirectory; // what the Export project puts into the game: project.bin, modules.txt, Assets.pak
    path_t reportFile;           // what the cook made, for people and tools (not a part of the game)
    // The world the game starts in: `startWorld` of project.json, relative to the project.
    std::string startWorld;
    std::string appName = "Panda Game"; // the name of the save folder of the game
    // Enabled modules of the project that have an engine part (ProjectModules::enabledIds):
    // the export project links only those (modules.txt in the cooked data).
    std::vector<std::string> modules;
};

// What a cook made (report.json next to the cooked data, and the answer of the `export` tool of
// the MCP server).
struct CookReportKind final : public Rain::Codable {
    std::string kind;    // "texture", "material", …; "engine" — what the engine adds (the registry, its scene shaders)
    uint32_t assets = 0; // how many assets of the kind the game uses
    uint64_t bytes = 0;  // what they take in the pack

    RAIN_FIELDS_BEGIN(CookReportKind)
    RAIN_FIELD(kind)
    RAIN_FIELD(assets)
    RAIN_FIELD(bytes)
    RAIN_FIELDS_END
};

struct CookReportFile final : public Rain::Codable {
    std::string path; // of an entry of the pack, or of an asset in the project
    std::string kind;
    uint64_t bytes = 0;
    // What the file holds, where the cook knows it: "2048×2048 BC7, 12 mips" of a texture,
    // "3 meshes, 5 primitives, 53201 vertices, 180000 indices" of a model, "88 entities" of a
    // world. It answers why a file is as large as it is.
    std::string detail;

    RAIN_FIELDS_BEGIN(CookReportFile)
    RAIN_FIELD(path)
    RAIN_FIELD(kind)
    RAIN_FIELD(bytes)
    RAIN_FIELD(detail)
    RAIN_FIELDS_END
};

struct CookReport final : public Rain::Codable {
    std::string startWorld; // the world the game starts in, as the project names it
    std::string pack;       // the pack file
    uint64_t packBytes = 0;
    uint32_t packFiles = 0;
    std::vector<CookReportKind> kinds; // what the game uses, by kind
    std::vector<CookReportFile> files; // every file of the pack, the largest first
    // Assets of the project the game does not reach from its start world: they were left out of
    // the pack. The size is of the source file — what the project carries, not what the game
    // would have.
    std::vector<CookReportFile> unused;
    uint64_t unusedBytes = 0;
    std::vector<std::string> warnings;

    // "12.4 MB": a size as the log and the tools say it.
    static std::string sizeText(uint64_t bytes);

    RAIN_FIELDS_BEGIN(CookReport)
    RAIN_FIELD(startWorld)
    RAIN_FIELD(pack)
    RAIN_FIELD(packBytes)
    RAIN_FIELD(packFiles)
    RAIN_FIELD(kinds)
    RAIN_FIELD(files)
    RAIN_FIELD(unused)
    RAIN_FIELD(unusedBytes)
    RAIN_FIELD(warnings)
    RAIN_FIELDS_END
};

struct ExportResult {
    bool success = false;
    std::string errorMessage;
    CookReport report;
};

// The cook of a game: the cooked data the Export project builds the game from. It is a function
// of the files of the project. It finds what the game uses — everything the start world reaches
// through references: its assets, the worlds its scripts are given, and what those name in turn.
// Nothing else can be in a game: a script gets an asset from a field, never from a number in the
// code (Bamboo/Base.hpp). The cook brings each of those assets to the form the runtime loads
// (a world, a material and a postprocessing chain in binary; a texture as cooked by the import; a
// model as arrays of vertices; a shader as the text of every backend) and writes them into one
// pack (AssetPack). An asset nothing reaches stays in the project and is named in the report.
class GameExporter final {
public:
    GameExporter();
    ~GameExporter() = default;

    ExportResult exportGame(const ExportSettings &settings, const path_t &projectPath, AssetHandlerEditor &assetHandler);

    static std::vector<std::string>
    validate(const path_t &projectPath, const std::string &startWorld, AssetHandlerEditor &assetHandler);

private:
    bool createRuntimeDataDirectory(const path_t &runtimeDataDir);
    bool exportRuntimeManifest(const path_t &runtimeDataDir, const ExportSettings &settings, const path_t &projectPath);
    bool exportModuleList(const path_t &runtimeDataDir, const std::vector<std::string> &modules);
    // What the game uses: the start world, what the project includes by name, and everything
    // those name, followed until nothing new is found. A world is cooked on the way (reading it
    // is what tells what it names). false — a world of the game could not be read.
    bool collectUsage(AssetHandlerEditor &assetHandler, const ExportSettings &settings);
    bool cookWorld(AssetHandlerEditor &assetHandler, AssetId id, Rain::Encoder &references);
    void exportAssetRegistry(AssetHandlerEditor &assetHandler);
    void exportMaterials(AssetHandlerEditor &assetHandler);
    void exportPostprocessing(AssetHandlerEditor &assetHandler);
    void exportTextures(AssetHandlerEditor &assetHandler);
    void exportMeshes(AssetHandlerEditor &assetHandler);
    void exportFileBasedAssets(AssetHandlerEditor &assetHandler);
    bool exportShaders(AssetHandlerEditor &assetHandler);
    CookReport makeReport(AssetHandlerEditor &assetHandler, const path_t &packFile);

    // An id found in a document: used when it is an asset of the project.
    void use(AssetHandlerEditor &assetHandler, AssetId id);
    bool isUsed(AssetId id) const {
        return m_used.contains(id);
    }
    // A file of the pack that belongs to an asset of a kind (the report counts by kind); detail —
    // what it holds (CookReportFile::detail).
    void addBytes(const std::string &path, std::string bytes, const std::string &kind, std::string detail = {});
    void addFile(const std::string &path, const path_t &source, const std::string &kind, std::string detail = {});
    void warn(std::string text);

    Rain::BinaryEncoder m_binaryEncoder;
    AssetPack::Writer m_pack;
    std::set<AssetId> m_used;
    std::vector<AssetId> m_pending;             // used, references not followed yet
    std::map<std::string, std::string> m_kinds;   // path in the pack → kind
    std::map<std::string, std::string> m_details; // path in the pack → what the file holds
    std::vector<std::string> m_warnings;
};

} // namespace Panda
