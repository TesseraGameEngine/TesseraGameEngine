#include "VulkanContext.h"
#include "Core/Version.h"
#include <iostream>
#include <set>
#include <cstring>
#include <stdexcept>
#include <algorithm>

VulkanContext& VulkanContext::Get() {
    static VulkanContext instance;
    return instance;
}

void VulkanContext::Init(GLFWwindow* window) {
    CreateInstance();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateCommandPool();
}

void VulkanContext::Cleanup() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Tessera";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Tessera";
    appInfo.engineVersion = VK_MAKE_VERSION(TESSERA_VERSION_MAJOR, TESSERA_VERSION_MINOR, TESSERA_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Query available instance extensions from the active Vulkan loader and driver
    uint32_t availableExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(availableExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());

    std::cout << "[VulkanContext] Available Vulkan instance extensions (" << availableExtCount << "):" << std::endl;
    for (const auto& prop : availableExts) {
        std::cout << "  * " << prop.extensionName << std::endl;
    }

    auto isSupported = [&](const char* name) {
        for (const auto& prop : availableExts) {
            if (std::strcmp(prop.extensionName, name) == 0) return true;
        }
        return false;
    };

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> desiredExtensions;
    if (glfwExtensions && glfwExtensionCount > 0) {
        desiredExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    } else {
        const char* errDesc = nullptr;
        int errCode = glfwGetError(&errDesc);
        std::cerr << "[VulkanContext] WARNING: glfwGetRequiredInstanceExtensions returned 0 extensions. GLFW error: "
                  << errCode << " (" << (errDesc ? errDesc : "none") << ")" << std::endl;
        desiredExtensions.push_back("VK_KHR_surface");
        desiredExtensions.push_back("VK_KHR_xcb_surface");
        desiredExtensions.push_back("VK_KHR_xlib_surface");
        desiredExtensions.push_back("VK_KHR_wayland_surface");
    }

    std::vector<const char*> enabledExtensions;
    for (const char* ext : desiredExtensions) {
        if (isSupported(ext)) {
            enabledExtensions.push_back(ext);
        } else {
            std::cout << "[VulkanContext] Skipping unsupported extension: " << ext << std::endl;
        }
    }

    // Ensure VK_KHR_surface is present if available
    if (isSupported("VK_KHR_surface")) {
        bool hasSurface = false;
        for (const char* ext : enabledExtensions) {
            if (std::strcmp(ext, "VK_KHR_surface") == 0) { hasSurface = true; break; }
        }
        if (!hasSurface) enabledExtensions.push_back("VK_KHR_surface");
    }

    std::cout << "[VulkanContext] Final enabled instance extensions (" << enabledExtensions.size() << "):" << std::endl;
    for (const char* ext : enabledExtensions) {
        std::cout << "  - " << ext << std::endl;
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;

    VkResult instRes = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (instRes != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance! Error code: " + std::to_string(instRes));
    }
}

void VulkanContext::CreateSurface(GLFWwindow* window) {
    VkResult res = glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
    if (res != VK_SUCCESS) {
        const char* errDesc = nullptr;
        int errCode = glfwGetError(&errDesc);
        std::string msg = "Failed to create window surface! VkResult: " + std::to_string(res);
        if (errDesc) {
            msg += " (GLFW error " + std::to_string(errCode) + ": " + errDesc + ")";
        }
        std::cerr << "[VulkanContext] " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}

QueueFamilyIndices VulkanContext::FindQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) break;
        i++;
    }

    return indices;
}

bool VulkanContext::CheckDeviceExtensionSupport(VkPhysicalDevice device) const {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

bool VulkanContext::DeviceHasExtension(VkPhysicalDevice device, const char* name) const {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    for (const auto& ext : availableExtensions) {
        if (std::strcmp(ext.extensionName, name) == 0) return true;
    }
    return false;
}

bool VulkanContext::DeviceSupportsRayQuery(VkPhysicalDevice device) const {
    if (!DeviceHasExtension(device, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
        !DeviceHasExtension(device, VK_KHR_RAY_QUERY_EXTENSION_NAME) ||
        !DeviceHasExtension(device, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
        return false;
    }
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    as.pNext = &rq;
    v12.pNext = &as;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &v12;
    vkGetPhysicalDeviceFeatures2(device, &f2);
    return rq.rayQuery && as.accelerationStructure && v12.bufferDeviceAddress;
}

void VulkanContext::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Score every usable GPU: a discrete GPU beats an integrated one, and hardware ray tracing
    // breaks ties (laptops and desktops with an iGPU list both)
    int bestScore = -1;
    for (const auto& device : devices) {
        QueueFamilyIndices indices = FindQueueFamilies(device);
        bool extensionsSupported = CheckDeviceExtensionSupport(device);
        if (!indices.isComplete() || !extensionsSupported) continue;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;
        if (DeviceSupportsRayQuery(device)) score += 500;
        std::cout << "[VulkanContext] GPU candidate: " << props.deviceName << " (score " << score << ")" << std::endl;
        if (score > bestScore) {
            bestScore = score;
            m_physicalDevice = device;
            m_queueIndices = indices;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void VulkanContext::CreateLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueIndices.graphicsFamily.value(),
        m_queueIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_FALSE;
    m_maxSamplerAnisotropy = 1.0f;
    if (supportedFeatures.samplerAnisotropy) {
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        m_maxSamplerAnisotropy = props.limits.maxSamplerAnisotropy;
    }
    if (supportedFeatures.fillModeNonSolid) {
        deviceFeatures.fillModeNonSolid = VK_TRUE;
    }
    // Block-compressed textures (BC1/BC3): a quarter to an eighth of the memory of RGBA8
    m_supportsTextureCompressionBC = supportedFeatures.textureCompressionBC == VK_TRUE;
    deviceFeatures.textureCompressionBC = supportedFeatures.textureCompressionBC;

    // Optional features go through a VkPhysicalDeviceFeatures2 chain (core 1.2 + ray tracing)
    VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    {
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &query);
    }

    std::vector<const char*> extensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    VkPhysicalDeviceVulkan12Features enable12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enableAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR enableRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    m_supportsDescriptorIndexing = supported12.descriptorIndexing && supported12.runtimeDescriptorArray &&
                                   supported12.descriptorBindingPartiallyBound &&
                                   supported12.descriptorBindingVariableDescriptorCount &&
                                   supported12.shaderSampledImageArrayNonUniformIndexing;
    m_supportsRayQuery = m_rayTracingAllowed && m_supportsDescriptorIndexing && DeviceSupportsRayQuery(m_physicalDevice);

    void* featureChain = nullptr;
    if (m_supportsRayQuery) {
        enable12.bufferDeviceAddress = VK_TRUE;
        enable12.descriptorIndexing = VK_TRUE;
        enable12.runtimeDescriptorArray = VK_TRUE;
        enable12.descriptorBindingPartiallyBound = VK_TRUE;
        enable12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        enable12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        enableAS.accelerationStructure = VK_TRUE;
        enableRQ.rayQuery = VK_TRUE;
        enable12.pNext = &enableAS;
        enableAS.pNext = &enableRQ;
        featureChain = &enable12;
        extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        m_supportsBufferDeviceAddress = true;

        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props2.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
        m_minASScratchAlignment = std::max<uint32_t>(asProps.minAccelerationStructureScratchOffsetAlignment, 1u);
    }

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.features = deviceFeatures;
    features2.pNext = featureChain;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr; // given by features2
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        std::cout << "[VulkanContext] Using GPU: " << props.deviceName
                  << (m_supportsRayQuery ? " (hardware ray tracing: ray query enabled)" : " (no hardware ray tracing)") << std::endl;
    }

    vkGetDeviceQueue(m_device, m_queueIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueIndices.presentFamily.value(), 0, &m_presentQueue);
}

void VulkanContext::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

VkFormat VulkanContext::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

VkFormat VulkanContext::FindDepthFormat() const {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkCommandBuffer VulkanContext::BeginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

