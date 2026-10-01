"""Differential verification of gt4444 against an independent solver.

WHY THIS IS SHIPPED, NOT A ONE-OFF. The C solver was wrong three separate times on this
game and every wrong version produced a clean-looking answer. What caught them was not the
known-answer checks -- it was running a second implementation that shares no representation
with the first. A check you can only run once, on the day you write the code, is a comment.

`verify_maxmin.py` is a plain max-min with an explicit turn and NO negation, in row-major
bit order. The C is negamax in column-major bit order. Different formulation, different
encoding, different search shape. They must agree on every position.

    cc -O3 -std=c11 -o gt4444 gt4444.c
    python3 verify.py            # ~2000 positions, about a minute

Sign convention, which cost one debugging round: the C reports from the point of view of
the PLAYER TO MOVE, and `verify_maxmin.value` reports from the point of view of PLAYER
ZERO. On an even ply those are the same; on an odd ply they are opposite. `verify.py`
normalises before comparing. Comparing them raw produces 34 confident false mismatches.
"""
import random, subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_maxmin as M

W = H = 4
def cbit(r, c): return 1 << (c * 5 + r)   # column-major, the C's layout
def mbit(r, c): return 1 << (r * W + c)   # row-major, max-min's layout

def build(n, seed):
    random.seed(seed)
    out = []
    while len(out) < n:
        a = b = 0; ca = cb = 0
        for ply in range(random.randint(0, 13)):
            cand = []
            for c in range(W):
                for r in range(H):
                    if not (a & mbit(r, c)) and not (b & mbit(r, c)):
                        cand.append((r, c)); break
            if not cand: break
            m = random.choice(cand)
            if ply % 2 == 0: a |= mbit(*m); ca |= cbit(*m)
            else:            b |= mbit(*m); cb |= cbit(*m)
            if M.has_line(a) or M.has_line(b): break
        turn = 0 if bin(a).count('1') == bin(b).count('1') else 1
        out.append((ca | cb, ca if turn == 0 else cb, a, b, turn))
    return out

if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    cases = build(n, 20261001)
    inp = "\n".join(f"{m} {p}" for m, p, _, _, _ in cases) + "\n"
    exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gt4444")
    r = subprocess.run([exe, "--probe"], input=inp, capture_output=True, text=True, timeout=3600)
    cv = [int(x) for x in r.stdout.split()]
    bad = 0
    for (m, p, a, b, t), c in zip(cases, cv):
        pv = M.value(a, b, t)
        if t == 1: pv = -pv          # normalise to the point of view of the player to move
        if pv != c:
            bad += 1
            if bad <= 5:
                print(f"MISMATCH mask={m:#x} pos(to-move)={p:#x} turn={t}  C={c:+d}  maxmin={pv:+d}")
    print(f"differential: {len(cases)} random legal positions")
    print(f"  agreements {len(cases)-bad}   disagreements {bad}")
    print(f"  {'PASS' if bad == 0 else 'FAIL'} -- C agrees with the independent max-min")
    sys.exit(1 if bad else 0)
