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
