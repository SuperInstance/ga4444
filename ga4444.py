"""Gale's game: 4x4, FOUR in a row. Not five.

The seed proposes scaling tic-tac-toe to 5x5 five-in-a-row and says 4x4 is the stepping
stone. But five-in-a-row is IMPOSSIBLE on a 4x4 grid -- the longest line is 4 cells, so
that game cannot end in a win at all. The solved 4x4 game is m,n,k = 4,4,4: FOUR in a row,
Gale's game, strong-solved with the first player winning.

Which makes it the right next rung for a different reason than the seed gives: it has
COMPLETE ground truth, and it is small enough to generate in a session. 5x5 is the rung
where a lookup table stops being possible, and that is exactly the point at which you no
longer know whether your model is reasoning or memorising. One rung earlier, you do.

Representation is flat tuples again, for the reason Connect 4 taught: the clever bitboard
failed five times in ways that looked like results.
"""
from __future__ import annotations
from functools import lru_cache

W = H = 4
K = 4                 # four in a row
EMPTY, P1, P2 = 0, 1, 2
CENTRE = (1, 2, 0, 3)

MAX_PLY = 9            # search horizon. Values are EXACT inside it and an upper bound outside.


def new_board():
    return tuple((EMPTY,) * W for _ in range(H))


def legal_cols(b):
    return tuple(c for c in range(W) if any(b[r][c] == EMPTY for r in range(H)))


def play(b, c, player):
    for r in range(H):
        if b[r][c] == EMPTY:
            rows = [list(x) for x in b]
            rows[r][c] = player
            return tuple(tuple(x) for x in rows)
    return None


def wins(b, player):
    for c in range(W):
        n = 0
        for r in range(H):
            n = n + 1 if b[r][c] == player else 0
            if n == K:
                return True
    for r in range(H):
        n = 0
        for c in range(W):
            n = n + 1 if b[r][c] == player else 0
            if n == K:
                return True
    for dr, dc in ((1, 1), (1, -1)):
        for r in range(H):
            for c in range(W):
                if all(0 <= r + i * dr < H and 0 <= c + i * dc < W
                       and b[r + i * dr][c + i * dc] == player for i in range(K)):
                    return True
    return False


def immediate_wins(b, player):
    return tuple(c for c in legal_cols(b)
                 if (nb := play(b, c, player)) is not None and wins(nb, player))


def legal_moves(b):
    """Every legal placement, not just column tops -- 4x4 four-in-a-row can place in a
    gap, so a column-only enumeration would MISS legal positions and quietly shrink the
    dataset. That is the tic-tac-toe 'only played one side' failure again."""
    out = []
    for r in range(H):
        for c in range(W):
            if b[r][c] == EMPTY:
                out.append((r, c))
    return out


def play_at(b, rc, player):
    r, c = rc
    rows = [list(x) for x in b]
    rows[r][c] = player
    return tuple(tuple(x) for x in rows)


@lru_cache(maxsize=1 << 22)
def negamax(b, turn, depth=MAX_PLY):
    """Value for the side to move: +1 win, 0 draw, -1 loss. EXACT within MAX_PLY."""
    opp = P2 if turn == P1 else P1
    if wins(b, opp):
        return -1
    if not legal_moves(b):
        return 0
    if depth <= 0:
        return 0
    best = -1
    for rc in [(r, c) for c in CENTRE for r in range(H)] + \
              [(r, c) for c in range(W) for r in range(H)]:
        if b[rc[0]][rc[1]] != EMPTY:
            continue
        v = -negamax(play_at(b, rc, turn), opp, depth - 1)
        if v > best:
            best = v
        if best == 1:
            return 1
    return best


def value_for_p1(b):
    return negamax(b, P1)
