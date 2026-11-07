#include "VideoPlayer.h"
#include "Scene/Scene.h"
#include "Scene/Node.h"
#include "Audio/AudioEngine.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Video {

VideoSystem& VideoSystem::Get() {
    static VideoSystem s_instance;
    return s_instance;
}

std::string VideoSystem::GetRuntimeTextureKey(uint32_t nodeId) {
    return "@video_" + std::to_string(nodeId);
}

glm::vec3 VideoSystem::GetAverageScreenColor(uint32_t nodeId) const {
    auto it = m_nodeStates.find(nodeId);
    if (it != m_nodeStates.end()) {
        return it->second.averageColor;
    }
    return glm::vec3(0.2f, 0.25f, 0.3f);
}

const VideoRuntimeState* VideoSystem::GetNodeVideoState(uint32_t nodeId) const {
    auto it = m_nodeStates.find(nodeId);
    return (it != m_nodeStates.end()) ? &it->second : nullptr;
}

void VideoSystem::ClearCache() {
    m_clipCache.clear();
    m_nodeStates.clear();
}

// Minimal 5x7 bitmap font for crisp text overlays inside procedural video channels (News ticker, CCTV timestamp, Cinema countdown, Phone UI)
static uint8_t GlyphRow(char c, int row) {
    if (row < 0 || row >= 7) return 0;
    static const uint8_t kDigits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}  // 9
    };
    if (c >= '0' && c <= '9') return kDigits[c - '0'][row];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
        case 'A': { const uint8_t g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'B': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
        case 'C': { const uint8_t g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
        case 'D': { const uint8_t g[7]={0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; return g[row]; }
        case 'E': { const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
        case 'F': { const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'G': { const uint8_t g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; return g[row]; }
        case 'H': { const uint8_t g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'I': { const uint8_t g[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
        case 'K': { const uint8_t g[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
        case 'L': { const uint8_t g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
        case 'M': { const uint8_t g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
        case 'N': { const uint8_t g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g[row]; }
        case 'O': { const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'P': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'R': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
        case 'S': { const uint8_t g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
        case 'T': { const uint8_t g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
        case 'U': { const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'V': { const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
        case 'W': { const uint8_t g[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
        case 'X': { const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
        case 'Y': { const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
        case ':': { const uint8_t g[7]={0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return g[row]; }
        case '-': { const uint8_t g[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
        case '.': { const uint8_t g[7]={0x00,0x00,0x00,0x00,0x00,0x06,0x06}; return g[row]; }
        case '/': { const uint8_t g[7]={0x01,0x02,0x04,0x04,0x08,0x10,0x00}; return g[row]; }
        default: return 0;
    }
}

static void DrawPixel(std::vector<uint8_t>& rgba, int W, int H, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    size_t idx = (static_cast<size_t>(y) * W + x) * 4;
    rgba[idx + 0] = r;
    rgba[idx + 1] = g;
    rgba[idx + 2] = b;
    rgba[idx + 3] = 255;
}

static void DrawRect(std::vector<uint8_t>& rgba, int W, int H, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    int sx = std::max(0, std::min(x0, x1));
    int ex = std::min(W - 1, std::max(x0, x1));
    int sy = std::max(0, std::min(y0, y1));
    int ey = std::min(H - 1, std::max(y0, y1));
    for (int y = sy; y <= ey; ++y) {
        for (int x = sx; x <= ex; ++x) {
            size_t idx = (static_cast<size_t>(y) * W + x) * 4;
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = 255;
        }
    }
}

static void DrawText5x7(std::vector<uint8_t>& rgba, int W, int H, int x, int y, const std::string& text,
                        uint8_t r, uint8_t g, uint8_t b, int scale = 1) {
    int curX = x;
    for (char c : text) {
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = GlyphRow(c, row);
            for (int col = 0; col < 5; ++col) {
                if ((bits >> (4 - col)) & 1) {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            DrawPixel(rgba, W, H, curX + col * scale + sx, y + row * scale + sy, r, g, b);
                        }
                    }
                }
            }
        }
        curX += 6 * scale;
    }
}

const DecodedVideoClip* VideoSystem::GetOrDecodeClip(const std::string& path, int atlasCols, int atlasRows, float defaultFps) {
    if (path.empty()) return nullptr;
    std::string cacheKey = path + "|" + std::to_string(atlasCols) + "x" + std::to_string(atlasRows);
    auto it = m_clipCache.find(cacheKey);
    if (it != m_clipCache.end()) {
        return it->second.valid ? &it->second : nullptr;
    }

    DecodedVideoClip clip;
    clip.sourcePath = path;
    clip.fps = std::max(1.0f, defaultFps);

    std::error_code ec;
    std::filesystem::path fsPath(path);

    // Case 1: Directory of numbered image frames (e.g. frames/frame_0001.png)
    if (std::filesystem::is_directory(fsPath, ec)) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(fsPath, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i < std::min<size_t>(files.size(), 240); ++i) {
            int w = 0, h = 0, ch = 0;
            stbi_uc* pixels = stbi_load(files[i].string().c_str(), &w, &h, &ch, 4);
            if (pixels && w > 0 && h > 0) {
                if (clip.width == 0) {
                    clip.width = static_cast<uint32_t>(w);
                    clip.height = static_cast<uint32_t>(h);
                }
                if (static_cast<uint32_t>(w) == clip.width && static_cast<uint32_t>(h) == clip.height) {
                    clip.frames.emplace_back(pixels, pixels + (static_cast<size_t>(w) * h * 4));
                }
                stbi_image_free(pixels);
            }
        }
        clip.frameCount = static_cast<int>(clip.frames.size());
        clip.valid = (clip.frameCount > 0);
        m_clipCache[cacheKey] = std::move(clip);
        return m_clipCache[cacheKey].valid ? &m_clipCache[cacheKey] : nullptr;
    }

    if (!std::filesystem::exists(fsPath, ec)) {
        m_clipCache[cacheKey] = clip;
        return nullptr;
    }

    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Case 2: Animated GIF file decoded natively via stbi_load_gif_from_memory
    if (ext == ".gif") {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize sz = file.tellg();
            file.seekg(0, std::ios::beg);
            if (sz > 0 && sz < 100 * 1024 * 1024) {
                std::vector<uint8_t> raw(static_cast<size_t>(sz));
                if (file.read(reinterpret_cast<char*>(raw.data()), sz)) {
                    int* delays = nullptr;
                    int w = 0, h = 0, z = 0, comp = 0;
                    stbi_uc* gifPixels = stbi_load_gif_from_memory(raw.data(), static_cast<int>(raw.size()), &delays, &w, &h, &z, &comp, 4);
                    if (gifPixels && w > 0 && h > 0 && z > 0) {
                        clip.width = static_cast<uint32_t>(w);
                        clip.height = static_cast<uint32_t>(h);
                        clip.frameCount = z;
                        if (delays && delays[0] > 0) {
                            clip.fps = std::clamp(1000.0f / static_cast<float>(delays[0]), 2.0f, 60.0f);
                        }
                        size_t frameBytes = static_cast<size_t>(w) * h * 4;
                        for (int f = 0; f < z; ++f) {
                            const stbi_uc* start = gifPixels + f * frameBytes;
                            clip.frames.emplace_back(start, start + frameBytes);
                        }
                        clip.valid = true;
                        stbi_image_free(gifPixels);
                        if (delays) stbi_image_free(delays);
                    }
                }
            }
        }
        m_clipCache[cacheKey] = std::move(clip);
        return m_clipCache[cacheKey].valid ? &m_clipCache[cacheKey] : nullptr;
    }

    // Case 3: Sprite-sheet / Contact-sheet video atlas (.png, .jpg, .tga, .bmp)
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
        int w = 0, h = 0, ch = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (pixels && w > 0 && h > 0) {
            int cols = std::max(1, atlasCols);
            int rows = std::max(1, atlasRows);
            int cellW = std::max(1, w / cols);
            int cellH = std::max(1, h / rows);
            clip.width = static_cast<uint32_t>(cellW);
            clip.height = static_cast<uint32_t>(cellH);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    std::vector<uint8_t> frame(static_cast<size_t>(cellW) * cellH * 4);
                    for (int y = 0; y < cellH; ++y) {
                        int srcY = r * cellH + y;
                        int srcX = c * cellW;
                        std::memcpy(&frame[static_cast<size_t>(y) * cellW * 4],
                                    &pixels[(static_cast<size_t>(srcY) * w + srcX) * 4],
                                    static_cast<size_t>(cellW) * 4);
                    }
                    clip.frames.push_back(std::move(frame));
                }
            }
            clip.frameCount = static_cast<int>(clip.frames.size());
            clip.valid = (clip.frameCount > 0);
            stbi_image_free(pixels);
        }
    }

    m_clipCache[cacheKey] = std::move(clip);
    return m_clipCache[cacheKey].valid ? &m_clipCache[cacheKey] : nullptr;
}

void VideoSystem::RenderFrameRGBA(int channelPreset,
                                  const std::string& sourcePath,
                                  float timeSec,
                                  float fps,
                                  int atlasCols,
                                  int atlasRows,
                                  float scanlineStrength,
                                  uint32_t outWidth,
                                  uint32_t outHeight,
                                  std::vector<uint8_t>& outRgba,
                                  glm::vec3& outAverageColor) {
    int W = static_cast<int>(std::max(16u, outWidth));
    int H = static_cast<int>(std::max(16u, outHeight));
    outRgba.resize(static_cast<size_t>(W) * H * 4);

    bool renderedExternal = false;
    if (!sourcePath.empty()) {
        const DecodedVideoClip* clip = VideoSystem::Get().GetOrDecodeClip(sourcePath, atlasCols, atlasRows, fps);
        if (clip && clip->valid && !clip->frames.empty()) {
            float rate = (fps > 0.5f) ? fps : clip->fps;
            int fIdx = static_cast<int>(std::floor(std::max(0.0f, timeSec) * rate)) % static_cast<int>(clip->frames.size());
            const auto& srcFrame = clip->frames[fIdx];
            int srcW = static_cast<int>(clip->width);
            int srcH = static_cast<int>(clip->height);
            for (int y = 0; y < H; ++y) {
                int sy = std::clamp((y * srcH) / H, 0, srcH - 1);
                for (int x = 0; x < W; ++x) {
                    int sx = std::clamp((x * srcW) / W, 0, srcW - 1);
                    size_t dstI = (static_cast<size_t>(y) * W + x) * 4;
                    size_t srcI = (static_cast<size_t>(sy) * srcW + sx) * 4;
                    outRgba[dstI + 0] = srcFrame[srcI + 0];
                    outRgba[dstI + 1] = srcFrame[srcI + 1];
                    outRgba[dstI + 2] = srcFrame[srcI + 2];
                    outRgba[dstI + 3] = 255;
                }
            }
            renderedExternal = true;
        }
    }

    if (!renderedExternal) {
        VideoChannelPreset preset = static_cast<VideoChannelPreset>(channelPreset);
        if (preset == VideoChannelPreset::ExternalFileOrAtlas) {
            preset = VideoChannelPreset::CinemaFilmReel;
        }

        if (preset == VideoChannelPreset::CinemaFilmReel) {
            // Ch 1: SMPTE 35mm Film Leader Countdown (first 3s of every 10s cycle) -> Widescreen Feature Film Sunset Reel
            float cycleT = std::fmod(std::max(0.0f, timeSec), 10.0f);
            if (cycleT < 3.0f) {
                // Vintage 35mm SMPTE countdown
                int countNum = 5 - static_cast<int>(cycleT * 1.66f);
                float sweepAngle = (cycleT * 2.0f * 3.14159265f);
                int cx = W / 2, cy = H / 2;
                int maxR = std::min(W, H) / 2 - 6;
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        int dx = x - cx, dy = y - cy;
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        float ang = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) + 3.14159265f;
                        float base = (ang < std::fmod(sweepAngle, 6.283185f)) ? 175.0f : 135.0f;
                        // Ring crosshairs
                        if (std::abs(dist - maxR * 0.85f) < 1.5f || std::abs(dist - maxR * 0.65f) < 1.5f ||
                            std::abs(dx) <= 1 || std::abs(dy) <= 1) {
                            base = 25.0f;
                        }
                        // Film grain + sepia warmth
                        uint32_t hash = static_cast<uint32_t>((x * 1973 + y * 9277 + static_cast<int>(timeSec * 24.0f) * 26699) | 1);
                        float grain = (static_cast<float>(hash & 0xFF) / 255.0f - 0.5f) * 22.0f;
                        uint8_t r = static_cast<uint8_t>(std::clamp(base * 1.05f + grain, 0.0f, 255.0f));
                        uint8_t g = static_cast<uint8_t>(std::clamp(base * 0.94f + grain, 0.0f, 255.0f));
                        uint8_t b = static_cast<uint8_t>(std::clamp(base * 0.76f + grain, 0.0f, 255.0f));
                        DrawPixel(outRgba, W, H, x, y, r, g, b);
                    }
                }
                DrawText5x7(outRgba, W, H, cx - 10, cy - 14, std::to_string(std::clamp(countNum, 1, 5)), 20, 18, 15, 4);
                DrawText5x7(outRgba, W, H, 10, 8, "35MM ACADEMY LEADER", 30, 25, 20, 1);
            } else {
                // Widescreen Feature Film Reel (Animated Outrun / Sunset Mountain Horizon & Moving Sun + Road)
                float t = cycleT - 3.0f;
                int horizonY = H * 52 / 100;
                int sunX = W / 2 + static_cast<int>(std::sin(t * 0.6f) * (W * 0.12f));
                int sunY = horizonY - H / 8;
                int sunR = std::min(W, H) / 5;
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        uint8_t r = 20, g = 15, b = 35;
                        if (y < horizonY) {
                            float skyT = static_cast<float>(y) / static_cast<float>( std::max(1, horizonY));
                            r = static_cast<uint8_t>(30 + skyT * 215);
                            g = static_cast<uint8_t>(18 + skyT * 95);
                            b = static_cast<uint8_t>(65 + (1.0f - skyT) * 90);
                            int dx = x - sunX, dy = y - sunY;
                            if (dx * dx + dy * dy < sunR * sunR) {
                                r = 255; g = static_cast<uint8_t>(210 - (y - (sunY - sunR)) * 2); b = 85;
                            }
                            // Mountain silhouette
                            float mtnH = std::sin(x * 0.045f + 1.2f) * 14.0f + std::sin(x * 0.11f) * 6.0f + 14.0f;
                            if (horizonY - y < static_cast<int>(mtnH)) {
                                r = 24; g = 16; b = 38;
                            }
                        } else {
                            // Animated perspective road / ocean reflections
                            float groundT = static_cast<float>(y - horizonY) / static_cast<float>(std::max(1, H - horizonY));
                            r = static_cast<uint8_t>(22 + groundT * 25);
                            g = static_cast<uint8_t>(18 + groundT * 20);
                            b = static_cast<uint8_t>(38 + groundT * 35);
                            float centerDist = std::abs(static_cast<float>(x - W / 2)) / (groundT * W * 0.65f + 4.0f);
                            if (centerDist < 0.45f) {
                                r = 42; g = 38; b = 52;
                                if (centerDist < 0.04f && std::fmod(1.0f / std::max(0.05f, groundT) - t * 6.0f, 1.0f) < 0.5f) {
                                    r = 245; g = 205; b = 70;
                                }
                            }
                        }
                        // Anamorphic letterbox bars
                        if (y < H / 10 || y > H - H / 10) { r = 8; g = 8; b = 10; }
                        DrawPixel(outRgba, W, H, x, y, r, g, b);
                    }
                }
                DrawText5x7(outRgba, W, H, 12, H - H / 10 + 3, "TESSERA CINEMA 2.39:1", 210, 185, 120, 1);
            }
        } else if (preset == VideoChannelPreset::TVNewsBroadcast) {
            // Ch 2: Live TV News & Weather Broadcast (Studio Anchor + Doppler Radar + Scrolling Breaking News Ticker)
            DrawRect(outRgba, W, H, 0, 0, W - 1, H - 1, 18, 34, 68);
            // Animated background world grid
            int scroll = static_cast<int>(timeSec * 24.0f);
            for (int y = 10; y < H - 32; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (((x + scroll) % 24 == 0) || (y % 18 == 0)) {
                        DrawPixel(outRgba, W, H, x, y, 28, 56, 105);
                    }
                }
            }
            // Doppler Weather Radar Box on Right
            int radX0 = W * 56 / 100, radY0 = 16, radX1 = W - 12, radY1 = H - 38;
            DrawRect(outRgba, W, H, radX0, radY0, radX1, radY1, 12, 42, 32);
            int rcx = (radX0 + radX1) / 2, rcy = (radY0 + radY1) / 2;
            float sweep = timeSec * 2.4f;
            for (int y = radY0 + 2; y < radY1 - 2; ++y) {
                for (int x = radX0 + 2; x < radX1 - 2; ++x) {
                    float dx = static_cast<float>(x - rcx), dy = static_cast<float>(y - rcy);
                    float d = std::sqrt(dx * dx + dy * dy);
                    float storm = std::sin(dx * 0.12f + timeSec) * std::cos(dy * 0.12f - timeSec * 0.7f);
                    if (storm > 0.35f && d < 36.0f) {
                        DrawPixel(outRgba, W, H, x, y, storm > 0.65f ? 235 : 45, storm > 0.65f ? 85 : 210, 65);
                    }
                    float a = std::atan2(dy, dx);
                    if (std::abs(std::sin(a - sweep)) < 0.08f && d < 40.0f) {
                        DrawPixel(outRgba, W, H, x, y, 120, 255, 150);
                    }
                }
            }
            // Anchor Silhouette on Left
            int ancX = W * 28 / 100, ancY = H * 54 / 100;
            DrawRect(outRgba, W, H, ancX - 22, ancY, ancX + 22, H - 36, 45, 52, 72); // suit
            DrawRect(outRgba, W, H, ancX - 4, ancY + 2, ancX + 4, H - 36, 200, 40, 45); // tie
            DrawRect(outRgba, W, H, ancX - 11, ancY - 22, ancX + 11, ancY - 2, 225, 180, 145); // head
            // LIVE Badge
            bool blink = (static_cast<int>(timeSec * 2.0f) % 2) == 0;
            DrawRect(outRgba, W, H, 8, 8, 52, 20, blink ? 220 : 150, 25, 30);
            DrawText5x7(outRgba, W, H, 12, 11, "LIVE TV", 255, 255, 255, 1);
            // Lower-Third Breaking News Banner & Ticker
            DrawRect(outRgba, W, H, 0, H - 34, W - 1, H - 14, 195, 22, 28);
            DrawText5x7(outRgba, W, H, 8, H - 28, "BREAKING NEWS: TESSERA LIVE FEED", 255, 255, 255, 1);
            DrawRect(outRgba, W, H, 0, H - 14, W - 1, H - 1, 240, 240, 245);
            int tickerOffset = W - (static_cast<int>(timeSec * 45.0f) % (W + 220));
            DrawText5x7(outRgba, W, H, tickerOffset, H - 11, "WEATHER ALERT: CLEAR SKIES 24C  -  MARKETS UP", 20, 25, 35, 1);
        } else if (preset == VideoChannelPreset::SecurityCCTVFeed) {
            // Ch 3: CCTV Security Camera Surveillance (Night-vision monochrome/green, REC indicator, moving target box)
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    // Perspective corridor grid
                    int cx = W / 2, cy = H / 2;
                    int dx = x - cx, dy = y - cy;
                    bool wall = (std::abs(dx) > W / 4 && std::abs(dy) < std::abs(dx) * H / W);
                    uint8_t lum = wall ? 48 : (y > cy ? 65 : 35);
                    if ((x % 32 == 0) || (y % 24 == 0)) lum = static_cast<uint8_t>(lum + 18);
                    uint32_t noise = static_cast<uint32_t>((x * 313 + y * 719 + static_cast<int>(timeSec * 30.0f) * 4099) & 0x1F);
                    uint8_t g = static_cast<uint8_t>(std::clamp<int>(lum + noise, 0, 255));
                    DrawPixel(outRgba, W, H, x, y, static_cast<uint8_t>(g * 0.72f), g, static_cast<uint8_t>(g * 0.78f));
                }
            }
            // Moving motion-tracked subject & green bounding box
            int tx = W / 2 + static_cast<int>(std::sin(timeSec * 1.3f) * (W * 0.25f));
            int ty = H * 58 / 100 + static_cast<int>(std::cos(timeSec * 2.6f) * 8.0f);
            DrawRect(outRgba, W, H, tx - 8, ty - 18, tx + 8, ty + 14, 150, 195, 160);
            // Bounding box corners
            DrawRect(outRgba, W, H, tx - 14, ty - 24, tx + 14, ty - 23, 80, 255, 100);
            DrawRect(outRgba, W, H, tx - 14, ty + 18, tx + 14, ty + 19, 80, 255, 100);
            if ((static_cast<int>(timeSec * 2.0f) % 2) == 0) {
                DrawRect(outRgba, W, H, 10, 10, 18, 18, 240, 35, 35);
            }
            DrawText5x7(outRgba, W, H, 22, 10, "REC  CAM-04 SECTOR B", 220, 255, 220, 1);
            int sec = static_cast<int>(timeSec) % 60;
            int min = (static_cast<int>(timeSec) / 60) % 60;
            char tcBuf[32];
            snprintf(tcBuf, sizeof(tcBuf), "TC 23:%02d:%02d", min, sec);
            DrawText5x7(outRgba, W, H, 10, H - 14, tcBuf, 210, 250, 210, 1);
        } else if (preset == VideoChannelPreset::SmartphoneMobileOS) {
            // Ch 4: Smartphone Mobile UI (Incoming Video Call + Live Audio Waveform + Notifications)
            DrawRect(outRgba, W, H, 0, 0, W - 1, H - 1, 16, 20, 32);
            // Top status bar
            DrawRect(outRgba, W, H, 0, 0, W - 1, 14, 10, 12, 20);
            DrawText5x7(outRgba, W, H, 6, 4, "5G  100", 220, 235, 255, 1);
            // Notification card
            DrawRect(outRgba, W, H, 10, 20, W - 10, 52, 32, 44, 68);
            DrawText5x7(outRgba, W, H, 16, 26, "INCOMING CALL", 110, 210, 255, 1);
            DrawText5x7(outRgba, W, H, 16, 38, "HQ DISPATCH", 255, 255, 255, 1);
            // Animated voice Equalizer / Waveform bars in middle
            int midY = H * 58 / 100;
            int numBars = 12;
            int barSpacing = std::max(4, (W - 24) / numBars);
            for (int b = 0; b < numBars; ++b) {
                float wave = std::abs(std::sin(timeSec * 6.5f + b * 0.7f)) * 0.75f + 0.2f;
                int bh = static_cast<int>(wave * (H * 0.20f));
                int bx = 14 + b * barSpacing;
                DrawRect(outRgba, W, H, bx, midY - bh, bx + barSpacing - 3, midY + bh, 65, 215, 165);
            }
            // Accept / Decline call buttons at bottom
            DrawRect(outRgba, W, H, 16, H - 30, W / 2 - 6, H - 10, 40, 200, 95);
            DrawText5x7(outRgba, W, H, 22, H - 23, "ACCEPT", 255, 255, 255, 1);
            DrawRect(outRgba, W, H, W / 2 + 6, H - 30, W - 16, H - 10, 215, 50, 55);
            DrawText5x7(outRgba, W, H, W / 2 + 12, H - 23, "END", 255, 255, 255, 1);
        } else if (preset == VideoChannelPreset::RetroCRTColorBars) {
            // Ch 5: SMPTE Color Bars with periodic VHS tracking bar & snow
            static const uint8_t kBars[7][3] = {
                {192, 192, 192}, {192, 192, 0}, {0, 192, 192}, {0, 192, 0},
                {192, 0, 192},   {192, 0, 0},   {0, 0, 192}
            };
            int glitchY = static_cast<int>( std::fmod(timeSec * 60.0f, static_cast<float>(H + 40)) ) - 20;
            for (int y = 0; y < H; ++y) {
                bool inGlitch = std::abs(y - glitchY) < 6;
                int xShift = inGlitch ? 12 : 0;
                for (int x = 0; x < W; ++x) {
                    int barIdx = std::clamp(((x + xShift) * 7) / W, 0, 6);
                    uint8_t r = kBars[barIdx][0], g = kBars[barIdx][1], b = kBars[barIdx][2];
                    if (inGlitch) {
                        uint32_t sn = static_cast<uint32_t>((x * 1103 + y * 4999 + static_cast<int>(timeSec * 60.0f) * 7919) & 0xFF);
                        r = g = b = static_cast<uint8_t>(sn);
                    }
                    DrawPixel(outRgba, W, H, x, y, r, g, b);
                }
            }
            DrawRect(outRgba, W, H, W / 2 - 42, H / 2 - 10, W / 2 + 42, H / 2 + 10, 15, 15, 20);
            DrawText5x7(outRgba, W, H, W / 2 - 36, H / 2 - 3, "NO SIGNAL CH-05", 240, 240, 240, 1);
        } else {
            // Ch 6: Sci-Fi Hologram & Radar Sweep Telemetry
            DrawRect(outRgba, W, H, 0, 0, W - 1, H - 1, 6, 18, 28);
            int cx = W / 3, cy = H / 2;
            int radR = std::min(W / 3, H / 2) - 10;
            float sweep = timeSec * 2.8f;
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    int dx = x - cx, dy = y - cy;
                    float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    if (std::abs(dist - radR) < 1.2f || std::abs(dist - radR * 0.5f) < 1.0f) {
                        DrawPixel(outRgba, W, H, x, y, 30, 190, 230);
                    } else if (dist < radR) {
                        float ang = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
                        float diff = std::fmod(sweep - ang + 6.283185f * 4.0f, 6.283185f);
                        if (diff < 0.65f) {
                            float intensity = (0.65f - diff) / 0.65f;
                            DrawPixel(outRgba, W, H, x, y, 20, static_cast<uint8_t>(90 + intensity * 155), static_cast<uint8_t>(120 + intensity * 135));
                        }
                    }
                }
            }
            // Oscilloscope sine wave on right half
            for (int x = W * 55 / 100; x < W - 10; ++x) {
                float phase = (x * 0.14f) + timeSec * 7.0f;
                int wy = H / 2 + static_cast<int>(std::sin(phase) * std::cos(phase * 0.4f) * (H * 0.25f));
                DrawRect(outRgba, W, H, x, wy - 1, x + 1, wy + 1, 60, 245, 255);
            }
            DrawText5x7(outRgba, W, H, W * 55 / 100, 12, "TELEMETRY ONLINE", 80, 235, 255, 1);
        }
    }

    // Apply optional CRT/LCD scanlines and compute average frame RGB color (for dynamic VXGI & screen glow)
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    float scan = std::clamp(scanlineStrength, 0.0f, 1.0f);
    for (int y = 0; y < H; ++y) {
        float rowMul = (scan > 0.001f && (y % 2 == 1)) ? (1.0f - scan * 0.42f) : 1.0f;
        for (int x = 0; x < W; ++x) {
            size_t idx = (static_cast<size_t>(y) * W + x) * 4;
            uint8_t r = static_cast<uint8_t>(outRgba[idx + 0] * rowMul);
            uint8_t g = static_cast<uint8_t>(outRgba[idx + 1] * rowMul);
            uint8_t b = static_cast<uint8_t>(outRgba[idx + 2] * rowMul);
            outRgba[idx + 0] = r;
            outRgba[idx + 1] = g;
            outRgba[idx + 2] = b;
            sumR += r;
            sumG += g;
            sumB += b;
        }
    }
    double invCount = 1.0 / (static_cast<double>(W) * H * 255.0);
    outAverageColor = glm::vec3(static_cast<float>(sumR * invCount),
                                static_cast<float>(sumG * invCount),
                                static_cast<float>(sumB * invCount));
}

void VideoSystem::Update(Scene& scene, float dt, bool isPlayMode) {
    // 1. Update Cutaway overlay if active
    if (m_cutaway.active) {
        m_cutaway.currentTime += dt;
        if (m_cutaway.duration > 0.0f && m_cutaway.currentTime >= m_cutaway.duration) {
            StopCutaway();
        } else {
            glm::vec3 avgCol(0.0f);
            RenderFrameRGBA(m_cutaway.channelPreset, m_cutaway.sourcePath, m_cutaway.currentTime, 24.0f,
                            4, 4, 0.12f, m_cutaway.width, m_cutaway.height, m_cutaway.rgbaPixels, avgCol);
        }
    }

    // 2. Update all 3D Video Screens (MeshNode with isVideoScreen) and 2D UIVideoNodes
    for (const auto& node : scene.GetNodes()) {
        if (!node) continue;
        bool isUIVideo = (node->nodeType == NodeType::UI_Video);
        if (!node->isVideoScreen && !isUIVideo) continue;

        auto& state = m_nodeStates[node->id];
        // Choose aspect-appropriate resolution (9:16 for Smartphone preset, 16:9 for TV/Cinema)
        if (node->videoChannelPreset == static_cast<int>(VideoChannelPreset::SmartphoneMobileOS)) {
            state.width = 144;
            state.height = 256;
        } else {
            state.width = 256;
            state.height = 144;
        }

        float dur = std::max(0.5f, node->videoDuration);
        if (node->videoPlaying) {
            node->videoCurrentTime += dt * std::max(0.0f, node->videoSpeed);
            if (node->videoCurrentTime >= dur) {
                if (node->videoLoop) {
                    node->videoCurrentTime = std::fmod(node->videoCurrentTime, dur);
                } else {
                    node->videoCurrentTime = dur;
                    node->videoPlaying = false;
                }
            }
        }
        if (isUIVideo) {
            node->uiProgress = std::clamp(node->videoCurrentTime / dur, 0.0f, 1.0f);
        }

        // Sync optional video audio in Play Mode
        if (isPlayMode && node->videoPlaying && !node->videoAudioPath.empty()) {
            auto& audio = Audio::AudioEngine::Get();
            if (state.activeAudioVoice == 0 || !audio.IsVoicePlaying(state.activeAudioVoice)) {
                audio.LoadWavFromFile(node->videoAudioPath, node->videoAudioPath);
                if (node->videoSpatialAudio && !isUIVideo) {
                    state.activeAudioVoice = audio.Play3D(node->videoAudioPath, node->GetWorldPosition(scene),
                                                          "SFX", node->videoAudioVolume, 2.0f, 35.0f, node->videoLoop);
                } else {
                    state.activeAudioVoice = audio.Play2D(node->videoAudioPath, "SFX",
                                                          node->videoAudioVolume, 1.0f, node->videoLoop);
                }
            }
        } else if ((!isPlayMode || !node->videoPlaying) && state.activeAudioVoice != 0) {
            Audio::AudioEngine::Get().StopVoice(state.activeAudioVoice);
            state.activeAudioVoice = 0;
        }

        float fps = std::clamp(node->videoFps, 1.0f, 60.0f);
        int frameIdx = static_cast<int>(std::floor(node->videoCurrentTime * fps));
        if (frameIdx != state.lastRenderedFrameIndex ||
            node->videoChannelPreset != state.lastPreset ||
            node->videoSourcePath != state.lastSourcePath ||
            state.rgbaPixels.empty()) {
            RenderFrameRGBA(node->videoChannelPreset,
                            node->videoSourcePath,
                            node->videoCurrentTime,
                            fps,
                            node->videoAtlasCols,
                            node->videoAtlasRows,
                            node->videoScanlineStrength,
                            state.width,
                            state.height,
                            state.rgbaPixels,
                            state.averageColor);
            state.lastRenderedFrameIndex = frameIdx;
            state.lastPreset = node->videoChannelPreset;
            state.lastSourcePath = node->videoSourcePath;
            state.dirty = true;
        }

        // Bind runtime texture key onto 3D MeshNode's Albedo + Emissive slots so screens glow & cast VXGI light
        if (!isUIVideo) {
            std::string rtKey = GetRuntimeTextureKey(node->id);
            node->texturePath = rtKey;
            if (node->videoScreenBrightness > 0.001f) {
                node->emissiveMapPath = rtKey;
                node->emissiveColor = glm::vec3(1.0f, 1.0f, 1.0f);
                node->emissiveIntensity = node->videoScreenBrightness;
            }
        }
    }
}

void VideoSystem::UploadTextures(Scene& scene, double currentTimeSec, TextureUploadCallback callback, void* userData) {
    if (!callback) return;
    for (const auto& node : scene.GetNodes()) {
        if (!node || !node->isVideoScreen || node->IsUINode()) continue;
        auto it = m_nodeStates.find(node->id);
        if (it == m_nodeStates.end()) continue;
        VideoRuntimeState& state = it->second;
        if (state.dirty && !state.rgbaPixels.empty() && (currentTimeSec - state.lastUploadTime >= (1.0 / 30.0))) {
            std::string rtKey = GetRuntimeTextureKey(node->id);
            callback(rtKey, state.rgbaPixels.data(), state.width, state.height, userData);
            state.dirty = false;
            state.lastUploadTime = currentTimeSec;
        }
    }
}

void VideoSystem::StartCutaway(int channelPreset, const std::string& sourcePath, float durationSec,
                               bool skippable, bool pictureInPicture, const std::string& title) {
    m_cutaway.active = true;
    m_cutaway.channelPreset = channelPreset;
    m_cutaway.sourcePath = sourcePath;
    m_cutaway.currentTime = 0.0f;
    m_cutaway.duration = std::max(0.5f, durationSec);
    m_cutaway.skippable = skippable;
    m_cutaway.pictureInPicture = pictureInPicture;
    m_cutaway.title = title.empty() ? "VIDEO CUTAWAY" : title;
    m_cutaway.width = (channelPreset == static_cast<int>(VideoChannelPreset::SmartphoneMobileOS)) ? 144 : 256;
    m_cutaway.height = (channelPreset == static_cast<int>(VideoChannelPreset::SmartphoneMobileOS)) ? 256 : 144;
    glm::vec3 avgCol(0.0f);
    RenderFrameRGBA(m_cutaway.channelPreset, m_cutaway.sourcePath, 0.0f, 24.0f,
                    4, 4, 0.12f, m_cutaway.width, m_cutaway.height, m_cutaway.rgbaPixels, avgCol);
}

void VideoSystem::StopCutaway() {
    m_cutaway.active = false;
    m_cutaway.currentTime = 0.0f;
}

bool VideoSystem::SkipCutaway() {
    if (!m_cutaway.active || !m_cutaway.skippable) return false;
    StopCutaway();
    return true;
}

} // namespace Video

