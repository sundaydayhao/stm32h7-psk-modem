# Program the STM32H743 through the Fire-DAP CMSIS-DAP JTAG interface.
# The probe can enumerate without ST-Link support, so STM32CubeProgrammer is
# not used here.  The target setup matches the validated board connection.
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "debug",
    [int]$AdapterSpeedKhz = 100,
    [string]$OpenOcd = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260424\openocd-esp32\bin\openocd.exe"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$elf = Join-Path $root "build\$Preset\stm32h743iit6_psk_modem.elf"
if (-not (Test-Path -LiteralPath $elf)) {
    throw "Missing ELF: $elf. Build first with: cmake --build --preset $Preset"
}
if (-not (Test-Path -LiteralPath $OpenOcd)) {
    throw "OpenOCD was not found: $OpenOcd. Pass -OpenOcd <path-to-openocd.exe>."
}

$openOcdRoot = Split-Path -Parent (Split-Path -Parent $OpenOcd)
$scripts = Join-Path $openOcdRoot "share\openocd\scripts"
if (-not (Test-Path -LiteralPath (Join-Path $scripts "target\stm32h7x.cfg"))) {
    throw "STM32H7 OpenOCD target scripts were not found under: $scripts"
}

$elfArg = (Resolve-Path -LiteralPath $elf).Path.Replace("\", "/")
& $OpenOcd `
    -s $scripts `
    -f interface/cmsis-dap.cfg `
    -c "transport select jtag" `
    -f target/stm32h7x.cfg `
    -c "adapter speed $AdapterSpeedKhz" `
    -c "init" `
    -c "reset halt" `
    -c "program $elfArg verify reset exit"

if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD JTAG flashing failed with exit code $LASTEXITCODE"
}
