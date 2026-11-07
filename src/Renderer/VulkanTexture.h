#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <imgui.h>

class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() = default;

    static std::shared_ptr<VulkanTexture> CreateDefaultWhite();
    static std::shared_ptr<VulkanTexture> CreateDefaultNormal();
    static std::shared_ptr<VulkanTexture> LoadFromFile(const std::string& filepath);
    // How material textures are block-compressed at load (when the GPU supports BC formats)
    enum class CompressionQuality {
        Off,      // plain RGBA8
        Standard, // colour: BC1 (opaque) / BC3 (with alpha); normal maps: BC5
        High      // colour: BC7; normal maps: BC5
    };
    // What a texture holds, which decides its compressed format
    enum class TextureUsage {
        Color,    // albedo, metallic, roughness, AO, emissive
        NormalMap // tangent-space normals: only X and Y are stored (BC5); shaders rebuild Z
    };
    // Material texture, block-compressed per the current quality, with a CPU-built mip chain. The
    // result is cached under cache/textures/ (keyed by path, size, modification time and format)
    // so later loads skip the compression. Falls back to LoadFromFile when compression is off or
    // unsupported.
    static std::shared_ptr<VulkanTexture> LoadCompressedFromFile(const std::string& filepath,
                                                                 TextureUsage usage = TextureUsage::Color);
    static void SetCompressionQuality(CompressionQuality quality) { s_compressionQuality = quality; }
    static CompressionQuality GetCompressionQuality() { return s_compressionQuality; }
    // Radiance .hdr (or any float image stb can read) as a mipmapped RGBA16F texture, for HDRI
    // environments. Returns nullptr if the file can't be read.
    static std::shared_ptr<VulkanTexture> LoadHDRFromFile(const std::string& filepath);
    // Colour grading LUT: an Adobe/Resolve .cube file, or an image strip N*N wide and N high
    // (N slices side by side, blue = slice, red = x within a slice, green = y; the layout Unreal
    // and Unity use). Returned as an RGBA8 strip in that layout; outSize receives N. Returns
    // nullptr if the file can't be read or isn't a valid LUT.
    static std::shared_ptr<VulkanTexture> LoadColorLUT(const std::string& filepath, uint32_t& outSize);
    // An RGBA8 (linear) texture made from pixels, with mips and edge clamping, that can be updated
    // in place (UpdatePixels) without changing its view, so descriptor sets using it stay valid:
    // for data painted in the editor, such as terrain splat maps
    static std::shared_ptr<VulkanTexture> CreateFromPixels(const unsigned char* rgba, uint32_t width, uint32_t height);
    // Waits for the GPU, then replaces the pixels (same size) and rebuilds the mips
    void UpdatePixels(const unsigned char* rgba);

    void Cleanup();

    VkImage GetImage() const { return m_image; }
    VkImageView GetImageView() const { return m_imageView; }
    VkSampler GetSampler() const { return m_sampler; }
    VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }
    void SetDescriptorSet(VkDescriptorSet ds) { m_descriptorSet = ds; }

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    const std::string& GetFilePath() const { return m_filePath; }

private:
    bool InitFromMemory(const unsigned char* pixels, uint32_t width, uint32_t height, VkFormat format,
                        uint32_t bytesPerPixel = 4);
    // Uploads pre-compressed mip levels (level 0 first) of a block-compressed format
    bool InitCompressed(VkFormat format, uint32_t width, uint32_t height, const std::vector<std::vector<unsigned char>>& mips);
    bool CreateViewAndSampler(VkFormat format, uint32_t mipLevels);
    bool m_clampToEdge{false};
    uint32_t m_mipLevels{1};

    static inline CompressionQuality s_compressionQuality = CompressionQuality::Standard;

    std::string m_filePath{""};
    uint32_t m_width{0};
    uint32_t m_height{0};

    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_imageMemory{VK_NULL_HANDLE};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
};

