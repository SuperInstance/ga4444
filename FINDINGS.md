

---

## MERGE NOTE — two independent corrections, and why both stay

This file was edited on two diverged branches and both sides **independently
reached the same correction**: the seed's "first player wins" is wrong and the
empty board is `0`, a draw. Neither side had seen the other. **That is the one
piece of convergence in this repository that is not a copy.**

**The two addenda are not competing numbers and must not be collapsed:**

| | quantity | count | digest |
|---|---|---|---|
| addendum A | the complete **game tree** — every reachable position, 9 plies max | **3,338** | `0xcdc9636c704a7ba2` |
| addendum B | every **legal position** at every ply, including positions past the end of a played game | **9,067,975** | `0x32d6d9539cffc85a` |

A is the set of positions a game can actually reach; B is every board that is
legal. B ⊃ A, and the draw fraction moves from **71.1% (A: 2,376/3,338)** to
**66.51% (B: 6,031,189/9,067,975)** for exactly that reason. **Both are correct
about different questions, and the reconciliation is the point.**

Both digests were independently recomputed rather than copied from the commit
messages that introduced them.

## ADDENDUM — the 4x4 solver exists, and the game is a DRAW

`gt4444.c`, verified two ways. **3,338 positions is the complete game tree**: the longest
4x4 four-in-a-row game is 9 plies, and every ply from 10 onward is empty.

```
check 1  empty board, value for P1 = +0   expected 0 (draw)   OK
check 2  three stacked in col 0, value for mover = +1   expected +1   OK
positions 3338   loss(-1) 629   draw(0) 2376   win(+1) 333
FNV-1a 64 digest  0xcdc9636c704a7ba2
all export controls passed
verify.py 2000 random positions: 2000 agreements, 0 disagreements
```

### The headline: 4x4 four-in-a-row is a DRAW, and I was wrong twice about it

I asserted the empty board was `+1`. My solver said `-1`. Both were wrong. The answer is `0`,
established independently by two methods that share no representation with each other:

- a **retrograde table** over all 161,029 reachable states, solved by backward induction;
- a **plain max-min** with an explicit turn and **no negation**, in row-major bit order.

Both report 0. The C solver, once its last bug was fixed, agrees.

**The lesson is not "I made a bug." It is: the known-answer check was the wrong instrument.**
I had written `expected +1` into the program *before* establishing it. When the solver
disagreed I was one step from editing the solver to match. Checking independently is what
turned a coin flip into a measurement. **Asserting a known answer you have not established is
how a wrong number survives.**

### The bug that made it wrong

The C tested `has_won(pos | move, m2)` inside the move loop — it only detected a win by the
**player to move**, and checked for a win by the previous mover only when the board was
*full*. So on a 4x4, where games end by ply 9 with two-thirds of the board empty, the search
carried on from positions the game had already ended in. Invisible on 7x6. Fatal here.

The fix moves the check to the top of the frame, exactly as the retrograde table does it:
**if the player who just moved has a line, the player to move has lost.**

### A fact worth having

All four one-ply positions are valued 0. **After any first move by player zero, the position
is still a draw** — player one can always hold at least a draw. So the 4x4 four-in-a-row
opening is not a winning attempt at all; it is a draw from every first move.

### A sign-convention trap that cost a debugging round

The C reports from the point of view of **the player to move**; `verify_maxmin.value`
reports from the point of view of **player zero**. On an even ply those coincide; on an odd
ply they are opposite. Comparing them raw produces **34 confident false mismatches** that look
exactly like a real solver bug. `verify.py` normalises before comparing, and says so.

### `verify.py` ships, because a check you can only run once is a comment

The differential test is in the repo, not in my shell history. The solver was wrong three
separate times and every wrong version produced a clean-looking answer. What caught them was
never the known-answer checks — it was a second implementation with a different
representation, a different encoding, and a different search shape.


---

## CORRECTION — the state count was impossible

This document previously said **180,361 reachable our-turn states**. That number cannot exist: a 3x3 board has 3^9 = 19,683 distinct states, so even labelling every one with whose turn gives at most 39,366. **180,361 is larger than the entire state space by a factor of 4.6.**

The real numbers, from the repo's own `enumerate_reachable()`:

- **5,478** reachable board states in total
- **2,423** of them with US to move — this is the training set size
- **1,177 (48.6%)** have more than one optimal move, not 14.7%
- optimal-set sizes run 1 to 9; 456 positions have 3 optimal moves, 116 have 5

The multi-optimal fraction matters more than the raw count: **a label set that picks one optimal move relabels 48.6% of positions as errors.** Any accuracy measured with single-move labels on this dataset is measuring agreement with an arbitrary tie-break, not correctness.

The number was wrong in the same way as the fleet's `historybloat` signal — a figure that nobody checked against the size of the space it claims to count. **3^9 = 19,683 is a fact you can check in your head; the number should have been checked against it before it was written down.**

---

## The ground truth exists now, and the seed's known answer for it is wrong

`gt4444.c` generates the **complete** table: every legal non-terminal position
of Gale's game at every ply, 9,067,975 of them, in 52 seconds on one core.
FNV-1a 64 `32d6d9539cffc85a`, independently reproduced in Python over the
files.

**The empty board is a draw (0), not a first-player win.** The seed says first
player wins. Three implementations disagree with the seed and agree with each
other: the production solver (202,160 nodes), the same search with no
transposition table and no threat shortcut (40,152,637 nodes), and an
independent Python negamax written from the rules with nothing but alpha-beta
(30,319,885 nodes). The program prints the failure and exits non-zero.

That is a better outcome than a pass would have been. "First player wins" is a
label; a draw is a label that a linear model cannot represent at all without
the parity that `pie-minimax` measured, and the composition test below is much
sharper on a board where a third of the positions are decided by a tie.

## The check that made the table trustworthy, and the three bugs it caught

There are two ways to check a solver: against another solver, or against the
rules of the game. Only the second can see a bug the solvers share, and all
three bugs here were shared.

- `can_win_next(opponent) → LOSS(1)`: unsound, the threatened square can be
  blocked. In all three solver variants at once.
- `mirror()` not adding a half-move for the move into the child: every
  distance-to-win wrong by one per recursion level, signs surviving by luck.
- The child call handing the stone just played to the opponent: every value in
  the table wrong, and the search 113x slower than it should be.

None of these was visible to variant-vs-variant comparison. All three were
visible to one line of C:

    value(P) = max over moves m of ( m wins now ? +1 : −value(P after m) )

applied to every exported position. 4,572,569 violations before, 0 after.

**A verification pass that only cross-checks your own implementations is a
control that cannot fail.** The ladder's other solvers should all carry a
Bellman self-consistency check; `../connect4/ctool.c` now has one too, limited
by how much of the table fits in a session.

## What the composition test now has to run on

- 2,730,266 positions where the side to move wins, 306,520 where it loses, and
  **6,031,189 draws** — 66% of the table.
- The draw class is the interesting one and it did not exist before. A position
  that is neither a win nor a loss for the mover is exactly where "count the
  threats" has to be right and a sum of per-cell votes has nothing to say.
- The `SIMPLE` / `COMPOSED` split in the README still applies, and now every
  row is exact, so a collapse in accuracy on `COMPOSED` is a fact about the
  representation and not about a horizon.
