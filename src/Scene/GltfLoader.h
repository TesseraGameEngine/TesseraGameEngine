#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "ObjLoader.h"

class GltfLoader {
public:
    // Loads a .gltf (JSON) or .glb (Binary glTF 2.0) file from disk into LoadedMesh
    static bool Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize = true);

    // Loads a .glb binary buffer or .gltf JSON buffer directly from memory
    static bool LoadFromMemory(const uint8_t* data, size_t size, LoadedMesh& outMesh,
                               const std::string& baseDir = "", bool autoCenterAndNormalize = true);

    // Helper that generates a valid binary .glb (glTF 2.0) triangle/cube mesh in memory
    static std::vector<uint8_t> GenerateMinimalGLB();
};

