#pragma once

#include <string>
#include "ObjLoader.h"

class FbxLoader {
public:
    // Loads an FBX file from disk into LoadedMesh, triangulating polygons, extracting normals, UVs and materials
    static bool Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize = true, const std::string& sourceFallbackPath = "");
};

