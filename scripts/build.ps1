$ErrorActionPreference = "Stop"
python -m pip install -e ".[dev]"
python -m PyInstaller --noconfirm --clean --windowed --name ScreenPilot `
  --icon assets/screenpilot.svg `
  --add-data "assets/screenpilot.svg;assets" `
  --collect-all monitorcontrol `
  --collect-all screen_brightness_control `
  --paths . screenpilot/app.py

$isccCandidates = @(
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
  throw "Inno Setup 6 was not found. Install it from https://jrsoftware.org/isinfo.php"
}
& $iscc "installer\ScreenPilot.iss"
Write-Host "Built installer-output\ScreenPilot-Setup.exe"
