# Bitcoinandy's CRB Miner Baseline

This repository starts from the protected CRB miner state frozen on
2026-06-26 19:56 UTC before deeper source surgery.

Protected live lanes:

- `3060mrig1` / Ryzen 9 7950X3D:
  `nmminer.premask.lto`, `NM_L3_DATASETS=1`, `32` threads, `lanes=1`,
  verified around `126-127 kH/s`, accepted shares, `0` rejects.
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
