# CFace companion setup for Windows PowerShell. Run from the clone:
#   .\setup.ps1 [-Board host[:port]] [-DryRun]
param([string]$Board = "", [switch]$DryRun)
$ErrorActionPreference = "Stop"
$dir = $PSScriptRoot

$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { throw "Python not found. Install Python 3 from https://www.python.org/downloads/ (tick 'Add to PATH') and re-run." }
& python -c "import sys; sys.exit(sys.version_info < (3,8))"
if ($LASTEXITCODE -ne 0) { throw "Python 3.8+ required." }

if (-not $Board) { $Board = Read-Host "ESP32 IP or host shown on the OLED (add :port if not 80)" }
$Board = ($Board -replace '^http://', '') -replace '/.*$', ''
if (-not $Board) { throw "No host given." }

$url = "http://$Board/notify"
$cmd = "python `"$dir\notify-oled.py`""
Write-Host "Config:  $dir\config.env  (ESP32_FACE_URL=$url)"
Write-Host "Hook:    $cmd"
if ($DryRun) { Write-Host "(dry run, nothing written)"; return }

(Get-Content "$dir\config.env.example") -replace '^ESP32_FACE_URL=.*', "ESP32_FACE_URL=$url" |
  Set-Content -Encoding ASCII "$dir\config.env"
& python "$dir\notify-oled.py" --text "Setup OK" --verbose
if ($LASTEXITCODE -ne 0) { Write-Warning "Could not reach the board. Is this machine on the same network? Setup continues." }
& python "$dir\merge-hook.py" --hooks "$HOME\.cursor\hooks.json" --command $cmd
Write-Host "Done. Restart Cursor so it loads the hook."
