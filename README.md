# AMD RDNA3 pure FP16 Wave32 + Wave64

> 해당코드는 Codex로 수정됨

Vulkan 1.4.344 헤더와 SPIR-V 1.6을 대상으로, 하나의 순수 FP16 compute
셰이더를 AMD RDNA3의 Wave32와 Wave64 파이프라인에서 선택해 실행하는
예시입니다.

## 구현 요약

- FP16 입력을 읽어 `value * value + FP16(2.0)`을 FP16으로 계산
- 변환 결과를 FP16 버퍼에 기록
- `subgroupAdd(float16_t)`도 FP16 결과로 유지
- `requiredSubgroupSize = 32/64`인 compute pipeline 두 개 생성
- `local_size_x = 64`이므로 Wave32는 workgroup당 합계 2개, Wave64는 1개 기록
- 마지막 불완전 dispatch의 범위 밖 lane은 FP16 0으로 reduction에 참여

## 파일

- `shaders-rdna3_pure_fp16_wave_reduce.comp`: 공통 Vulkan GLSL 셰이더
- `shaders-rdna3_pure_fp16_wave_reduce.slang`: 동일 로직의 SPIR-V 1.6 기준 구현
- `host-rdna3_fp16_wave_pipeline.hpp/.cpp`: 기능 조회와 Wave32/64 파이프라인 생성
- `tests-test_fp16_wave_reference.py`: FP16 반올림과 Wave 분할 CPU 기준 테스트
- `tools-compile_rdna3_fp16_wave.ps1`: Slang 기반 SPIR-V 1.6 컴파일·검증
- `tools-check_rdna3_isa.ps1`: RGP/RGA에서 추출한 RDNA3 ISA 검사
- `docs-rdna3_fp16_wave_notes.md`: 설계와 검증 세부사항
- `build/*.spv`, `build/*.spv-asm`: Slang 2026.14로 검증한 SPIR-V 1.6 산출물
- `VALIDATION.md`, `SHA256SUMS.txt`: 실제 검증 기록과 산출물 해시

## descriptor와 push constant

| set | binding | 내용 |
|---:|---:|---|
| 0 | 0 | `float16_t` 입력 SSBO |
| 0 | 1 | 원소별 `float16_t` 출력 SSBO |
| 0 | 2 | subgroup별 `float16_t` 합계 SSBO |

push constant는 `uint32_t elementCount` 하나다. 입력·원소 출력 버퍼는
`elementCount * sizeof(uint16_t)` 바이트가 필요하다. subgroup 출력 원소 수는
`subgroupOutputElementCount()`로 계산한다.

## 장치 생성

```cpp
const auto support = rdna3::fp16::queryWaveSupport(physicalDevice);
rdna3::fp16::requireRdna3WaveSupport(support);

rdna3::fp16::RequiredDeviceFeatures requiredFeatures;
VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
deviceInfo.pNext = requiredFeatures.head();
// queue create info를 채운 뒤 vkCreateDevice(...)
```

셰이더 모듈과 descriptor/pipeline layout을 만든 후 두 파이프라인을 생성한다.

```cpp
auto pipelines = rdna3::fp16::createWavePipelines(
    device, shaderModule, pipelineLayout);

// pipelines.wave32 또는 pipelines.wave64를 선택해 bind/dispatch
```

## 빌드와 검증

Vulkan SDK 1.4.344를 설치하고 `VULKAN_SDK`를 설정한다. SDK에 포함된
`slangc`가 SPIR-V 생성과 내부 검증을 함께 수행한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools-compile_rdna3_fp16_wave.ps1
cmake -S . -B build-host
cmake --build build-host --config Release
python .\tests-test_fp16_wave_reference.py
```

SPIR-V는 장치 중립 IR이다. 실제 RDNA3 ISA는 AMD Vulkan 드라이버가
파이프라인 생성 때 만들므로 RGP/RGA에서 별도로 추출해 검사한다.

```powershell
.\tools-check_rdna3_isa.ps1 -IsaPath .\capture\shader.isa
```

자세한 전제와 Wave32/64 reduction 의미 차이는
`docs-rdna3_fp16_wave_notes.md`를 참고한다.
