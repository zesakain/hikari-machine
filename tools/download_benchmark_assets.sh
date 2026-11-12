#!/usr/bin/env bash
# tools/download_benchmark_assets.sh
# Reproducible downloader for Milestone 10G benchmark assets (Sponza, FlightHelmet, DamagedHelmet)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 "${SCRIPT_DIR}/download_benchmark_assets.py" "$@"
