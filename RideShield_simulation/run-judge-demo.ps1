param(
    [string]$Scenario = "scenario-judge-demo.yaml",
    [int]$Timeout = 65000
)

$ErrorActionPreference = "Stop"
$RawLog = "judge-demo-raw.log"

Write-Host ""
Write-Host "==============================================="
Write-Host "        RideShield Semi-Final Judge Demo"
Write-Host "==============================================="
Write-Host "Scenario : $Scenario"
Write-Host "Raw log  : $RawLog"
Write-Host ""

# Check Wokwi CLI
if (-not (Get-Command wokwi-cli -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] wokwi-cli was not found in PATH." -ForegroundColor Red
    Write-Host "Install/configure Wokwi CLI first, then reopen PowerShell."
    exit 1
}

# Check scenario file
if (-not (Test-Path $Scenario)) {
    Write-Host "[ERROR] Scenario file not found: $Scenario" -ForegroundColor Red
    exit 1
}

# Patterns that should be hidden from the judge-facing view.
$hidePatterns = @(
    '^\[RideShield Full Automated',
    '^Wokwi CLI',
    '^Connected to Wokwi',
    '^Starting simulation',
    '^ESP-ROM:',
    '^Build:',
    '^rst:',
    '^SPIWP:',
    '^mode:',
    '^load:',
    '^entry ',
    '^E \([0-9]+\) ledc:',
    '^\[STATUS\] state=COUNTDOWN'
)

# Patterns that should be shown to judges.
$showPatterns = @(
    '^\[BOOT\]',
    '^\[HELMET\]',
    '^\[CRASH\]',
    '^\[VERIFY\]',
    '^\[COUNTDOWN\]',
    '^\[CANCEL\]',
    '^\[EMERGENCY\]',
    '^\[LTE\]',
    '^\[SD\]',
    '^\[BLACKBOX\]',
    '^\[RESET\]',
    '^\[STATUS\] state=NOT_WORN',
    '^\[STATUS\] state=READY',
    '^\s+RIDESHIELD EMERGENCY ALERT',
    '^\s+Event ID:',
    '^\s+Event severity:',
    '^\s+Pre-event speed:',
    '^\s+GPS:',
    '^\s+LTE:',
    '^\s+SMS:',
    '^===== /events\.csv =====',
    '^event_id,time_ms,outcome',
    '^[0-9]+,[0-9]+,(CANCELLED|CONFIRMED|REJECTED),',
    '^===== END =====',
    'Scenario completed successfully'
)

function Matches-AnyPattern {
    param(
        [string]$Text,
        [string[]]$Patterns
    )

    foreach ($pattern in $Patterns) {
        if ($Text -match $pattern) {
            return $true
        }
    }
    return $false
}

# Remove an old log so this run is unambiguous.
if (Test-Path $RawLog) {
    Remove-Item $RawLog -Force
}

Write-Host "[INFO] Starting Wokwi automated simulation..."
Write-Host "[INFO] Judge view is filtered; full output is saved to $RawLog."
Write-Host ""

# Run Wokwi.
# Each raw line is written to judge-demo-raw.log first, then selected
# RideShield firmware lines are shown on screen.
& wokwi-cli . --scenario $Scenario --timeout $Timeout 2>&1 |
    ForEach-Object {
        $line = $_.ToString()

        # Save every raw line exactly for evidence/debugging.
        Add-Content -Path $RawLog -Value $line

        if (Matches-AnyPattern -Text $line -Patterns $hidePatterns) {
            return
        }

        if (Matches-AnyPattern -Text $line -Patterns $showPatterns) {
            # Optional light color coding for readability.
            if ($line -match '^\[EMERGENCY\]' -or $line -match '^\s+SMS:') {
                Write-Host $line -ForegroundColor Red
            }
            elseif ($line -match '^\[CANCEL\]') {
                Write-Host $line -ForegroundColor Yellow
            }
            elseif ($line -match '^\[VERIFY\]' -or $line -match '^\[CRASH\]') {
                Write-Host $line -ForegroundColor Cyan
            }
            elseif ($line -match '^\[LTE\]' -or $line -match '^\s+LTE:') {
                Write-Host $line -ForegroundColor Magenta
            }
            elseif ($line -match '^\[SD\]' -or $line -match '^===== /events\.csv =====') {
                Write-Host $line -ForegroundColor Green
            }
            else {
                Write-Host $line
            }
        }
    }

$exitCode = $LASTEXITCODE

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "==============================================="
    Write-Host " Judge demo finished successfully." -ForegroundColor Green
    Write-Host " Full raw log: $RawLog"
    Write-Host "==============================================="
}
else {
    Write-Host "==============================================="
    Write-Host " Wokwi exited with code $exitCode" -ForegroundColor Red
    Write-Host " Check the full raw log: $RawLog"
    Write-Host "==============================================="
}

exit $exitCode
