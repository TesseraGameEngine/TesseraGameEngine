#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ParticleSheet;

// Animated sprite sheets the engine draws itself, so emitters look like fire, smoke, explosions
// or sparks without any texture: 8 x 8 frames (left to right, top to bottom) of 128 x 128 pixels,
// RGBA8, transparent at the frame borders. Fire, smoke and sparks loop; the explosion plays once.
namespace ParticleSheets {

constexpr int kColumns = 8;
constexpr int kRows = 8;
constexpr int kFrameSize = 128;
constexpr int kSize = kColumns * kFrameSize; // the sheet is kSize x kSize

std::vector<uint8_t> Generate(ParticleSheet sheet);
// Runtime texture key ('@' keys: VulkanPipeline::SetRuntimeTexture)
std::string TextureKey(ParticleSheet sheet);

} // namespace ParticleSheets
