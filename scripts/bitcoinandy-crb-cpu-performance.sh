#!/usr/bin/env bash
set -euo pipefail

# Keep 7950X3D CRB hosts in the runtime mode that sustained the protected
# 126-127 kH/s window on 3060mrig1. This is intentionally host tuning, not a
# miner-code change.
if [[ -w /sys/devices/system/cpu/amd_pstate/status ]]; then
  printf guided > /sys/devices/system/cpu/amd_pstate/status || true
fi

for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  [[ -e "$governor" ]] && printf performance > "$governor" || true
done

for epp in /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference; do
  [[ -e "$epp" ]] && printf performance > "$epp" || true
done

if [[ -w /sys/devices/system/cpu/cpufreq/boost ]]; then
  printf 1 > /sys/devices/system/cpu/cpufreq/boost || true
fi
