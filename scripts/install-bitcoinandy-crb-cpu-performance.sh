#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

install -m 0755 "$script_dir/bitcoinandy-crb-cpu-performance.sh" \
  /usr/local/sbin/bitcoinandy-crb-cpu-performance.sh
install -m 0644 "$script_dir/bitcoinandy-crb-cpu-performance.service" \
  /etc/systemd/system/bitcoinandy-crb-cpu-performance.service

systemctl daemon-reload
systemctl enable --now bitcoinandy-crb-cpu-performance.service

printf 'amd-pstate status: '
cat /sys/devices/system/cpu/amd_pstate/status 2>/dev/null || printf 'unavailable\n'
systemctl --no-pager --full status bitcoinandy-crb-cpu-performance.service
