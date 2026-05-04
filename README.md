# GoldenRecomp — macOS port (work in progress)

Native macOS port of GoldenEye 007 (N64) using static recompilation via [N64ModernRuntime](https://github.com/Mr-Wiseguy/N64ModernRuntime) + [RT64](https://github.com/rt64/rt64).

> **This is a fork of [kholdfuzion/GoldenRecomp](https://github.com/kholdfuzion/GoldenRecomp).** All upstream credit goes to kholdfuzion and contributors. This fork focuses on getting the project running natively on **macOS (Apple Silicon)**, with a longer-term goal of also targeting **Android** once macOS is playable.

## Status

🚧 **Not yet playable.** The pipeline runs end-to-end (DLs reach RT64, triangles transform with sane screen coordinates, FBs publish), but rasterization is not yet producing visible scene geometry. Active blockers:

1. **F3D_Gold microcode handling** — GoldenEye uses Rare's custom F3D_Gold ucode. RT64 supports stock F3D and F3D_PD; F3D_Gold is the outlier. Bytes at game-emitted matrix addresses (e.g. `0x00264500`) decode to non-libultra-Mtx values, suggesting either a non-standard matrix layout or a w1 encoding we don't yet replicate.
2. **Game-loop animation stall** — bossMainloop waits for OSScMsg events; substitute-DL fix landed but full animation pacing still needs tuning.

The project is under active investigation; PRs and issues welcome.

## Differences from upstream

This fork carries cumulative changes across all submodules to enable macOS builds and progress past upstream's known blockers. Major submodules forked:

| Submodule | Origin | Fork |
|---|---|---|
| `lib/rt64` | [rt64/rt64](https://github.com/rt64/rt64) | [mgrz18/rt64@mac-port](https://github.com/mgrz18/rt64/tree/mac-port) |
| `lib/N64ModernRuntime` | [kholdfuzion/N64ModernRuntime](https://github.com/kholdfuzion/N64ModernRuntime) | [mgrz18/N64ModernRuntime@mac-port](https://github.com/mgrz18/N64ModernRuntime/tree/mac-port) |
| `…/N64Recomp` (nested) | [N64Recomp/N64Recomp](https://github.com/N64Recomp/N64Recomp) | [mgrz18/N64Recomp@mac-port](https://github.com/mgrz18/N64Recomp/tree/mac-port) |
| `…/hlslpp` (nested) | [redorav/hlslpp](https://github.com/redorav/hlslpp) | [mgrz18/hlslpp@mac-port](https://github.com/mgrz18/hlslpp/tree/mac-port) |

Notable changes carried in this fork:
- macOS-specific support (`src/main/support_apple.mm`, `.github/macos/` CI workflows)
- F3D_Gold ucode handlers in RT64 (`0xB1` G_TRIX, `0xBD` remap)
- Scheduler fix for game-loop stall (G_ENDDL substitution for bogus OSTasks)
- Framebuffer-manager bridge between `State` and `PresentQueue`
- Diagnostic env-var gates: `GE_DEEP_SHADOW`, `GE_REMAP_VTX`, `GE_LOCK_MATRICES`, `GE_NO_BD`, `GE_SHADOW_MATRICES`
- `PHYS()` macro in N64Recomp for sign-extended-MIPS-address handling
- Tolerance for unpaired HI16 relocs in the recompiler (required for GE 007's ELF)

## Roadmap

- [ ] Visible scene geometry (resolve F3D_Gold matrix decoding)
- [ ] Stable game-loop animation
- [ ] Audio
- [ ] Input mapping (modern dual-analog)
- [ ] Gun fire-rate fix (60 Hz native vs 30 Hz original timing)
- [ ] Skybox in DAM / Sky+Water in Frigate (custom RDP commands; same blocker upstream documented)
- [ ] Multiplayer UI
- [ ] **Android port** (planned once macOS is playable)

## Building (macOS)

> **Status:** the project builds but does not yet produce playable output.

Requirements:
- macOS 14+ on Apple Silicon
- CMake 3.20+, Ninja
- Xcode command line tools

```bash
git clone --recurse-submodules https://github.com/mgrz18/GoldenRecomp
cd GoldenRecomp
```

You also need:
- A TLB-free GoldenEye 007 ROM. Build from [kholdfuzion/goldeneye_src @ TBLFREE_NOCOMPRESSION](https://github.com/kholdfuzion/goldeneye_src/compare/TBLFREE_NOCOMPRESSION). Place `ge007.tlbfree.elf` and `ge007.tlbfree.z64` at the repo root.
- An N64Recomp build (the `mac-port` fork is required). Build from [mgrz18/N64Recomp@mac-port](https://github.com/mgrz18/N64Recomp/tree/mac-port).

Generate recompiled sources:

```bash
N64Recomp us.toml
N64Recomp us.toml --dump-context
RSPRecomp aspMain.us.toml
```

Configure and build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build GoldenRecomp
```

The signed `.app` bundle ends up at `build/GoldenRecomp.app`.

## Running (current state)

The binary launches and exercises the full pipeline but does not yet render visible game frames. Useful diagnostic combination:

```bash
GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1 \
  ./build/GoldenRecomp.app/Contents/MacOS/GoldenRecomp -level_10
```

This drops you into an instrumented run with the current best-known render config. Logs go to stderr.

## Background

GoldenEye 007 has historically been the hardest Rare N64 title to port natively because Rare wrote a custom RSP graphics microcode (`F3D_Gold`) for it that no public renderer fully understands. Banjo-Kazooie reverted to stock F3DEX so [BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp) works on the same RT64 + N64ModernRuntime stack out of the box. Perfect Dark used a different custom ucode (`F3D_PD`) which has a working PC port. GE has been the outlier — this project is one of the public attempts to bridge that gap.

## License

GPL-3.0 (inherited from upstream).

## Credits

- [kholdfuzion](https://github.com/kholdfuzion) — original GoldenRecomp project, decomp branch, N64Recomp modifications
- [Mr-Wiseguy](https://github.com/Mr-Wiseguy) — N64ModernRuntime, N64Recomp
- [rt64](https://github.com/rt64/rt64) — RT64 renderer
- [n64decomp](https://github.com/n64decomp) team — GoldenEye 007 decompilation reference
- theboy — earlier upstream contributions referenced in `patches/workbench_theboy.c`
