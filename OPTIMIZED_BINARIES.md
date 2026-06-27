# Optimized Binary Lanes

This repo includes the current protected full-speed CRB binaries so Docker and
fresh installs do not accidentally fall back to the generic `./nmminer` build.

Validated lanes:

| CPU | binary | runtime |
|---|---|---|
| Ryzen 9 7950X3D | `optimized-binaries/7950x3d/nmminer.premask.lto` | `NM_L3_DATASETS=1`, `32` threads, `lanes=1` |
| Ryzen 9 7950X | `optimized-binaries/7950x/nmminer.gcc12native-test` | `28` threads, `lanes=1` |
| Ryzen 9 9950X | `optimized-binaries/9950x/nmminer.gcc12lto-live` | `28` threads, `lanes=1` |

Known hashes:

```text
8454d55a29f89dad161b7d71857fca01187f7e4f755b4ad1089eaf861788aa48  optimized-binaries/7950x3d/nmminer.premask.lto
b6ba266818a4781a007f9c13093c7ce2e9b74d23d7e991608b7692435232b2b4  optimized-binaries/7950x/nmminer.gcc12native-test
c0eb31b96125c103a86bec08a9e443016b71b21a6001ed1e83f202eed4ade2c8  optimized-binaries/9950x/nmminer.gcc12lto-live
c60a17d2df24e8f51453ad9ad3f1d1512466ddd8f48e809934c2509575925cda  optimized-binaries/nmminer-overlay
```

Use `scripts/select-optimized-nmminer.sh` on a target host to install the best
known binary for the detected CPU model.
