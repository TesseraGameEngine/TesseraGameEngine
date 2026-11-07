#pragma once

#include "Scene/Mesh.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Geometry {

struct LODChainResult {
    std::vector<MeshData> levels;          // Index 0 = LOD1, 1 = LOD2, 2 = LOD3...
    std::vector<float> suggestedDistances; // Suggested distance thresholds (e.g. 20m, 45m, 80m)
    std::vector<float> reductionRatios;    // Target reduction ratios (e.g. 0.5, 0.25, 0.10)
    std::vector<uint32_t> triangleCounts; // Resulting triangle counts
};

class MeshSimplifier {
public:
    // Simplifies an input MeshData down to targetRatio (0.0 < targetRatio < 1.0, e.g. 0.5 = 50% triangles)
    // using Quadric Error Metrics (QEM) edge collapse with normal-flip prevention and UV/bone weight preservation.
    static MeshData Simplify(const MeshData& inputMesh, float targetRatio, float aggressiveness = 7.0f);

    // Simplifies an input MeshData down to targetTriangles count.
    static MeshData SimplifyToTriangleCount(const MeshData& inputMesh, size_t targetTriangles, float aggressiveness = 7.0f);

    // Generates a multi-tier LOD chain (e.g. LOD1 at 50%, LOD2 at 25%, LOD3 at 10%)
    static LODChainResult GenerateLODChain(const MeshData& baseMesh,
                                          const std::vector<float>& targetRatios = {0.50f, 0.25f, 0.10f},
                                          const std::vector<float>& distances = {20.0f, 45.0f, 80.0f});
};

} // namespace Geometry

