"""Plain max-min, explicit turn, NO negation. A different formulation from both the C
negamax and the retrograde table, so a sign error in either cannot hide here."""
from functools import lru_cache
W = H = 4
def bit(r,c): return 1 << (r*W+c)
LINES = [sum(bit(r,c) for c in range(W)) for r in range(H)] \
      + [sum(bit(r,c) for r in range(H)) for c in range(W)] \
      + [sum(bit(i,i) for i in range(W)), sum(bit(i,W-1-i) for i in range(W))]
def has_line(s): return any(s & L == L for L in LINES)
def drops(taken):
    out=[]
    for c in range(W):
        for r in range(H):
            m=bit(r,c)
            if not (taken & m): out.append(m); break
    return out

@lru_cache(maxsize=None)
def value(a, b, turn):          # 0 = A's turn, 1 = B's turn. Result from A's point of view.
    if has_line(a): return 1
    if has_line(b): return -1
    ms = drops(a | b)
    if not ms: return 0
    if turn == 0:
        # A maximises
        return max(value(a | m, b, 1) for m in ms)
    # B minimises
    return min(value(a, b | m, 0) for m in ms)

v = value(0, 0, 0)
print(f"  plain max-min, empty 4x4 four-in-a-row, value for PLAYER A = {v:+d}")
print(f"  states explored: {value.cache_info().currsize}")
# hand-checkable sanity cases
a = sum(bit(0,c) for c in range(W))                 # A has the whole bottom row
print(f"  A already owns the bottom row          -> {value(a, 0, 1):+d}  (expect +1, B to move, game over)")
a2 = bit(0,0)|bit(0,1)|bit(0,2)
print(f"  A has three along the bottom, B to move -> {value(a2, 0, 1):+d}  (expect -1, B takes the fourth)")
a3 = bit(0,0)|bit(0,1)|bit(0,2)|bit(0,3)
print(f"  A has the whole bottom row              -> {value(a3, 0, 1):+d}  (expect +1)")
