#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanContext {
public:
    static VulkanContext& Get();

    void Init(GLFWwindow* window);
    void Cleanup();

    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    bool SupportsTextureCompressionBC() const { return m_supportsTextureCompressionBC; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    VkCommandPool GetCommandPool() const { return m_commandPool; }
    QueueFamilyIndices GetQueueIndices() const { return m_queueIndices; }
    // 1.0 when anisotropic filtering is unsupported
    float GetMaxSamplerAnisotropy() const { return m_maxSamplerAnisotropy; }

    // Hardware ray tracing (VK_KHR_acceleration_structure + VK_KHR_ray_query), enabled at device
    // creation when the GPU supports it. Buffer device addresses come with it.
    bool SupportsRayQuery() const { return m_supportsRayQuery; }
    bool SupportsBufferDeviceAddress() const { return m_supportsBufferDeviceAddress; }
    // Bindless sampled-image arrays (descriptor indexing): used by the ray tracing alpha test
    bool SupportsDescriptorIndexing() const { return m_supportsDescriptorIndexing; }
    uint32_t GetMinASScratchAlignment() const { return m_minASScratchAlignment; }
    // Set before Init() to refuse ray tracing (e.g. --no-raytracing), so the fallback path can be tested
    void SetRayTracingAllowed(bool allowed) { m_rayTracingAllowed = allowed; }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    VkFormat FindDepthFormat() const;

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

private:
    VulkanContext() = default;
    ~VulkanContext() = default;

    void CreateInstance();
    void CreateSurface(GLFWwindow* window);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateCommandPool();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
    bool DeviceHasExtension(VkPhysicalDevice device, const char* name) const;
    bool DeviceSupportsRayQuery(VkPhysicalDevice device) const;

    bool m_supportsRayQuery{false};
    bool m_supportsBufferDeviceAddress{false};
    bool m_supportsDescriptorIndexing{false};
    bool m_rayTracingAllowed{true};
    uint32_t m_minASScratchAlignment{256};

    VkInstance m_instance{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    bool m_supportsTextureCompressionBC{false};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    float m_maxSamplerAnisotropy{1.0f};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    QueueFamilyIndices m_queueIndices;

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

