import sys, time, random, itertools
sys.path.insert(0, '/workspace/projects/ga4444')
import numpy as np
import ga4444 as G

def encode(b):
    v = np.zeros((G.W, G.H))
    for r in range(G.H):
        for c in range(G.W):
            v[r, c] = 1.0 if b[r][c] == G.P1 else (-1.0 if b[r][c] == G.P2 else 0.0)
    return v.ravel()

def softmax(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z); return e / e.sum(axis=1, keepdims=True)

def generate(n_target=6000, seed=0):
    rng = random.Random(seed); rows = []; seen = set()
    def walk(b, turn, plies):
        if plies >= 9 or len(rows) >= n_target: return
        if G.wins(b, G.P2 if turn == G.P1 else G.P1) or not G.legal_moves(b): return
        if turn == G.P1:
            mine = G.immediate_wins(b, G.P1); theirs = G.immediate_wins(b, G.P2)
            v = G.negamax(b, G.P1)
            k = b
            if k not in seen:
                seen.add(k)
                # SIMPLE: at most one immediate win for EITHER side -- a local pattern.
                # COMPOSED: two or more, a fork, which forces a threat COUNT.
                rows.append({"b": b, "value": v, "composed": (len(mine) >= 2) or (len(theirs) >= 2)})
            for rc in G.legal_moves(b):
                walk(G.play_at(b, rc, G.P1), G.P2, plies + 1)
        else:
            for rc in G.legal_moves(b):
                walk(G.play_at(b, rc, G.P2), G.P1, plies + 1)
    walk(G.new_board(), G.P1, 0)
    return rows

if __name__ == "__main__":
    t = time.time(); rows = generate(); 
    comp = sum(1 for r in rows if r["composed"])
    print(f"  generated {len(rows)} positions in {time.time()-t:.1f}s (MAX_PLY={G.MAX_PLY})")
    print(f"    COMPOSED (>=2 simultaneous wins either side): {comp} ({comp/max(1,len(rows)):.1%})")
    import collections
    print(f"    value histogram: {dict(sorted(collections.Counter(r['value'] for r in rows).items()))}")
    X = np.array([encode(r["b"]) for r in rows]); y = np.array([r["value"] for r in rows])
    compv = np.array([r["composed"] for r in rows])
    n = len(X); cut = int(n*0.75)
    rng = np.random.default_rng(0)
    W = rng.normal(0,.1,(16,3)); b = np.zeros(3)
    for step in range(1500):
        Xb, yb = X[:cut], y[:cut]
        L = Xb @ W + b
        P = softmax(L)
        Y1 = np.zeros((cut,3)); Y1[np.arange(cut), yb] = 1.0
        d = (P - Y1)/cut
        W += 0.5 * (Xb.T @ d); b += 0.5 * d.sum(axis=0)
    pred = (X[cut:] @ W + b).argmax(1); yt = y[cut:]
    for name, mask in (("SIMPLE  ", ~compv[cut:]), ("COMPOSED", compv[cut:])):
        if mask.sum():
            print(f"    LINEAR model on {name} n={int(mask.sum()):4}  accuracy {float((pred[mask]==yt[mask]).mean()):.4f}")
    print(f"    overall test accuracy {float((pred==yt).mean()):.4f}")
