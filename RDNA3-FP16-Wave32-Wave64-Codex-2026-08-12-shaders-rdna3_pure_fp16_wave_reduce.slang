// 해당코드는 Codex로 수정됨
struct DispatchInfo {
    uint elementCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<half> inputData;
[[vk::binding(1, 0)]] RWStructuredBuffer<half> elementData;
[[vk::binding(2, 0)]] RWStructuredBuffer<half> subgroupData;
[[vk::push_constant]] ConstantBuffer<DispatchInfo> dispatchInfo;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    const uint index = dispatchThreadID.x;
    const bool valid = index < dispatchInfo.elementCount;

    half transformed = half(0.0);
    if (valid)
    {
        const half value = inputData[index];
        const half squared = value * value;
        transformed = squared + half(2.0);
        elementData[index] = transformed;
    }

    const half subgroupSum = WaveActiveSum(transformed);
    if (WaveIsFirstLane())
    {
        const uint waveSize = WaveGetLaneCount();
        const uint subgroupsPerWorkgroup = 64 / waveSize;
        const uint subgroupID = groupIndex / waveSize;
        const uint outputIndex =
            groupID.x * subgroupsPerWorkgroup + subgroupID;
        subgroupData[outputIndex] = subgroupSum;
    }
}
