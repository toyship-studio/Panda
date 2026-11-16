#include "Panda/Assets/Base/CookedMesh.hpp"

#include <cstdint>
#include <cstring>

namespace Panda::CookedMesh {

namespace {

    constexpr char MAGIC[4] = {'P', 'M', 'S', 'H'};
    constexpr uint32_t VERSION = 1;
    // What a file may claim: a count beyond this is a damaged file, not a model.
    constexpr uint32_t MAX_COUNT = 64 * 1024 * 1024;

    struct Header {
        char magic[4];
        uint32_t version;
        uint32_t vertexSize; // sizeof(Vertex): the arrays are the engine's own structs
        uint32_t meshCount;
    };

    template<typename T>
    void put(std::ostream &stream, const T &value) {
        stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
    }

    template<typename T>
    bool get(std::istream &stream, T &value) {
        stream.read(reinterpret_cast<char *>(&value), sizeof(T));
        return static_cast<bool>(stream);
    }

} // namespace

void write(std::ostream &stream, const std::vector<ImportedMeshData> &meshes) {
    Header header{};
    std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
    header.version = VERSION;
    header.vertexSize = sizeof(Vertex);
    header.meshCount = static_cast<uint32_t>(meshes.size());
    put(stream, header);
    for (const ImportedMeshData &mesh : meshes) {
        put(stream, static_cast<uint32_t>(mesh.primitives.size()));
        for (const ImportedPrimitiveData &primitive : mesh.primitives) {
            put(stream, static_cast<uint32_t>(primitive.vertices.size()));
            put(stream, static_cast<uint32_t>(primitive.indices.size()));
            stream.write(
                reinterpret_cast<const char *>(primitive.vertices.data()),
                static_cast<std::streamsize>(primitive.vertices.size() * sizeof(Vertex))
            );
            stream.write(
                reinterpret_cast<const char *>(primitive.indices.data()),
                static_cast<std::streamsize>(primitive.indices.size() * sizeof(uint32_t))
            );
        }
    }
}

bool read(std::istream &stream, std::vector<ImportedMeshData> &meshes) {
    meshes.clear();
    Header header{};
    if (!get(stream, header) || std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 || header.version != VERSION ||
        header.vertexSize != sizeof(Vertex) || header.meshCount > MAX_COUNT) {
        return false;
    }
    meshes.resize(header.meshCount);
    for (ImportedMeshData &mesh : meshes) {
        uint32_t primitiveCount = 0;
        if (!get(stream, primitiveCount) || primitiveCount > MAX_COUNT) { return false; }
        mesh.primitives.resize(primitiveCount);
        for (ImportedPrimitiveData &primitive : mesh.primitives) {
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            if (!get(stream, vertexCount) || !get(stream, indexCount) || vertexCount > MAX_COUNT || indexCount > MAX_COUNT) {
                return false;
            }
            primitive.vertices.resize(vertexCount);
            primitive.indices.resize(indexCount);
            stream.read(
                reinterpret_cast<char *>(primitive.vertices.data()), static_cast<std::streamsize>(vertexCount * sizeof(Vertex))
            );
            stream.read(
                reinterpret_cast<char *>(primitive.indices.data()), static_cast<std::streamsize>(indexCount * sizeof(uint32_t))
            );
            if (!stream) { return false; }
        }
    }
    return true;
}

} // namespace Panda::CookedMesh
