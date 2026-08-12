param(
    [string[]]$Modes = @("QPSK31", "QPSK63"),
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$WavDir = "C:\projects\psk-script\generated-wav",
    [string]$ExpectedText = "C:\projects\psk-script\input.txt",
    [string]$LogPath = "C:\projects\stm32h7-psk-modem\log.txt",
    [int]$SelectDelaySeconds = 8,
    [int]$ModeWaitSeconds = 60,
    [int]$MaxSeconds = 140,
    [int]$MatchLength = 80,
    [int]$OutputDevice = -1,
    [double]$OutputGain = 1.0
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

function Get-ExpectedFragments {
    param(
        [string]$Text,
        [int]$Length
    )

    $clean = ($Text -replace "\s+", " ").Trim()
    $fragments = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i + $Length -le $clean.Length; $i += [math]::Max(1, [int]($Length / 2))) {
        $fragments.Add($clean.Substring($i, $Length))
        if ($fragments.Count -ge 80) {
            break
        }
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
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    foreach ($mode in $Modes) {
        $wav = Join-Path $WavDir "$mode.wav"
        if (-not (Test-Path $wav)) {
            Write-Host "SKIP $mode missing $wav"
            continue
        }

        Write-Host ""
        Write-Host "Select $mode on the board now. Waiting up to $ModeWaitSeconds seconds for diag confirmation."
        $serial.DiscardInBuffer()
        $modeDeadline = [DateTime]::UtcNow.AddSeconds($ModeWaitSeconds)
        $modeConfirmed = $false
        while ([DateTime]::UtcNow -lt $modeDeadline) {
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
            Write-Host "SKIP $mode board did not confirm mode within $ModeWaitSeconds seconds"
            continue
        }

        if ($SelectDelaySeconds -gt 0) {
            Start-Sleep -Seconds $SelectDelaySeconds
        }

        $duration = Get-WavDurationSeconds -Path $wav
        $limit = [math]::Min($duration + 5, $MaxSeconds)
        $decoded = New-Object System.Text.StringBuilder
        $raw = New-Object System.Text.StringBuilder
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

            foreach ($line in ($chunk -split "\r?\n")) {
                if ($line -match "^\[$([regex]::Escape($mode))\]\s+(.*)$") {
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
        } else {
            Write-Host "FAIL $mode no $MatchLength-char expected fragment matched within $limit seconds"
        }
    }
} finally {
    if ($player -ne $null) {
        $player.Stop()
    }
    if ($playerProcess -ne $null -and -not $playerProcess.HasExited) {
        Stop-Process -Id $playerProcess.Id
    }
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
