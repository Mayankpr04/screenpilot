$ErrorActionPreference = "Stop"

cmake -S native-windows -B build-native -G "Visual Studio 17 2022" -A x64
cmake --build build-native --config Release --parallel

New-Item -ItemType Directory -Force dist-native | Out-Null
Copy-Item build-native\Release\ScreenPilot.exe dist-native\ScreenPilot.exe -Force

$isccCandidates = @(
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
  throw "Inno Setup 6 was not found. Install it from https://jrsoftware.org/isinfo.php"
}

& $iscc "installer\ScreenPilot-Native.iss"
Write-Host "Built installer-output\ScreenPilot-Setup.exe"

