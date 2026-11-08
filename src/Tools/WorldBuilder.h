#pragma once

// Builds "Headland", the open-world showcase: a ~1.5 km coastal island streamed in 128 m sectors.
//
//   Tessera --build-world=projects/Headland/scenes [--world-seed=7] [--world-preview=<file.png>]
//
// Needs the CC0 models and textures fetched and optimized first:
//   python tools/fetch_assets.py
//   Tessera --optimize-models=assets/external/polyhaven/models
//
// Output (all ordinary engine files, built through the Scene / TerrainNode API):
//   base.scene        always loaded: sky, sun, ocean, a coarse terrain tile per sector (LOD_x_z),
//                     the player camera and HUD
//   sectors/*.scene   each land sector: a detailed 128 x 128 terrain tile (painted), its trees,
//                     undergrowth, rocks and cliffs (real-scale models with LODs and colliders),
//                     ground-cover grass; the fishing camp sits in its sector
//   headland.world    the streaming manifest

#include <string>

namespace Tools {

struct WorldBuildOptions {
    std::string outputDir{"projects/Headland/scenes"};
    std::string assetRoot{"assets/external/polyhaven"};
    unsigned seed{7};
    std::string previewPng; // optional: a top-down height / material map of the island
};

bool BuildHeadlandWorld(const WorldBuildOptions& options, std::string& error);

// Handles --build-world; `handled` is set when it was given
int RunWorldBuilderCommandLine(int argc, char* argv[], bool& handled);

} // namespace Tools
