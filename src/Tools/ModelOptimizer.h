#pragma once

// Offline model optimizer: turns heavy scanned glTF models (millions of triangles) into a chain
// of real-scale LOD glTF files the engine can scatter by the hundred.
//
//   <dir>/<name>.gltf  ->  <dir>/<name>_lod0.gltf, <name>_lod1.gltf, ... (+ .bin each)
//
// - Solid parts (trunks, rocks, props) are decimated with the engine's QEM simplifier.
// - Alpha-tested leaf / needle cards are thinned instead (QEM would collapse the quads): a
//   deterministic share of whole cards is kept and each kept card is enlarged to keep the
//   canopy's coverage.
// - Geometry is written in metres with the model's own pivot, and the files are marked so the
//   loader keeps that scale (every LOD is the same size; scene nodes use scale 1).
// - Materials, textures and images are kept as they are (texture transforms are baked into UVs).
//
// Command line (runs without a window, then exits):
//   Tessera --optimize-model=<file.gltf> [--lod-triangles=150000,30000,6000]
//   Tessera --optimize-models=<directory>  (every <id>/<id>_<res>.gltf inside; up-to-date ones skipped)

#include <cstdint>
#include <string>
#include <vector>

namespace Tools {

struct ModelOptimizeResult {
    bool ok{false};
    std::string error;
    uint32_t sourceTriangles{0};
    std::vector<std::string> outputs;          // written files, LOD0 first
    std::vector<uint32_t> triangleCounts;      // per output
};

ModelOptimizeResult OptimizeModel(const std::string& gltfPath, const std::vector<uint32_t>& lodTriangleTargets);

// Output file of a model's LOD (whether or not it exists yet)
std::string OptimizedLodPath(const std::string& gltfPath, int lod);

// Handles the optimizer's command line options. `handled` is set when one was given (the caller
// then exits with the returned code instead of starting the engine).
int RunModelOptimizerCommandLine(int argc, char* argv[], bool& handled);

} // namespace Tools
