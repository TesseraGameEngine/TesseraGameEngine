#pragma once

#include "RenderCommon.h"
#include <cstdint>

namespace Graphics {

class IRenderTarget {
public:
    virtual ~IRenderTarget() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void* GetTextureId() const = 0;
};

} // namespace Graphics

