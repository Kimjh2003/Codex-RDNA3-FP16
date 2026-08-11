# 해당코드는 Codex로 수정됨
param(
    [string]$OutputDirectory = "build"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-VulkanTool([string]$ToolName) {
    $fromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        return $fromPath.Source
    }

    if (Test-Path Env:VULKAN_SDK) {
        $candidate = Join-Path $env:VULKAN_SDK "Bin\$ToolName.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "$ToolName was not found. Install Vulkan SDK 1.4.344 and set VULKAN_SDK."
}

$slangc = Resolve-VulkanTool "slangc"

$shader = Join-Path $PSScriptRoot "shaders-rdna3_pure_fp16_wave_reduce.slang"
$outputRoot = Join-Path $PSScriptRoot $OutputDirectory
$spv = Join-Path $outputRoot "rdna3_pure_fp16_wave_reduce.spv"
$assembly = Join-Path $outputRoot "rdna3_pure_fp16_wave_reduce.spv-asm"

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

& $slangc $shader `
    -entry main -stage compute -target spirv `
    -capability "spirv_1_6+spvGroupNonUniform+spvGroupNonUniformArithmetic" `
    -validate-ir -o $spv
if ($LASTEXITCODE -ne 0) {
    throw "slangc SPIR-V compilation or validation failed"
}

& $slangc $shader `
    -entry main -stage compute -target spirv-asm `
    -capability "spirv_1_6+spvGroupNonUniform+spvGroupNonUniformArithmetic" `
    -validate-ir -o $assembly
if ($LASTEXITCODE -ne 0) {
    throw "slangc SPIR-V assembly generation or validation failed"
}

$text = Get-Content -Raw -LiteralPath $assembly
$requiredPatterns = @(
    "; Version: 1.6",
    "OpCapability Float16",
    "OpCapability UniformAndStorageBuffer16BitAccess",
    "OpCapability GroupNonUniformArithmetic",
    "OpTypeFloat 16",
    "OpGroupNonUniformFAdd"
)

foreach ($pattern in $requiredPatterns) {
    if (-not $text.Contains($pattern)) {
        throw "Required SPIR-V marker is missing: $pattern"
    }
}

if ($text.Contains("OpTypeFloat 32")) {
    throw "Unexpected FP32 type found in the supposedly pure-FP16 shader"
}

Write-Host "Validated Vulkan 1.4 / SPIR-V 1.6 pure-FP16 shader: $spv"
