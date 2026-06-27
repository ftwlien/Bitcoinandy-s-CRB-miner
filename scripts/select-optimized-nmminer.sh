#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-${repo_dir}/nmminer}"

cpu_model="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')"
source_bin=""
lane_name=""
threads=""
lanes="1"
requires_l3="0"

case "${cpu_model}" in
  *"7950X3D"*)
    source_bin="${repo_dir}/optimized-binaries/7950x3d/nmminer.premask.lto"
    lane_name="7950X3D premask LTO"
    threads="32"
    requires_l3="1"
    ;;
  *"7950X"*)
    source_bin="${repo_dir}/optimized-binaries/7950x/nmminer.gcc12native-test"
    lane_name="7950X GCC12 native"
    threads="28"
    ;;
  *"9950X"*)
    source_bin="${repo_dir}/optimized-binaries/9950x/nmminer.gcc12lto-live"
    lane_name="9950X GCC12 LTO"
    threads="28"
    ;;
  *)
    source_bin="${repo_dir}/nmminer"
    lane_name="generic build"
    threads=""
    lanes="auto"
    ;;
esac

if [[ "${source_bin}" == "${repo_dir}/nmminer" && ! -x "${source_bin}" ]]; then
  make -C "${repo_dir}" ARCH=x86-64-v3
fi

if [[ ! -x "${source_bin}" ]]; then
  echo "Optimized binary is missing or not executable: ${source_bin}" >&2
  exit 1
fi

install -m 0755 "${source_bin}" "${dest}"
echo "Installed ${lane_name}: ${source_bin} -> ${dest}"

if [[ -n "${threads}" ]]; then
  echo "Recommended runtime: CRB_THREADS=${threads} CRB_LANES=${lanes}"
else
  echo "Recommended runtime: CRB_THREADS=auto CRB_LANES=${lanes}"
fi

if [[ "${requires_l3}" == "1" ]]; then
  echo "Recommended env: NM_L3_DATASETS=1"
fi
