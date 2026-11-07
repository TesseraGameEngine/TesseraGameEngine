#pragma once

#include "RenderCommon.h"
#include "IRenderTarget.h"
#include <string>
#include <memory>

struct GLFWwindow;
class Scene;
class Camera;

namespace Graphics {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual RenderBackendType GetBackendType() const = 0;
    virtual const char* GetBackendName() const = 0;

    virtual bool Init(GLFWwindow* window, uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    virtual void SetPolygonMode(PolygonMode mode) = 0;
    virtual PolygonMode GetPolygonMode() const = 0;

    virtual IRenderTarget* GetViewportTarget() = 0;
    virtual IRenderTarget* GetCameraPreviewTarget() = 0;
};

} // namespace Graphics

