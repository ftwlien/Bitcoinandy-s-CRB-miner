# Bitcoinandy's CRB Miner Baseline

This repository starts from the protected CRB miner state frozen on
2026-06-26 19:56 UTC before deeper source surgery.

Protected live lanes:

- `3060mrig1` / Ryzen 9 7950X3D:
  `nmminer.premask.lto`, `NM_L3_DATASETS=1`, `32` threads, `lanes=1`,
  host CPU mode `amd-pstate guided`, verified around `126-127 kH/s`,
  accepted shares, `0` rejects.
- `maskin7950xrig1` / Ryzen 9 7950X:
  `nmminer.gcc12native-test`, `28` threads, `lanes=1`,
  verified around `90.5-90.8 kH/s`, accepted shares, `0` rejects.
- `maskin9950xrig3` / Ryzen 9 9950X:
  `nmminer.gcc12lto-live`, `28` threads, `lanes=1`,
  verified around `91-92 kH/s`, accepted shares, `0` rejects.

Rule for future optimization: every faster result is guilty until it proves
accepted live pool shares with `0` rejects on the relevant CPU class.

The exact protected snapshot is stored under:

`protected-baselines/bitcoinandys-crb-miner-protected-baseline-20260626T1956Z/`

That folder includes the manifest and exact per-rig work trees/binaries used
for the three protected lanes.

The currently promoted optimized binaries are also copied to
`optimized-binaries/` for direct install and Docker packaging:

- `optimized-binaries/7950x3d/nmminer.premask.lto`
- `optimized-binaries/7950x/nmminer.gcc12native-test`
- `optimized-binaries/9950x/nmminer.gcc12lto-live`

Use `scripts/select-optimized-nmminer.sh` on a target host to install the
matching full-speed binary for the detected CPU model.

## 7950X3D Host Tuning Fix

On 2026-06-26 the protected 7950X3D lane on `3060mrig1` dropped from the
validated `126-127 kH/s` window to about `118-120 kH/s` while still running the
same `nmminer.premask.lto` binary, `NM_L3_DATASETS=1`, `32` threads, and
`lanes=1`. The miner was clean and shares were accepted with `0` rejects; the
regression was the host CPU runtime mode.

Switching `amd-pstate` from `active` to `guided`, then keeping performance
governors/EPP and CPU boost enabled, immediately restored the live CRB window:
`126.3`, `126.9`, `127.3`, `127.1`, and `126.1 kH/s`, accepted shares, `0`
rejects.

The persistent fix is included under `scripts/`:

```bash
sudo scripts/install-bitcoinandy-crb-cpu-performance.sh
```

Installed files:

- `/usr/local/sbin/bitcoinandy-crb-cpu-performance.sh`
- `/etc/systemd/system/bitcoinandy-crb-cpu-performance.service`

Verify after boot or service restart:

```bash
cat /sys/devices/system/cpu/amd_pstate/status
systemctl is-enabled bitcoinandy-crb-cpu-performance.service
systemctl is-active bitcoinandy-crb-cpu-performance.service
```

Expected status on the protected 7950X3D lane is `guided`, `enabled`, and
`active`.
