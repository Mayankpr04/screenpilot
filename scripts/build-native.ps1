$ErrorActionPreference = "Stop"

function Invoke-Checked {
  param([Parameter(Mandatory = $true)][scriptblock]$Command)

  & $Command

  if ($LASTEXITCODE -ne 0) {
    throw "Native command failed with exit code $LASTEXITCODE"
  }
}

# GitHub windows-latest now uses Visual Studio 2026.
# Fall back to Visual Studio 2022 for local developer machines.
$cmakeHelp = cmake --help | Out-String

if ($cmakeHelp -match "Visual Studio 18 2026") {
  $generator = "Visual Studio 18 2026"
}
elseif ($cmakeHelp -match "Visual Studio 17 2022") {
  $generator = "Visual Studio 17 2022"
}
else {
  throw "No supported Visual Studio CMake generator was found."
}

Write-Host "Using CMake generator: $generator"

Invoke-Checked {
  cmake -S native-windows -B build-native -G $generator -A x64
}

Invoke-Checked {
  cmake --build build-native --config Release --parallel
}

New-Item -ItemType Directory -Force dist-native | Out-Null
Copy-Item build-native\Release\ScreenPilot.exe `
  dist-native\ScreenPilot.exe -Force

$isccCandidates = @(
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)

$iscc = $isccCandidates |
  Where-Object { Test-Path $_ } |
  Select-Object -First 1

if (-not $iscc) {
  throw "Inno Setup 6 was not found."
}

& $iscc "installer\ScreenPilot-Native.iss"

if ($LASTEXITCODE -ne 0) {
  throw "Inno Setup failed with exit code $LASTEXITCODE"
}

Write-Host "Built installer-output\ScreenPilot-Setup.exe"
