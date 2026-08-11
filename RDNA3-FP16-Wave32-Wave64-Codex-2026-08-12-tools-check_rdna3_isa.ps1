# 해당코드는 Codex로 수정됨
param(
    [Parameter(Mandatory = $true)]
    [string]$IsaPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$resolved = Resolve-Path -LiteralPath $IsaPath
$text = Get-Content -Raw -LiteralPath $resolved

$fp16Instructions = [regex]::Matches(
    $text,
    "(?im)\b(?:v_pk_[a-z0-9_]*f16|v_[a-z0-9_]*f16)\b")
$fp32Instructions = [regex]::Matches(
    $text,
    "(?im)\b(?:v_pk_[a-z0-9_]*f32|v_[a-z0-9_]*f32)\b")

if ($fp16Instructions.Count -eq 0) {
    throw "No RDNA FP16 VALU instruction was found in $resolved"
}

if ($fp32Instructions.Count -ne 0) {
    $uniqueFp32 = $fp32Instructions.Value | Sort-Object -Unique
    throw "Unexpected FP32 VALU instructions: $($uniqueFp32 -join ', ')"
}

$uniqueFp16 = $fp16Instructions.Value | Sort-Object -Unique
Write-Host "RDNA3 FP16 VALU instructions: $($uniqueFp16 -join ', ')"
Write-Host "No FP32 VALU instruction was found."
