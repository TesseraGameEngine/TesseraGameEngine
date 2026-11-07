#pragma once

#include "Scene/Mesh.h"
#include <string>

// Built-in foliage meshes for terrain foliage (FoliageType::builtIn), made procedurally with
// vertex colours so they need no assets: grass tufts, flowers, a bush, a broadleaf tree, a pine
// and a rock. Each stands on its origin, about 1 m across (trees are taller), +y up.
namespace FoliageMeshes {

enum Kind { Grass = 0, Flowers, Bush, Tree, Pine, Rock, Count };

MeshData Build(int kind);
// Name the renderer registers each under (VulkanPipeline custom meshes)
std::string MeshName(int kind);

} // namespace FoliageMeshes
