#!/usr/bin/env bash
set -euo pipefail

if [[ "${WILDRIG_WORKER:-}" == "vast-defjob" ]]; then
  WILDRIG_WORKER="$(hostname -s 2>/dev/null || hostname || echo vast-defjob)"
fi

if [[ "${CRB_WORKER:-}" == "vast-defjob" ]]; then
  CRB_WORKER="$(hostname -s 2>/dev/null || hostname || echo vast-defjob)"
fi

miner="${MINER:-wildrig}"
pass_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    wildrig|crb|srbminer|xmrig|both)
      miner="$1"
      shift
      ;;
    --miner)
      miner="${2:?--miner requires wildrig, crb, srbminer, xmrig, or both}"
      shift 2
      ;;
    --wildrig-algo)
      WILDRIG_ALGO="${2:?--wildrig-algo requires a value}"
      shift 2
      ;;
    --wildrig-pool)
      WILDRIG_POOL="${2:?--wildrig-pool requires a value}"
      shift 2
      ;;
    --wildrig-wallet)
      WILDRIG_WALLET="${2:?--wildrig-wallet requires a value}"
      shift 2
      ;;
    --wildrig-worker)
      WILDRIG_WORKER="${2:?--wildrig-worker requires a value}"
      shift 2
      ;;
    --wildrig-user-template)
      WILDRIG_USER_TEMPLATE="${2:?--wildrig-user-template requires a value}"
      shift 2
      ;;
    --wildrig-gpu-list)
      WILDRIG_GPU_LIST="${2:?--wildrig-gpu-list requires a value}"
      shift 2
      ;;
    --wildrig-password)
      WILDRIG_PASSWORD="${2:?--wildrig-password requires a value}"
      shift 2
      ;;
    --wildrig-opencl-platforms)
      WILDRIG_OPENCL_PLATFORMS="${2:?--wildrig-opencl-platforms requires a value}"
      shift 2
      ;;
    --crb-algo|--xmrig-algo)
      CRB_ALGO="${2:?--crb-algo requires a value}"
      shift 2
      ;;
    --crb-pool|--crb-url|--xmrig-url)
      CRB_POOL="${2:?--crb-pool requires a value}"
      shift 2
      ;;
    --crb-wallet|--crb-user|--xmrig-user)
      CRB_WALLET="${2:?--crb-wallet requires a value}"
      shift 2
      ;;
    --crb-pass|--crb-password|--xmrig-pass)
      CRB_PASSWORD="${2:?--crb-pass requires a value}"
      shift 2
      ;;
    --xmrig-tls)
      # Compatibility no-op: the rplant CRB endpoint is plain TCP.
      shift
      ;;
    --crb-worker|--xmrig-worker)
      CRB_WORKER="${2:?--crb-worker requires a value}"
      shift 2
      ;;
    --crb-threads|--xmrig-threads)
      CRB_THREADS="${2:?--crb-threads requires a value}"
      shift 2
      ;;
    --crb-lanes)
      CRB_LANES="${2:?--crb-lanes requires a value}"
      shift 2
      ;;
    --crb-cpuset)
      CRB_CPUSET="${2:?--crb-cpuset requires a value}"
      shift 2
      ;;
    --crb-extra-args)
      CRB_EXTRA_ARGS="${2:?--crb-extra-args requires a value}"
      shift 2
      ;;
    --)
      shift
      pass_args+=("$@")
      break
      ;;
    *)
      pass_args+=("$1")
      shift
      ;;
  esac
done

pretty_logs_enabled() {
  [[ "${DOCKER_LOG_STYLE:-pretty}" != "raw" ]]
}

log_line() {
  local prefix="$1"
  shift
  if pretty_logs_enabled; then
    echo "[${prefix}] $*"
  else
    echo "$*"
  fi
}

detect_cpu_model() {
  grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//' || true
}

select_crb_binary() {
  local requested="${CRB_BINARY:-auto}"
  local cpu_model="${CPU_MODEL_OVERRIDE:-$(detect_cpu_model)}"
  local selected="/opt/bitcoinandy-crb-miner/nmminer"
  local profile="generic build"

  case "${requested}" in
    auto|"")
      case "${cpu_model}" in
        *"7950X3D"*)
          selected="/opt/bitcoinandy-crb-miner/optimized-binaries/7950x3d/nmminer.premask.lto"
          profile="7950X3D premask LTO"
          export NM_L3_DATASETS="${NM_L3_DATASETS:-1}"
          CRB_THREADS="${CRB_THREADS:-32}"
          if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
          ;;
        *"7950X"*)
          selected="/opt/bitcoinandy-crb-miner/optimized-binaries/7950x/nmminer.gcc12native-test"
          profile="7950X GCC12 native"
          CRB_THREADS="${CRB_THREADS:-28}"
          if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
          ;;
        *"9950X"*)
          selected="/opt/bitcoinandy-crb-miner/optimized-binaries/9950x/nmminer.gcc12lto-live"
          profile="9950X GCC12 LTO"
          CRB_THREADS="${CRB_THREADS:-28}"
          if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
          ;;
      esac
      ;;
    7950x3d|x3d|premask)
      selected="/opt/bitcoinandy-crb-miner/optimized-binaries/7950x3d/nmminer.premask.lto"
      profile="7950X3D premask LTO"
      export NM_L3_DATASETS="${NM_L3_DATASETS:-1}"
      CRB_THREADS="${CRB_THREADS:-32}"
      if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
      ;;
    7950x)
      selected="/opt/bitcoinandy-crb-miner/optimized-binaries/7950x/nmminer.gcc12native-test"
      profile="7950X GCC12 native"
      CRB_THREADS="${CRB_THREADS:-28}"
      if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
      ;;
    9950x|zen5)
      selected="/opt/bitcoinandy-crb-miner/optimized-binaries/9950x/nmminer.gcc12lto-live"
      profile="9950X GCC12 LTO"
      CRB_THREADS="${CRB_THREADS:-28}"
      if [[ -z "${CRB_LANES:-}" || "${CRB_LANES:-}" == "auto" ]]; then CRB_LANES="1"; fi
      ;;
    generic)
      selected="/opt/bitcoinandy-crb-miner/nmminer"
      profile="generic build"
      ;;
    /*)
      selected="${requested}"
      profile="custom ${requested}"
      ;;
    *)
      selected="/opt/bitcoinandy-crb-miner/${requested}"
      profile="custom ${requested}"
      ;;
  esac

  if [[ ! -x "${selected}" ]]; then
    echo "Selected CRB binary is missing or not executable: ${selected}" >&2
    exit 66
  fi

  CRB_SELECTED_BINARY="${selected}"
  CRB_SELECTED_PROFILE="${profile}"
  CRB_SELECTED_CPU_MODEL="${cpu_model}"
}

format_wildrig_log() {
  cat
}

format_crb_log() {
  if ! pretty_logs_enabled; then
    cat
    return
  fi

  sed -u 's/^/[CRB] /'
}

start_logged_child() {
  local formatter="$1"
  local pid_var="$2"
  local filter_pid_var="$3"
  local pipe_path
  shift 3

  pipe_path="/tmp/miner-log-${pid_var}-$$.fifo"
  rm -f "${pipe_path}"
  mkfifo "${pipe_path}"
  "${formatter}" < "${pipe_path}" &
  printf -v "${filter_pid_var}" '%s' "$!"
  "$@" > "${pipe_path}" 2>&1 &
  printf -v "${pid_var}" '%s' "$!"
}

run_wildrig() {
  local algo_arg pool_arg wallet_arg worker_arg password_arg user_template user_arg
  algo_arg="${WILDRIG_ALGO:?WILDRIG_ALGO is required}"
  pool_arg="${WILDRIG_POOL:?WILDRIG_POOL is required}"
  wallet_arg="${WILDRIG_WALLET:?WILDRIG_WALLET is required}"
  worker_arg="${WILDRIG_WORKER:?WILDRIG_WORKER is required}"
  password_arg="${WILDRIG_PASSWORD:-x}"
  user_template="${WILDRIG_USER_TEMPLATE:-wallet}"

  case "${user_template}" in
    "wallet"|"{wallet}"|"%WAL%")
      user_arg="${wallet_arg}"
      ;;
    "wallet.worker"|"{wallet}.{worker}"|"%WAL%.%WORKER_NAME%")
      user_arg="${wallet_arg}.${worker_arg}"
      ;;
    *)
      user_arg="${user_template}"
      user_arg="${user_arg//\{wallet\}/${wallet_arg}}"
      user_arg="${user_arg//\{worker\}/${worker_arg}}"
      user_arg="${user_arg//%WAL%/${wallet_arg}}"
      user_arg="${user_arg//%WORKER_NAME%/${worker_arg}}"
      ;;
  esac

  args=(
    --algo "${algo_arg}"
    --url "${pool_arg}"
    --user "${user_arg}"
    --pass "${password_arg}"
    --print-time "${WILDRIG_PRINT_TIME:-30}"
    --no-color
  )

  if [[ -n "${WILDRIG_LOG_FILE:-}" ]]; then
    args+=(--log-file "${WILDRIG_LOG_FILE}")
  fi

  if [[ -n "${WILDRIG_OPENCL_PLATFORMS:-}" ]]; then
    args+=(--opencl-platforms "${WILDRIG_OPENCL_PLATFORMS}")
  fi

  if [[ "${WILDRIG_GPU_LIST:-auto}" != "auto" && -n "${WILDRIG_GPU_LIST:-}" ]]; then
    args+=(--gpu-list "${WILDRIG_GPU_LIST}")
  fi

  if [[ -n "${WILDRIG_EXTRA_ARGS:-}" ]]; then
    # Deliberately shell-split operator-supplied extra args, matching miner CLI usage.
    # shellcheck disable=SC2206
    extra=( ${WILDRIG_EXTRA_ARGS} )
    args+=("${extra[@]}")
  fi

  if [[ "${RUN_AS_CHILD:-0}" == "1" ]]; then
    log_line PEARL "Starting WildRig: /opt/wildrig/wildrig-multi ${args[*]} ${pass_args[*]}"
    wildrig_cmd=(/opt/wildrig/wildrig-multi)
    start_logged_child format_wildrig_log WILDRIG_PID WILDRIG_LOG_PID \
      "${wildrig_cmd[@]}" "${args[@]}" "${pass_args[@]}"
  else
    log_line PEARL "Starting WildRig: /opt/wildrig/wildrig-multi ${args[*]} ${pass_args[*]}"
    wildrig_cmd=(/opt/wildrig/wildrig-multi)
    start_logged_child format_wildrig_log WILDRIG_PID WILDRIG_LOG_PID \
      "${wildrig_cmd[@]}" "${args[@]}" "${pass_args[@]}"
    set +e
    wait "${WILDRIG_PID}" 2>/dev/null
    local status=$?
    set -e
    wait "${WILDRIG_LOG_PID}" 2>/dev/null || true
    exit "${status}"
  fi
}

run_crb() {
  args=()
  local login_arg="${CRB_WALLET:?CRB_WALLET is required}"
  select_crb_binary

  if [[ -n "${CRB_WORKER:-}" && "${CRB_WALLET}" != *.* ]]; then
    login_arg="${CRB_WALLET}.${CRB_WORKER}"
  fi

  args+=(-o "${CRB_POOL:?CRB_POOL is required}")
  args+=(-u "${login_arg}")
  args+=(-p "${CRB_PASSWORD:-x}")

  if [[ -n "${CRB_THREADS:-}" ]]; then
    args+=(-threads "${CRB_THREADS}")
  fi

  if [[ -n "${CRB_LANES:-}" ]]; then
    args+=(-lanes "${CRB_LANES}")
  fi

  if [[ -n "${CRB_EXTRA_ARGS:-}" ]]; then
    # Deliberately shell-split operator-supplied extra args, matching miner CLI usage.
    # shellcheck disable=SC2206
    extra=( ${CRB_EXTRA_ARGS} )
    args+=("${extra[@]}")
  fi

  crb_cmd=("${CRB_SELECTED_BINARY}")
  if command -v stdbuf >/dev/null 2>&1; then
    crb_cmd=(stdbuf -oL -eL "${crb_cmd[@]}")
  fi

  if [[ "${CRB_CPUSET:-auto}" == "auto" && -n "${CRB_THREADS:-}" ]]; then
    gpu_idx="${VAST_DEVICE_IDXS:-${NVIDIA_VISIBLE_DEVICES:-}}"
    gpu_idx="${gpu_idx%%,*}"
    if [[ "${gpu_idx}" =~ ^[0-9]+$ && "${CRB_THREADS}" =~ ^[0-9]+$ && "${CRB_THREADS}" -gt 0 ]]; then
      cpu_start=$((gpu_idx * CRB_THREADS))
      cpu_end=$((cpu_start + CRB_THREADS - 1))
      ncpu="$(nproc 2>/dev/null || echo 0)"
      if [[ "${ncpu}" =~ ^[0-9]+$ && "${ncpu}" -gt 0 && "${cpu_start}" -lt "${ncpu}" ]]; then
        if [[ "${cpu_end}" -ge "${ncpu}" ]]; then
          cpu_end=$((ncpu - 1))
        fi
        CRB_CPUSET="${cpu_start}-${cpu_end}"
      fi
    fi
  fi
  if [[ -n "${CRB_CPUSET:-}" && "${CRB_CPUSET}" != "auto" ]]; then
    log_line CRB "Binding CRB nmminer to CPU set ${CRB_CPUSET}"
    crb_cmd=(taskset -c "${CRB_CPUSET}" "${crb_cmd[@]}")
  fi

  if [[ "${RUN_AS_CHILD:-0}" == "1" ]]; then
    log_line CRB "CPU model: ${CRB_SELECTED_CPU_MODEL:-unknown}"
    log_line CRB "Selected optimized profile: ${CRB_SELECTED_PROFILE}"
    log_line CRB "Starting BitcoinAndy CRB nmminer: ${crb_cmd[*]} ${args[*]} ${pass_args[*]}"
    start_logged_child format_crb_log CRB_PID CRB_LOG_PID \
      "${crb_cmd[@]}" "${args[@]}" "${pass_args[@]}"
  else
    log_line CRB "CPU model: ${CRB_SELECTED_CPU_MODEL:-unknown}"
    log_line CRB "Selected optimized profile: ${CRB_SELECTED_PROFILE}"
    log_line CRB "Starting BitcoinAndy CRB nmminer: ${crb_cmd[*]} ${args[*]} ${pass_args[*]}"
    start_logged_child format_crb_log CRB_PID CRB_LOG_PID \
      "${crb_cmd[@]}" "${args[@]}" "${pass_args[@]}"
    set +e
    wait "${CRB_PID}" 2>/dev/null
    local status=$?
    set -e
    wait "${CRB_LOG_PID}" 2>/dev/null || true
    exit "${status}"
  fi
}

run_both() {
  if [[ -z "${CRB_POOL:-}" || -z "${CRB_WALLET:-}" ]]; then
    echo "MINER=both requires CRB_POOL and CRB_WALLET, or --crb-pool and --crb-wallet." >&2
    exit 64
  fi

  log_line VAST "Starting WildRig + BitcoinAndy CRB nmminer together..."
  RUN_AS_CHILD=1 run_wildrig
  local wildrig_pid="${WILDRIG_PID}"
  RUN_AS_CHILD=1 run_crb
  local crb_pid="${CRB_PID}"

  terminate_children() {
    trap - TERM INT
    kill -TERM "${wildrig_pid}" "${crb_pid}" 2>/dev/null || true
    kill -TERM "${WILDRIG_LOG_PID:-}" "${CRB_LOG_PID:-}" 2>/dev/null || true
    wait "${wildrig_pid}" "${crb_pid}" 2>/dev/null || true
  }

  trap terminate_children TERM INT

  set +e
  wait -n "${wildrig_pid}" "${crb_pid}"
  local status=$?
  set -e

  log_line VAST "One miner exited; stopping the paired miner so Vast restarts a clean container." >&2
  terminate_children
  exit "${status}"
}

case "${miner}" in
  wildrig)
    run_wildrig
    ;;
  crb|srbminer|xmrig)
    run_crb
    ;;
  both)
    run_both
    ;;
  *)
    echo "Unsupported MINER='${miner}'. Use 'wildrig', 'crb', 'srbminer', 'xmrig', or 'both'." >&2
    exit 64
    ;;
esac
