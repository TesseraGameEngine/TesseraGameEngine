#pragma once

#include <cstdint>

// Minimal BC7 encoder for texture compression at load time. Uses BC7 mode 6 only: one colour
// line per 4x4 block with RGBA endpoints (7 bits + a shared low "p" bit each) and 16 steps
// between them. Noticeably better than BC1 on gradients and colourful detail (4-bit indices
// instead of 2, 8-bit endpoints instead of 5:6:5), at twice BC1's size (8 bits per texel).
namespace Bc7 {

// Encodes one block: 16 RGBA8 texels, row by row (64 bytes) -> 16 bytes of BC7
void EncodeBlockMode6(const uint8_t* rgba, uint8_t* out);

} // namespace Bc7
