#pragma once

#include "Panda/Terrain/FloraSettings.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"
#include "Panda/Terrain/TerrainData3D.hpp"
#include "Panda/Terrain/TerrainTrees.hpp"

#include <cstdint>
#include <istream>
#include <ostream>

namespace Panda {

// Terrain asset kind; the value is persisted in the .terrain header — do not change.
enum class TerrainKind : uint32_t {
    Heightmap2D = 0,
    Voxels3D = 1,
};

// Binary .terrain format: magic 'PTRN', version, kind, then the data of the kind.
// A single format for the editor and the cooked runtime (copied as is). Only the current
// format version is read — there are no migrations.
namespace TerrainSerialization {

    // Header (magic + version + kind); after a successful read the stream is at the kind data.
    // false — not a .terrain or the format version is not the current one.
    bool readHeader(std::istream &stream, TerrainKind &outKind);
    // 2D: the kind data, then the vegetation — settings (a TerrainFloraDto JSON blob with its
    // length) and tree points (count + TreeInstance[count]).
    bool read2DPayload(
        std::istream &stream, TerrainData2D &outData, FloraSettings &outFlora, TerrainTrees &outTrees
    );
    bool read3DPayload(std::istream &stream, TerrainData3D &outData);

    void write2D(
        std::ostream &stream, const TerrainData2D &data, const FloraSettings &flora,
        const TerrainTrees &trees
    );
    void write3D(std::ostream &stream, const TerrainData3D &data);

} // namespace TerrainSerialization

} // namespace Panda
