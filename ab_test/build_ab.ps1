# Builds tools/host_psk_rtty_wav.exe using the MSVC host toolchain (not the ARM cross
# toolchain used by the firmware CMake build). This lets the real shipped decoder
# (third_party/psk_rtty/psk_rtty.c) run natively on Windows against WAV files, instead
# of relying on tools/wav_decode_regression.py's independent Python reimplementation.
param(
    [string]$VcVarsAll = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    [string]$OutDir = "$PSScriptRoot"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outExe = Join-Path $OutDir "stage2_ab.exe"

$srcTool = Join-Path $root "host_psk_rtty_wav.c"
$srcCore = Join-Path $root "psk_rtty.c"
$srcDecim = Join-Path $root "audio_decim.c"
$incDir = $root
$incDecim = $root

$cmd = "call `"$VcVarsAll`" x64 >NUL 2>NUL && cl /nologo /std:c11 /O2 /W3 /I `"$incDir`" /I `"$incDecim`" `"$srcTool`" `"$srcCore`" `"$srcDecim`" /Fe:`"$outExe`" /Fo:`"$OutDir\\`""
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$result = cmd.exe /c $cmd 2>&1
$ErrorActionPreference = $prevEap
Write-Output $result
if (-not (Test-Path $outExe)) {
    Write-Error "build failed: $outExe not produced"
    exit 1
}
Write-Output "OK: $outExe"
