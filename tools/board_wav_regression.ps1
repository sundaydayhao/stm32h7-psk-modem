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

function Get-DecodeMatchInfo {
    param(
        [string]$Expected,
        [string]$Decoded,
        [System.Collections.Generic.List[string]]$Fragments
    )

    $expectedClean = ($Expected -replace "\s+", " ").Trim()
    $decodedClean = ($Decoded -replace "\s+", " ").Trim()

    $info = [ordered]@{
        Found = $false
        FragmentLength = 0
        ExpectedOffset = -1
        DecodedOffset = -1
        WindowBER = 1.0
        WindowCER = 1.0
        WindowBitErrors = 0
        WindowTotalBits = 0
    }

    foreach ($fragment in $Fragments) {
        $decodedOffset = $decodedClean.IndexOf($fragment, [System.StringComparison]::Ordinal)
        if ($decodedOffset -lt 0) {
            continue
        }

        $expectedOffset = $expectedClean.IndexOf($fragment, [System.StringComparison]::Ordinal)
        $length = $fragment.Length
        $info.Found = $true
        $info.FragmentLength = $length
        $info.ExpectedOffset = $expectedOffset
        $info.DecodedOffset = $decodedOffset
        $info.WindowBER = 0.0
        $info.WindowCER = 0.0
        $info.WindowTotalBits = $length * 8
        return [pscustomobject]$info
    }

    return [pscustomobject]$info
}

function Get-BitCount {
    param([int]$Value)

    $count = 0
    $v = $Value -band 0xff
    while ($v -ne 0) {
        $v = $v -band ($v - 1)
        $count++
    }
    return $count
}

function Get-DecodeErrorStats {
    param(
        [string]$Expected,
        [string]$Decoded
    )

    $expectedClean = ($Expected -replace "\s+", " ").Trim()
    $decodedClean = ($Decoded -replace "\s+", " ").Trim()
    $decodedLen = $decodedClean.Length
    $expectedLen = $expectedClean.Length

    $stats = [ordered]@{
        DecodedLength = $decodedLen
        BestOffset = -1
        ComparedChars = 0
        CharErrors = 0
        BitErrors = 0
        TotalBits = 0
        CER = 1.0
        BER = 1.0
    }

    if ($decodedLen -le 0 -or $expectedLen -le 0) {
        return [pscustomobject]$stats
    }

    $bestBitErrors = [int]::MaxValue
    $bestCharErrors = [int]::MaxValue
    $bestCompared = 0
    $bestTotalBits = 0
    $bestOffset = -1

    for ($offset = 0; $offset -lt $expectedLen; $offset++) {
        $compared = [math]::Min($decodedLen, $expectedLen - $offset)
        if ($compared -le 0) {
            continue
        }

        $bitErrors = [math]::Abs($decodedLen - $compared) * 8
        $charErrors = [math]::Abs($decodedLen - $compared)
        for ($i = 0; $i -lt $compared; $i++) {
            $expectedByte = [int][byte][char]$expectedClean[$offset + $i]
            $decodedByte = [int][byte][char]$decodedClean[$i]
            if ($expectedByte -ne $decodedByte) {
                $charErrors++
                $bitErrors += Get-BitCount ($expectedByte -bxor $decodedByte)
            }
        }

        $totalBits = [math]::Max($decodedLen, $compared) * 8
        if ($bitErrors -lt $bestBitErrors -or
            ($bitErrors -eq $bestBitErrors -and $charErrors -lt $bestCharErrors)) {
            $bestBitErrors = $bitErrors
            $bestCharErrors = $charErrors
            $bestCompared = $compared
            $bestTotalBits = $totalBits
            $bestOffset = $offset
        }
    }

    if ($bestTotalBits -gt 0) {
        $stats.BER = [double]$bestBitErrors / [double]$bestTotalBits
        $stats.CER = [double]$bestCharErrors / [double][math]::Max($decodedLen, $bestCompared)
    }
    $stats.BestOffset = $bestOffset
    $stats.ComparedChars = $bestCompared
    $stats.CharErrors = $bestCharErrors
    $stats.BitErrors = $bestBitErrors
    $stats.TotalBits = $bestTotalBits
    return [pscustomobject]$stats
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

function Send-BoardCommandRepeated {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [int]$Count = 2,
        [int]$DelayMs = 150
    )

    for ($i = 0; $i -lt $Count; $i++) {
        Send-BoardCommand -Serial $Serial -Command $Command
        Start-Sleep -Milliseconds $DelayMs
    }
}

function Wait-BoardResponse {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [string]$Pattern,
        [string]$LogPath,
        [int]$TimeoutSeconds,
        [int]$RetryMilliseconds = 1000
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $nextCommand = [DateTime]::MinValue
    $response = ""
    while ([DateTime]::UtcNow -lt $deadline) {
        if ([DateTime]::UtcNow -ge $nextCommand) {
            Send-BoardCommand -Serial $Serial -Command $Command
            $nextCommand = [DateTime]::UtcNow.AddMilliseconds($RetryMilliseconds)
        }
        Start-Sleep -Milliseconds 100
        $chunk = $Serial.ReadExisting()
        if ($chunk.Length -eq 0) {
            continue
        }
        Add-SerialLog -Path $LogPath -Chunk $chunk
        $response += $chunk
        if ($response -match $Pattern) {
            return $true
        }
        if ($response.Length -gt 4096) {
            $response = $response.Substring($response.Length - 4096)
        }
    }
    return $false
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
        Send-BoardCommandRepeated -Serial $serial -Command "TEST 1"
        Start-Sleep -Milliseconds 300
        Add-SerialLog -Path $LogPath -Chunk $serial.ReadExisting()
    }
    foreach ($mode in $Modes) {
        $wav = Join-Path $WavDir "$mode.wav"
        if (-not (Test-Path $wav)) {
            Write-Host "SKIP $mode missing $wav"
            $skipCount++
            continue
        }

        Write-Host ""
        Start-Sleep -Milliseconds 300
        $serial.DiscardInBuffer()
        Write-Host "Selecting $mode on board"
        $modePattern = "\[mode\]\s+selected\s+$([regex]::Escape($mode))\b"
        $modeConfirmed = Wait-BoardResponse -Serial $serial -Command "MODE $mode" `
            -Pattern $modePattern -LogPath $LogPath -TimeoutSeconds $ModeWaitSeconds
        if (-not $modeConfirmed) {
            Write-Host "FAIL $mode board did not confirm mode within $ModeWaitSeconds seconds"
            $failCount++
            continue
        }

        $logPattern = "\[log\]\s+rx=$([regex]::Escape($RxLogFormat))\b"
        $logConfirmed = Wait-BoardResponse -Serial $serial -Command "LOG $RxLogFormat" `
            -Pattern $logPattern -LogPath $LogPath -TimeoutSeconds $ModeWaitSeconds
        if (-not $logConfirmed) {
            Write-Host "FAIL $mode board did not confirm LOG $RxLogFormat within $ModeWaitSeconds seconds"
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
                if ($RxLogFormat -eq "HEX") {
                    if ($line -match "^\[rxhex\]\s+(.*)$") {
                        [void]$decoded.Append((Convert-RxHex $Matches[1]))
                    }
                } else {
                    if ($line -notmatch "^\[" -and $line.Length -gt 0) {
                        [void]$decoded.Append((Convert-RxEscapes $line))
                    } elseif ($line -match "^\[$([regex]::Escape($mode))\]\s+(.*)$") {
                        [void]$decoded.Append((Convert-RxEscapes $Matches[1]))
                    }
                }
            }
            $matchInfo = Get-DecodeMatchInfo -Expected $expected -Decoded $decoded.ToString() -Fragments $fragments
            if ($matchInfo.Found) {
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

        $stats = Get-DecodeErrorStats -Expected $expected -Decoded $decoded.ToString()
        $matchInfo = Get-DecodeMatchInfo -Expected $expected -Decoded $decoded.ToString() -Fragments $fragments
        $berText = $stats.BER.ToString("P4", [System.Globalization.CultureInfo]::InvariantCulture)
        $cerText = $stats.CER.ToString("P4", [System.Globalization.CultureInfo]::InvariantCulture)
        $winBerText = $matchInfo.WindowBER.ToString("P4", [System.Globalization.CultureInfo]::InvariantCulture)
        $winCerText = $matchInfo.WindowCER.ToString("P4", [System.Globalization.CultureInfo]::InvariantCulture)
        if ($passed) {
            Write-Host "PASS $mode matched board decoded text BER=$berText CER=$cerText winBER=$winBerText winCER=$winCerText chars=$($stats.DecodedLength) biterr=$($stats.BitErrors)/$($stats.TotalBits) offset=$($stats.BestOffset) winOffset=$($matchInfo.DecodedOffset)"
            $passCount++
        } else {
            Write-Host "FAIL $mode no $MatchLength-char expected fragment matched within $limit seconds BER=$berText CER=$cerText winBER=$winBerText winCER=$winCerText chars=$($stats.DecodedLength) biterr=$($stats.BitErrors)/$($stats.TotalBits) offset=$($stats.BestOffset)"
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
