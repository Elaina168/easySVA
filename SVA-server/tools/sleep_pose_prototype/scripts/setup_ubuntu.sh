#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

sudo apt-get update
sudo apt-get install -y python3 python3-venv python3-pip ffmpeg libgl1

cd "${PROJECT_DIR}"
python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python -m unittest discover -s tests -v

printf '\nSetup complete. Activate the environment with:\n'
printf '  cd %s\n' "${PROJECT_DIR}"
printf '  source .venv/bin/activate\n'
