#pragma once

#include <cstdint>
#include <vector>

class MyceliaVulkanCompute {
public:
    MyceliaVulkanCompute();
    ~MyceliaVulkanCompute();

    bool initialize();
    void shutdown();

    bool encrypt(const std::vector<uint8_t> &input,
                 std::vector<uint8_t> &output,
                 const std::vector<uint8_t> &seed);

    bool decrypt(const std::vector<uint8_t> &input,
                 std::vector<uint8_t> &output,
                 const std::vector<uint8_t> &seed);

private:
    bool ensureVulkanReady();
    bool dispatchXor(const std::vector<uint8_t> &input,
                     std::vector<uint8_t> &output,
                     const std::vector<uint8_t> &keystream);

    std::vector<uint8_t> generateKeystream(std::size_t size,
                                           const std::vector<uint8_t> &seed);

    bool initialized_ = false;

    // Vulkan handles (kept minimal; created on init)
    struct VulkanState;
    VulkanState *vk_ = nullptr;
};
