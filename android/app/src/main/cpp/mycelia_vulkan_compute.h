#pragma once

#include <cstdint>
#include <string>
#include <vector>

class MyceliaVulkanCompute {
public:
    explicit MyceliaVulkanCompute(std::string shader_dir = "");
    ~MyceliaVulkanCompute();

    bool initialize();
    void shutdown();

    bool encrypt(const std::vector<uint8_t> &input,
                 std::vector<uint8_t> &output,
                 const std::vector<uint8_t> &seed,
                 uint64_t stream_offset);

    bool decrypt(const std::vector<uint8_t> &input,
                 std::vector<uint8_t> &output,
                 const std::vector<uint8_t> &seed,
                 uint64_t stream_offset);

private:
    struct VulkanState;

    bool ensureVulkanReady();
    bool ensurePipelines();
    bool ensureBuffers(std::size_t input_size_bytes);
    bool recordAndSubmitBlock(uint32_t offset_in_block,
                              uint32_t chunk_len_bytes);

    bool uploadInput(const std::vector<uint8_t> &input);
    void downloadOutput(std::vector<uint8_t> &output, std::size_t size_bytes);

    uint64_t seedToUint64(const std::vector<uint8_t> &seed_bytes) const;

    std::string shader_dir_;
    bool initialized_ = false;

    VulkanState *vk_ = nullptr;
};
