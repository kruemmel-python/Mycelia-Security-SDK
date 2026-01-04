#include "mycelia_vulkan_compute.h"

#include <android/log.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <stdexcept>

namespace {
constexpr const char *kLogTag = "MyceliaVulkan";

struct ScopedLog {
    static void error(const char *msg) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", msg);
    }
    static void info(const char *msg) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
    }
};

uint32_t xorshift32(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
} // namespace

struct MyceliaVulkanCompute::VulkanState {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;
};

MyceliaVulkanCompute::MyceliaVulkanCompute() = default;

MyceliaVulkanCompute::~MyceliaVulkanCompute() {
    shutdown();
}

bool MyceliaVulkanCompute::initialize() {
    if (initialized_) {
        return true;
    }

    vk_ = new VulkanState();
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Mycelia";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instanceInfo, nullptr, &vk_->instance) != VK_SUCCESS) {
        ScopedLog::error("Failed to create Vulkan instance");
        shutdown();
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vk_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        ScopedLog::error("No Vulkan physical devices found");
        shutdown();
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(vk_->instance, &deviceCount, devices.data());
    vk_->physicalDevice = devices.front();

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_->physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundCompute = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            vk_->computeQueueFamily = i;
            foundCompute = true;
            break;
        }
    }

    if (!foundCompute) {
        ScopedLog::error("No compute queue found");
        shutdown();
        return false;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = vk_->computeQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    if (vkCreateDevice(vk_->physicalDevice, &deviceInfo, nullptr, &vk_->device) != VK_SUCCESS) {
        ScopedLog::error("Failed to create Vulkan device");
        shutdown();
        return false;
    }

    vkGetDeviceQueue(vk_->device, vk_->computeQueueFamily, 0, &vk_->computeQueue);

    initialized_ = true;
    ScopedLog::info("Vulkan compute initialized");
    return true;
}

void MyceliaVulkanCompute::shutdown() {
    if (!vk_) {
        initialized_ = false;
        return;
    }

    if (vk_->device != VK_NULL_HANDLE) {
        vkDestroyDevice(vk_->device, nullptr);
    }

    if (vk_->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(vk_->instance, nullptr);
    }

    delete vk_;
    vk_ = nullptr;
    initialized_ = false;
}

bool MyceliaVulkanCompute::ensureVulkanReady() {
    if (initialized_) {
        return true;
    }
    return initialize();
}

std::vector<uint8_t> MyceliaVulkanCompute::generateKeystream(std::size_t size,
                                                             const std::vector<uint8_t> &seed) {
    std::vector<uint8_t> keystream(size, 0);
    if (size == 0) {
        return keystream;
    }

    uint32_t state = 0xA5A5A5A5u;
    for (uint8_t byte : seed) {
        state ^= static_cast<uint32_t>(byte) + 0x9e3779b9u + (state << 6) + (state >> 2);
    }

    for (std::size_t i = 0; i < size; ++i) {
        uint32_t next = xorshift32(state);
        keystream[i] = static_cast<uint8_t>(next & 0xFFu);
    }

    return keystream;
}

bool MyceliaVulkanCompute::dispatchXor(const std::vector<uint8_t> &input,
                                      std::vector<uint8_t> &output,
                                      const std::vector<uint8_t> &keystream) {
    if (input.size() != keystream.size()) {
        ScopedLog::error("Keystream size mismatch");
        return false;
    }

    output.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = static_cast<uint8_t>(input[i] ^ keystream[i]);
    }

    return true;
}

bool MyceliaVulkanCompute::encrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed) {
    if (!ensureVulkanReady()) {
        ScopedLog::error("Vulkan not initialized");
        return false;
    }

    auto keystream = generateKeystream(input.size(), seed);
    return dispatchXor(input, output, keystream);
}

bool MyceliaVulkanCompute::decrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed) {
    if (!ensureVulkanReady()) {
        ScopedLog::error("Vulkan not initialized");
        return false;
    }

    auto keystream = generateKeystream(input.size(), seed);
    return dispatchXor(input, output, keystream);
}
