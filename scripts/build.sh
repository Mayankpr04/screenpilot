#!/usr/bin/env bash
set -euo pipefail
python3 -m pip install -e '.[dev]'
python3 -m PyInstaller --noconfirm --clean --windowed --name ScreenPilot --paths . screenpilot/app.py
echo "Built dist/ScreenPilot/ScreenPilot"
