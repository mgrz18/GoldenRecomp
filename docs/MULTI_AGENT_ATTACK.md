# Multi-agent attack playbook

This repo includes infrastructure to use **coordinated AI agents** (e.g. Claude Code) to autonomously iterate on the open blocker issues. The bottleneck for finishing the GoldenEye port isn't intelligence — it's the bandwidth of build → run → analyse → patch cycles. Multi-agent coordination is well-suited to this.

## Why this approach

- The remaining blockers are well-defined (each issue has acceptance criteria)
- The codebase is fully instrumented (harness emits structured JSON metrics)
- The build/run cycle is fully scripted (`scripts/harness.sh`)
- Each blocker is **falsifiable**: change X, expect metric Y to move from A to B
- That's exactly what an LLM can do at scale

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  ORCHESTRATOR  (you, /loop, or scheduled agent)     │
│  - Reads issue + INVESTIGATION.md + last harness    │
│  - Picks one falsifiable hypothesis                 │
│  - Dispatches specialized subagents                 │
│  - Synthesizes results                              │
│  - Decides commit/revert/loop                       │
└──────┬──────────────────────────────────────────────┘
       │
       ├── DISASSEMBLY agent (read gmain.s)
       │   "Trace handler at IMEM offset X — list every w1 read"
       │
       ├── CODE_NAVIGATION agent
       │   "Find every writer of g_CurrentPlayer->field_10E0"
       │
       ├── PATCH_AUTHOR agent
       │   "Apply this exact change to file:line, build it"
       │
       └── HARNESS_RUNNER (you call scripts/harness.sh)
           Returns structured JSON metrics
```

## Tools provided

### `scripts/harness.sh`

Builds, runs (with timeout), and scores. Emits JSON to stdout:

```bash
GE_FORCE_SHADE=1 GE_RAW_VTX_COLOR=1 \
GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1 \
GE_DUMP_ALL=1 \
  scripts/harness.sh 90 > result.json
```

JSON schema:

```jsonc
{
  "duration_s": 90,
  "scheduler": {
    "dones": 156,           // bossMainloop OS_SC_DONE_MSG count (target ≥ 100)
    "retraces": 20,
    "junk_msgs": 0,         // OSScMsg with bogus type. Target = 0
    "heal_count": 4,
    "learn_count": 1
  },
  "rendering": {
    "fullsync_count": 8,
    "publish_fb_count": 8,
    "tri_visible": 20,
    "tri_nan": 0,           // Target = 0
    "tri_drawn_total": 100,
    "loadblock_count": 27,  // Target > 0 once textures work
    "settile_count": 30,
    "settimg_count": 20,
    "matrix_malformed": 0,  // Target = 0 once w1 mystery solved
    "crashes": 0
  },
  "framebuffers": {
    "fbs_total": 80,
    "fbs_with_content": 5,    // Target ≥ 1 deterministically
    "top_nonzero": 412792,    // Target > 100k for visible scene
    "top_file": "/tmp/ge_fb_0082.ppm"
  },
  "diagnostics": {
    "fbpair_lines": "...",
    "tri_samples": "...",
    "matrix_malformed_lines": "...",
    "log_path": "/tmp/harness_run_PID_TS.log"
  }
}
```

Key metrics for each blocker are documented in [the per-blocker table below](#per-blocker-success-metrics).

### `.claude/commands/attack-blocker`

Custom Claude Code slash command. Invoke as:

```
/attack-blocker 2
```

…and it'll run the orchestrator workflow against issue #2 (or whichever number you pass). The full workflow is in [`.claude/commands/attack-blocker.md`](../.claude/commands/attack-blocker.md):

1. Read state (issue, docs, source)
2. Form falsifiable hypothesis
3. Implement smallest change
4. Run harness ×3 (account for non-determinism)
5. Compare to baseline
6. Commit-or-revert
7. Loop or exit

The command enforces **hard rules**: never commit regressions, one hypothesis per iteration, stop after 10 iterations max.

## How to run autonomously

### Option 1 — `/loop` (auto-pacing)

In Claude Code, run:

```
/loop /attack-blocker 2
```

Without an interval, Claude self-paces between iterations (typically 5-30 min based on cache). Will keep going for hours/days unattended until either:
- Acceptance criterion met
- 10 iterations max
- You stop it

### Option 2 — One-shot manual

Run `/attack-blocker N` once. The orchestrator does up to 10 iterations and stops with a status report. Resume by running again.

### Option 3 — Scheduled (CronCreate)

Use Claude Code's `/schedule` skill to create a cron job that runs the orchestrator nightly:

```
/schedule "every day at 2am, run /attack-blocker on whichever issue has the
oldest 'in progress' label and lowest priority number"
```

## Per-blocker success metrics

| Issue | Metric | Acceptance |
|---|---|---|
| [#1 stall](https://github.com/mgrz18/GoldenRecomp/issues/1) | `dones` ≥ 100 in 5/5 runs | 100% determinism |
| [#2 w1 mystery](https://github.com/mgrz18/GoldenRecomp/issues/2) | `matrix_malformed = 0` AND `tri_visible > 50` | clean matrix decode + real geometry |
| [#3 scene tris](https://github.com/mgrz18/GoldenRecomp/issues/3) | rainbow shader → `top_nonzero > 100k` | per-pixel gradient appears |
| [#4 combiner alpha](https://github.com/mgrz18/GoldenRecomp/issues/4) | `top_nonzero > 100k` WITHOUT early-return | real combiner output |
| [#5 textures](https://github.com/mgrz18/GoldenRecomp/issues/5) | `loadblock` src ∈ valid texture range | clean loads |
| [#6 perspective](https://github.com/mgrz18/GoldenRecomp/issues/6) | `tri_nan = 0` with perspective+`GE_MV_TZ=N` | projection works |

## What works well with this approach

- **Code archaeology**: tracing call chains across decomp + RT64 + N64ModernRuntime
- **Hypothesis bisection**: comment out one path at a time, observe metric change
- **Documenting failed hypotheses**: each iteration's findings get appended to issue thread automatically
- **Cross-referencing references**: gmain.s ↔ libultra docs ↔ GLideN64 source ↔ our impl

## What doesn't work well

- **Pure intuition leaps**: "this looks like X" — agents are weaker here than humans
- **Visual debugging**: agents can't "see" PPMs, so we score by non-zero count, not actual content. A human glance at the PPM tells you scene structure.
- **Subtle CPU-side bugs**: race conditions, timing issues — hard to reproduce, even harder to diagnose without traces
- **Exploring novel architectures**: the orchestrator is best at "what's the next change" within a known approach, not "should we redesign"

## Cost / budget

Rough numbers for a single iteration (one hypothesis end-to-end):
- Read state: ~5k tokens (cached)
- Hypothesis + implementation: ~5k tokens
- 3 harness runs: ~3 × 90s = 4.5 min wall time, no token cost
- Result analysis + decide: ~3k tokens
- Commit message + issue comment: ~1k tokens

**Total: ~15k tokens + 5 min wall time per iteration.** A 10-iteration session is ~150k tokens (~1 hour wall).

For Claude Max 20x users this is comfortable for unattended overnight runs. For Pro users, budget more carefully.

## Tips for orchestrator design

- **Prefer specialized one-shot agents over chatty conversations** — saves tokens, keeps each agent focused
- **Hand each agent the exact file:line they need** — don't make them search
- **Always pass the last harness JSON** as context — agents need to know what's currently true
- **Verify before committing**: read git diff, confirm only the intended change is staged
- **Use the issue as the diary**: each iteration's findings become a comment, future iterations read history

## Contributing your own attack

If you make progress on a blocker:
1. Open a draft PR linking to the issue
2. Include harness JSON before/after in the PR description
3. The orchestrator can pick up from a partial fix and continue
4. Even failed attempts are valuable — comment on the issue with what you tried and the negative result

## Limits / what to do when stuck

If 3 consecutive iterations regress, the orchestrator stops and writes a "stuck" report. At that point, options are:
- A human reviews the report and provides direction
- A "fresh perspective" agent (different prompt) tackles the same blocker
- We pivot to a different blocker
- We bring in a human specialist (RSP microcode, libultra OS, etc.)

The framework doesn't replace specialist knowledge for hard problems — but it dramatically multiplies productive iterations on well-defined problems.
