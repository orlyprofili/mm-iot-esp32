#!/usr/bin/env bash
# Back up all OpenWrt config from the HaLow Pi to the Mac.
# Usage:  ./backup-halow.sh [user@host]   (# defaults to root@10.0.0.15)

set -euo pipefail

PI_HOST=${1:-root@10.0.0.15}
DEST_DIR="./halow-backups"
STAMP=$(date +%Y%m%d-%H%M)
DEST_FILE="${DEST_DIR}/halow_cfg_${STAMP}.tar.gz"

mkdir -p "${DEST_DIR}"
echo "📡  Connecting to ${PI_HOST} …  (enter the Pi password if prompted)"

# OpenWrt’s built-in backup command: sysupgrade -b -
#   -b  → make backup
#   -   → write to stdout
ssh "${PI_HOST}" sysupgrade -b - > "${DEST_FILE}"

echo "✅  Saved router config to ${DEST_FILE}"
