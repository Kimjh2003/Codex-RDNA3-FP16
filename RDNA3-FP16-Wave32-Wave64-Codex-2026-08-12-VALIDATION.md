# Validation record

> 해당코드는 Codex로 수정됨

검증일: 2026-08-12

## 성공한 검증

- 공식 Slang `2026.14`로 직접 SPIR-V 생성
- `-capability spirv_1_6+spvGroupNonUniform+spvGroupNonUniformArithmetic`
- Slang 내부 SPIR-V/IR 검증 활성화 (`-validate-ir`, `-skip-spirv-validation` 미사용)
- SPIR-V 헤더 `Version: 1.6`
- `OpCapability Float16`
- `OpCapability UniformAndStorageBuffer16BitAccess`
- `OpCapability GroupNonUniformArithmetic`
- `%half = OpTypeFloat 16`
- `%half` 결과의 `OpFMul`, `OpFAdd`, `OpGroupNonUniformFAdd`
- `OpTypeFloat 32` 부재
- Python CPU 기준 및 소스 계약 테스트 5개 통과

## 환경상 남은 검증

현재 로컬 환경에는 Vulkan SDK 1.4.344 헤더·C++ 빌드 도구와 RDNA3
RGP/RGA 캡처가 없어 다음 항목은 대상 시스템에서 실행해야 한다.

- `CMakeLists.txt`를 이용한 호스트 모듈 빌드
- 실제 RDNA3 장치에서 Wave32/64 pipeline 생성 및 dispatch
- RGP/RGA ISA에서 FP16 VALU와 Wave 모드 확인

이를 조용히 통과한 것으로 간주하지 않도록 헤더 버전 `static_assert`, 런타임
기능 조회, SPIR-V 검사 스크립트와 ISA 검사 스크립트를 패키지에 포함했다.
