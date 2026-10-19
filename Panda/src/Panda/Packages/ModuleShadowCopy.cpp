#include "Panda/Packages/ModuleShadowCopy.hpp"

#include <Foundation/Foundation.hpp>

#include <chrono>
#include <thread>

namespace Panda {

namespace {

    bool copyFileWithRetry(const path_t &from, const path_t &to) {
        // An antivirus/indexer may hold a just written or unloaded file for a moment —
        // a couple of retries makes the copying robust.
        for (int attempt = 0; attempt < 3; attempt++) {
            std::error_code error;
            if (std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

} // namespace

bool copyModuleToShadowCache(const path_t &moduleFile, const path_t &cacheDirectory, path_t &outShadowFile) {
    static uint32_t s_shadowCounter = 0; // uniqueness within the process: the files of past
                                         // sessions are not loaded and are safely overwritten
    const std::string stem = moduleFile.stem().string();
    const std::string extension = moduleFile.extension().string();
    const std::string shadowPrefix = stem + "_shadow";
    const std::string shadowStem = shadowPrefix + std::to_string(++s_shadowCounter);
    std::error_code error;
    std::filesystem::create_directories(cacheDirectory, error);
    for (const auto &entry : std::filesystem::directory_iterator(cacheDirectory, error)) {
        const std::string fileName = entry.path().filename().string();
        if (fileName.rfind(shadowPrefix, 0) == 0 && entry.path().stem().string() != shadowStem) {
            std::error_code removeError;
            std::filesystem::remove(entry.path(), removeError);
        }
    }
    const path_t shadowFile = cacheDirectory / (shadowStem + extension);
    if (!copyFileWithRetry(moduleFile, shadowFile)) {
        LOG_ERROR(
            "MODULE SHADOW COPY: can't copy %s to %s", moduleFile.string().c_str(), cacheDirectory.string().c_str()
        );
        return false;
    }
    const path_t symbols = moduleFile.parent_path() / (stem + ".pdb");
    if (std::filesystem::exists(symbols)) { copyFileWithRetry(symbols, cacheDirectory / (shadowStem + ".pdb")); }
    outShadowFile = shadowFile;
    return true;
}

} // namespace Panda
