# Bitcoinandy's CRB Miner - Protected Baseline

Created: 2026-06-26 19:56 UTC

This snapshot freezes the current proven CRB miner state before deeper source
surgery. Do not overwrite or demote these lanes unless a replacement proves
accepted live pool shares with `0` rejects on the relevant CPU class.

Archive:

- `artifacts/crb-miner-fork/bitcoinandys-crb-miner-protected-baseline-20260626T1956Z.tgz`
- Final SHA256 is stored beside the archive in
  `bitcoinandys-crb-miner-protected-baseline-20260626T1956Z.tgz.sha256`.

Protected live lanes:

- `3060mrig1` / Ryzen 9 7950X3D:
  `NMMINER_BIN=./nmminer.premask.lto NM_L3_DATASETS=1 ./nmminer-overlay ... -threads 32 -lanes 1 -noauto`
  Verified live around `126-127 kH/s`, accepted shares, `0` rejects.
- `maskin7950xrig1` / Ryzen 9 7950X:
  `NMMINER_BIN=./nmminer.gcc12native-test ./nmminer-overlay ... -threads 28 -lanes 1 -noauto`
  Verified live around `90.5-90.8 kH/s`, accepted shares, `0` rejects.
- `maskin9950xrig3` / Ryzen 9 9950X:
  `NMMINER_BIN=./nmminer.gcc12lto-live ./nmminer-overlay ... -threads 28 -lanes 1 -noauto`
  Verified live around `91-92 kH/s`, accepted shares, `0` rejects.

Important rules:

- Treat every faster result as guilty until it proves accepted shares with
  `0` rejects on the live pool.
- Keep `7950X3D` on `premask.lto` + `NM_L3_DATASETS=1` + `32` threads unless
  a new X3D-specific lane beats it live. Do not demote it from one slow
  restart window.
- Do not move the `7950X` and `9950X` lanes backward while experimenting on
  the `7950X3D`, or vice versa.
- Do source-level experiments in a separate work tree first; promote only after
  per-CPU validation.
- No GitHub/Docker publishing was done for this snapshot.

Contents:

- `local-source/`: local source-opt source snapshot and notes.
- `rig-binaries/3060mrig1-7950x3d/`: exact remote work tree and binaries from
  the winning 7950X3D lane.
- `rig-binaries/maskin7950xrig1-7950x/`: exact remote work tree and binaries
  from the winning 7950X lane.
- `rig-binaries/maskin9950xrig3-9950x/`: exact remote work tree and binaries
  from the winning 9950X lane.
