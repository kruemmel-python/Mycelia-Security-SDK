#include "mycelia_vulkan_compute.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <iostream>
#endif

namespace {
#ifdef __ANDROID__
constexpr const char *kLogTag = "MyceliaVulkan";
inline void log_info(const char *msg) { __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg); }
inline void log_error(const char *msg) { __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", msg); }
#else
inline void log_info(const char *msg) { std::cerr << "[MyceliaVulkan] " << msg << "\n"; }
inline void log_error(const char *msg) { std::cerr << "[MyceliaVulkan] ERROR: " << msg << "\n"; }
#endif

constexpr uint32_t kLocalSize = 256;

inline uint32_t alignUp(uint32_t v, uint32_t a) {
    return (v + a - 1u) / a * a;
}

static std::vector<uint32_t> read_spv_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open SPV file: " + path);
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || (size % 4) != 0) {
        std::fclose(f);
        throw std::runtime_error("Invalid SPV file size: " + path);
    }
    std::vector<uint32_t> buf(static_cast<size_t>(size / 4));
    if (std::fread(buf.data(), 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) {
        std::fclose(f);
        throw std::runtime_error("Failed to read SPV file: " + path);
    }
    std::fclose(f);
    return buf;
}

static void key_nonce_from_seed(uint64_t seed, std::array<uint32_t, 8> &key, std::array<uint32_t, 3> &nonce) {
    // Einfache deterministische Ableitung (nicht kryptografisch die "Seed-Härtung" selbst,
    // sondern nur ein deterministischer Mixer als Wrapper). Die eigentliche Sicherheit kommt
    // durch ChaCha20-Keystream + korrekte Nonce/Counter-Policy.
    auto mix32 = [](uint64_t x) -> uint32_t {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return static_cast<uint32_t>(x & 0xFFFFFFFFu);
    };

    for (int i = 0; i < 8; ++i) {
        key[i] = mix32(seed + static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL);
    }

    nonce[0] = mix32(seed ^ 0xA5A5A5A5A5A5A5A5ULL);
    nonce[1] = mix32(seed ^ 0x0123456789ABCDEFULL);
    nonce[2] = mix32(seed ^ 0xF0E1D2C3B4A59687ULL);
}

} // namespace

struct MyceliaVulkanCompute::VulkanState {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline xorPipeline = VK_NULL_HANDLE;

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        VkDeviceSize size = 0;
    };

    Buffer input;
    Buffer output;
};

MyceliaVulkanCompute::MyceliaVulkanCompute(std::string shader_dir)
    : shader_dir_(std::move(shader_dir)) {}

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
        log_error("Failed to create Vulkan instance");
        shutdown();
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vk_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        log_error("No Vulkan physical devices found");
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
        log_error("No compute queue found");
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
        log_error("Failed to create Vulkan device");
        shutdown();
        return false;
    }

    vkGetDeviceQueue(vk_->device, vk_->computeQueueFamily, 0, &vk_->computeQueue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = vk_->computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(vk_->device, &poolInfo, nullptr, &vk_->commandPool) != VK_SUCCESS) {
        log_error("Failed to create command pool");
        shutdown();
        return false;
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = vk_->commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vk_->device, &cmdAlloc, &vk_->commandBuffer) != VK_SUCCESS) {
        log_error("Failed to allocate command buffer");
        shutdown();
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(vk_->device, &fenceInfo, nullptr, &vk_->fence) != VK_SUCCESS) {
        log_error("Failed to create fence");
        shutdown();
        return false;
    }

    if (!ensurePipelines()) {
        log_error("Failed to ensure pipelines");
        shutdown();
        return false;
    }

    initialized_ = true;
    log_info("Vulkan compute initialized");
    return true;
}

void MyceliaVulkanCompute::shutdown() {
    if (!vk_) {
        initialized_ = false;
        return;
    }

    if (vk_->device != VK_NULL_HANDLE) {
        if (vk_->input.mapped) {
            vkUnmapMemory(vk_->device, vk_->input.memory);
            vk_->input.mapped = nullptr;
        }
        if (vk_->output.mapped) {
            vkUnmapMemory(vk_->device, vk_->output.memory);
            vk_->output.mapped = nullptr;
        }

        if (vk_->xorPipeline) vkDestroyPipeline(vk_->device, vk_->xorPipeline, nullptr);
        if (vk_->pipelineLayout) vkDestroyPipelineLayout(vk_->device, vk_->pipelineLayout, nullptr);

        if (vk_->descriptorPool) vkDestroyDescriptorPool(vk_->device, vk_->descriptorPool, nullptr);
        if (vk_->descriptorSetLayout) vkDestroyDescriptorSetLayout(vk_->device, vk_->descriptorSetLayout, nullptr);

        if (vk_->input.buffer) vkDestroyBuffer(vk_->device, vk_->input.buffer, nullptr);
        if (vk_->input.memory) vkFreeMemory(vk_->device, vk_->input.memory, nullptr);

        if (vk_->output.buffer) vkDestroyBuffer(vk_->device, vk_->output.buffer, nullptr);
        if (vk_->output.memory) vkFreeMemory(vk_->device, vk_->output.memory, nullptr);

        if (vk_->fence) vkDestroyFence(vk_->device, vk_->fence, nullptr);
        if (vk_->commandPool) vkDestroyCommandPool(vk_->device, vk_->commandPool, nullptr);

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

static uint32_t findMemoryType(VkPhysicalDevice phy, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phy, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && ((memProps.memoryTypes[i].propertyFlags & props) == props)) {
            return i;
        }
    }
    throw std::runtime_error("No suitable memory type found");
}

bool MyceliaVulkanCompute::ensurePipelines() {
    if (!vk_ || !vk_->device) {
        return false;
    }
    if (vk_->xorPipeline != VK_NULL_HANDLE) {
        return true;
    }

    VkDescriptorSetLayoutBinding inBind{};
    inBind.binding = 17;
    inBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    inBind.descriptorCount = 1;
    inBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding outBind{};
    outBind.binding = 18;
    outBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outBind.descriptorCount = 1;
    outBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[2] = {inBind, outBind};

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vk_->device, &dsl, nullptr, &vk_->descriptorSetLayout) != VK_SUCCESS) {
        log_error("Failed to create descriptor set layout");
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = poolSizes;
    dpci.maxSets = 1;

    if (vkCreateDescriptorPool(vk_->device, &dpci, nullptr, &vk_->descriptorPool) != VK_SUCCESS) {
        log_error("Failed to create descriptor pool");
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = vk_->descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &vk_->descriptorSetLayout;

    if (vkAllocateDescriptorSets(vk_->device, &dsai, &vk_->descriptorSet) != VK_SUCCESS) {
        log_error("Failed to allocate descriptor set");
        return false;
    }

	VkPushConstantRange pushXor{};
	pushXor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushXor.offset = 0;
	pushXor.size = 64;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vk_->descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushXor;

    if (vkCreatePipelineLayout(vk_->device, &pipelineLayoutInfo, nullptr, &vk_->pipelineLayout) != VK_SUCCESS) {
        log_error("Failed to create pipeline layout");
        return false;
    }

    std::string spvPath = shader_dir_.empty()
        ? std::string("mycelia_keystream_xor.spv")
        : (shader_dir_ + "/mycelia_keystream_xor.spv");

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    try {
        auto spv = read_spv_file(spvPath);
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spv.size() * sizeof(uint32_t);
        smci.pCode = spv.data();
        if (vkCreateShaderModule(vk_->device, &smci, nullptr, &shaderModule) != VK_SUCCESS) {
            log_error("Failed to create shader module");
            return false;
        }
    } catch (const std::exception &e) {
        log_error(e.what());
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shaderModule;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = vk_->pipelineLayout;

    if (vkCreateComputePipelines(vk_->device, VK_NULL_HANDLE, 1, &cpci, nullptr, &vk_->xorPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(vk_->device, shaderModule, nullptr);
        log_error("Failed to create compute pipeline");
        return false;
    }

    vkDestroyShaderModule(vk_->device, shaderModule, nullptr);
    return true;
}

bool MyceliaVulkanCompute::ensureBuffers(std::size_t input_size_bytes) {
    if (!vk_ || !vk_->device) return false;

    const VkDeviceSize needed = static_cast<VkDeviceSize>(alignUp(static_cast<uint32_t>(input_size_bytes), 4));

    auto ensureOne = [&](VulkanState::Buffer &buf) -> bool {
        if (buf.buffer != VK_NULL_HANDLE && buf.size >= needed) {
            return true;
        }

        if (buf.mapped) {
            vkUnmapMemory(vk_->device, buf.memory);
            buf.mapped = nullptr;
        }
        if (buf.buffer) vkDestroyBuffer(vk_->device, buf.buffer, nullptr);
        if (buf.memory) vkFreeMemory(vk_->device, buf.memory, nullptr);
        buf.buffer = VK_NULL_HANDLE;
        buf.memory = VK_NULL_HANDLE;
        buf.size = 0;

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = needed;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(vk_->device, &bci, nullptr, &buf.buffer) != VK_SUCCESS) {
            log_error("Failed to create buffer");
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(vk_->device, buf.buffer, &memReq);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(vk_->physicalDevice, memReq.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(vk_->device, &mai, nullptr, &buf.memory) != VK_SUCCESS) {
            log_error("Failed to allocate buffer memory");
            return false;
        }

        if (vkBindBufferMemory(vk_->device, buf.buffer, buf.memory, 0) != VK_SUCCESS) {
            log_error("Failed to bind buffer memory");
            return false;
        }

        if (vkMapMemory(vk_->device, buf.memory, 0, needed, 0, &buf.mapped) != VK_SUCCESS) {
            log_error("Failed to map buffer memory");
            return false;
        }

        buf.size = needed;
        return true;
    };

    if (!ensureOne(vk_->input)) return false;
    if (!ensureOne(vk_->output)) return false;

    VkDescriptorBufferInfo inInfo{};
    inInfo.buffer = vk_->input.buffer;
    inInfo.offset = 0;
    inInfo.range = vk_->input.size;

    VkDescriptorBufferInfo outInfo{};
    outInfo.buffer = vk_->output.buffer;
    outInfo.offset = 0;
    outInfo.range = vk_->output.size;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = vk_->descriptorSet;
    writes[0].dstBinding = 17;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &inInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = vk_->descriptorSet;
    writes[1].dstBinding = 18;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &outInfo;

    vkUpdateDescriptorSets(vk_->device, 2, writes, 0, nullptr);
    return true;
}

bool MyceliaVulkanCompute::uploadInput(const std::vector<uint8_t> &input) {
    if (!vk_ || !vk_->input.mapped) return false;
    std::memset(vk_->input.mapped, 0, static_cast<size_t>(vk_->input.size));
    if (!input.empty()) {
        std::memcpy(vk_->input.mapped, input.data(), input.size());
    }
    return true;
}

void MyceliaVulkanCompute::downloadOutput(std::vector<uint8_t> &output, std::size_t size_bytes) {
    output.resize(size_bytes);
    if (!vk_ || !vk_->output.mapped) {
        return;
    }
    std::memcpy(output.data(), vk_->output.mapped, size_bytes);
}

uint64_t MyceliaVulkanCompute::seedToUint64(const std::vector<uint8_t> &seed_bytes) const {
    uint64_t seed = 0;
    size_t len = seed_bytes.size() < sizeof(uint64_t) ? seed_bytes.size() : sizeof(uint64_t);
    for (size_t i = 0; i < len; ++i) {
        seed |= static_cast<uint64_t>(seed_bytes[i]) << (8 * i);
    }
    return seed;
}

bool MyceliaVulkanCompute::encrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed,
                                  uint64_t stream_offset) {
    if (!ensureVulkanReady()) {
        log_error("Vulkan not initialized");
        return false;
    }

    if (input.empty()) {
        output.clear();
        return true;
    }

    if (!ensureBuffers(input.size())) {
        log_error("Failed to ensure buffers");
        return false;
    }

    if (!uploadInput(input)) {
        log_error("Failed to upload input");
        return false;
    }

    uint64_t master_seed = seedToUint64(seed);
    std::array<uint32_t, 8> key{};
    std::array<uint32_t, 3> nonce{};
    key_nonce_from_seed(master_seed, key, nonce);

    uint64_t block_index = stream_offset / 64u;
    uint32_t counter_base = static_cast<uint32_t>(block_index & 0xFFFFFFFFu);
    uint32_t counter_high = static_cast<uint32_t>((block_index >> 32) & 0xFFFFFFFFu);
    nonce[2] ^= counter_high;
    uint32_t offset_in_block = static_cast<uint32_t>(stream_offset % 64u);

    VkCommandBuffer cmd = vk_->commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->pipelineLayout, 0, 1, &vk_->descriptorSet, 0, nullptr);

	struct XorParamsPC final {
		uint32_t key[8];          // 0..31
		uint32_t nonce0;          // 32..35
		uint32_t nonce1;          // 36..39
		uint32_t nonce2;          // 40..43
		uint32_t counter_base;    // 44..47
		uint32_t offset_in_block; // 48..51
		uint32_t data_len;        // 52..55
		uint32_t pad0;            // 56..59
		uint32_t pad1;            // 60..63
	} xorParams{};
	static_assert(sizeof(XorParamsPC) == 64, "PushConstant size must be 64 bytes");

	for (int j = 0; j < 8; ++j) {
		xorParams.key[j] = key[j];
	}
	xorParams.nonce0 = nonce[0];
	xorParams.nonce1 = nonce[1];
	xorParams.nonce2 = nonce[2];

	xorParams.counter_base    = counter_base;
	xorParams.offset_in_block = offset_in_block;
	xorParams.data_len        = static_cast<uint32_t>(input.size());
	xorParams.pad0            = 0;
	xorParams.pad1            = 0;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->xorPipeline);
	vkCmdPushConstants(cmd, vk_->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(xorParams), &xorParams);

    uint32_t wordCount = alignUp(static_cast<uint32_t>(input.size()), 4) / 4;
    uint32_t xorGroups = (wordCount + kLocalSize - 1) / kLocalSize;
    vkCmdDispatch(cmd, xorGroups, 1, 1);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        return false;
    }

    vkResetFences(vk_->device, 1, &vk_->fence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(vk_->computeQueue, 1, &submitInfo, vk_->fence) != VK_SUCCESS) {
        return false;
    }

    if (vkWaitForFences(vk_->device, 1, &vk_->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return false;
    }

    downloadOutput(output, input.size());
    return true;
}

bool MyceliaVulkanCompute::decrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed,
                                  uint64_t stream_offset) {
    return encrypt(input, output, seed, stream_offset);
}
