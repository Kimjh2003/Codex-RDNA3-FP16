# RDNA3 순수 FP16 Wave32/Wave64 구현 노트

> 해당코드는 Codex로 수정됨

## 타깃

- Vulkan SDK 헤더: **1.4.344 이상**
- 런타임 API: **Vulkan 1.4 이상**
- 셰이더 IR: **SPIR-V 1.6**
- 장치 정책: **AMD vendor ID `0x1002` + Wave32/64 요구 크기 지원**
- 마이크로아키텍처 검증 대상: **AMD RDNA3**

Vulkan은 RDNA3의 `gfx1100`, `gfx1101`, `gfx1102` 같은 ISA 타깃으로
SPIR-V를 미리 고정하지 않는다. AMD Vulkan 드라이버가 파이프라인 생성 시
선택된 물리 장치의 ISA로 최종 컴파일한다. 따라서 이 패키지의 호스트 코드는
AMD 장치와 필요한 기능을 검사하고, 최종 ISA는 RGP/RGA 캡처로 확인한다.

## Wave 크기 제어

셰이더의 `local_size_x = 64`는 워크그룹 크기일 뿐 Wave 크기가 아니다.
호스트가 다음 파이프라인 상태를 사용해 실제 subgroup 크기를 요구한다.

```cpp
VkPipelineShaderStageRequiredSubgroupSizeCreateInfo required{};
required.requiredSubgroupSize = 32; // 또는 64
stage.pNext = &required;
stage.flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
```

동일 워크그룹에서 Wave32는 subgroup 두 개, Wave64는 subgroup 한 개다.
따라서 reduction 출력 개수는 각각 `dispatchGroupCount * 2`와
`dispatchGroupCount * 1`이다.

## 순수 FP16 범위

셰이더의 사용자 수치 연산은 다음 순서를 유지한다.

```text
FP16 SSBO load
  -> FP16 multiply
  -> FP16 add
  -> FP16 subgroupAdd
  -> FP16 SSBO store
```

인덱스와 길이는 주소 계산이므로 `uint32`를 사용한다. 순수 FP16이라는 표현은
사용자 부동소수점 산술 경로에 FP32 승격 또는 FP32 누산이 없다는 뜻이다.

부동소수점 subgroup reduction의 결합 순서는 구현에 따라 달라질 수 있으므로,
일반 입력에 대해 Wave32의 두 합을 다시 더한 결과와 Wave64의 한 합이 항상
비트 단위로 일치한다고 가정하면 안 된다.

## 필요한 Vulkan 기능

- `shaderFloat16`
- `storageBuffer16BitAccess`
- `uniformAndStorageBuffer16BitAccess` (배포되는 Slang SPIR-V capability와 일치)
- `shaderSubgroupExtendedTypes`
- compute stage subgroup basic/arithmetic
- `subgroupSizeControl`
- `computeFullSubgroups`
- compute stage의 required subgroup size 지원
- `minSubgroupSize <= 32`, `maxSubgroupSize >= 64`

`RequiredDeviceFeatures`가 `VkDeviceCreateInfo::pNext`용 체인을 제공한다.
먼저 `queryWaveSupport()`와 `requireRdna3WaveSupport()`로 물리 장치를 검사한
뒤 해당 체인을 장치 생성에 사용해야 한다.

## SPIR-V와 RDNA3 ISA 검증

`tools-compile_rdna3_fp16_wave.ps1`은 Vulkan SDK 1.4.344의 Slang 도구로 셰이더를
컴파일하고 다음 항목을 검사한다.

- SPIR-V 1.6
- `Float16`
- `UniformAndStorageBuffer16BitAccess`
- `GroupNonUniformArithmetic`
- 16비트 `OpTypeFloat`
- `OpGroupNonUniformFAdd`
- 32비트 `OpTypeFloat` 부재

최종 RDNA3 ISA는 Radeon GPU Profiler 또는 Radeon GPU Analyzer에서 추출한
텍스트를 `tools-check_rdna3_isa.ps1`에 전달한다. 일반 VALU 경로에서
`*_f16`/`v_pk_*_f16`을 확인하고 뜻하지 않은 `*_f32`가 없는지 검사한다.
