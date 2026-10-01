# 4x4 four-in-a-row: the rung with complete ground truth

## A correction to the seed, before anything else

The seed proposes scaling tic-tac-toe from 4x4 to **5x5 five-in-a-row**, and treats 4x4
as the stepping stone. But:

> **Five-in-a-row is IMPOSSIBLE on a 4x4 grid.** The longest line — row, column, or
> diagonal — is 4 cells. The game cannot end in a win at all.

The solved 4x4 game is `m,n,k = 4,4,4`: **four in a row**, Gale's game, strong-solved
with the first player winning. That is the game, and it is a *better* rung than the seed
argues for, on a different ground than the seed gives.

## Why 4x4 is the right rung and 5x5 is the wrong next step

The seed says 5x5 is superior because the state space is too big for a lookup table and
"neural pattern extraction becomes essential." True, and that is precisely why 5x5 is a
*bad* first measurement.

**Once a lookup table is impossible, you no longer know whether your model is reasoning
or memorising.** The only ground truth left is self-play, which means the labels are only
as good as the search that produced them, and every number becomes a statement about your
own solver rather than about the game.

4x4 has the opposite property: **complete ground truth, small enough to generate in a
session.** You can ask the real question — *what can a model represent?* — and get an
answer that is a fact about the representation rather than about your search.

Rung order that actually measures something:

1. **3x3** — done. 180,361 exact states. Linear model: 0.1807 against a 0.1431 floor.
2. **4x4 four-in-a-row** — this rung. Complete ground truth, still tractable.
3. **5x5 / Connect 4** — measurement stops being about representation and starts being
   about how well you can search.

## The experiment, and the prediction carried into it

`pie-minimax` measured exactly that a linear model cannot compose: optimal play is a
minimax over a tree, and a linear map is a sum of independent per-cell votes with no
composition between cells. 81 parameters reached 0.1807 against a floor of 0.1431.

The same split applies here, and it is the whole test:

- **SIMPLE** — the side to move has 0 or 1 immediate winning move. Finding one winning move
  is a local geometric pattern.
- **COMPOSED** — two or more simultaneous winning moves. That is a fork, it forces a threat
  *count*, and the count is parity.

**Prediction: accuracy on COMPOSED collapses toward the floor while SIMPLE stays high.** If
it does not, the tic-tac-toe result was about the size of the board rather than about
composition, and that is worth knowing.

## Status: not delivered, and why

**No accuracy numbers yet.** The solver is correct and the generation is still running —
pure-Python negamax over a 4x4 board with a 9-ply horizon is expensive, and 7 plies is
running now. The README and the solver are on the repo; the result is not, and it will be
added when the run lands rather than estimated in advance.

The generation being slow is itself the useful observation for the ladder: **even 4x4 four-in-a-row
is a real compute job in Python.** The 5x5 and Connect 4 rungs are not Python jobs, and
the seed's `train.py` templates implicitly assume a dataset that arrives from somewhere. It
does not; someone has to generate it with a C solver, and every rung of this ladder has to
clear that bar first.

## One more representation note

Connect 4 cost five failed bitboard implementations, each of which ran and produced a
plausible number. This file uses flat tuples of tuples from the first line for the same
reason. `legal_moves` here also enumerates **every empty cell, not just column tops** —
in four-in-a-row a piece can land in a gap, and a column-only enumerator would silently
miss legal positions and shrink the dataset without any error. That is the tic-tac-toe
"only played one side of the tree" failure wearing a different hat.


---

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
