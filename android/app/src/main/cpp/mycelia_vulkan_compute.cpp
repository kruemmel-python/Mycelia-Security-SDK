#include "mycelia_vulkan_compute.h"

#include <vulkan/vulkan.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace {
constexpr uint32_t kBlockSize = 256 * 256; // 65536 floats/bytes
constexpr uint32_t kLocalSize = 256;

void log_info(const char *msg) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "MyceliaVulkan", "%s", msg);
#else
    std::fprintf(stdout, "[MyceliaVulkan] %s\n", msg);
#endif
}

void log_error(const char *msg) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, "MyceliaVulkan", "%s", msg);
#else
    std::fprintf(stderr, "[MyceliaVulkan][ERR] %s\n", msg);
#endif
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void *mapped = nullptr;
};

struct ShaderModuleOwned {
    VkShaderModule module = VK_NULL_HANDLE;
    std::vector<uint32_t> code;
};

VkShaderModule createShaderModule(VkDevice device, const std::vector<uint32_t> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
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

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkPipeline initPipeline = VK_NULL_HANDLE;
    VkPipeline simulationPipeline = VK_NULL_HANDLE;
    VkPipeline xorPipeline = VK_NULL_HANDLE;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    Buffer energy;
    Buffer phase;
    Buffer interference;
    Buffer nodeFlag;
    Buffer spin;
    Buffer topology;
    Buffer pressure;
    Buffer gravity;
    Buffer magnetism;
    Buffer temperature;
    Buffer potential;
    Buffer driftX;
    Buffer driftY;
    Buffer rngEnergy;
    Buffer rngPhase;
    Buffer rngSpin;
    Buffer fieldMap;
    Buffer input;
    Buffer output;
};

MyceliaVulkanCompute::MyceliaVulkanCompute(std::string shader_dir)
    : shader_dir_(std::move(shader_dir)) {}

MyceliaVulkanCompute::~MyceliaVulkanCompute() {
    shutdown();
}

static bool createBuffer(VkPhysicalDevice physicalDevice,
                         VkDevice device,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         Buffer &out) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &out.buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, out.buffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((memRequirements.memoryTypeBits & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
        return false;
    }

    if (vkBindBufferMemory(device, out.buffer, out.memory, 0) != VK_SUCCESS) {
        return false;
    }

    out.size = size;
    if (vkMapMemory(device, out.memory, 0, size, 0, &out.mapped) != VK_SUCCESS) {
        return false;
    }

    return true;
}

static void destroyBuffer(VkDevice device, Buffer &buffer) {
    if (buffer.mapped) {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
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

    if (!ensureBuffers(0)) {
        log_error("Failed to create buffers");
        shutdown();
        return false;
    }

    if (!ensurePipelines()) {
        log_error("Failed to create pipelines");
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
        vkDeviceWaitIdle(vk_->device);
    }

    if (vk_->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_->device, vk_->descriptorPool, nullptr);
    }
    if (vk_->descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_->device, vk_->descriptorSetLayout, nullptr);
    }
    if (vk_->pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_->device, vk_->pipelineLayout, nullptr);
    }
    if (vk_->initPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_->device, vk_->initPipeline, nullptr);
    }
    if (vk_->simulationPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_->device, vk_->simulationPipeline, nullptr);
    }
    if (vk_->xorPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_->device, vk_->xorPipeline, nullptr);
    }

    destroyBuffer(vk_->device, vk_->energy);
    destroyBuffer(vk_->device, vk_->phase);
    destroyBuffer(vk_->device, vk_->interference);
    destroyBuffer(vk_->device, vk_->nodeFlag);
    destroyBuffer(vk_->device, vk_->spin);
    destroyBuffer(vk_->device, vk_->topology);
    destroyBuffer(vk_->device, vk_->pressure);
    destroyBuffer(vk_->device, vk_->gravity);
    destroyBuffer(vk_->device, vk_->magnetism);
    destroyBuffer(vk_->device, vk_->temperature);
    destroyBuffer(vk_->device, vk_->potential);
    destroyBuffer(vk_->device, vk_->driftX);
    destroyBuffer(vk_->device, vk_->driftY);
    destroyBuffer(vk_->device, vk_->rngEnergy);
    destroyBuffer(vk_->device, vk_->rngPhase);
    destroyBuffer(vk_->device, vk_->rngSpin);
    destroyBuffer(vk_->device, vk_->fieldMap);
    destroyBuffer(vk_->device, vk_->input);
    destroyBuffer(vk_->device, vk_->output);

    if (vk_->fence != VK_NULL_HANDLE) {
        vkDestroyFence(vk_->device, vk_->fence, nullptr);
    }
    if (vk_->commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk_->device, vk_->commandPool, nullptr);
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

static void appendDescriptor(VkDescriptorBufferInfo &info, const Buffer &buffer) {
    info.buffer = buffer.buffer;
    info.offset = 0;
    info.range = buffer.size;
}

bool MyceliaVulkanCompute::ensureBuffers(std::size_t input_size_bytes) {
    if (!vk_) {
        return false;
    }

    VkDevice device = vk_->device;

    auto ensureFixedBuffer = [&](Buffer &buf, VkDeviceSize size) -> bool {
        if (buf.buffer != VK_NULL_HANDLE && buf.size >= size) {
            return true;
        }
        destroyBuffer(device, buf);
        return createBuffer(vk_->physicalDevice, device, size,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           buf);
    };

    VkDeviceSize fp_bytes = static_cast<VkDeviceSize>(kBlockSize) * sizeof(float);
    VkDeviceSize int_bytes = static_cast<VkDeviceSize>(kBlockSize) * sizeof(int32_t);

    if (!ensureFixedBuffer(vk_->energy, fp_bytes) ||
        !ensureFixedBuffer(vk_->phase, fp_bytes) ||
        !ensureFixedBuffer(vk_->interference, fp_bytes) ||
        !ensureFixedBuffer(vk_->nodeFlag, int_bytes) ||
        !ensureFixedBuffer(vk_->spin, int_bytes) ||
        !ensureFixedBuffer(vk_->topology, int_bytes) ||
        !ensureFixedBuffer(vk_->pressure, fp_bytes) ||
        !ensureFixedBuffer(vk_->gravity, fp_bytes) ||
        !ensureFixedBuffer(vk_->magnetism, fp_bytes) ||
        !ensureFixedBuffer(vk_->temperature, fp_bytes) ||
        !ensureFixedBuffer(vk_->potential, fp_bytes) ||
        !ensureFixedBuffer(vk_->driftX, fp_bytes) ||
        !ensureFixedBuffer(vk_->driftY, fp_bytes) ||
        !ensureFixedBuffer(vk_->rngEnergy, fp_bytes) ||
        !ensureFixedBuffer(vk_->rngPhase, fp_bytes) ||
        !ensureFixedBuffer(vk_->rngSpin, fp_bytes) ||
        !ensureFixedBuffer(vk_->fieldMap, fp_bytes)) {
        return false;
    }

    VkDeviceSize input_words = alignUp(static_cast<uint32_t>(input_size_bytes), 4) / 4;
    VkDeviceSize input_bytes = input_words * sizeof(uint32_t);
    if (input_bytes == 0) {
        input_bytes = sizeof(uint32_t);
    }

    if (!ensureFixedBuffer(vk_->input, input_bytes) || !ensureFixedBuffer(vk_->output, input_bytes)) {
        return false;
    }

    return true;
}

bool MyceliaVulkanCompute::ensurePipelines() {
    if (!vk_ || vk_->descriptorSetLayout != VK_NULL_HANDLE) {
        return vk_ && vk_->descriptorSetLayout != VK_NULL_HANDLE;
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(19);
    auto addBinding = [&](uint32_t binding) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(b);
    };

    for (uint32_t i = 0; i <= 18; ++i) {
        addBinding(i);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vk_->device, &layoutInfo, nullptr, &vk_->descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange pushInit{};
    pushInit.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushInit.offset = 0;
    pushInit.size = sizeof(float) * 5 + sizeof(uint32_t);

    VkPushConstantRange pushSim{};
    pushSim.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushSim.offset = 0;
    pushSim.size = sizeof(float) * 3 + sizeof(int) * 4;

    VkPushConstantRange pushXor{};
    pushXor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushXor.offset = 0;
    pushXor.size = sizeof(uint32_t) * 2;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vk_->descriptorSetLayout;

    VkPushConstantRange ranges[] = {pushInit, pushSim, pushXor};
    pipelineLayoutInfo.pushConstantRangeCount = 3;
    pipelineLayoutInfo.pPushConstantRanges = ranges;

    if (vkCreatePipelineLayout(vk_->device, &pipelineLayoutInfo, nullptr, &vk_->pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 19;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(vk_->device, &poolInfo, nullptr, &vk_->descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vk_->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vk_->descriptorSetLayout;

    if (vkAllocateDescriptorSets(vk_->device, &allocInfo, &vk_->descriptorSet) != VK_SUCCESS) {
        return false;
    }

    std::string shaderPrefix = shader_dir_.empty() ? std::string(".") : shader_dir_;
    std::string initPath = shaderPrefix + "/subqg_init.spv";
    std::string simPath = shaderPrefix + "/subqg_simulation.spv";
    std::string xorPath = shaderPrefix + "/mycelia_keystream_xor.spv";

    auto loadSpv = [&](const std::string &path, ShaderModuleOwned &out) -> bool {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            log_error("Failed to open shader file");
            return false;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0 || size % 4 != 0) {
            log_error("Invalid shader size");
            return false;
        }
        out.code.resize(static_cast<size_t>(size / 4));
        if (!file.read(reinterpret_cast<char *>(out.code.data()), size)) {
            log_error("Failed to read shader file");
            return false;
        }
        out.module = createShaderModule(vk_->device, out.code);
        return out.module != VK_NULL_HANDLE;
    };

    ShaderModuleOwned initShader;
    ShaderModuleOwned simShader;
    ShaderModuleOwned xorShader;

    if (!loadSpv(initPath, initShader) || !loadSpv(simPath, simShader) || !loadSpv(xorPath, xorShader)) {
        return false;
    }

    auto createPipeline = [&](VkShaderModule module, VkPipeline &pipeline) -> bool {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = module;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = vk_->pipelineLayout;

        return vkCreateComputePipelines(vk_->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;
    };

    if (!createPipeline(initShader.module, vk_->initPipeline)) {
        return false;
    }
    if (!createPipeline(simShader.module, vk_->simulationPipeline)) {
        return false;
    }
    if (!createPipeline(xorShader.module, vk_->xorPipeline)) {
        return false;
    }

    VkDescriptorBufferInfo infos[19] = {};
    appendDescriptor(infos[0], vk_->energy);
    appendDescriptor(infos[1], vk_->phase);
    appendDescriptor(infos[2], vk_->interference);
    appendDescriptor(infos[3], vk_->nodeFlag);
    appendDescriptor(infos[4], vk_->spin);
    appendDescriptor(infos[5], vk_->topology);
    appendDescriptor(infos[6], vk_->pressure);
    appendDescriptor(infos[7], vk_->gravity);
    appendDescriptor(infos[8], vk_->magnetism);
    appendDescriptor(infos[9], vk_->temperature);
    appendDescriptor(infos[10], vk_->potential);
    appendDescriptor(infos[11], vk_->driftX);
    appendDescriptor(infos[12], vk_->driftY);
    appendDescriptor(infos[13], vk_->rngEnergy);
    appendDescriptor(infos[14], vk_->rngPhase);
    appendDescriptor(infos[15], vk_->rngSpin);
    appendDescriptor(infos[16], vk_->fieldMap);
    appendDescriptor(infos[17], vk_->input);
    appendDescriptor(infos[18], vk_->output);

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(19);
    for (uint32_t i = 0; i < 19; ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vk_->descriptorSet;
        write.dstBinding = i;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &infos[i];
        writes.push_back(write);
    }

    vkUpdateDescriptorSets(vk_->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    return true;
}

bool MyceliaVulkanCompute::uploadInput(const std::vector<uint8_t> &input) {
    if (!vk_ || !vk_->input.mapped) {
        return false;
    }

    uint32_t wordCount = alignUp(static_cast<uint32_t>(input.size()), 4) / 4;
    if (wordCount == 0) {
        wordCount = 1;
    }
    std::memset(vk_->input.mapped, 0, wordCount * sizeof(uint32_t));
    std::memcpy(vk_->input.mapped, input.data(), input.size());
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

bool MyceliaVulkanCompute::recordAndSubmitBlock(uint32_t offset_in_block,
                                                uint32_t chunk_len_bytes) {
    VkCommandBuffer cmd = vk_->commandBuffer;

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->pipelineLayout, 0, 1, &vk_->descriptorSet, 0, nullptr);

    struct InitParams {
        float init_energy;
        float init_phase;
        float rng_energy;
        float rng_phase;
        float rng_spin;
        uint32_t cell_count;
    } initParams{};
    initParams.init_energy = 0.5f;
    initParams.init_phase = 0.5f;
    initParams.rng_energy = 0.5f;
    initParams.rng_phase = 0.5f;
    initParams.rng_spin = 0.5f;
    initParams.cell_count = kBlockSize;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->initPipeline);
    vkCmdPushConstants(cmd, vk_->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(initParams), &initParams);
    uint32_t groups = (kBlockSize + kLocalSize - 1) / kLocalSize;
    vkCmdDispatch(cmd, groups, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &barrier,
                         0,
                         nullptr,
                         0,
                         nullptr);

    struct SimParams {
        float noise_level;
        float threshold;
        float noise_factor;
        int grid_width;
        int grid_height;
        int cell_count;
        int write_field_map;
    } simParams{};
    simParams.noise_level = 0.005f;
    simParams.threshold = 0.5f;
    simParams.noise_factor = 1.0f;
    simParams.grid_width = 256;
    simParams.grid_height = 256;
    simParams.cell_count = static_cast<int>(kBlockSize);
    simParams.write_field_map = 1;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->simulationPipeline);
    vkCmdPushConstants(cmd, vk_->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(simParams), &simParams);
    vkCmdDispatch(cmd, groups, 1, 1);

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &barrier,
                         0,
                         nullptr,
                         0,
                         nullptr);

    struct XorParams {
        uint32_t offset_in_block;
        uint32_t data_len;
    } xorParams{};
    xorParams.offset_in_block = offset_in_block;
    xorParams.data_len = chunk_len_bytes;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->xorPipeline);
    vkCmdPushConstants(cmd, vk_->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(xorParams), &xorParams);
    uint32_t wordCount = alignUp(chunk_len_bytes, 4) / 4;
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

    return true;
}

bool MyceliaVulkanCompute::encrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed,
                                  uint64_t stream_offset) {
    if (!ensureVulkanReady()) {
        log_error("Vulkan not initialized");
        return false;
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
    (void)master_seed; // deterministic mode uses seed indirectly via RNG in OpenCL; Vulkan uses fixed RNG arrays.

    size_t current_processed = 0;
    while (current_processed < input.size()) {
        size_t abs_pos = stream_offset + current_processed;
        size_t block_index = abs_pos / kBlockSize;
        size_t offset_in_block = abs_pos % kBlockSize;

        (void)block_index; // block_seed would influence deterministic RNG in OpenCL; Vulkan uses fixed RNG arrays.

        size_t remaining = input.size() - current_processed;
        size_t available_in_block = kBlockSize - offset_in_block;
        size_t to_process = remaining < available_in_block ? remaining : available_in_block;

        if (!recordAndSubmitBlock(static_cast<uint32_t>(offset_in_block), static_cast<uint32_t>(to_process))) {
            log_error("Failed to dispatch block");
            return false;
        }

        current_processed += to_process;
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
