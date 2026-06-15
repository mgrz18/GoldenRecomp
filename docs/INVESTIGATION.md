# GoldenRecomp macOS port — investigation notes

Consolidated technical knowledge from the multi-session R&D effort. Read this before attacking any of the open blockers — most hypotheses have already been tested.

## ⚠️ UPDATE 2026-06-15 — Blocker #1 reframed: DONE delivery WORKS; the real gate is the gfx loop not self-sustaining

Multi-agent hypothesis attack + runtime-side instrumentation overturned the original Blocker #1 premise.

### Critical build gotcha — READ FIRST
- **`patches/*.c` do NOT compile in this build dir.** `SKIP_PATCHES:BOOL=ON` in `build/CMakeCache.txt`, and `llvm-objcopy` is missing (the MIPS patch toolchain is incomplete on this Mac), so the `patches/ → patches.elf → N64Recomp → RecompiledPatches/patches.c` pipeline is disabled; the build compiles the **pre-generated** `RecompiledPatches/patches.c`. **Editing `patches/workbench_theboy.c` (incl. `bossMainloop`) is a NO-OP** until you `brew install llvm` and reconfigure with `-DSKIP_PATCHES=OFF`. (An earlier "boss decrement" metric was invalid for this reason — the log never compiled.)
- `RecompiledFuncs/*.c` (e.g. `funcs_0.c`) and `lib/N64ModernRuntime/**` (`mesgqueue.cpp`, `events.cpp`) **do** compile directly — instrument/fix there.
- The harness `dones` metric (`grep -c 'type=2'`) only counts `funcs_0.c:6959`, **capped at ≤5** — useless as a success signal. Use the runtime `do_recv` counter below.

### Reliable runtime-side metric (compiles)
Instrument `do_recv` (mesgqueue.cpp) at the clientQ (`0x80141C90`) return point: count non-RETRACE (type≠1) messages the boss actually dequeues = DONEs truly delivered to `bossMainloop`. Pair with a `do_send`→clientQ type=2 log.

### Findings (GE_NO_INJECT_DONE=1, 60s, valid metric)
- **DONE delivery is NOT broken.** `__scTaskComplete` generates type=2 DONEs, `do_send` enqueues them to clientQ, and `do_recv` returns them to the boss with **type=2 intact** (not overwritten, not starved). The boss loops ~5500×/60s draining retraces — it is NOT stalled on recv.
- BUT only **~3 gfx tasks are submitted and ~2 real DONEs generated** in 60s, then submission stops. The **default injection band-aid** (GE_NO_INJECT_DONE unset) is the ONLY thing limping the game to the documented ~40-50% content — it papers over the fact that the **natural gfx submission loop does not self-sustain past boot**.
- Hyp4 (`funcs_0.c:6928` mask `0x3→0x2`, "forward DONE on RSP bit alone") tested with the valid metric: marginal (1→2 DONEs), **NOT the fix**. Reverted.

### Reframed root cause
Blocker #1 is NOT "DONEs don't reach the boss." It is: **bossMainloop only submits gfx in the `g_MainStageNum < 0 && pendingGfx < 2` boot path (`workbench_theboy.c:644`); once boot ends / the level should load, submission doesn't continue.** Reaching `dones>=100` requires the game to ADVANCE past boot into continuous scene rendering — overlapping Blockers #2/#3, not a scheduler/queue fix.

### Next step
`brew install llvm` + reconfigure a **separate** build dir with `-DSKIP_PATCHES=OFF` (keep the known-good build intact), then instrument `g_MainStageNum` + `pendingGfx` over time in `bossMainloop` to pinpoint why submission stops at the boot→level transition.

## Pipeline overview

```
Game CPU code (recompiled)
    │
    │ submit_rsp_task(OSTask*)
    ▼
ultramodern/events.cpp  ───── shadow-snapshots DL bytes + sub-DL targets
    │                          (deep_shadow heuristic, see lib/N64ModernRuntime/ultramodern/src/events.cpp)
    │ enqueue SpTaskAction
    ▼
gfx_thread (action_queue)
    │
    ▼
RT64 Interpreter (DL walker)  ─── lib/rt64/src/hle/rt64_interpreter.cpp
    │
    │ per-cmd dispatch via gbi->map[opCode]
    ▼
RSP::* / RDP::* handlers     ─── lib/rt64/src/hle/{rt64_rsp.cpp, rt64_rdp.cpp}
    │   (matrix, setVertex, drawIndexedTri, setTextureImage, loadBlock, …)
    ▼
fbPair (color/depth target + draw rect)
    │
    │ State::fullSync()
    ▼
copyRenderTargetToNative → RDRAM at colorFb->addressStart
    │
    ▼
update_screen() reads VI_ORIGIN
    │
    ▼
PPM dump @ /tmp/ge_fb_NNNN.ppm
```

This pipeline is **fully validated** — `GE_FORCE_FB_MARKER=1` writes a magenta block at `colorFb->addressStart` and that block appears in the PPM dump (see `docs/screenshots/fb-marker-validation.png`). The mystery is upstream: why aren't real triangles producing pixels?

## F3D_Gold microcode spec

Extracted from `n64decomp/007 rsp/graphics/gmain.s` (1545 lines RSP MIPS asm) via subagent disasm. Confirmed against `n64decomp/007/include/PR/gbi.h` C-side macros.

### Verified facts
- C-side uses **stock libultra `gbi.h`**. Decomp Makefile does NOT define `F3DEX_GBI` or `F3DEX_GBI_2`. Stock F3D opcode set: `G_MTX=0x01, G_DL=0x06, G_VTX=0x04, G_MOVEMEM=0x03, G_ENDDL=0xB8, G_SETTIMG=0xFD, G_SETTILE=0xF5, G_LOADBLOCK=0xF3, G_SETCIMG=0xFF`.
- All `gSPMatrix` calls wrap with `osVirtualToPhysical()` — matrices are plain physical RDRAM addresses (`/tmp/ge_decomp/src/fr.c:1696-1698`, `model.c:6486`).
- Matrices produced by `guMtxF2L` → standard libultra format (16× s16 int + 16× u16 frac, BE).
- All matrix calls use `G_MTX_NOPUSH | (LOAD|MUL) | (MODELVIEW|PROJECTION)`. **PUSH never used.** `gSPPopMatrix` never called.
- Vertices use **segment 4** (`SPSEGMENT_MODEL_VTX`), set per DL block (`/tmp/ge_decomp/src/game/model.c:6121,6146,6198`).
- Active segments per `/tmp/ge_decomp/src/bondconstants.h:2466-2478`: 0(NULL), 1(font), 2(GETITLE), 4(MODEL_VTX), 5/6(MODEL_COL), 13-15(BG).
- Microcode entry symbol: `gsp3DTextStart` (`/tmp/ge_decomp/src/game/rsp.c:208,245`).

### Ucode-side quirks (from gmain.s)
- `segmented_to_physical` (IMEM 0x10a8): masks with **DMEM-resident `mem[0xb8]`** before segment lookup. RT64 hardcodes `0x00FFFFF8`. If GE writes a different mask via MOVEWORD, addresses resolve wrong.
- Segment table at DMEM 0x160, indexed by `(addr>>22) & 0x3c` (== `((addr>>24)&0xf)*4` byte offset).
- **Opcode 0xBD** is allegedly a bitfield-MOVEWORD per agent disasm. **Tested: NEVER FIRES** in level 10 boot+intro. Hypothesis ruled out.
- G_TRIX (0xB1) packs ≤4 tris in 4-bit nibbles. RT64's impl uses raw nibbles (no remap table) — appears to work (20 visible tris reach drawTri).
- G_DL (0x06): segment byte from w0 top byte, not w1. Values 0xFD..0xFF bypass translation.
- Vertex DMEM stride 0x50 post-transform, base 0x420; ≤32 vertices.
- Matrix stack: 9 levels at DMEM 0x336.

### The "w1 low-bits" mystery (UNRESOLVED)
Observed pattern across matrix/vertex/texture calls:
- `seg=0x00264503` for matrix (low 3 bits = 0x3 = exactly the params byte 0x03)
- `seg=0x0026450B` for vertex (low 4 bits = 0xB)
- `seg=0x00264503` for texture (same)

Per gmain.s: F3D_Gold ucode is **tolerant** of low 3 bits in w1 — DMA hardware truncates them and ucode never reads them as flags. But in our run the bytes at the masked address (`0x00264500`) decode as a **DL stream** (opcodes E7, BA, B9, BB, FC, FD, F5), not as matrix/vertex/texture data.

Conclusion: the address resolution is NOT the bug. The bug is upstream — something is passing a non-Mtx pointer to gSPMatrix, similar for vtx/texture. Hypothesis: heap aliasing in `g_GfxMemPos` (the bump allocator in `/tmp/ge_decomp/src/game/dyn.c:85`) where DL data and matrix data share storage.

## Diagnostic env vars

| Var | Default | Effect |
|---|---|---|
| `GE_DEEP_SHADOW` | off | Snapshot DL+matrix+vertex+movemem references into shadow region at submit time. Required for stable boot. |
| `GE_REMAP_VTX` | off | Redirect vertex addresses through shadow snapshot. Required (without it, NaN vertex positions). |
| `GE_LOCK_MATRICES` | off | Drop all matrix loads except injected ortho/identity at 0x007F0000-0x007F01FF. Recommended; without it, NaN tri explosion. |
| `GE_ORTHO_SCALE=N` | 15 | Inverse-power-of-2 ortho scale: 1/2^N. Default 1/32768. Try 12-14 for wider scale. |
| `GE_FORCE_SHADE` | off | Force `geometryMode |= G_SHADE`. Required for vertex colors to propagate to combiner. |
| `GE_RAW_VTX_COLOR` | off | Force `lightCount=0` so compute shader uses raw vertex rgba (bypass lights producing zero RGB). |
| `GE_DUMP_ALL` | off | Dump every FB to PPM (default: sparse 1/30). |
| `GE_FORCE_FB_MARKER` | off | Stamp magenta block at `colorFb->addressStart` AFTER copyNativeToRAM. Validates FB write path. |
| `GE_INJECT_PERSPECTIVE` | off | Replace ortho with real perspective. Currently produces NaN due to z=0 vertices → div-by-zero. |
| `GE_PERSP_SCALE_DIV=N`, `GE_PERSP_FOVY/_NEAR/_FAR` | 2048/60/10/30000 | Perspective tuning. |
| `GE_MV_TZ=N` | 0 | Inject `translate(0,0,-N)` into modelview. For perspective without div/0. |
| `GE_NO_BD` | off | Gate 0xBD opcode to no-op. Effectively does nothing (0xBD never fires in level 10 boot). |
| `GE_NO_MTX_FILTER` | off | Disable malformed-matrix detector. |
| `GE_SHADOW_MATRICES` | off | Restore old behavior of routing matrix addresses through shadow. |
| `GE_SHADOW_TEXTURES` | off | Re-enable shadow remap for textures (was causing stale-DL reads). |
| `GE_LOG_WALK` | off | Log every DL command dispatched. |

**Best-known visible-content combo:**
```
GE_FORCE_SHADE=1 GE_RAW_VTX_COLOR=1 GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1 GE_DUMP_ALL=1
```

## Hypothesis matrix (tested + ruled out)

| Hypothesis | Status | Evidence |
|---|---|---|
| RT64 dispatches 0x01 as G_MTX but it's actually G_DL in F3D_Gold | ❌ ruled out | Switching produced cycle detection + crash. Decomp Makefile uses stock F3D opcodes (G_MTX=1, G_DL=6). |
| F3D_Gold uses paletted Vtx layout like F3D_PD | ❌ ruled out | GE's `Vertex` struct in `bondtypes.h:711` is "Binary compatible with gbi Vtx" — stock libultra 16-byte layout. |
| Opcode 0xBD bitfield-MOVEWORD corrupts DMEM state | ❌ ruled out | Logging shows 0 invocations in level 10 boot+intro. |
| RT64 reads matrix from wrong offset (e.g. w1±64) | ❌ ruled out | Offset scanner found no affine matrix in ±192 bytes around target address. |
| GE_DEEP_SHADOW captures matrix bytes from stale snapshot | ❌ ruled out | Removing shadow remap for matrices: bytes still decode as DL stream, not Mtx. |
| Vertex colors zero kills alpha cascade | ❌ ruled out | Forcing `vertex.rgba = (255,255,255,255)` did not produce visible content. |
| Combiner produces zero output | ✅ confirmed | Decoded `0xFC121824FF33FFFF` = `RGB=TEXEL0², ALPHA=(SHADE-0)·TEXEL0+PRIM`. With shade=0 + missing texture + primAlpha=0, output is zero. |
| FB write path is broken | ❌ ruled out | `GE_FORCE_FB_MARKER` produces visible turquoise block in dumps. |
| Triangle rasterization is broken | ❌ ruled out | `FORCE_MAGENTA` early-return in pixel shader fills lower half of screen with magenta — full-screen triangle rasterization works. |
| Stall heal corrupts game memory at hardcoded 0x803B38B8 | ⚠️ inconclusive | gen.type stamping at that address sometimes makes things worse; address might not be stable. |

## Open blockers (priority order)

### 1. Stall fix non-deterministic
**Symptom:** bossMainloop receives 1 DONE per run sometimes, 152 DONEs other times. Same env vars, same code.

**What we know (UPDATED 2026-05-05 via orchestrator dogfood):**
- `__scTaskComplete` (`/tmp/ge_decomp/src/sched.c:406-433`) forwards `t->msg` to clientQ.
- **`cur_msg = 0x803B38B8` in 100% of observed GFX submissions** — the hardcoded fallback IS the right address. Agent A's earlier hypothesis (that real address is stack-local `&localGfxDoneMsg`) was wrong.
- `learn_count = 0` is misleading: the assign happens, but the "learned" log only fires when value CHANGES; since initial == observed, no log.
- `sp_complete` fires ~26 times per 60s run (audio + gfx); `dp_complete` fires ~3 times (gfx only).
- bossMainloop receives **20× type=1 (RETRACE), 0× type=2 (DONE)** — sp/dp complete fire correctly but DONEs don't reach clientQ.

**Real cause:** the recompiled game's `__scHandleSP → __scTaskComplete → osSendMesg(clientQ, doneMsg)` chain doesn't successfully forward DONEs even when sp/dp completion signals arrive. This is a **recompilation-level bug**, not a heal-message-corruption bug.

**Hypothesis 1 (FALSIFIED 2026-05-05):** Remove hardcoded fallback `g_known_done_msg_ptr = 0` → made it WORSE (dones=[1,3,1,0,2] vs baseline [5,3,2,2,1]). Reverted.

**Hypothesis 2 (PARTIAL 2026-05-05):** Bypass game's `__scTaskComplete` entirely — inject DONE msg directly into `clientQ` at `0x80141C90` with `DONE_MSG=0x803B38B8` after each gfx task. **Result: 2/8 wins (25%) vs 1/5 baseline (20%).** Marginal improvement, not deterministic. The injection succeeds (`osSendMesg` returns 0) but bossMainloop's NOBLOCK drain at `workbench_theboy.c:616` eats it before pendingGfx > 0. Code: `lib/N64ModernRuntime/ultramodern/src/events.cpp:368+`. Disable via `GE_NO_INJECT_DONE=1`.

**Try next (specialist-level):**
- Game-side `__scHandleSP` and `__scTaskComplete` recompiled versions need inspection — these are in `RecompiledFuncs/` (specifically the recompiled sched.c)
- `OS_EVENT_SP` registers msg=`0x29B` which is the SP-DONE message ID (not a pointer); verify recompiled `__scHandleSP` correctly handles this case
- Trace why DONE msgs aren't being osSendMesg'd to clientQ from the recompiled binary
- Possible: spawn a continuous "DONE heartbeat" thread that injects every 16ms (VI rate), but won't help if bossMainloop never reaches the pendingGfx>0 state

This requires N64Recomp / N64 OS scheduler expertise.

### 2. Scene triangles not reaching pixel shader
**Symptom:** When game advances frames (DONEs > 100), the rainbow-test pixel shader (which colors each fragment by screen position) produces ZERO visible output. But `FORCE_MAGENTA` does work in earlier-stage runs.

**Hypothesis:** Stall-recovered frames render only fillrects (boot transitions), not scene triangles. The actual game's `bondviewMain → bgLevelRender → model_render` chain hasn't been reached yet — game is still in some prelevel state.

**Try next:**
- Force game to skip to a level (look at `/tmp/ge_decomp/src/main.c` for boot fast-path arguments)
- Confirm rainbow test in a config that DOES produce scene tris (need to find that config)
- Add per-tri log of `renderFlagRect(rp.flags)` to count rect-vs-tri pixel shader invocations

### 3. F3D_Gold w1 encoding mystery
See "The 'w1 low-bits' mystery" section above.

**Try next:** Read `n64decomp/007 rsp/graphics/gmain.s` more carefully. Find the EXACT instruction that consumes w1 in matrix/vertex/texture handlers. Verify low-bits truly are masked vs extracted as flags.

### 4. Combiner producing alpha=0
**Symptom:** Combiner is `RGB=TEXEL0², ALPHA=(SHADE-0)·TEXEL0+PRIM`. With shade=0 + texture-not-loaded + primAlpha=0, output alpha=0 → coverage discard.

**Try next:**
- Find where game emits `gSPSetGeometryMode(G_SHADE)` and verify RT64 catches it
- Find where game emits `gDPSetPrimColor(...)` with non-zero alpha
- If neither fires in current run, the issue is upstream DL processing

### 5. Texture data corruption
**Symptom:** `loadBlock src=0x0060FFFB` (formerly), or `src=0x00264500` (post-mask-fix) — but bytes there are DL stream, not texture data.

Same root cause as matrix mystery — heap aliasing in `g_GfxMemPos`.

### 6. Real perspective without div/0
**Symptom:** `GE_INJECT_PERSPECTIVE=1` produces NaN tris because z=0 vertices → w_out=0 → division by zero.

**Try next:** `GE_MV_TZ=2000` injects translate(0,0,-2000) so all vertices have non-zero w. Then tune scale + near/far for sane NDC.

## Reference: visible-content frame examples

- `docs/screenshots/scene-magenta-l.png` — magenta L-shape + white rectangle (multi-color, possibly real boot transition)
- `docs/screenshots/scene-magenta-full.png` — uniform magenta (clear/transition)
- `docs/screenshots/force-magenta-validation.png` — `FORCE_MAGENTA` shader bypass (rasterizer proof)
- `docs/screenshots/fb-marker-validation.png` — turquoise marker (FB write path proof, color is byte-swapped magenta)

## Files that matter

| Component | File |
|---|---|
| Stall heal | `lib/N64ModernRuntime/ultramodern/src/events.cpp:568-700` |
| RDP texture handlers | `lib/rt64/src/hle/rt64_rdp.cpp` (setTile, loadBlock, loadTLUT) |
| RSP matrix/vertex/texture image | `lib/rt64/src/hle/rt64_rsp.cpp` |
| F3D_Gold opcode setup | `lib/rt64/src/gbi/rt64_gbi_f3dgolden.cpp` |
| Pixel shader | `lib/rt64/src/shaders/RasterPS.hlsl` |
| Compute shader (vertex transform) | `lib/rt64/src/shaders/RSPProcessCS.hlsl` |
| Vertex shader | `lib/rt64/src/shaders/RasterVS.hlsl` |
| Render context (matrix/vp/mv injection) | `src/main/rt64_render_context.cpp` |
| FB dump path | `src/main/rt64_render_context.cpp::update_screen` |

## How to reproduce visible content

```bash
GE_FORCE_SHADE=1 GE_RAW_VTX_COLOR=1 \
GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1 \
GE_DUMP_ALL=1 \
./build/GoldenRecomp.app/Contents/MacOS/GoldenRecomp -level_10 &
APP_PID=$!; sleep 90; kill $APP_PID

# Find FBs with content
for f in /tmp/ge_fb_*.ppm; do
  NZ=$(tail -c +20 "$f" | od -An -tx1 | tr -d ' \n' | tr -d '0' | wc -c)
  [ "$NZ" -gt 100 ] && echo "$f: $NZ"
done

# Convert to PNG (macOS native tool)
sips -s format png /tmp/ge_fb_NNNN.ppm --out frame.png
```

Expect ~40-50% of runs to produce a frame with 250k+ non-zero pixels. The rest stall in the boot loop and produce only black FBs.

## Acknowledgements

Pipeline reverse-engineering and instrumentation done with assistance from [Claude](https://claude.com) (Anthropic) — see commit history for full attribution.
