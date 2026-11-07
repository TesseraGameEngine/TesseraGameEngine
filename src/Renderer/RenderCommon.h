#pragma once

#include <cstdint>

namespace Graphics {

enum class PolygonMode {
    Solid,
    Wireframe,
    WireframeOnShaded
};

enum class RenderBackendType {
    Vulkan,
    Metal,
    DirectX12,
    OpenGL
};

} // namespace Graphics

