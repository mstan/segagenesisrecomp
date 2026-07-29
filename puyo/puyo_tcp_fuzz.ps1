param(
    [string]$GameDir = "F:\Projects\segagenesisrecomp\PuyoPuyoRecomp\build-vs\Release",
    [int]$Port = 4378,
    [int]$GameplayIterations = 120,
    [int]$SettleFrames = 900,
    [string]$CoverageOut = "tcp_fuzz.exec.txt",
    [switch]$KeepRunning,
    [switch]$Launch,
    [switch]$ForceInterpreter
)

$ErrorActionPreference = "Stop"
$script:CommandId = 1

function Connect-Puyo {
    $deadline = (Get-Date).AddSeconds(10)
    do {
        try {
            return [Net.Sockets.TcpClient]::new("127.0.0.1", $Port)
        } catch {
            Start-Sleep -Milliseconds 100
        }
    } while ((Get-Date) -lt $deadline)
    throw "Puyo TCP server did not become ready on port $Port"
}

function Send-PuyoCommand([string]$Body) {
    $script:Writer.WriteLine($Body)
    $script:Writer.Flush()
    try {
        $line = $script:Reader.ReadLine()
    } catch {
        throw "Puyo process disconnected while sending: $Body"
    }
    if (-not $line) {
        throw "Puyo process disconnected while sending: $Body"
    }
    return $line
}

function Get-PuyoFrame {
    $id = $script:CommandId++
    $line = Send-PuyoCommand "{`"id`":$id,`"cmd`":`"ping`"}"
    $match = [regex]::Match($line, '"frame":(\d+)')
    if (-not $match.Success) {
        throw "Malformed ping response: $line"
    }
    return [int]$match.Groups[1].Value
}

function Wait-PuyoFrames([int]$Count) {
    $start = Get-PuyoFrame
    $frame = $start
    while ($frame -lt $start + $Count) {
        $frame = Get-PuyoFrame
    }
    return $frame
}

function Set-PuyoInput([int]$Mask) {
    $id = $script:CommandId++
    $keys = "{0:X2}" -f $Mask
    [void](Send-PuyoCommand "{`"id`":$id,`"cmd`":`"set_input`",`"keys`":`"$keys`"}")
}

function Tap-PuyoButton([int]$Mask, [int]$HeldFrames, [int]$SettleFrames) {
    Set-PuyoInput $Mask
    [void](Wait-PuyoFrames $HeldFrames)
    Set-PuyoInput 0
    [void](Wait-PuyoFrames $SettleFrames)
}

function Save-PuyoScreenshot([string]$Name) {
    $id = $script:CommandId++
    [void](Send-PuyoCommand "{`"id`":$id,`"cmd`":`"screenshot`",`"path`":`"$Name`"}")
}

if ($Launch) {
    $exe = Join-Path $GameDir "PuyoRecomp.exe"
    $rom = Join-Path $GameDir "puyo.bin"
    if ($ForceInterpreter) {
        $env:GENESIS_FORCE_INTERP = "1"
    }
    Start-Process -FilePath $exe `
        -ArgumentList @($rom, "--no-launcher", "--exec-coverage-out", $CoverageOut) `
        -WorkingDirectory $GameDir `
        -RedirectStandardOutput (Join-Path $GameDir "tcp_fuzz.stdout.log") `
        -RedirectStandardError (Join-Path $GameDir "tcp_fuzz.stderr.log") `
        -WindowStyle Hidden | Out-Null
}

$script:Client = Connect-Puyo
$script:Stream = $script:Client.GetStream()
$script:Writer = [IO.StreamWriter]::new(
    $script:Stream, [Text.UTF8Encoding]::new($false))
$script:Writer.NewLine = "`n"
$script:Reader = [IO.StreamReader]::new($script:Stream)

try {
    $frame = Get-PuyoFrame
    if ($frame -lt 180) {
        [void](Wait-PuyoFrames (180 - $frame))
    }

    # Start is sampled in a narrow title-state window. Repeated clean edges
    # navigate title -> mode -> level -> intro without relying on wall timing.
    for ($i = 1; $i -le 16; $i++) {
        Tap-PuyoButton 0x80 4 40
        if (($i % 4) -eq 0) {
            Save-PuyoScreenshot ("tcp_fuzz_start_{0:D2}.png" -f $i)
        }
    }

    # Stable, deterministic gameplay corpus. It exercises both rotations,
    # horizontal repeat, soft drop, and combined move/rotate inputs.
    $masks = @(
        0x04, 0x08, 0x40, 0x10, 0x20, 0x02,
        0x44, 0x48, 0x14, 0x18, 0x24, 0x28,
        0x42, 0x12, 0x22, 0x06, 0x0A
    )
    for ($i = 0; $i -lt $GameplayIterations; $i++) {
        $mask = $masks[$i % $masks.Count]
        $held = 2 + (($i * 7) % 19)
        $settle = 8 + (($i * 11) % 29)
        Tap-PuyoButton $mask $held $settle
        if ((($i + 1) % 20) -eq 0) {
            Save-PuyoScreenshot ("tcp_fuzz_game_{0:D3}.png" -f ($i + 1))
            Write-Output ("iteration={0} frame={1}" -f ($i + 1), (Get-PuyoFrame))
        }
    }

    if ($SettleFrames -gt 0) {
        [void](Wait-PuyoFrames $SettleFrames)
        Save-PuyoScreenshot "tcp_fuzz_settle.png"
    }

    $id = $script:CommandId++
    Write-Output (Send-PuyoCommand "{`"id`":$id,`"cmd`":`"audio_stats`"}")
    Save-PuyoScreenshot "tcp_fuzz_final.png"
    Write-Output ("completed frame={0}" -f (Get-PuyoFrame))
    if (-not $KeepRunning) {
        $id = $script:CommandId++
        [void](Send-PuyoCommand "{`"id`":$id,`"cmd`":`"quit`"}")
    }
} finally {
    if ($script:Client) {
        $script:Client.Close()
    }
}
