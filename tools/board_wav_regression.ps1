param(
    [string[]]$Modes = @(
        "BPSK31", "BPSK63", "BPSK63F", "BPSK125", "BPSK250", "BPSK500", "BPSK1000",
        "12xPSK125", "6xPSK250", "2xPSK500", "4xPSK500", "2xPSK800", "2xPSK1000",
        "QPSK31", "QPSK63", "QPSK125", "QPSK250", "QPSK500",
        "8PSK125", "8PSK250", "8PSK500", "8PSK1000",
        "8PSK125F", "8PSK250F", "8PSK500F", "8PSK1000F", "8PSK1200F"
    ),
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$WavDir = "C:\projects\psk-script\generated-wav-48000",
    [string]$ExpectedText = "C:\projects\psk-script\input.txt",
    [string]$LogPath = "C:\projects\stm32h7-psk-modem\tools\board_wav_regression.log",
    [int]$SelectDelaySeconds = 2,
    [int]$ModeWaitSeconds = 10,
    [int]$MaxSeconds = 140,
    [int]$MatchLength = 80,
    [int]$OutputDevice = -1,
    [double]$OutputGain = 1.0,
    [bool]$SerialTestMode = $false,
    [ValidateSet("TEXT", "HEX")]
    [string]$RxLogFormat = "TEXT"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-WavDurationSeconds {
    param([string]$Path)

    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $riff = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
        [void]$br.ReadUInt32()
        $wave = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
            throw "Not a RIFF/WAVE file: $Path"
        }

        $byteRate = 0
        $dataBytes = 0
        while ($fs.Position -lt $fs.Length) {
            $chunkId = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
            $chunkSize = [int]$br.ReadUInt32()
            $chunkStart = $fs.Position
            if ($chunkId -eq "fmt ") {
                [void]$br.ReadUInt16()
                [void]$br.ReadUInt16()
                [void]$br.ReadUInt32()
                $byteRate = [int]$br.ReadUInt32()
            } elseif ($chunkId -eq "data") {
                $dataBytes = $chunkSize
                break
            }
            $fs.Position = $chunkStart + $chunkSize + ($chunkSize % 2)
        }
        if ($byteRate -le 0 -or $dataBytes -le 0) {
            throw "Could not read WAV byte rate/data size: $Path"
        }
        return [math]::Ceiling($dataBytes / $byteRate)
    } finally {
        $fs.Dispose()
    }
}

function Convert-RxEscapes {
    param([string]$Text)

    $builder = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $Text.Length; $i++) {
        if ($Text[$i] -eq "\" -and ($i + 1) -lt $Text.Length) {
            $next = $Text[$i + 1]
            if ($next -eq "r" -or $next -eq "n") {
                [void]$builder.Append(" ")
                $i++
                continue
            }
            if ($next -eq "x" -and ($i + 3) -lt $Text.Length) {
                $hex = $Text.Substring($i + 2, 2)
                try {
                    [void]$builder.Append([char][Convert]::ToInt32($hex, 16))
                    $i += 3
                    continue
                } catch {
                }
            }
        }
        [void]$builder.Append($Text[$i])
    }
    return $builder.ToString()
}

function Convert-RxHex {
    param([string]$HexText)

    $builder = New-Object System.Text.StringBuilder
    foreach ($token in ($HexText -split "\s+")) {
        if ($token.Length -eq 0) {
            continue
        }
        if ($token -notmatch "^[0-9A-Fa-f]{2}$") {
            continue
        }
        [void]$builder.Append([char][Convert]::ToInt32($token, 16))
    }
    return $builder.ToString()
}

function Get-ExpectedFragments {
    param(
        [string]$Text,
        [int]$Length
    )

    $clean = ($Text -replace "\s+", " ").Trim()
    $fragments = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i + $Length -le $clean.Length; $i += [math]::Max(1, [int]($Length / 2))) {
        $fragments.Add($clean.Substring($i, $Length))
    }
    return $fragments
}

function Test-DecodedText {
    param(
        [string]$Decoded,
        [System.Collections.Generic.List[string]]$Fragments
    )

    $clean = ($Decoded -replace "\s+", " ").Trim()
    foreach ($fragment in $Fragments) {
        if ($clean.Contains($fragment)) {
            return $true
        }
    }
    return $false
}

function Add-SerialLog {
    param(
        [string]$Path,
        [string]$Chunk
    )

    try {
        Add-Content -Path $Path -Value $Chunk -NoNewline -Encoding ASCII
    } catch {
        Write-Host "WARN could not append to ${Path}: $($_.Exception.Message)"
    }
}

function Send-BoardCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command
    )

    $Serial.Write("$Command`n")
}

if (-not (Test-Path $ExpectedText)) {
    throw "Expected text file not found: $ExpectedText"
}

$expected = Get-Content -Raw -Path $ExpectedText
$fragments = Get-ExpectedFragments -Text $expected -Length $MatchLength
$serial = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 100
$serial.NewLine = "`n"

New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($LogPath)) | Out-Null
"# board WAV regression $(Get-Date -Format o)" | Set-Content -Path $LogPath -Encoding ASCII

$player = $null
$playerProcess = $null
$passCount = 0
$failCount = 0
$skipCount = 0
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    if ($SerialTestMode) {
        Send-BoardCommand -Serial $serial -Command "TEST 1"
        Start-Sleep -Milliseconds 300
        Add-SerialLog -Path $LogPath -Chunk $serial.ReadExisting()
    }
    Send-BoardCommand -Serial $serial -Command "LOG $RxLogFormat"
    Start-Sleep -Milliseconds 300
    Add-SerialLog -Path $LogPath -Chunk $serial.ReadExisting()
    Send-BoardCommand -Serial $serial -Command "STATUS"
    foreach ($mode in $Modes) {
        $wav = Join-Path $WavDir "$mode.wav"
        if (-not (Test-Path $wav)) {
            Write-Host "SKIP $mode missing $wav"
            $skipCount++
            continue
        }

        Write-Host ""
        $serial.DiscardInBuffer()
        Write-Host "Selecting $mode on board"
        $modeDeadline = [DateTime]::UtcNow.AddSeconds($ModeWaitSeconds)
        $nextModeCommand = [DateTime]::MinValue
        $modeConfirmed = $false
        while ([DateTime]::UtcNow -lt $modeDeadline) {
            if ([DateTime]::UtcNow -ge $nextModeCommand) {
                Send-BoardCommand -Serial $serial -Command "MODE $mode"
                $nextModeCommand = [DateTime]::UtcNow.AddSeconds(1)
            }
            Start-Sleep -Milliseconds 100
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -eq 0) {
                continue
            }
            Add-SerialLog -Path $LogPath -Chunk $chunk
            if ($chunk -match "\[diag\]\s+mode=$([regex]::Escape($mode))\b" -or
                $chunk -match "\[mode\]\s+selected\s+$([regex]::Escape($mode))\b") {
                $modeConfirmed = $true
                break
            }
        }
        if (-not $modeConfirmed) {
            Write-Host "FAIL $mode board did not confirm mode within $ModeWaitSeconds seconds"
            $failCount++
            continue
        }

        if ($SelectDelaySeconds -gt 0) {
            Start-Sleep -Seconds $SelectDelaySeconds
        }

        $duration = Get-WavDurationSeconds -Path $wav
        $limit = [math]::Min($duration + 5, $MaxSeconds)
        $decoded = New-Object System.Text.StringBuilder
        $raw = New-Object System.Text.StringBuilder
        $lineBuffer = ""
        $serial.DiscardInBuffer()
        if ($OutputDevice -ge 0) {
            $playerProcess = Start-Process -FilePath "python" -ArgumentList @(
                "tools\play_wav_device.py",
                "--device",
                $OutputDevice.ToString(),
                "--gain",
                $OutputGain.ToString([System.Globalization.CultureInfo]::InvariantCulture),
                $wav
            ) -PassThru -WindowStyle Hidden
        } else {
            $player = New-Object System.Media.SoundPlayer($wav)
            $player.Load()
            $player.Play()
        }

        $deadline = [DateTime]::UtcNow.AddSeconds($limit)
        $passed = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -eq 0) {
                continue
            }
            [void]$raw.Append($chunk)
            Add-SerialLog -Path $LogPath -Chunk $chunk

            $lineBuffer += $chunk
            $parts = [regex]::Split($lineBuffer, "\r?\n")
            if ($parts.Count -eq 0) {
                continue
            }
            $lineBuffer = $parts[$parts.Count - 1]
            for ($lineIndex = 0; $lineIndex -lt ($parts.Count - 1); $lineIndex++) {
                $line = $parts[$lineIndex]
                if ($RxLogFormat -eq "HEX" -and $line -match "^\[rxhex\]\s+(.*)$") {
                    [void]$decoded.Append((Convert-RxHex $Matches[1]))
                } elseif ($RxLogFormat -eq "TEXT" -and $line -notmatch "^\[" -and $line.Length -gt 0) {
                    [void]$decoded.Append((Convert-RxEscapes $line))
                } elseif ($line -match "^\[$([regex]::Escape($mode))\]\s+(.*)$") {
                    [void]$decoded.Append((Convert-RxEscapes $Matches[1]))
                }
            }
            if (Test-DecodedText -Decoded $decoded.ToString() -Fragments $fragments) {
                $passed = $true
                break
            }
        }
        if ($player -ne $null) {
            $player.Stop()
            $player = $null
        }
        if ($playerProcess -ne $null -and -not $playerProcess.HasExited) {
            Stop-Process -Id $playerProcess.Id
            $playerProcess = $null
        }

        if ($passed) {
            Write-Host "PASS $mode matched board decoded text"
            $passCount++
        } else {
            Write-Host "FAIL $mode no $MatchLength-char expected fragment matched within $limit seconds"
            $failCount++
        }
    }
    Write-Host ""
    Write-Host "SUMMARY PASS=$passCount FAIL=$failCount SKIP=$skipCount TOTAL=$($passCount + $failCount + $skipCount)"
} finally {
    if ($player -ne $null) {
        $player.Stop()
    }
    if ($playerProcess -ne $null -and -not $playerProcess.HasExited) {
        Stop-Process -Id $playerProcess.Id
    }
    if ($serial.IsOpen) {
        try {
            Send-BoardCommand -Serial $serial -Command "TEST 0"
            Start-Sleep -Milliseconds 100
            Send-BoardCommand -Serial $serial -Command "LOG TEXT"
            Start-Sleep -Milliseconds 100
        } catch {
        }
        $serial.Close()
    }
}
