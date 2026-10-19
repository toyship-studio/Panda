#include "Panda/Renderer/ShaderInterface/ShaderInterface.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"

#include <Foundation/Foundation.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Panda::ShaderInterface {

namespace {

    // HLSL type by Rain TypeId. Only the types that cbuffer and std140 lay out identically are
    // supported: scalars (4 bytes, alignment 4), 16-byte float4/uint4/int4 and a 4×4 matrix
    // (64 bytes, alignment 16); float2/float3 are rejected — cbuffer and std140 pack them
    // differently, and arrays of scalars take 16 bytes per element in a cbuffer.
    struct HlslType {
        const char *name;
        uint32_t size;
    };

    template<typename T>
    Rain::TypeId typeId() {
        return Rain::getTypeRegistry()->findOrCreateType<T>().id;
    }

    const std::unordered_map<Rain::TypeId, HlslType> &hlslTypes() {
        static const std::unordered_map<Rain::TypeId, HlslType> table = {
            {typeId<float>(), {"float", 4}},
            {typeId<uint32_t>(), {"uint", 4}},
            {typeId<int32_t>(), {"int", 4}},
            {typeId<glm::vec4>(), {"float4", 16}},
            {typeId<glm::uvec4>(), {"uint4", 16}},
            {typeId<glm::ivec4>(), {"int4", 16}},
            {typeId<glm::mat4>(), {"float4x4", 64}},
        };
        return table;
    }

    struct Block {
        const char *cbufferName;
        const char *structName;
        const char *fileName;
        Rain::TypeInfo info;
    };

    // The block meta comes straight from the struct (RAIN_FIELDS + sizeof): UBOs are not
    // registered in the Rain registry, they are not serialized.
    template<typename T>
    Block block(const char *cbufferName, const char *structName, const char *fileName) {
        Rain::TypeInfo info(0, structName);
        info.size = sizeof(T);
        info.fields = T::getFields();
        return {cbufferName, structName, fileName, info};
    }

    std::vector<Block> blocks() {
        return {
            block<SceneLightsUbo>("SCENE_LIGHTS", "SceneLightsUbo", "panda/scene_lights.hlsli"),
            block<PandaFieldsUbo>("PANDA_FIELDS", "PandaFieldsUbo", "panda/panda_fields.hlsli"),
            block<VertexUbo>("UBO_VERT", "VertexUbo", "panda/vertex_ubo.hlsli"),
            block<PostDataUbo>("POST_DATA", "PostDataUbo", "panda/post_data.hlsli"),
        };
    }

    std::string format(const char *fmt, const char *a, const char *b, uint32_t x, uint32_t y) {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), fmt, a, b, x, y);
        return buffer;
    }

    // The cbuffer text from the meta: the offset of every field is computed by the cbuffer rules
    // and must match the offset in C++ — otherwise the struct is bound where the shader does not read.
    bool generateBlock(const Block &block, std::string &text, std::string &error) {
        const auto &types = hlslTypes();
        std::string body;
        uint32_t offset = 0;
        for (const Rain::FieldInfo &field : block.info.fields) {
            const auto it = types.find(field.elementTypeId);
            if (it == types.end()) {
                error = format(
                    "%s.%s: unsupported type (allowed: float, uint, int, float4, uint4, int4, float4x4 and arrays)",
                    block.structName, field.name, 0, 0
                );
                return false;
            }
            const HlslType &type = it->second;
            const bool scalar = type.size == 4;
            if (scalar && field.isArray()) {
                error = format(
                    "%s.%s: an array of scalars takes 16 bytes per element in a cbuffer — declare float4",
                    block.structName, field.name, 0, 0
                );
                return false;
            }
            const uint32_t align = scalar ? 4 : 16;
            offset = (offset + align - 1) / align * align;
            if (offset != field.offset) {
                error = format(
                    "%s.%s: offset in C++ is %u, by the cbuffer rules %u (alignas(16) needed?)", block.structName,
                    field.name, field.offset, offset
                );
                return false;
            }
            body += "    ";
            body += type.name;
            body += ' ';
            body += field.name;
            if (field.isArray()) { body += "[" + std::to_string(field.count) + "]"; }
            body += ";\n";
            offset += type.size * (field.isArray() ? field.count : 1);
        }
        const uint32_t size = (offset + 15) / 16 * 16;
        if (size != block.info.size) {
            error = format(
                "%s%s: struct size in C++ is %u, by the cbuffer rules %u", block.structName, "", block.info.size, size
            );
            return false;
        }
        text = "// Generated from the C++ struct Panda::";
        text += block.structName;
        text += " (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):\n";
        text += "// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning\n";
        text += "// and layout of the fields are described in the struct; new fields go at the end only.\n";
        text += "cbuffer ";
        text += block.cbufferName;
        text += " {\n";
        text += body;
        text += "};\n";
        return true;
    }

    std::string normalizeNewlines(std::string text) {
        std::string result;
        result.reserve(text.size());
        for (const char c : text) {
            if (c != '\r') { result += c; }
        }
        return result;
    }

} // namespace

std::vector<Include> generate(std::string &error) {
    std::vector<Include> includes;
    for (const Block &block : blocks()) {
        Include include;
        include.fileName = block.fileName;
        if (!generateBlock(block, include.text, error)) { return {}; }
        includes.push_back(std::move(include));
    }
    return includes;
}

bool writeIncludes(const path_t &includeRoot, std::string &error) {
    const std::vector<Include> includes = generate(error);
    if (!error.empty()) { return false; }
    for (const Include &include : includes) {
        const path_t path = includeRoot / include.fileName;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "failed to write " + path.string();
            return false;
        }
        file << include.text;
    }
    return true;
}

std::string checkIncludes(const path_t &includeRoot) {
    std::string error;
    const std::vector<Include> includes = generate(error);
    if (!error.empty()) { return "Shader include generation: " + error; }
    for (const Include &include : includes) {
        const path_t path = includeRoot / include.fileName;
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        if (file.is_open()) { buffer << file.rdbuf(); }
        if (!file.is_open() || normalizeNewlines(buffer.str()) != include.text) {
            return "Shader include " + include.fileName +
                   " does not match the C++ struct (ShaderBlocks.hpp): regenerate it — "
                   "PandaSmoke --write-shader-includes Panda/resources/default-shaders/include";
        }
    }
    return {};
}

} // namespace Panda::ShaderInterface
