# PARTITION-44 — the composition partition re-derived on 4×4 (wave-63)

Carries the pie-minimax wave-63 corrections (M2/M3: blocked-line bug; M1: empty-cell-aware
scoring) to the 4×4 rung BEFORE any GPU experiment consumes the composition story.
Receipts: `receipts/partition44-receipt.json`, pinned position dump
`receipts/partition44-positions.txt` (sha256 `7318cd08…`, FNV-1a 64 `0x2d30aec881bfbbb1`,
canary-verified). Population: 2,171 LATE our-turn boards (walk to ply 16, P1 ≥ 5 stones,
node budget 400k) + run.py's 6,000 early boards as the comparability anchor.

---

## 1. THE GENERATOR FINDING (blocks Experiment 3 as currently written)

run.py's walk caps at `plies >= 9`, so every our-turn board has P1 ≤ 4 stones. A double
threat needs ≥ 5 own stones (two 3-own lines share at most one cell). **The composition
class is STRUCTURALLY EMPTY in the current generator: COMPOSED n = 0 under all three
definitions on the early population.** "Blocked on data" was right, but the blocker is
the walk horizon, not the C solver. Any 4×4 composition experiment must sample LATE
boards (P1 ≥ 5 stones, ply ≥ 10).

## 2. The three definitions compared (on the late population, n = 2,171)

| definition | SIMPLE n (chance) | COMPOSED n (chance) | reading |
|---|---|---|---|
| DEF-A — pie-minimax old-bug analog (lines with 3 own + 1 non-own; dead lines counted) | 1,343 (0.634) | **828 (0.565)** | dead-line inflation is massive at 4×4: 38% of boards labeled "composed" by BLOCKED lines |
| **DEF-B — corrected** (immediate wins: 3 own + 1 empty, ours only) | 2,135 (0.607) | **36 (0.667)** | the true composition class exists at 4×4 but is 1.7% of late boards — executable, LOW_POWER (n < 50) |
| DEF-C — run.py's current flag (ours ≥ 2 OR theirs ≥ 2) | 2,085 (0.605) | 86 (0.663) | 50 of 86 are OPPONENT-fork boards; conflates who must compose |

Multi-optimal: **961/2,171 = 44.3%** of distinct late boards (3×3: 48.6% — the
set-valued-label lesson carries to this rung; report top-1 AND set-recall).

## 3. Reference baselines under DEF-B (5-fold CV, empty-cell-aware scoring)

Values from `ga4444.py` negamax at MAX_PLY=9 — exact WITHIN the horizon, an upper bound
outside; these are reference numbers for the GPU queue, not final 4×4 truth (the C
ground-truth table / LANES draw finding remains the exactness anchor).

| model | SIMPLE (n=2,135) | COMPOSED (n=36) |
|---|---|---|
| floor (random empty cell) | 0.5986 | 0.6111 |
| logistic 16→16 (empty-aware) | **0.9438** | **1.0000** |
| sklearn tree depth 16 (empty-aware) | 0.8796 | 1.0000 |

Filled headroom: logreg S 0.857 / C 1.000; tree S 0.694 / C 1.000.

## 4. What this means for the queue (GPU-EXPERIMENTS.md rows 3/4/7)

- **"The 3×3 composition effect replicates" is now UNLIKELY as pre-registered.** At 4×4,
  under the corrected partition, the linear model shows NO composition penalty — it fills
  ALL of the COMPOSED headroom (LOW_POWER, n=36). Combined with the 3×3 re-derivation
  (composition class n=22, all trivial), the pre-registered composition-collapse test has
  no executable positive instance at either rung so far.
- The DEF-A and DEF-C variants must not be consumed by GPU runs: one measures blocked-line
  pressure, the other conflates the opponent's forks. DEF-B is the partition of record.
- Honest alternative for the mechanism hunt: **generated double-threat boards** (construct
  positions with ≥ 2 open immediate wins by design, matched against single-threat
  controls at equal stone count and equal |empty|), rather than natural-walk sampling —
  natural play rarely produces the class (1.7%). Pre-register THAT before the GPU spend.
- LOW_POWER discipline: n=36 means the COMPOSED column's 1.0000 carries a wide interval;
  treat it as "no penalty observed", not "penalty refuted", until the generated-board
  experiment scales the class.

## 5. Controls

- FNV canary checked before the digest (0x24a555471370b18d).
- Floor ≈ per-class chance on both classes (0.5986 vs 0.6067; 0.6111 vs 0.6667) — the
  floor is honest, the chance basis is per-class (the run3 hygiene lesson, computed
  in-script this time — M5 applied).
- Scoring respects move legality (empty cells only — M1 applied); classes_ mapping used
  so folds with missing labels cannot index out of bounds.
- The int/tuple pick-comparison bug in v1 of this script was caught by its own output
  (floor == logreg == tree exactly — an impossible result) and fixed; the receipt is from
  the corrected run only.
