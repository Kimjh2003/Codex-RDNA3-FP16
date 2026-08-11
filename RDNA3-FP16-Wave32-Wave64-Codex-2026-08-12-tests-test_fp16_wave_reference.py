# 해당코드는 Codex로 수정됨
import math
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parent
WORKGROUP_SIZE = 64


def round_fp16(value: float) -> float:
    if math.isnan(value):
        return math.nan
    try:
        return struct.unpack("<e", struct.pack("<e", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def pure_fp16_transform(value: float) -> float:
    value_fp16 = round_fp16(value)
    squared_fp16 = round_fp16(value_fp16 * value_fp16)
    return round_fp16(squared_fp16 + round_fp16(2.0))


def sequential_fp16_sum(values: list[float]) -> float:
    # GPU subgroup reductions may use a different tree order. The exact-valued
    # cases below intentionally avoid order-dependent rounding.
    result = round_fp16(0.0)
    for value in values:
        result = round_fp16(result + round_fp16(value))
    return result


def simulate_dispatch(values: list[float], wave_size: int) -> tuple[list[float], list[float]]:
    if wave_size not in (32, 64):
        raise ValueError("wave_size must be 32 or 64")

    element_outputs = [pure_fp16_transform(value) for value in values]
    group_count = (len(values) + WORKGROUP_SIZE - 1) // WORKGROUP_SIZE
    subgroup_outputs: list[float] = []

    for group_index in range(group_count):
        group_begin = group_index * WORKGROUP_SIZE
        for subgroup_begin in range(0, WORKGROUP_SIZE, wave_size):
            transformed: list[float] = []
            for lane in range(wave_size):
                index = group_begin + subgroup_begin + lane
                transformed.append(
                    pure_fp16_transform(values[index])
                    if index < len(values)
                    else round_fp16(0.0)
                )
            subgroup_outputs.append(sequential_fp16_sum(transformed))

    return element_outputs, subgroup_outputs


class PureFp16WaveReferenceTest(unittest.TestCase):
    def test_each_arithmetic_step_rounds_to_fp16(self) -> None:
        value = struct.unpack("<e", struct.pack("<H", 0x3C01))[0]
        expected = round_fp16(round_fp16(value * value) + round_fp16(2.0))
        self.assertEqual(pure_fp16_transform(value), expected)

    def test_wave32_and_wave64_partition_the_same_workgroup(self) -> None:
        values = [0.0] * 64
        elements32, sums32 = simulate_dispatch(values, 32)
        elements64, sums64 = simulate_dispatch(values, 64)

        self.assertEqual(elements32, elements64)
        self.assertEqual(sums32, [64.0, 64.0])
        self.assertEqual(sums64, [128.0])

    def test_partial_workgroup_uses_zero_contribution(self) -> None:
        values = [0.0] * 33
        _, sums32 = simulate_dispatch(values, 32)
        _, sums64 = simulate_dispatch(values, 64)
        self.assertEqual(sums32, [64.0, 2.0])
        self.assertEqual(sums64, [66.0])

    def test_shader_declares_the_complete_fp16_subgroup_contract(self) -> None:
        shader = (ROOT / "shaders-rdna3_pure_fp16_wave_reduce.comp").read_text(
            encoding="utf-8"
        )
        required_tokens = (
            "GL_EXT_shader_16bit_storage",
            "GL_EXT_shader_explicit_arithmetic_types_float16",
            "GL_EXT_shader_subgroup_extended_types_float16",
            "GL_KHR_shader_subgroup_arithmetic",
            "float16_t subgroupSum = subgroupAdd(transformed)",
            "layout(local_size_x = 64",
        )
        for token in required_tokens:
            self.assertIn(token, shader)

    def test_host_forces_both_required_subgroup_sizes(self) -> None:
        host = (ROOT / "host-rdna3_fp16_wave_pipeline.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("requiredSubgroupSize = waveSizes[index]", host)
        self.assertIn("uniformAndStorageBuffer16BitAccess = VK_TRUE", host)
        self.assertIn(
            "VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT", host
        )
        self.assertIn("constexpr std::array<std::uint32_t, 2> waveSizes{32, 64}", host)


if __name__ == "__main__":
    unittest.main()
