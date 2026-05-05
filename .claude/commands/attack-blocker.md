---
description: Multi-agent attack on a GoldenRecomp blocker issue. Spawns specialized agents, runs harness, iterates until success criteria met or budget exhausted.
---

# Attack a GoldenRecomp blocker (multi-agent orchestrator)

Goal: autonomously iterate on a specific blocker issue from `mgrz18/GoldenRecomp` until its acceptance criteria are met (or token budget runs out / loop limit hit).

## Inputs (from $ARGUMENTS)

- Issue number to attack (e.g. `1`, `2`, ..., `6`). Defaults to `2` (the F3D_Gold w1 mystery).
- Optional max iterations (default 10).

## Workflow

### Phase 0 — Read state

1. `gh issue view <N> --repo mgrz18/GoldenRecomp` — load the blocker context, reproducer, files-to-touch, and acceptance criteria.
2. Read `docs/INVESTIGATION.md` for cumulative knowledge.
3. Read `docs/MULTI_AGENT_ATTACK.md` for the orchestrator playbook.
4. Read the relevant `lib/...` source files mentioned in the issue.
5. Capture a baseline run: `scripts/harness.sh 60 > /tmp/baseline.json` with the env-var combo from the issue.

### Phase 1 — Hypothesize (single Plan agent or self)

Synthesize a falsifiable hypothesis: "If we change X at `file:line`, then metric Y in harness output should improve from `baseline_value` to `target_value`."

If no hypothesis can be formed (we're truly stuck), report and stop.

### Phase 2 — Implement (1 Implementation agent OR self)

Apply the smallest possible code change that tests the hypothesis. NOT a refactor. NOT speculative cleanup. ONE change at a time.

### Phase 3 — Validate (run harness)

```bash
ninja -C build GoldenRecomp 2>&1 | tail -3   # build
GE_FORCE_SHADE=1 GE_RAW_VTX_COLOR=1 GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1 GE_DUMP_ALL=1 \
  scripts/harness.sh 90 --no-build > /tmp/iter.json   # run + score
```

Run **3 times** to account for non-determinism. Aggregate: average + variance per metric.

### Phase 4 — Decide

- **Improved**: commit (with a message that describes hypothesis + result), update issue with progress comment, decide if criterion met.
- **No change / regression**: revert the change, log hypothesis as falsified in issue thread, generate next hypothesis from remaining ideas.
- **Acceptance criterion met**: close issue, write summary, exit loop.

### Phase 5 — Loop or exit

- If criterion met → exit
- If max iterations reached → write status report to issue, exit
- Otherwise → back to Phase 1 with updated state

## Hard rules

1. **Never commit a regression.** Always run baseline before, candidate after, and confirm the metric of interest improved without breaking other metrics.
2. **One hypothesis per iteration.** Never apply multiple unrelated changes in one cycle — you can't tell which one caused improvement.
3. **Always log to the issue thread.** Each iteration's hypothesis + result becomes a comment.
4. **Stop after 10 iterations max** unless explicitly told otherwise. Prevents runaway token spend on dead ends.
5. **No human-in-the-loop questions during iterations.** Either the orchestrator decides autonomously or it stops with a report.
6. **Submodule pushes only on green.** rt64, N64ModernRuntime have `mac-port` branches; push only when the harness shows clear improvement.

## Per-blocker default acceptance criteria

| Issue | Metric | Target |
|---|---|---|
| #1 stall | `dones >= 100` in 5/5 runs | 100% determinism |
| #2 w1 mystery | `matrix_malformed = 0` AND `tri_visible > 50` | clean matrix decode + real geometry |
| #3 scene tris | enable rainbow shader → `top_nonzero > 100k` | rainbow gradient appears |
| #4 combiner alpha | `top_nonzero > 100k` WITHOUT early-return stopgap | real combiner output |
| #5 textures | `loadblock` src ∈ valid texture ranges, no `0x00264500`-style heap-aliased | clean texture loads |
| #6 perspective | `tri_nan = 0` with `GE_INJECT_PERSPECTIVE=1 GE_MV_TZ=2000` | projection works |

## Safety

- Always read `git status` before committing — don't accidentally commit unrelated changes.
- Always check `git log --oneline -3` before pushing — confirm we're not overwriting upstream commits.
- If a build takes >5 min, abort and revert (something's wrong).
- If 3 iterations in a row regress: stop and dump diagnostic state to a fresh issue comment.

## Start

If `$ARGUMENTS` provided an issue number, start with that. Otherwise prompt user (only on first invocation).

```
$ARGUMENTS
```
