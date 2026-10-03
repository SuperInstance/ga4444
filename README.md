# ga4444

4x4 **four** in a row — Gale's game. Not five; five is impossible on a 4x4 grid.

The rung with **complete ground truth**: too big for tic-tac-toe's elegance, small enough to
generate in a session. That is the point of doing it before 5x5, because once a lookup
table is impossible you can no longer tell whether a model is reasoning or memorising.

- `gt4444.c` — the C11 solver and the **complete** ground-truth generator.
  Self-contained, no dependencies. **The table is delivered.**
- `gt4444.md` — what it produced, the conventions, and the checks.
- `data/ply_NN.txt` — **9,067,975 positions**: every legal non-terminal
  position of the game, at every ply 0..16.  FNV-1a 64 `32d6d9539cffc85a`.
- `Makefile` — `make selftest`, `make export`.
- `ga4444.py`, `run.py` — the earlier Python solver and driver, superseded.
- `FINDINGS.md` — the 5-in-a-row correction, why this rung, and the results.

**The test:** split positions by whether they need a threat *count* (≥2 simultaneous wins —
a fork, which is parity) or only a threat *location* (0 or 1). `pie-minimax` showed a linear
model cannot compose, exactly, on a solved 3x3 board. The prediction here is that accuracy
collapses on the count positions.


## Status: delivered, and the known answer in the seed is wrong

The seed's known answer for this rung is **"first player wins"**. The table
says the empty board is **0 — a draw**, and three independent computations
agree (see `gt4444.md`). The program prints the failure and exits non-zero.

**9,067,975 positions, complete, 52 seconds, and every one of them checked
against the game's own recursion with 0 violations.** The composition test can
now be run against real ground truth instead of a horizon.
