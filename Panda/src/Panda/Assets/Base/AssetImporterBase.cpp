#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/World.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Miren/Miren.hpp>
#include <Fern/Fern.hpp>
#include <stb_image.h>
#include <glm/gtc/packing.hpp>
#include <cctype>
#include <fstream>
#include <sstream>
#include <cgltf.h>

namespace Panda {

namespace {

    constexpr uint32_t COOKED_TEXTURE_MAGIC = 0x50544558; // PTEX
    constexpr uint32_t COOKED_TEXTURE_VERSION = 1;

    struct CookedTextureHeader {
        uint32_t magic = COOKED_TEXTURE_MAGIC;
        uint32_t version = COOKED_TEXTURE_VERSION;
        uint32_t format = Miren::TextureFormat::RGBA8;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t isCubeMap = 0;
        uint32_t dataSize = 0;
    };

    FileData loadFileDataInternal(const path_t &path, bool nullTerminate) {
        const path_t fullPath = Fern::getStaticResourcesPath() / path;
#ifndef PLATFORM_ANDROID
        if (!std::filesystem::exists(fullPath)) { LOG_ERROR("FILE %s DOESN'T EXISTS", path.string().c_str()); }
#endif
        auto inputStream = Fern::createStaticResourceReader(fullPath);
        const size_t fileSize = inputStream->totalSize();
        PND_ASSERT(fileSize != 0, "UNKNOWN ASSET SIZE");
        const size_t allocatedSize = nullTerminate ? fileSize + 1 : fileSize;
        Foundation::Memory data = Foundation::Memory::alloc(static_cast<uint32_t>(allocatedSize));
        if (nullTerminate) { static_cast<uint8_t *>(data.data)[fileSize] = 0; }
        inputStream->readData(data.data, fileSize);
        Fern::disposeResourceReader(inputStream);
        return {data, static_cast<uint32_t>(fileSize)};
    }

} // namespace

void releaseImage(void *data, void *userInfo) {
    stbi_image_free(data);
}

bool AssetImporterBase::loadHeightmapSamples(
    const path_t &absolutePath, std::vector<float> &outSamples, uint32_t &outWidth, uint32_t &outHeight
) {
    const std::string pathString = absolutePath.string();
    int width = 0, height = 0, channels = 0;
    // req_comp = 1: stb itself reduces color images to grayscale (luma).
    if (stbi_is_16_bit(pathString.c_str())) {
        uint16_t *pixels = stbi_load_16(pathString.c_str(), &width, &height, &channels, 1);
        if (pixels == nullptr) { return false; }
        outSamples.resize(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < outSamples.size(); i++) {
            outSamples[i] = pixels[i] / 65535.f;
        }
        stbi_image_free(pixels);
    } else {
        uint8_t *pixels = stbi_load(pathString.c_str(), &width, &height, &channels, 1);
        if (pixels == nullptr) { return false; }
        outSamples.resize(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < outSamples.size(); i++) {
            outSamples[i] = pixels[i] / 255.f;
        }
        stbi_image_free(pixels);
    }
    outWidth = static_cast<uint32_t>(width);
    outHeight = static_cast<uint32_t>(height);
    return true;
}

Miren::TextureCreate AssetImporterBase::load2DTexture(const path_t &path) {
#ifdef PLATFORM_ANDROID
    int width, height, channels;
    auto inputStream = Fern::createStaticResourceReader(path);
    size_t fileSize = inputStream->totalSize();
    PND_ASSERT(fileSize != 0, "UNKNOWN ASSET SIZE");
    Foundation::Memory textureMem = Foundation::Memory::alloc(fileSize);
    inputStream->readData(textureMem.data, fileSize);
    void *image = stbi_load_from_memory((uint8_t *)textureMem.data, fileSize, &width, &height, &channels, 4);
    textureMem.release();
    Fern::disposeResourceReader(inputStream);
    if (image == nullptr) {
        LOG_ERROR("Failed to load a texture file! %s", stbi_failure_reason());
        return {};
    }
    Miren::TextureCreate texture;
    texture.m_format = Miren::TextureFormat::RGBA8;
    texture.m_width = width;
    texture.m_height = height;
    texture.m_data = Foundation::Memory(image);
    texture.m_data.releaseFn = &releaseImage;
    texture.m_wrap = Miren::TextureWrapMode::REPEAT;
    texture.m_isCubeMap = false;
    return texture;
#else
    return load2DTextureFile(Fern::getStaticResourcesPath() / path);
#endif
}

Miren::TextureCreate AssetImporterBase::load2DTextureFile(const path_t &fullPath) {
    // stbi_set_flip_vertically_on_load(true);
    int width, height, channels;
    // Radiance .hdr (environment maps): linear float → RGBA16F. Half — clamp to its range (a sun
    // in an HDRI frame brighter than 65504 is never useful — the bloom is saturated anyway).
    if (stbi_is_hdr(fullPath.string().c_str())) {
        float *pixels = stbi_loadf(fullPath.string().c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            LOG_ERROR("Failed to load an HDR texture file! %s", stbi_failure_reason());
            return {};
        }
        const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        Foundation::Memory halfData = Foundation::Memory::alloc(count * sizeof(uint16_t));
        uint16_t *half = static_cast<uint16_t *>(halfData.data);
        for (size_t i = 0; i < count; i++) { half[i] = glm::packHalf1x16(glm::clamp(pixels[i], 0.f, 65504.f)); }
        stbi_image_free(pixels);
        Miren::TextureCreate texture;
        texture.m_format = Miren::TextureFormat::RGBA16F;
        texture.m_width = width;
        texture.m_height = height;
        texture.m_data = halfData;
        texture.m_wrap = Miren::TextureWrapMode::REPEAT;
        texture.m_isCubeMap = false;
        return texture;
    }
    void *image = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
    if (image == nullptr) {
        LOG_ERROR("Failed to load a texture file! %s", stbi_failure_reason());
        // TODO: Return "Missing" texture
        // https://ru.yougile.com/team/91006f9f80d3/#PANDA-41
        return {};
    }

    Miren::TextureCreate texture;
    texture.m_format = Miren::TextureFormat::RGBA8;
    texture.m_width = width;
    texture.m_height = height;
    texture.m_data = Foundation::Memory(image);
    texture.m_data.releaseFn = &releaseImage;
    texture.m_wrap = Miren::TextureWrapMode::REPEAT;
    texture.m_isCubeMap = false;
    return texture;
}

Miren::TextureCreate AssetImporterBase::loadCookedTexture(const path_t &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("FAILED TO OPEN COOKED TEXTURE %s", path.string().c_str());
        return {};
    }

    CookedTextureHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || header.magic != COOKED_TEXTURE_MAGIC || header.version != COOKED_TEXTURE_VERSION) {
        LOG_ERROR("INVALID COOKED TEXTURE HEADER %s", path.string().c_str());
        return {};
    }

    Miren::TextureCreate texture;
    texture.m_format = static_cast<Miren::TextureFormat>(header.format);
    texture.m_width = static_cast<uint16_t>(header.width);
    texture.m_height = static_cast<uint16_t>(header.height);
    texture.m_isCubeMap = header.isCubeMap != 0;
    texture.m_data = Foundation::Memory::alloc(header.dataSize);

    file.read(reinterpret_cast<char *>(texture.m_data.data), static_cast<std::streamsize>(header.dataSize));
    if (!file) {
        LOG_ERROR("FAILED TO READ COOKED TEXTURE DATA %s", path.string().c_str());
        texture.m_data.release();
        return {};
    }

    return texture;
}

bool AssetImporterBase::saveCookedTexture(const path_t &path, const Miren::TextureCreate &create) {
    std::filesystem::create_directories(path.parent_path());

    const uint32_t numSides = create.m_isCubeMap ? 6u : 1u;
    const size_t dataSize = static_cast<size_t>(create.bytesPerTexel()) * create.m_width * create.m_height * numSides;
    if (!create.m_data.data || dataSize == 0 || dataSize > UINT32_MAX) {
        LOG_ERROR("INVALID COOKED TEXTURE DATA %s", path.string().c_str());
        return false;
    }

    CookedTextureHeader header;
    header.format = static_cast<uint32_t>(create.m_format);
    header.width = create.m_width;
    header.height = create.m_height;
    header.isCubeMap = create.m_isCubeMap ? 1u : 0u;
    header.dataSize = static_cast<uint32_t>(dataSize);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("FAILED TO WRITE COOKED TEXTURE %s", path.string().c_str());
        return false;
    }

    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(reinterpret_cast<const char *>(create.m_data.data), static_cast<std::streamsize>(dataSize));
    return file.good();
}

path_t AssetImporterBase::getCookedTextureAssetPath(const path_t &sourcePath) {
    path_t result = ".Panda/Textures";
    result /= sourcePath;
    result.replace_extension(".ptex");
    return result;
}

path_t AssetImporterBase::getShaderIncludeRoot() {
    return Fern::getStaticResourcesPath() / "default-shaders" / "include";
}

path_t AssetImporterBase::getIntermediateShaderExpandedPath(const path_t &sourcePath) {
    return path_t(".Panda/Shaders/intermediate") / sourcePath;
}

path_t AssetImporterBase::getIntermediateShaderSpvPath(const path_t &sourcePath) {
    path_t result = ".Panda/Shaders/intermediate";
    result /= sourcePath;
    result.replace_extension(".spv");
    return result;
}

path_t AssetImporterBase::getCookedShaderAssetPath(const path_t &sourcePath, Foundation::GraphicsAPI backend) {
    path_t result = ".Panda/Shaders/cooked";
    result /= Foundation::getGraphicsAPITraits(backend).shaderBackendName;
    result /= sourcePath;
    result.replace_extension(sourcePath.extension() == ".hlsl" ? "" : sourcePath.extension().string());
    return result;
}

path_t AssetImporterBase::getShaderReflectionAssetPath(const path_t &fragmentSourcePath) {
    path_t result = ".Panda/Shaders/reflection";
    result /= fragmentSourcePath;
    result.replace_extension(".json");
    return result;
}

FileData AssetImporterBase::loadTextData(const path_t &path) {
    return loadFileDataInternal(path, true);
}

FileData AssetImporterBase::loadBinaryData(const path_t &path) {
    return loadFileDataInternal(path, false);
}

Miren::ProgramCreate AssetImporterBase::loadShaderProgram(const path_t &vertexPath, const path_t &fragmentPath) {
    // Vulkan shaders are cooked into binary SPIR-V, the other backends use text.
    const bool binary = Foundation::ACTIVE_GRAPHICS_API == Foundation::GraphicsAPI::Vulkan;
    FileData vertexData = binary ? loadBinaryData(vertexPath) : loadTextData(vertexPath);
    FileData fragmentData = binary ? loadBinaryData(fragmentPath) : loadTextData(fragmentPath);
    Miren::ProgramCreate program;
    program.m_vertexBinary = vertexData.memory;
    program.m_fragmentBinary = fragmentData.memory;
    program.m_vertexBinarySize = vertexData.size;
    program.m_fragmentBinarySize = fragmentData.size;
    return program;
}

Foundation::Memory AssetImporterBase::loadData(const path_t &path) {
    return loadTextData(path).memory;
}

cgltf_size getMeshIndex(const cgltf_data *data, const cgltf_mesh *mesh) {
    if (!data || !mesh || !data->meshes) { return -1; }
    ptrdiff_t diff = mesh - data->meshes;
    if (diff >= 0 && diff < (ptrdiff_t)data->meshes_count) { return (cgltf_size)diff; }
    return -1;
}

cgltf_size getMaterialIndex(const cgltf_data *data, const cgltf_material *material) {
    if (!data || !material || !data->materials) { return -1; }
    ptrdiff_t diff = material - data->materials;
    if (diff >= 0 && diff < (ptrdiff_t)data->materials_count) { return (cgltf_size)diff; }
    return -1;
}

bool isProjectRelativePath(const path_t &path) {
    if (path.empty() || path.is_absolute()) { return false; }
    auto it = path.begin();
    return it == path.end() || *it != "..";
}

path_t
makeProjectRelativeTexturePath(const path_t &gltfPath, const path_t &projectPath, const cgltf_texture_view &view) {
    if (!view.texture || !view.texture->image) { return {}; }
    const cgltf_image *image = view.texture->image;
    if (!image->uri || image->buffer_view) { return {}; }

    std::string uri = image->uri;
    if (uri.starts_with("data:") || uri.find("://") != std::string::npos) { return {}; }

    path_t uriPath(uri);
    path_t resolvedPath = uriPath.is_absolute() ? uriPath : (gltfPath.parent_path() / uriPath);
    path_t relativePath = resolvedPath.lexically_normal().lexically_relative(projectPath.lexically_normal());
    if (!isProjectRelativePath(relativePath)) { return {}; }
    return relativePath;
}

Vec4 toVec4(const cgltf_float *value, float w) {
    return Vec4(value[0], value[1], value[2], w);
}

Vec4 toVec4(const cgltf_float *value) {
    return Vec4(value[0], value[1], value[2], value[3]);
}

std::string sanitizeMaterialFileName(std::string value, const char *fallback) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '_' || character == '-') {
            result.push_back(static_cast<char>(character));
            continue;
        }
        if (result.empty() || result.back() == '_') { continue; }
        result.push_back('_');
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) { return fallback; }
    return result;
}

void processGltfNodeRecurse(
    cgltf_data *data,
    cgltf_node *node,
    Entity &parentEntity,
    World &world,
    AssetRef<ImportedMeshAsset> asset,
    const path_t &gltfPath
) {
    TransformComponent &transform = parentEntity.getTransform();
    if (node->has_matrix) {
        cgltf_float *matrix = node->matrix;
        glm::mat4 glmMatrix(
            matrix[0],
            matrix[1],
            matrix[2],
            matrix[3],
            matrix[4],
            matrix[5],
            matrix[6],
            matrix[7],
            matrix[8],
            matrix[9],
            matrix[10],
            matrix[11],
            matrix[12],
            matrix[13],
            matrix[14],
            matrix[15]
        );
        transform.setTransform(glmMatrix);
    } else {
        if (node->has_translation) {
            transform.setPosition(glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
        }
        if (node->has_rotation) {
            transform.setRotation(glm::quat(
                node->rotation[3], // w
                node->rotation[0], // x
                node->rotation[1], // y
                node->rotation[2]  // z
            ));
        }
        if (node->has_scale) { transform.setScale(glm::vec3(node->scale[0], node->scale[1], node->scale[2])); }
    }
    if (node->mesh && node->mesh->primitives_count) {
        for (size_t i = 0; i < node->mesh->primitives_count; ++i) {
            Entity primitiveEntity = world.instantiateEntity();
            primitiveEntity.setName("primitive " + std::to_string(i));
            world.setParent(primitiveEntity, parentEntity, false);
            ImportedSubmeshComponent &meshComponent = primitiveEntity.addComponent<ImportedSubmeshComponent>();
            meshComponent.importedMesh = asset.asBaseAsset();
            meshComponent.submeshIndex = getMeshIndex(data, node->mesh);
            meshComponent.primitiveIndex = i;
            // material (override) is empty: the primitive follows the slot layout of the mesh
            // asset — the same in the editor and the runtime, there are no material copies in
            // the scene.
        }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
        cgltf_node *childNode = node->children[i];
        Entity childEntity = world.instantiateEntity();
        if (childNode->name) { childEntity.setName(childNode->name); }
        world.setParent(childEntity, parentEntity, false);
        processGltfNodeRecurse(data, childNode, childEntity, world, asset, gltfPath);
    }
}

path_t AssetImporterBase::getGltfMaterialAssetPath(
    const path_t &gltfPath, const std::string &materialName, size_t materialIndex
) {
    // Material file = the glTF material name in the mesh folder: meshes of one folder SHARE
    // materials by name (variations and LODs of one plant — a single bark.mat), like
    // Unreal Search Location: Local. An unnamed material is unique per mesh.
    if (materialName.empty()) {
        std::string meshName = sanitizeMaterialFileName(gltfPath.stem().string(), "mesh");
        return gltfPath.parent_path() /
               (meshName + "_material_" + std::to_string(materialIndex) + ".mat");
    }
    std::string sanitizedMaterialName = sanitizeMaterialFileName(materialName, "material");
    return gltfPath.parent_path() / (sanitizedMaterialName + ".mat");
}

UUID AssetImporterBase::addGltfSceneToWorld(const path_t &path, AssetRef<ImportedMeshAsset> asset, World &world) {
    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result cgltfResult = cgltf_parse_file(&options, path.string().c_str(), &data);
    if (cgltfResult != cgltf_result_success) {
        LOG_ERROR("ERROR LOADING GLTF AT PATH %s", path.string().c_str());
        return UUID(0);
    }
    if (!data->scene) {
        LOG_ERROR("GLTF HAS NO DEFAULT SCENE AT PATH %s", path.string().c_str());
        cgltf_free(data);
        return UUID(0);
    }
    Entity rootEntity = world.instantiateEntity();
    rootEntity.setName(path.filename().string());
    for (size_t i = 0; i < data->scene->nodes_count; ++i) {
        processGltfNodeRecurse(data, data->scene->nodes[i], rootEntity, world, asset, path);
    }
    LOG_INFO("MESH %s ADDED TO WORLD. TOTAL %d NODES", path.filename().string().c_str(), data->nodes_count);
    LOG_INFO_EDITOR("MESH %s ADDED TO WORLD. TOTAL %d NODES", path.filename().string().c_str(), data->nodes_count);
    cgltf_free(data);
    return rootEntity.getId();
}

AssetInfo::MeshSettings AssetImporterBase::loadGltfMeshMeta(const path_t &gltfPath, const path_t &projectPath) {
    AssetInfo::MeshSettings meta;

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result cgltfResult = cgltf_parse_file(&options, gltfPath.string().c_str(), &data);
    if (cgltfResult != cgltf_result_success) {
        LOG_ERROR("ERROR LOADING GLTF META AT PATH %s", gltfPath.string().c_str());
        LOG_ERROR_EDITOR("ERROR LOADING GLTF META AT PATH %s", gltfPath.string().c_str());
        return meta;
    }

    for (size_t materialIndex = 0; materialIndex < data->materials_count; ++materialIndex) {
        cgltf_material *material = data->materials + materialIndex;
        AssetInfo::GltfMaterial &materialMeta = meta.materials.emplace_back();
        materialMeta.name = material->name ? material->name : "material_" + std::to_string(materialIndex);
        materialMeta.baseColorTexturePath =
            makeProjectRelativeTexturePath(gltfPath, projectPath, material->pbr_metallic_roughness.base_color_texture);
        materialMeta.baseColorFactor = toVec4(material->pbr_metallic_roughness.base_color_factor);
        materialMeta.metallicRoughnessTexturePath = makeProjectRelativeTexturePath(
            gltfPath, projectPath, material->pbr_metallic_roughness.metallic_roughness_texture
        );
        materialMeta.metallicFactor = material->pbr_metallic_roughness.metallic_factor;
        materialMeta.roughnessFactor = material->pbr_metallic_roughness.roughness_factor;
        materialMeta.normalTexturePath =
            makeProjectRelativeTexturePath(gltfPath, projectPath, material->normal_texture);
        materialMeta.normalScale = material->normal_texture.scale;
        materialMeta.occlusionTexturePath =
            makeProjectRelativeTexturePath(gltfPath, projectPath, material->occlusion_texture);
        materialMeta.occlusionStrength = material->occlusion_texture.scale;
        materialMeta.emissiveTexturePath =
            makeProjectRelativeTexturePath(gltfPath, projectPath, material->emissive_texture);
        materialMeta.emissiveFactor = toVec4(material->emissive_factor, 1.f);
        materialMeta.alphaMode = static_cast<uint32_t>(material->alpha_mode);
        materialMeta.alphaCutoff = material->alpha_cutoff;
        materialMeta.doubleSided = material->double_sided;
    }

    for (size_t meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        cgltf_mesh *mesh = data->meshes + meshIndex;
        for (size_t primitiveIndex = 0; primitiveIndex < mesh->primitives_count; ++primitiveIndex) {
            cgltf_primitive *primitive = mesh->primitives + primitiveIndex;
            AssetInfo::GltfPrimitiveMaterial &primitiveMeta = meta.primitiveMaterials.emplace_back();
            primitiveMeta.submeshIndex = meshIndex;
            primitiveMeta.primitiveIndex = primitiveIndex;
            primitiveMeta.materialIndex = static_cast<int32_t>(getMaterialIndex(data, primitive->material));
        }
    }

    cgltf_free(data);
    return meta;
}

std::vector<ImportedMeshData> AssetImporterBase::loadGltfMesh(const path_t &path) {
    std::vector<ImportedMeshData> result;
    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result cgltfResult = cgltf_parse_file(&options, path.string().c_str(), &data);
    if (cgltfResult != cgltf_result_success) {
        LOG_ERROR("ERROR LOADING GLTF AT PATH %s", path.string().c_str());
        return {};
    }
    cgltfResult = cgltf_load_buffers(&options, data, path.string().c_str());
    if (cgltfResult != cgltf_result_success) {
        LOG_ERROR("ERROR LOADING GLTF BUFFERS AT PATH %s", path.string().c_str());
        cgltf_free(data);
        return {};
    }
    // A binary of a size other than the gltf expects is almost always a foreign .bin with the
    // same name (two exports into one folder): the accessors will read garbage — the geometry
    // "explodes".
    for (size_t bufferIndex = 0; bufferIndex < data->buffers_count; ++bufferIndex) {
        const cgltf_buffer &buffer = data->buffers[bufferIndex];
        if (buffer.uri == nullptr) { continue; } // embedded (glb/base64)
        std::error_code errorCode;
        const path_t bufferPath = path.parent_path() / buffer.uri;
        const uintmax_t actualSize = std::filesystem::file_size(bufferPath, errorCode);
        if (!errorCode && actualSize != buffer.size) {
            LOG_ERROR(
                "GLTF BUFFER SIZE MISMATCH: %s expects %zu bytes, file %s is %zu bytes. "
                "Likely another gltf's .bin with the same name — export to a separate "
                "folder or rename the buffer.",
                path.string().c_str(), (size_t)buffer.size, buffer.uri, (size_t)actualSize
            );
        }
    }
    for (size_t meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        cgltf_mesh *mesh = data->meshes + meshIndex;
        ImportedMeshData &meshData = result.emplace_back();
        for (size_t primitiveIndex = 0; primitiveIndex < mesh->primitives_count; ++primitiveIndex) {
            cgltf_primitive *primitive = mesh->primitives + primitiveIndex;
            ImportedPrimitiveData &primitiveData = meshData.primitives.emplace_back();
            if (!primitive || primitive->type != cgltf_primitive_type_triangles || !primitive->indices) { continue; }
            cgltf_accessor *positionAccessor = nullptr;
            cgltf_accessor *uvAccessor = nullptr;
            cgltf_accessor *normalAccessor = nullptr;
            cgltf_accessor *colorAccessor = nullptr;
            // COLOR_1 — the channel of baked vertex data (written by the vegetation
            // generator): .r goes into Vertex::light (a LIGHT multiplier in the shaders —
            // crown shading), .g/.b — the card id (low/high byte × 255): the phase of the
            // card's sway in the wind. COLOR_0 is taken by the color and the wind weight in alpha.
            cgltf_accessor *shadingAccessor = nullptr;
            for (size_t i = 0; i < primitive->attributes_count; ++i) {
                cgltf_attribute *attribute = &primitive->attributes[i];
                switch (attribute->type) {
                    case cgltf_attribute_type_position:
                        positionAccessor = attribute->data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        uvAccessor = attribute->data;
                        break;
                    case cgltf_attribute_type_normal:
                        normalAccessor = attribute->data;
                        break;
                    case cgltf_attribute_type_color:
                        // The sets differ by index: without this COLOR_1 would overwrite the color.
                        if (attribute->index == 0) {
                            colorAccessor = attribute->data;
                        } else if (attribute->index == 1) {
                            shadingAccessor = attribute->data;
                        }
                        break;
                    default:
                        break;
                }
            }
            if (!positionAccessor) { continue; }
            size_t vertexCount = positionAccessor->count;
            primitiveData.vertices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                cgltf_accessor_read_float(positionAccessor, i, &primitiveData.vertices[i].pos.x, 3);
                primitiveData.vertices[i].light = 1.0f;
            }
            if (shadingAccessor) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    float shading[4] = {1.f, 1.f, 1.f, 1.f};
                    cgltf_accessor_read_float(
                        shadingAccessor, i, shading, shadingAccessor->type == cgltf_type_vec3 ? 3 : 4
                    );
                    primitiveData.vertices[i].light = shading[0];
                    primitiveData.vertices[i].id =
                        (int32_t)(lroundf(shading[1] * 255.f) + (lroundf(shading[2] * 255.f) << 8));
                }
            }
            if (uvAccessor) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    cgltf_accessor_read_float(uvAccessor, i, &primitiveData.vertices[i].uv.x, 2);
                }
            } else {
                for (auto &vertex : primitiveData.vertices) {
                    vertex.uv = glm::vec2(0.0f);
                }
            }
            if (normalAccessor) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    cgltf_accessor_read_float(normalAccessor, i, &primitiveData.vertices[i].normal.x, 3);
                }
            } else {
                for (auto &vertex : primitiveData.vertices) {
                    vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                }
            }
            if (colorAccessor) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    cgltf_accessor_read_float(
                        colorAccessor,
                        i,
                        &primitiveData.vertices[i].color.x,
                        colorAccessor->type == cgltf_type_vec3 ? 3 : 4
                    );
                    if (colorAccessor->type == cgltf_type_vec3) { primitiveData.vertices[i].color.a = 1.0f; }
                }
            } else {
                for (auto &vertex : primitiveData.vertices) {
                    vertex.color = glm::vec4(1.0f);
                }
            }
            cgltf_accessor *indexAccessor = primitive->indices;
            size_t indexCount = indexAccessor->count;
            primitiveData.indices.resize(indexCount);
            if (indexAccessor->component_type == cgltf_component_type_r_16u) {
                for (size_t i = 0; i < indexCount; ++i) {
                    primitiveData.indices[i] = cgltf_accessor_read_index(indexAccessor, i);
                }
            } else if (indexAccessor->component_type == cgltf_component_type_r_32u) {
                const uint32_t *data = (const uint32_t *)((const uint8_t *)indexAccessor->buffer_view->buffer->data +
                                                          indexAccessor->offset + indexAccessor->buffer_view->offset);
                primitiveData.indices.assign(data, data + indexCount);
            }
        }
    }
    cgltf_free(data);
    LOG_INFO("MESH DATA %s LOADED. TOTAL %d SUBMESHES", path.filename().string().c_str(), result.size());
    LOG_INFO_EDITOR("MESH DATA %s LOADED. TOTAL %d SUBMESHES", path.filename().string().c_str(), result.size());
    return result;
}

} // namespace Panda
