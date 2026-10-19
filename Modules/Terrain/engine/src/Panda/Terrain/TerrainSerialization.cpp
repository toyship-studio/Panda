#include "Panda/Terrain/TerrainSerialization.hpp"

#include "Panda/Terrain/TerrainDtos.hpp"

#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <sstream>

namespace Panda {

namespace TerrainSerialization {

    namespace {

        constexpr uint32_t TERRAIN_MAGIC = 0x4E525450; // 'PTRN'
        // ONLY the current version is read (common to both kinds): a layout change = a bump,
        // files of previous versions do not open — there are no migrations on purpose.
        constexpr uint32_t TERRAIN_VERSION = 6;
        constexpr uint32_t MAX_FLORA_JSON = 16u * 1024 * 1024; // ceiling against a garbage size
        constexpr uint32_t MAX_TREES = 16u * 1024 * 1024;
        static_assert(sizeof(TreeInstance) == 20, "TreeInstance is written to .terrain as is");

        // Ceilings against garbage files. The grid size comes from the data itself, so that the
        // write path cannot create an asset that the read path would reject.
        constexpr uint32_t MAX_CELLS_2D = TerrainData2D::MAX_CELLS;
        constexpr uint32_t MAX_CELLS_3D = TerrainData3D::MAX_CELLS;
        constexpr uint64_t MAX_VOXELS_3D = 512ull * 1024 * 1024;

        template<typename T>
        void writeValue(std::ostream &stream, const T &value) {
            stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
        }

        template<typename T>
        bool readValue(std::istream &stream, T &value) {
            stream.read(reinterpret_cast<char *>(&value), sizeof(T));
            return stream.good();
        }

        void writeHeader(std::ostream &stream, TerrainKind kind) {
            writeValue(stream, TERRAIN_MAGIC);
            writeValue(stream, TERRAIN_VERSION);
            writeValue(stream, static_cast<uint32_t>(kind));
        }

        TerrainFloraDto floraToDto(const FloraSettings &flora) {
            TerrainFloraDto dto;
            dto.grain.root = flora.grain.root;
            dto.grain.tip = flora.grain.tip;
            dto.grain.extra = flora.grain.extra;
            dto.grain.extraShare = flora.grain.extraShare;
            dto.grain.coverage = flora.grain.coverage;
            dto.grain.soil = flora.grain.soil;
            dto.grain.brightness = flora.grain.brightness;
            for (const GrassLayer &layer : flora.grass) {
                GrassLayerDto layerDto;
                layerDto.name = layer.name;
                layerDto.enabled = layer.enabled;
                layerDto.kind = static_cast<uint32_t>(layer.kind);
                layerDto.placement = static_cast<uint32_t>(layer.placement);
                layerDto.maskChannel = layer.maskChannel;
                layerDto.density = layer.density;
                layerDto.spacing = layer.spacing;
                layerDto.soilLayer = layer.soilLayer;
                layerDto.minHeight = layer.minHeight;
                layerDto.maxHeight = layer.maxHeight;
                layerDto.maxSlopeDeg = layer.maxSlopeDeg;
                layerDto.noiseCoverage = layer.noiseCoverage;
                layerDto.scaleMin = layer.scaleMin;
                layerDto.scaleMax = layer.scaleMax;
                layerDto.windInfluence = layer.windInfluence;
                layerDto.brownian = layer.brownian;
                layerDto.mesh = layer.mesh.getId();
                layerDto.meshMaterial = layer.meshMaterial.getId();
                layerDto.castShadows = layer.castShadows;
                dto.grass.push_back(std::move(layerDto));
            }
            for (const TreeLayer &layer : flora.trees) {
                TreeLayerDto layerDto;
                layerDto.id = layer.id;
                layerDto.name = layer.name;
                layerDto.enabled = layer.enabled;
                layerDto.mesh = layer.mesh.getId();
                layerDto.meshMaterial = layer.meshMaterial.getId();
                layerDto.imposter = layer.imposter;
                layerDto.canopyDensity = layer.canopyDensity;
                layerDto.scaleMin = layer.scaleMin;
                layerDto.scaleMax = layer.scaleMax;
                dto.trees.push_back(std::move(layerDto));
            }
            return dto;
        }

        FloraSettings floraFromDto(const TerrainFloraDto &dto) {
            FloraSettings flora;
            flora.grain.root = dto.grain.root;
            flora.grain.tip = dto.grain.tip;
            flora.grain.extra = dto.grain.extra;
            flora.grain.extraShare = dto.grain.extraShare;
            flora.grain.coverage = dto.grain.coverage;
            flora.grain.soil = dto.grain.soil;
            flora.grain.brightness = dto.grain.brightness;
            for (const GrassLayerDto &layerDto : dto.grass) {
                GrassLayer layer;
                layer.name = layerDto.name;
                layer.enabled = layerDto.enabled;
                layer.kind = static_cast<GrassKind>(
                    std::min(layerDto.kind, static_cast<uint32_t>(GrassKind::Mesh))
                );
                layer.placement = static_cast<GrassPlacement>(
                    std::min(layerDto.placement, static_cast<uint32_t>(GrassPlacement::Combined))
                );
                layer.maskChannel = std::min(layerDto.maskChannel, TerrainData2D::FLORA_MASK_CHANNELS - 1);
                layer.density = layerDto.density;
                layer.spacing = layerDto.spacing;
                layer.soilLayer = layerDto.soilLayer;
                layer.minHeight = layerDto.minHeight;
                layer.maxHeight = layerDto.maxHeight;
                layer.maxSlopeDeg = layerDto.maxSlopeDeg;
                layer.noiseCoverage = layerDto.noiseCoverage;
                layer.scaleMin = layerDto.scaleMin;
                layer.scaleMax = layerDto.scaleMax;
                layer.windInfluence = layerDto.windInfluence;
                layer.brownian = layerDto.brownian;
                layer.mesh = AssetRef<Asset>(layerDto.mesh);
                layer.meshMaterial = AssetRef<Asset>(layerDto.meshMaterial);
                layer.castShadows = layerDto.castShadows;
                flora.grass.push_back(std::move(layer));
            }
            for (const TreeLayerDto &layerDto : dto.trees) {
                TreeLayer layer;
                layer.id = layerDto.id;
                layer.name = layerDto.name;
                layer.enabled = layerDto.enabled;
                layer.mesh = AssetRef<Asset>(layerDto.mesh);
                layer.meshMaterial = AssetRef<Asset>(layerDto.meshMaterial);
                layer.imposter = layerDto.imposter;
                layer.canopyDensity = layerDto.canopyDensity;
                layer.scaleMin = layerDto.scaleMin;
                layer.scaleMax = layerDto.scaleMax;
                flora.trees.push_back(std::move(layer));
            }
            return flora;
        }

    } // namespace

    bool readHeader(std::istream &stream, TerrainKind &outKind) {
        uint32_t magic = 0, version = 0, kind = 0;
        if (!readValue(stream, magic) || magic != TERRAIN_MAGIC) { return false; }
        if (!readValue(stream, version) || version != TERRAIN_VERSION) { return false; }
        if (!readValue(stream, kind) || kind > static_cast<uint32_t>(TerrainKind::Voxels3D)) {
            return false;
        }
        outKind = static_cast<TerrainKind>(kind);
        return true;
    }

    bool read2DPayload(
        std::istream &stream, TerrainData2D &outData, FloraSettings &outFlora, TerrainTrees &outTrees
    ) {
        TerrainData2D data;
        if (!readValue(stream, data.cellsX) || !readValue(stream, data.cellsZ) ||
            !readValue(stream, data.cellSize)) {
            return false;
        }
        if (data.cellsX == 0 || data.cellsZ == 0 || data.cellsX > MAX_CELLS_2D ||
            data.cellsZ > MAX_CELLS_2D || data.cellSize <= 0.f) {
            return false;
        }
        // Heights are uint16 (the quantum is TerrainData2D::HEIGHT_QUANTUM), the layer and flora
        // maps are on their own, coarser grids (layerShift/floraShift).
        if (!readValue(stream, data.layerShift) || !readValue(stream, data.floraShift)) {
            return false;
        }
        if (data.layerShift > TerrainData2D::MAX_MAP_SHIFT ||
            data.floraShift > TerrainData2D::MAX_MAP_SHIFT) {
            return false;
        }
        const size_t count = static_cast<size_t>(data.vertexCountX()) * data.vertexCountZ();
        const size_t layerCount = static_cast<size_t>(data.layerCountX()) * data.layerCountZ();
        const size_t floraCount = static_cast<size_t>(data.floraCountX()) * data.floraCountZ() *
                                  TerrainData2D::FLORA_MASK_CHANNELS;
        data.heights.resize(count);
        stream.read(reinterpret_cast<char *>(data.heights.data()), count * sizeof(uint16_t));
        if (!stream.good()) { return false; }
        data.layers.resize(layerCount);
        stream.read(reinterpret_cast<char *>(data.layers.data()), layerCount);
        if (!stream.good()) { return false; }
        data.floraMask.resize(floraCount);
        stream.read(reinterpret_cast<char *>(data.floraMask.data()), floraCount);
        if (!stream.good()) { return false; }
        FloraSettings flora;
        uint32_t jsonSize = 0;
        if (!readValue(stream, jsonSize) || jsonSize > MAX_FLORA_JSON) { return false; }
        if (jsonSize > 0) {
            std::string json(jsonSize, '\0');
            stream.read(json.data(), jsonSize);
            if (!stream.good()) { return false; }
            TerrainFloraDto dto;
            Rain::JsonDecoder decoder;
            std::istringstream jsonStream(json);
            Rain::Decoder *decoderPtr = &decoder;
            if (!decoderPtr->decode(jsonStream, dto)) { return false; }
            flora = floraFromDto(dto);
        }
        uint32_t treeCount = 0;
        if (!readValue(stream, treeCount) || treeCount > MAX_TREES) { return false; }
        std::vector<TreeInstance> instances(treeCount);
        stream.read(
            reinterpret_cast<char *>(instances.data()),
            static_cast<std::streamsize>(treeCount) * sizeof(TreeInstance)
        );
        if (treeCount > 0 && !stream.good()) { return false; }
        TerrainTrees trees;
        trees.assign(instances);
        outData = std::move(data);
        outFlora = std::move(flora);
        outTrees = std::move(trees);
        return true;
    }

    bool read3DPayload(std::istream &stream, TerrainData3D &outData) {
        TerrainData3D data;
        if (!readValue(stream, data.cellsX) || !readValue(stream, data.cellsY) ||
            !readValue(stream, data.cellsZ) || !readValue(stream, data.cellSize)) {
            return false;
        }
        if (data.cellsX == 0 || data.cellsY == 0 || data.cellsZ == 0 ||
            data.cellsX > MAX_CELLS_3D || data.cellsY > MAX_CELLS_3D ||
            data.cellsZ > MAX_CELLS_3D || data.cellSize <= 0.f) {
            return false;
        }
        const uint64_t count = static_cast<uint64_t>(data.cellsX) * data.cellsY * data.cellsZ;
        if (count > MAX_VOXELS_3D) { return false; }
        data.materials.resize(count);
        stream.read(reinterpret_cast<char *>(data.materials.data()), count);
        if (!stream.good()) { return false; }
        outData = std::move(data);
        return true;
    }

    void write2D(
        std::ostream &stream, const TerrainData2D &data, const FloraSettings &flora,
        const TerrainTrees &trees
    ) {
        writeHeader(stream, TerrainKind::Heightmap2D);
        writeValue(stream, data.cellsX);
        writeValue(stream, data.cellsZ);
        writeValue(stream, data.cellSize);
        writeValue(stream, data.layerShift);
        writeValue(stream, data.floraShift);
        stream.write(
            reinterpret_cast<const char *>(data.heights.data()),
            data.heights.size() * sizeof(uint16_t)
        );
        stream.write(reinterpret_cast<const char *>(data.layers.data()), data.layers.size());
        stream.write(
            reinterpret_cast<const char *>(data.floraMask.data()), data.floraMask.size()
        );
        // The vegetation settings go as a JSON blob (the Dto is already Rain-Codable — the binary
        // layout is not duplicated by hand).
        TerrainFloraDto dto = floraToDto(flora);
        std::ostringstream jsonStream;
        Rain::JsonEncoder encoder;
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(jsonStream, dto);
        const std::string json = jsonStream.str();
        writeValue(stream, static_cast<uint32_t>(json.size()));
        stream.write(json.data(), json.size());
        // Tree points — as a dense POD array (TreeInstance: 4 float + uint32).
        const std::vector<TreeInstance> instances = trees.flatten();
        writeValue(stream, static_cast<uint32_t>(instances.size()));
        stream.write(
            reinterpret_cast<const char *>(instances.data()),
            static_cast<std::streamsize>(instances.size() * sizeof(TreeInstance))
        );
    }

    void write3D(std::ostream &stream, const TerrainData3D &data) {
        writeHeader(stream, TerrainKind::Voxels3D);
        writeValue(stream, data.cellsX);
        writeValue(stream, data.cellsY);
        writeValue(stream, data.cellsZ);
        writeValue(stream, data.cellSize);
        stream.write(
            reinterpret_cast<const char *>(data.materials.data()), data.materials.size()
        );
    }

} // namespace TerrainSerialization

} // namespace Panda
