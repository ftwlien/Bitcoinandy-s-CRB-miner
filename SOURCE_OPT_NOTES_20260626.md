# Source Optimization Notes - 2026-06-26

Work tree only. Do not copy this over the live miner without a fresh bench and
live pool validation.

## Tested candidates

- Lazy branch-counter reset:
  - Replaced per-loop `memset(taken)` with generation stamps.
  - Correctness passed on all three rigs.
  - Bench result was mixed/noisy: flat on 7950X3D, worse on 7950X, tiny/noisy on
    9950X.
  - Left behind as compile-time option `-DNM_LAZY_BRANCH_RESET=1`; default is off.

- VM scratch word indexing:
  - Changed LOAD/STORE/AESR/loop-fold address math from byte mask then shift to
    direct word-index masking.
  - Correctness passed on all three rigs.
  - Bench result was mixed: sometimes small positive, sometimes noise. Not
    promoted to live.

- Single-chain dataset prefetch:
  - Added compile-time option `-DNM_PREFETCH_SINGLE_WALK=1`.
  - Correctness passed.
  - Bench result was mixed and mostly worse except one noisy 9950X run. Default
    remains off.

- IEEE float fast-path cleanup:
  - Replaced `fabs()` with direct sign-bit masking and replaced
    `isnan()/isinf()/v==0.0` with direct IEEE exponent/zero bit checks.
  - Correctness passed on all three rigs after the gateway resume:
    `VECTOR match YES` and `batch==single YES`.
  - Live validation from the separate source-opt work dir reached clean accepted
    shares with `0` rejected on all three rigs:
    - 7950X3D: `./nmminer.float.lto`, 32 threads, `NM_L3_DATASETS=1`,
      about `113.3 kH/s` at 2m17s, 67 accepted, 0 rejected.
    - 7950X: work-copy `./nmminer`, 28 threads, about `88.4 kH/s` at 2m18s,
      74 accepted, 0 rejected.
    - 9950X: work-copy `./nmminer`, 28 threads, about `89.6-90.5 kH/s`
      at 2m18s, 71 accepted, 0 rejected.
  - The 7950X3D gain is real-looking and worth keeping live for validation.
    7950X is a small live lift. 9950X is basically flat but clean.

- Inline AESR candidate:
  - Loaded AES round keys once per VM loop and inlined the AESR opcode path.
  - Correctness passed on all three rigs.
  - Rejected after fair old-vs-new benches: 7950X3D got worse
    (`83.1 -> 81.0 kH/s` offline), 7950X was flat (`60.06 -> 60.00 kH/s`),
    and 9950X was flat (`56.66 -> 56.65 kH/s`).
  - Left as compile-time option `-DNM_VM_AESR_INLINE=1`; default is off.

- Pre-masked VM operands:
  - Masked `dst/src` once in `gen_program()` and removed `&15` from the VM
    dispatch path.
  - Correctness passed on all three rigs.
  - The early offline bench made this look bad on 7950X3D
    (`85.6 -> 80.6 kH/s`) and slightly worse on 7950X
    (`60.26 -> 59.72 kH/s`), but that was the wrong call for the live X3D
    path.
  - Correction after Andy caught the regression: the 17:32 live hardcopy on
    `3060mrig1` showed the actual near-best X3D state was
    `nmminer.premask.lto`, `NM_L3_DATASETS=1`, `32` threads:
    `116.3-117.4 kH/s`, accepted shares, `0` rejects. Restarting the later
    demoted `float.lto` path only recovered about `102.5 kH/s`, while
    restarting `nmminer.premask.lto` on 2026-06-26 19:21 UTC recovered and
    improved to about `125.3-126.9 kH/s`, `145` accepted, `0` rejected after
    4m37s.
  - 9950X repeated as a small win: `premask 56.89`, `old 56.74`,
    `premask 57.21 kH/s` over 30s runs. Live validation recovered to about
    `92.1 kH/s`, with accepted shares and `0` rejected after about 5 minutes.
  - Treat premask as live-good for 7950X3D and 9950X unless a longer
    validation disproves it. Do not demote the 7950X3D lane based only on the
    synthetic offline bench.

- Branch-back precompute:
  - Tried storing the `CBRANCH` back distance in `nm_instr` during
    `gen_program()` to avoid `imm % 31` inside the VM dispatch.
  - Correctness passed on 9950X (`VECTOR match YES`, `batch==single YES`).
  - Rejected as noise/flat on 9950X: old premask `56.721` / `56.866 kH/s`,
    branch-back `56.764` / `56.849 kH/s`.
  - The source was reverted to the premask baseline after testing.

- Branch-back plus LTO:
  - Correctness passed on 9950X.
  - Also rejected as too close to call: old premask `56.729` then
    `57.182 kH/s`, branch-back LTO `56.900` / `57.199 kH/s`.
  - Not promoted to live validation.

- 9950X thread and CCD split recheck:
  - Current premask binary still likes `28` threads best in short offline
    checks: `24=54.080`, `26=55.859`, `27=55.987`, `28=56.589`,
    `29=56.525`, `30=55.936`, `32=51.353 kH/s`.
  - Two concurrent CCD-pinned 14-thread miners were worse:
    `28.595 + 27.486 = 56.081 kH/s`.
  - Keep 9950X live validation as one miner, `28` threads, `lanes=1`.

- Multi-CPU optimizer pass after Andy corrected scope:
  - Re-expanded the optimizer matrix to all three CPU classes:
    `3060mrig1` / 7950X3D, `maskin7950xrig1` / 7950X, and
    `maskin9950xrig3` / 9950X.
  - Clean baselines with `minertest` stopped and Vast GPU-only Docker still
    running low CPU:
    - 7950X3D: `nmminer.float-base.lto`, 32 threads, `82.205 kH/s`.
    - 7950X: `nmminer.float-base`, 28 threads, `60.643 kH/s`.
    - 9950X: `nmminer.gcc12lto-live`, 28 threads, `57.944 kH/s`.
  - GCC 12/LTO:
    - 7950X3D: clear win, `84.524 kH/s` without the L3 env and up to
      `90.763 kH/s` in a 25s forced-VAES512 check with `NM_L3_DATASETS=1`.
    - 7950X: flat with LTO, but GCC 12 non-LTO won: `61.317 kH/s`.
    - 9950X: current GCC 12/LTO candidate stayed around `57.8 kH/s`; other
      GCC 12 variants and premask did not beat it cleanly.
  - Direct `gen_program()` rewrite:
    - Already present in the 9950X source.
    - Loses/flat on 7950X (`60.679 kH/s` vs GCC12 native `61.317 kH/s`).
    - Worth validating on 7950X3D with GCC12/LTO; correctness passed and 35s
      check hit `84.985 kH/s`, while a 25s default VAES512 check hit
      `90.763 kH/s`. Left 7950X3D live on this directgen binary for pool
      validation.
  - VAES fill override check:
    - Keep default VAES-512. Forced VAES-256 and AES-NI were slower on all
      three CPUs:
      - 7950X3D directgen/GCC12/LTO: VAES512 `90.763`, VAES256 `72.857`,
        AES-NI `59.177 kH/s`.
      - 7950X GCC12 native: VAES512 `61.430`, VAES256 `53.616`,
        AES-NI `48.271 kH/s`.
      - 9950X GCC12/LTO: VAES512 `57.252`, VAES256 `50.276`,
        AES-NI `45.136 kH/s`.
  - Thread sweeps on current best binaries:
    - 7950X: `28` remains best (`26=59.443`, `27=60.390`,
      `28=61.312`, `29=60.851`, `30=60.160`, `31=58.278`,
      `32=56.128 kH/s`).
    - 9950X: `28` remains best (`26=56.985`, `27=57.113`,
      `28=57.856`, `29=57.216`, `30=56.614`, `31=53.734`,
      `32=52.595 kH/s`).
    - 7950X3D: 31-vs-32 was noisy on the later directgen/GCC12 path
      (`31=90.514/81.774`, `32=83.441/86.286 kH/s`), but that path was later
      proven worse live. Keep the live X3D lane on `nmminer.premask.lto`,
      `NM_L3_DATASETS=1`, `32` threads.
  - Current live validation commands after this pass:
    - 7950X3D correction: `NMMINER_BIN=./nmminer.premask.lto`,
      `NM_L3_DATASETS=1`, `-threads 32 -lanes 1`.
    - 7950X: `NMMINER_BIN=./nmminer.gcc12native-test`,
      `-threads 28 -lanes 1`.
    - 9950X: `NMMINER_BIN=./nmminer.gcc12lto-live`,
      `-threads 28 -lanes 1`.

- Follow-up protected-baseline checks after Andy asked to keep optimizing every
  CPU class:
  - `3060mrig1` / 7950X3D:
    - Current live-good command remains `nmminer.premask.lto`,
      `NM_L3_DATASETS=1`, `32` threads, `lanes=1`.
    - Offline 32-thread premask bench was better than 31/30 threads:
      `32=87.232 kH/s`, `31=81.962 kH/s`, `30=81.642 kH/s` in the short
      12s check. Do not switch back to 31.
    - `lanes=2` is bad: `86.169 kH/s` at `lanes=1` versus
      `51.865 kH/s` at `lanes=2`.
    - Runtime inspection found no competing CPU miner, governor `performance`,
      all logical CPUs around `4.54-4.62 GHz`, and worker LWPs pinned across
      `0-31`.
    - A slow post-restart live window sat around `113-115 kH/s`, but a live
      no-L3 A/B then hit `126.430 kH/s` and restoring the L3 baseline
      immediately hit `126.276` then `127.386 kH/s`, with accepted shares and
      `0` rejects. Treat the slow first window as runtime noise, not a reason
      to demote the X3D lane.
  - `maskin7950xrig1` / 7950X:
    - Current live-good command remains `nmminer.gcc12native-test`,
      `28` threads, `lanes=1`, one dataset.
    - Short offline checks: current `60.992 kH/s`, L3 remap
      `60.650 kH/s`, `-nonuma` `61.162 kH/s`, premask `59.648 kH/s`,
      27 threads `60.364 kH/s`, 29 threads `60.979 kH/s`.
    - `lanes=2` is bad: `61.491 kH/s` at `lanes=1` versus
      `27.136 kH/s` at `lanes=2`.
    - Final live check after restore: about `90.607 kH/s`, accepted shares,
      `0` rejects.
  - `maskin9950xrig3` / 9950X:
    - Current live-good command remains `nmminer.gcc12lto-live`,
      `28` threads, `lanes=1`, one dataset.
    - Short offline checks: current `57.856 kH/s`, L3 remap
      `56.938 kH/s`, `-nonuma` `55.893 kH/s`, premask `56.960 kH/s`,
      GCC12 native `57.429 kH/s`, 27 threads `57.420 kH/s`,
      29 threads `57.523 kH/s`.
    - `lanes=2` is bad: `57.702 kH/s` at `lanes=1` versus
      `23.936 kH/s` at `lanes=2`.
    - Final live check after restore: about `92.023 kH/s`, accepted shares,
      `0` rejects.

- Deeper source/codegen pass after the protected baseline was published:
  - GCC12 PGO/LTO on 9950X was rejected. Plain GCC12/LTO was flat
    (`57.6 -> 57.7 kH/s` short offline), while PGO/LTO dropped to about
    `55.5 kH/s`.
  - 32-bit dataset-index source variant was correctness-clean on 9950X
    (`VECTOR match YES`, `batch==single YES`) but slower:
    old/live `57.920 kH/s`, clean source `57.776 kH/s`, idx32
    `57.609` then `57.328 kH/s`.
  - 9950X codegen sweep had noisy short-run temptations, but none survived a
    longer A/B. Best-looking `-frename-registers` checked as old
    `57.824` / `57.269 kH/s` versus candidate `57.461` / `57.344 kH/s`;
    correct, not promoted.
  - 7950X codegen sweep found a tiny `-fno-tree-vectorize` candidate that was
    correctness-clean and slightly ahead offline: old `61.376` / `60.981 kH/s`
    versus candidate `61.450` / `61.312 kH/s`. Live pool validation rejected
    it anyway: it settled around `87-88 kH/s`, below the protected
    `90.5-90.8 kH/s` lane, with accepted shares and `0` rejects.
  - 7950X3D codegen sweep found an alignment-only candidate
    (`-falign-functions=64 -falign-loops=64 -falign-jumps=64`) that looked
    interesting in short offline checks (`91.871 kH/s` versus old
    `87.007 kH/s`), but longer checks were noisy and live pool validation
    rejected it. It settled around `120-121 kH/s`, below the protected
    `126-127 kH/s` lane, with accepted shares and `0` rejects.
  - Combination flags on 7950X3D (`align64 + -frename-registers`) were worse:
    old `88.746 kH/s`, combo `85.006` / `83.882 kH/s`.
  - Result: no deeper candidate from this pass beat the protected live lanes.
    All three live rigs were restored to their protected binaries.

## Current conclusion

The hot profile is dominated by the dependent 64 MiB dataset walk:

- 7950X3D: about 58% dataset, 26% VM, 14% setup/fill.
- 7950X: about 57% dataset, 22% VM, 16% setup/fill.
- 9950X: about 61% dataset, 27% VM, 10% setup/fill.

After the float cleanup, the live `minertest` screens were intentionally left
running from the separate source-opt work directories for validation. Later,
the 7950X3D was mistakenly demoted to a directgen/GCC12/31-thread validation
path after noisy offline checks; Andy caught the regression from roughly
`116-117 kH/s` to about `104-105 kH/s`. The corrected 7950X3D live state is
`nmminer.premask.lto`, `NM_L3_DATASETS=1`, `32` threads, verified at about
`126 kH/s` with accepted shares and `0` rejects. The 7950X and 9950X lane
improvements were not reverted. Additional 9950X branch-back, LTO,
thread-count, and CCD-split checks did not beat the premask/GCC12 candidate
there. The preserved live miner dirs and timestamped backups were not
overwritten.
