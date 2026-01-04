#include "mycelia_vulkan_compute.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace {
constexpr uint32_t kBlockSize = 256 * 256; // 65536 bytes per block
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

// --- SHA-256 / HMAC / HKDF (CPU reference) ---
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
};

constexpr uint32_t kSha256Init[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

uint32_t rotr(uint32_t v, uint32_t r) {
    return (v >> r) | (v << (32u - r));
}

void sha256_init(Sha256Ctx &ctx) {
    std::memcpy(ctx.state, kSha256Init, sizeof(kSha256Init));
    ctx.bitlen = 0;
    ctx.buffer_len = 0;
}

void sha256_transform(Sha256Ctx &ctx, const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + kSha256K[i] + m[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx.buffer[ctx.buffer_len++] = data[i];
        if (ctx.buffer_len == 64) {
            sha256_transform(ctx, ctx.buffer);
            ctx.bitlen += 512;
            ctx.buffer_len = 0;
        }
    }
}

void sha256_final(Sha256Ctx &ctx, uint8_t out[32]) {
    uint64_t bitlen = ctx.bitlen + static_cast<uint64_t>(ctx.buffer_len) * 8;

    ctx.buffer[ctx.buffer_len++] = 0x80;
    if (ctx.buffer_len > 56) {
        while (ctx.buffer_len < 64) {
            ctx.buffer[ctx.buffer_len++] = 0x00;
        }
        sha256_transform(ctx, ctx.buffer);
        ctx.buffer_len = 0;
    }
    while (ctx.buffer_len < 56) {
        ctx.buffer[ctx.buffer_len++] = 0x00;
    }

    for (int i = 7; i >= 0; --i) {
        ctx.buffer[ctx.buffer_len++] = static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFFu);
    }

    sha256_transform(ctx, ctx.buffer);

    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFFu);
    }
}

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
    uint8_t key_block[64];
    if (key_len > 64) {
        Sha256Ctx ctx;
        sha256_init(ctx);
        sha256_update(ctx, key, key_len);
        sha256_final(ctx, key_block);
        std::memset(key_block + 32, 0, 32);
    } else {
        std::memcpy(key_block, key, key_len);
        if (key_len < 64) {
            std::memset(key_block + key_len, 0, 64 - key_len);
        }
    }

    uint8_t o_key_pad[64];
    uint8_t i_key_pad[64];
    for (int i = 0; i < 64; ++i) {
        o_key_pad[i] = key_block[i] ^ 0x5cu;
        i_key_pad[i] = key_block[i] ^ 0x36u;
    }

    Sha256Ctx inner;
    sha256_init(inner);
    sha256_update(inner, i_key_pad, 64);
    sha256_update(inner, data, data_len);
    uint8_t inner_hash[32];
    sha256_final(inner, inner_hash);

    Sha256Ctx outer;
    sha256_init(outer);
    sha256_update(outer, o_key_pad, 64);
    sha256_update(outer, inner_hash, 32);
    sha256_final(outer, out);
}

void hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                 const uint8_t *salt, size_t salt_len,
                 const uint8_t *info, size_t info_len,
                 uint8_t *out, size_t out_len) {
    uint8_t prk[32];
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);

    uint8_t t[32];
    size_t t_len = 0;
    uint8_t counter = 1;
    size_t offset = 0;

    while (offset < out_len) {
        Sha256Ctx ctx;
        uint8_t hmac_key[32];
        std::memcpy(hmac_key, prk, 32);

        size_t data_len = t_len + info_len + 1;
        std::vector<uint8_t> data(data_len);
        if (t_len > 0) {
            std::memcpy(data.data(), t, t_len);
        }
        if (info_len > 0) {
            std::memcpy(data.data() + t_len, info, info_len);
        }
        data[data_len - 1] = counter;

        hmac_sha256(hmac_key, 32, data.data(), data.size(), t);
        t_len = 32;

        size_t to_copy = (out_len - offset < t_len) ? (out_len - offset) : t_len;
        std::memcpy(out + offset, t, to_copy);
        offset += to_copy;
        ++counter;
    }
}

std::array<uint8_t, 44> derive_key_nonce_hkdf(uint64_t seed) {
    uint8_t ikm[8];
    for (int i = 0; i < 8; ++i) {
        ikm[i] = static_cast<uint8_t>((seed >> (8 * i)) & 0xFFu);
    }
    const char salt[] = "mycelia-hkdf-salt";
    const char info[] = "mycelia-chacha20";
    std::array<uint8_t, 44> out{};
    hkdf_sha256(ikm, sizeof(ikm),
                reinterpret_cast<const uint8_t *>(salt), sizeof(salt) - 1,
                reinterpret_cast<const uint8_t *>(info), sizeof(info) - 1,
                out.data(), out.size());
    return out;
}

void key_nonce_from_seed(uint64_t seed, std::array<uint32_t, 8> &key, std::array<uint32_t, 3> &nonce) {
    auto out = derive_key_nonce_hkdf(seed);
    for (int i = 0; i < 8; ++i) {
        key[i] = static_cast<uint32_t>(out[i * 4]) |
                 (static_cast<uint32_t>(out[i * 4 + 1]) << 8) |
                 (static_cast<uint32_t>(out[i * 4 + 2]) << 16) |
                 (static_cast<uint32_t>(out[i * 4 + 3]) << 24);
    }
    size_t nonce_offset = 32;
    for (int i = 0; i < 3; ++i) {
        nonce[i] = static_cast<uint32_t>(out[nonce_offset + i * 4]) |
                   (static_cast<uint32_t>(out[nonce_offset + i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(out[nonce_offset + i * 4 + 2]) << 16) |
                   (static_cast<uint32_t>(out[nonce_offset + i * 4 + 3]) << 24);
    }
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

    VkPipeline xorPipeline = VK_NULL_HANDLE;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

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
    if (vk_->xorPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_->device, vk_->xorPipeline, nullptr);
    }

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
    bindings.reserve(2);
    auto addBinding = [&](uint32_t binding) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(b);
    };

    addBinding(17);
    addBinding(18);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vk_->device, &layoutInfo, nullptr, &vk_->descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange pushXor{};
    pushXor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushXor.offset = 0;
    pushXor.size = sizeof(uint32_t) * (8 + 3 + 3);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &vk_->descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushXor;

    if (vkCreatePipelineLayout(vk_->device, &pipelineLayoutInfo, nullptr, &vk_->pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2;

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

    ShaderModuleOwned xorShader;
    if (!loadSpv(xorPath, xorShader)) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = xorShader.module;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = vk_->pipelineLayout;

    if (vkCreateComputePipelines(vk_->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vk_->xorPipeline) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo infos[2] = {};
    appendDescriptor(infos[0], vk_->input);
    appendDescriptor(infos[1], vk_->output);

    VkWriteDescriptorSet writes[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = vk_->descriptorSet;
        writes[i].dstBinding = 17 + i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &infos[i];
    }

    vkUpdateDescriptorSets(vk_->device, 2, writes, 0, nullptr);

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

bool MyceliaVulkanCompute::encrypt(const std::vector<uint8_t> &input,\n                                  std::vector<uint8_t> &output,\n                                  const std::vector<uint8_t> &seed,\n                                  uint64_t stream_offset) {\n    if (!ensureVulkanReady()) {\n        log_error(\"Vulkan not initialized\");\n        return false;\n    }\n\n    if (!ensureBuffers(input.size())) {\n        log_error(\"Failed to ensure buffers\");\n        return false;\n    }\n\n        if (input.empty()) {
        output.clear();
        return true;
    }

if (!uploadInput(input)) {\n        log_error(\"Failed to upload input\");\n        return false;\n    }\n\n    uint64_t master_seed = seedToUint64(seed);\n    std::array<uint32_t, 8> key{};\n    std::array<uint32_t, 3> nonce{};\n    key_nonce_from_seed(master_seed, key, nonce);\n\n    uint64_t block_index = stream_offset / 64u;\n    uint32_t counter_base = static_cast<uint32_t>(block_index & 0xFFFFFFFFu);\n    uint32_t counter_high = static_cast<uint32_t>((block_index >> 32) & 0xFFFFFFFFu);\n    nonce[2] ^= counter_high;\n    uint32_t offset_in_block = static_cast<uint32_t>(stream_offset % 64u);\n\n    VkCommandBuffer cmd = vk_->commandBuffer;\n    vkResetCommandBuffer(cmd, 0);\n\n    VkCommandBufferBeginInfo beginInfo{};\n    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;\n    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {\n        return false;\n    }\n\n    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->pipelineLayout, 0, 1, &vk_->descriptorSet, 0, nullptr);\n\n    struct XorParams {\n        uint32_t key[8];\n        uint32_t nonce[3];\n        uint32_t counter_base;\n        uint32_t offset_in_block;\n        uint32_t data_len;\n    } xorParams{};\n\n    for (int i = 0; i < 8; ++i) {\n        xorParams.key[i] = key[i];\n    }\n    xorParams.nonce[0] = nonce[0];\n    xorParams.nonce[1] = nonce[1];\n    xorParams.nonce[2] = nonce[2];\n    xorParams.counter_base = counter_base;\n    xorParams.offset_in_block = offset_in_block;\n    xorParams.data_len = static_cast<uint32_t>(input.size());\n\n    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->xorPipeline);\n    vkCmdPushConstants(cmd, vk_->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(xorParams), &xorParams);\n    uint32_t wordCount = alignUp(static_cast<uint32_t>(input.size()), 4) / 4;\n    uint32_t xorGroups = (wordCount + kLocalSize - 1) / kLocalSize;\n    vkCmdDispatch(cmd, xorGroups, 1, 1);\n\n    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {\n        return false;\n    }\n\n    vkResetFences(vk_->device, 1, &vk_->fence);\n\n    VkSubmitInfo submitInfo{};\n    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;\n    submitInfo.commandBufferCount = 1;\n    submitInfo.pCommandBuffers = &cmd;\n\n    if (vkQueueSubmit(vk_->computeQueue, 1, &submitInfo, vk_->fence) != VK_SUCCESS) {\n        return false;\n    }\n\n    if (vkWaitForFences(vk_->device, 1, &vk_->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {\n        return false;\n    }\n\n    downloadOutput(output, input.size());\n    return true;\n}\n
bool MyceliaVulkanCompute::decrypt(const std::vector<uint8_t> &input,
                                  std::vector<uint8_t> &output,
                                  const std::vector<uint8_t> &seed,
                                  uint64_t stream_offset) {
    return encrypt(input, output, seed, stream_offset);
}
