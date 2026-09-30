# ga4444

4x4 **four** in a row — Gale's game. Not five; five is impossible on a 4x4 grid.

The rung with **complete ground truth**: too big for tic-tac-toe's elegance, small enough to
generate in a session. That is the point of doing it before 5x5, because once a lookup
table is impossible you can no longer tell whether a model is reasoning or memorising.

- `ga4444.py` — exact negamax solver, flat tuples, documented search horizon.
- `run.py` — ground-truth generation and the composition test.
- `FINDINGS.md` — the 5-in-a-row correction, why this rung and not 5x5, and the status.

**The test:** split positions by whether they need a threat *count* (≥2 simultaneous wins —
a fork, which is parity) or only a threat *location* (0 or 1). `pie-minimax` showed a linear
model cannot compose, exactly, on a solved 3x3 board. The prediction here is that accuracy
collapses on the count positions.
