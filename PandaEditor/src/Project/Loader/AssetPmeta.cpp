#include "AssetPmeta.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <fstream>

namespace Panda {

namespace AssetPmeta {

    path_t sidecarPathFor(const path_t &assetFilePath) {
        path_t result = assetFilePath;
        result += EXTENSION;
        return result;
    }

    bool read(const path_t &sidecarAbsPath, AssetInfo &out) {
        std::ifstream file(sidecarAbsPath);
        if (!file.is_open()) { return false; }
        Rain::JsonDecoder decoder;
        Rain::Decoder *decoderPtr = &decoder;
        decoderPtr->decode(file, out);
        // An unknown kind (not in the kind registry: the plugin is not loaded or the sidecar is
        // broken) — the sidecar is not accepted.
        return !(out.type == AssetType::NONE) && GameContext::getExtensions().assetKinds.find(out.type) != nullptr;
    }

    bool write(const path_t &sidecarAbsPath, const AssetInfo &info) {
        std::ofstream file(sidecarAbsPath);
        if (!file.is_open()) { return false; }
        Rain::JsonEncoder encoder(true);
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(file, info);
        return true;
    }

} // namespace AssetPmeta

} // namespace Panda
