// 해당코드는 Codex로 수정됨
#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

static_assert(
    VK_HEADER_VERSION >= 344,
    "Vulkan SDK headers 1.4.344 or newer are required");

namespace rdna3::fp16 {

inline constexpr std::uint32_t kAmdVendorId = 0x1002;
inline constexpr std::uint32_t kWorkgroupSize = 64;

enum class WaveSize : std::uint32_t {
    wave32 = 32,
    wave64 = 64,
};

struct WaveSupport {
    bool vulkan14OrNewer = false;
    bool amdVendor = false;
    bool shaderFloat16 = false;
    bool storageBuffer16BitAccess = false;
    bool uniformAndStorageBuffer16BitAccess = false;
    bool shaderSubgroupExtendedTypes = false;
    bool subgroupSizeControl = false;
    bool computeFullSubgroups = false;
    bool computeSubgroups = false;
    bool subgroupBasic = false;
    bool subgroupArithmetic = false;
    bool requiredSizeForCompute = false;
    bool wave32 = false;
    bool wave64 = false;
    std::uint32_t minSubgroupSize = 0;
    std::uint32_t maxSubgroupSize = 0;
    std::uint32_t maxComputeWorkgroupSubgroups = 0;
};

// Owns a pNext chain suitable for VkDeviceCreateInfo. Keep this object alive
// until vkCreateDevice returns.
struct RequiredDeviceFeatures {
    VkPhysicalDeviceShaderFloat16Int8Features float16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroupExtended{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSize{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};

    RequiredDeviceFeatures() noexcept;
    RequiredDeviceFeatures(const RequiredDeviceFeatures&) = delete;
    RequiredDeviceFeatures& operator=(const RequiredDeviceFeatures&) = delete;
    RequiredDeviceFeatures(RequiredDeviceFeatures&&) = delete;
    RequiredDeviceFeatures& operator=(RequiredDeviceFeatures&&) = delete;

    [[nodiscard]] const void* head() const noexcept { return &float16; }
};

struct WavePipelines {
    VkPipeline wave32 = VK_NULL_HANDLE;
    VkPipeline wave64 = VK_NULL_HANDLE;
};

[[nodiscard]] WaveSupport queryWaveSupport(VkPhysicalDevice physicalDevice);

// Throws std::runtime_error unless the selected device exposes the complete
// Vulkan 1.4 + FP16 + required Wave32/Wave64 contract.
void requireRdna3WaveSupport(const WaveSupport& support);

// shaderModule must contain the SPIR-V 1.6 shader with LocalSize 64 supplied in
// this package. pipelineLayout must match its three descriptors and push constant.
[[nodiscard]] WavePipelines createWavePipelines(
    VkDevice device,
    VkShaderModule shaderModule,
    VkPipelineLayout pipelineLayout,
    VkPipelineCache pipelineCache = VK_NULL_HANDLE);

void destroyWavePipelines(VkDevice device, WavePipelines& pipelines) noexcept;

[[nodiscard]] std::uint32_t subgroupOutputElementCount(
    std::uint32_t inputElementCount,
    WaveSize waveSize) noexcept;

}  // namespace rdna3::fp16
