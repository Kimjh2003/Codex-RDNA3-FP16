// 해당코드는 Codex로 수정됨
#include "host-rdna3_fp16_wave_pipeline.hpp"

#include <array>
#include <stdexcept>

namespace rdna3::fp16 {
namespace {

[[nodiscard]] bool isVulkan14OrNewer(std::uint32_t apiVersion) noexcept {
    const std::uint32_t major = VK_API_VERSION_MAJOR(apiVersion);
    const std::uint32_t minor = VK_API_VERSION_MINOR(apiVersion);
    return major > 1 || (major == 1 && minor >= 4);
}

[[nodiscard]] bool containsFlag(VkFlags value, VkFlags flag) noexcept {
    return (value & flag) == flag;
}

[[nodiscard]] bool supportsRequiredWave(
    std::uint32_t waveSize,
    const VkPhysicalDeviceSubgroupSizeControlProperties& sizeProperties) noexcept {
    if (waveSize < sizeProperties.minSubgroupSize ||
        waveSize > sizeProperties.maxSubgroupSize) {
        return false;
    }

    const std::uint32_t subgroupsPerWorkgroup = kWorkgroupSize / waveSize;
    return kWorkgroupSize % waveSize == 0 &&
           subgroupsPerWorkgroup <=
               sizeProperties.maxComputeWorkgroupSubgroups;
}

}  // namespace

RequiredDeviceFeatures::RequiredDeviceFeatures() noexcept {
    float16.pNext = &storage16;
    float16.shaderFloat16 = VK_TRUE;

    storage16.pNext = &subgroupExtended;
    storage16.storageBuffer16BitAccess = VK_TRUE;
    storage16.uniformAndStorageBuffer16BitAccess = VK_TRUE;

    subgroupExtended.pNext = &subgroupSize;
    subgroupExtended.shaderSubgroupExtendedTypes = VK_TRUE;

    subgroupSize.pNext = nullptr;
    subgroupSize.subgroupSizeControl = VK_TRUE;
    subgroupSize.computeFullSubgroups = VK_TRUE;
}

WaveSupport queryWaveSupport(VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::invalid_argument("physicalDevice must not be VK_NULL_HANDLE");
    }

    VkPhysicalDeviceShaderFloat16Int8Features float16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroupExtended{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSize{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};

    float16.pNext = &storage16;
    storage16.pNext = &subgroupExtended;
    subgroupExtended.pNext = &subgroupSize;

    VkPhysicalDeviceFeatures2 features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &float16;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    VkPhysicalDeviceSubgroupProperties subgroupProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceSubgroupSizeControlProperties sizeProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    subgroupProperties.pNext = &sizeProperties;

    VkPhysicalDeviceProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &subgroupProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    WaveSupport support{};
    support.vulkan14OrNewer =
        isVulkan14OrNewer(properties.properties.apiVersion);
    support.amdVendor = properties.properties.vendorID == kAmdVendorId;
    support.shaderFloat16 = float16.shaderFloat16 == VK_TRUE;
    support.storageBuffer16BitAccess =
        storage16.storageBuffer16BitAccess == VK_TRUE;
    support.uniformAndStorageBuffer16BitAccess =
        storage16.uniformAndStorageBuffer16BitAccess == VK_TRUE;
    support.shaderSubgroupExtendedTypes =
        subgroupExtended.shaderSubgroupExtendedTypes == VK_TRUE;
    support.subgroupSizeControl = subgroupSize.subgroupSizeControl == VK_TRUE;
    support.computeFullSubgroups = subgroupSize.computeFullSubgroups == VK_TRUE;
    support.computeSubgroups = containsFlag(
        subgroupProperties.supportedStages, VK_SHADER_STAGE_COMPUTE_BIT);
    support.subgroupBasic = containsFlag(
        subgroupProperties.supportedOperations, VK_SUBGROUP_FEATURE_BASIC_BIT);
    support.subgroupArithmetic = containsFlag(
        subgroupProperties.supportedOperations,
        VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);
    support.requiredSizeForCompute = containsFlag(
        sizeProperties.requiredSubgroupSizeStages,
        VK_SHADER_STAGE_COMPUTE_BIT);
    support.minSubgroupSize = sizeProperties.minSubgroupSize;
    support.maxSubgroupSize = sizeProperties.maxSubgroupSize;
    support.maxComputeWorkgroupSubgroups =
        sizeProperties.maxComputeWorkgroupSubgroups;

    const bool commonWaveRequirements = support.subgroupSizeControl &&
                                        support.computeFullSubgroups &&
                                        support.computeSubgroups &&
                                        support.requiredSizeForCompute;
    support.wave32 = commonWaveRequirements &&
                     supportsRequiredWave(32, sizeProperties);
    support.wave64 = commonWaveRequirements &&
                     supportsRequiredWave(64, sizeProperties);
    return support;
}

void requireRdna3WaveSupport(const WaveSupport& support) {
    if (!support.vulkan14OrNewer) {
        throw std::runtime_error("Vulkan 1.4 runtime support is required");
    }
    if (!support.amdVendor) {
        throw std::runtime_error("The selected device is not an AMD GPU");
    }
    if (!support.shaderFloat16 || !support.storageBuffer16BitAccess ||
        !support.uniformAndStorageBuffer16BitAccess) {
        throw std::runtime_error(
            "shaderFloat16 and both 16-bit buffer features are required");
    }
    if (!support.shaderSubgroupExtendedTypes || !support.subgroupBasic ||
        !support.subgroupArithmetic) {
        throw std::runtime_error(
            "FP16 subgroup arithmetic is unavailable on this device");
    }
    if (!support.wave32 || !support.wave64) {
        throw std::runtime_error(
            "Both required subgroup sizes 32 and 64 must be available");
    }
}

WavePipelines createWavePipelines(
    VkDevice device,
    VkShaderModule shaderModule,
    VkPipelineLayout pipelineLayout,
    VkPipelineCache pipelineCache) {
    if (device == VK_NULL_HANDLE || shaderModule == VK_NULL_HANDLE ||
        pipelineLayout == VK_NULL_HANDLE) {
        throw std::invalid_argument(
            "device, shaderModule and pipelineLayout must be valid");
    }

    std::array<VkPipelineShaderStageRequiredSubgroupSizeCreateInfo, 2>
        requiredSizes{};
    std::array<VkComputePipelineCreateInfo, 2> createInfos{};
    constexpr std::array<std::uint32_t, 2> waveSizes{32, 64};

    for (std::size_t index = 0; index < createInfos.size(); ++index) {
        requiredSizes[index].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        requiredSizes[index].requiredSubgroupSize = waveSizes[index];

        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.pNext = &requiredSizes[index];
        stage.flags =
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shaderModule;
        stage.pName = "main";

        createInfos[index].sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        createInfos[index].stage = stage;
        createInfos[index].layout = pipelineLayout;
    }

    std::array<VkPipeline, 2> rawPipelines{
        VK_NULL_HANDLE, VK_NULL_HANDLE};
    const VkResult result = vkCreateComputePipelines(
        device,
        pipelineCache,
        static_cast<std::uint32_t>(createInfos.size()),
        createInfos.data(),
        nullptr,
        rawPipelines.data());

    if (result != VK_SUCCESS) {
        for (VkPipeline pipeline : rawPipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        throw std::runtime_error(
            "vkCreateComputePipelines failed for Wave32/Wave64");
    }

    return WavePipelines{rawPipelines[0], rawPipelines[1]};
}

void destroyWavePipelines(VkDevice device, WavePipelines& pipelines) noexcept {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (pipelines.wave32 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.wave32, nullptr);
        pipelines.wave32 = VK_NULL_HANDLE;
    }
    if (pipelines.wave64 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.wave64, nullptr);
        pipelines.wave64 = VK_NULL_HANDLE;
    }
}

std::uint32_t subgroupOutputElementCount(
    std::uint32_t inputElementCount,
    WaveSize waveSize) noexcept {
    if (inputElementCount == 0) {
        return 0;
    }

    const std::uint32_t wave = static_cast<std::uint32_t>(waveSize);
    if (wave != 32 && wave != 64) {
        return 0;
    }

    const std::uint32_t groups = inputElementCount / kWorkgroupSize +
                                 (inputElementCount % kWorkgroupSize != 0);
    return groups * (kWorkgroupSize / wave);
}

}  // namespace rdna3::fp16
