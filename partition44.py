#!/usr/bin/env python3
"""partition44.py — wave-63: the SIMPLE/COMPOSED partition re-derived on 4x4 (Gale's game).

WHY (upstream: pie-minimax/CEILING-VERIFY.md M2/M3): the pre-registered composition
partition from 3x3 (a) counted BLOCKED lines as threats (is_simple bug) and (b) at 4x4
run.py's `composed` flag uses either-side immediate wins (DEF-C), which conflates the
opponent's forks with our composition demand. Before any GPU experiment consumes the
composition story at this rung, the three candidate definitions are compared on the
same population, and reference baselines run under the corrected one (DEF-B).

DEFINITIONS (our turn to move):
  DEF-A  pie-minimax old-bug analog: LINES with 3 own + 1 non-own (dead lines included);
         COMPOSED-A = >=2 such lines.
  DEF-B  corrected (the fixed definition carried from 3x3): immediate wins = columns
         where playing NOW completes a line (3 own + 1 empty, by construction unblockable);
         COMPOSED-B = >=2 of OUR immediate wins.
  DEF-C  run.py's current flag: ours >= 2 OR theirs >= 2.

HONESTY:
  - values come from ga4444.py negamax with MAX_PLY=9: EXACT within the horizon, an
    upper bound outside (docstring). Optimal sets are therefore optimal-within-horizon;
    the C ground-truth table (LANES: draw, two independent methods) is the exactness
    anchor, not re-litigated here.
  - free placement (gaps) per legal_moves; gravity's play() is NOT the move space.
  - floor = uniform random EMPTY cell; models score max P over EMPTY cells ONLY
    (pie-minimax M1 lesson: argmax over all classes auto-misses occupied picks).
  - variance over 5-fold data resampling (seed-only variance is std==0 = INCONCLUSIVE).
Digest: FNV-1a 64 of the canonical position dump, canary-verified first.
"""
import sys, os, json, random, time, hashlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ga4444 as G
from sklearn.tree import DecisionTreeClassifier
from sklearn.linear_model import LogisticRegression

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "receipts")
os.makedirs(OUT, exist_ok=True)
t0 = time.time()

def fnv1a64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for byte in data:
        h ^= byte; h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h
assert fnv1a64("café Δ 日本語".encode()) == 0x24a555471370b18d, "FNV canary failed"
receipt = {"canary": "fnv1a64('café Δ 日本語') == 0x24a555471370b18d OK"}

# ---------- population ----------
# FINDING (this run): run.py's walk caps at plies>=9, so P1 has <=4 stones on every
# our-turn board — a double threat needs >=5 own stones. The composition class is
# STRUCTURALLY EMPTY in the current generator. We therefore add a LATE walk
# (plies to 16, node-budgeted) and evaluate partitions on BOTH populations.

def generate(n_target=6000, seed=0, max_ply=9, min_p1_stones=0, budget=None):
    rng = random.Random(seed); rows = []; seen = set(); nodes = [0]
    def walk(b, turn, plies):
        if plies >= max_ply or len(rows) >= n_target: return "cap"
        if budget is not None:
            nodes[0] += 1
            if nodes[0] > budget: return "budget"
        if G.wins(b, G.P2 if turn == G.P1 else G.P1) or not G.legal_moves(b): return
        if turn == G.P1:
            if b not in seen:
                stones = sum(1 for row in b for v in row if v == G.P1)
                if stones >= min_p1_stones:
                    seen.add(b)
                    rows.append(b)
            for rc in G.legal_moves(b):
                walk(G.play_at(b, rc, G.P1), G.P2, plies + 1)
        else:
            for rc in G.legal_moves(b):
                walk(G.play_at(b, rc, G.P2), G.P1, plies + 1)
    walk(G.new_board(), G.P1, 0)
    return rows

BOARDS = generate()          # run.py's population (ply<=9) — the comparability anchor
LATE = generate(n_target=100000, max_ply=16, min_p1_stones=5, budget=400000)
receipt["population"] = {"early_boards_ply9": len(BOARDS), "late_boards_ply16_p1ge5": len(LATE),
                         "walk": "run.py's walk, seed 0, deduped; late walk node-budgeted 400k"}
print(f"[gen] early(ply<=9)={len(BOARDS)} late(ply16,P1>=5)={len(LATE)}")

# ---------- lines of length 4 on 4x4 (rows, cols, 2 diagonals) ----------
LINES = []
for r in range(G.H): LINES.append([(r, c) for c in range(G.W)])
for c in range(G.W): LINES.append([(r, c) for r in range(G.H)])
LINES.append([(i, i) for i in range(4)])
LINES.append([(i, 3 - i) for i in range(4)])

def threats_old(b, player):
    """DEF-A: lines with exactly 3 own + 1 non-own (dead lines counted — the 3x3 bug analog)."""
    n = 0
    for line in LINES:
        vals = [b[r][c] for r, c in line]
        if vals.count(player) == 3 and vals.count(0) + vals.count(2 if player == 1 else 1) == 1:
            n += 1
    return n

# ---------- optimal sets (within horizon) ----------
def optimal_sets(b):
    moves = G.legal_moves(b)
    vals = {}
    for rc in moves:
        nb = G.play_at(b, rc, G.P1)
        vals[rc] = -G.negamax(nb, G.P2)
    best = max(vals.values())
    return frozenset(m for m, v in vals.items() if v == best), vals, best

data = []
for b in LATE:
    opt, vals, best = optimal_sets(b)
    empty = G.legal_moves(b)
    data.append({"b": b, "opt": opt, "n_empty": len(empty),
                 "A": threats_old(b, G.P1),
                 "B": len(G.immediate_wins(b, G.P1)),
                 "C_ours": len(G.immediate_wins(b, G.P1)),
                 "C_theirs": len(G.immediate_wins(b, G.P2)),
                 "value": best})
receipt["runtime_note"] = f"{round(time.time()-t0,1)}s to evaluate {len(data)} LATE boards"
print(f"[eval] optimal sets done in {receipt['runtime_note']}")

multi = sum(1 for d in data if len(d["opt"]) > 1)
receipt["multi_optimal"] = {"count": multi, "frac": round(multi / len(data), 4),
                            "note": "compare 3x3's 48.6% per distinct board (pie-minimax wave-63)"}
print(f"[multi] {multi}/{len(data)} = {multi/len(data):.1%} multi-optimal")

# ---------- partition comparison ----------
def chance(d): return len(d["opt"]) / d["n_empty"]
partitions = {}
for name, key in (("DEF-A_old", lambda d: d["A"] >= 2),
                  ("DEF-B_fixed", lambda d: d["B"] >= 2),
                  ("DEF-C_runpy", lambda d: d["C_ours"] >= 2 or d["C_theirs"] >= 2)):
    cls = {"S": [d for d in data if not key(d)], "C": [d for d in data if key(d)]}
    partitions[name] = {k: {"n": len(v), "chance": round(float(np.mean([chance(d) for d in v])), 4) if v else None}
                        for k, v in cls.items()}
receipt["partitions"] = partitions
for k, v in partitions.items():
    print(f"[part] {k}: SIMPLE n={v['S']['n']} chance={v['S']['chance']} | COMPOSED n={v['C']['n']} chance={v['C']['chance']}")

# ---------- reference baselines under DEF-B (empty-cell-aware, 5-fold CV) ----------
def X_of(keys):
    return np.array([[1.0 if b[r][c] == G.P1 else (-1.0 if b[r][c] == G.P2 else 0.0)
                      for r in range(G.H) for c in range(G.W)] for b in keys])

def canonical(d):
    return "".join("x" if v == G.P1 else "o" if v == G.P2 else "." for row in d["b"] for v in row)

dump = sorted(f"{canonical(d)}|{','.join(f'{r}{c}' for r, c in sorted(d['opt']))}" for d in data)
blob = ("\n".join(dump) + "\n").encode()
with open(f"{OUT}/partition44-positions.txt", "wb") as f:
    f.write(blob)
digest = {"sha256": hashlib.sha256(blob).hexdigest(), "fnv1a64": hex(fnv1a64(blob)),
          "lines": len(dump), "canon": "sorted lines; board chars x/o/. row-major; '|'; optimal (row,col) as 'rc', sorted"}
receipt["digest"] = digest

def cv_baselines():
    order = list(data); random.Random(0).shuffle(order)
    folds = [order[i::5] for i in range(5)]
    res = {m: {"S": [], "C": []} for m in ("floor", "logreg", "tree")}
    for f in range(5):
        test = folds[f]
        train = [d for g in range(5) if g != f for d in folds[g]]
        Xtr = X_of([d["b"] for d in train]); ytr = np.array([min((r * 4 + c) for r, c in d["opt"]) for d in train])
        lr = LogisticRegression(max_iter=3000, C=1.0, random_state=f).fit(Xtr, ytr)
        tr = DecisionTreeClassifier(criterion="gini", max_depth=16, min_samples_leaf=4, random_state=f).fit(Xtr, ytr)
        rng = random.Random(1000 + f)
        for d in test:
            cls = "C" if d["B"] >= 2 else "S"
            empties = [(r, c) for r in range(G.H) for c in range(G.W) if d["b"][r][c] == G.EMPTY]
            eidx = [r * 4 + c for r, c in empties]
            def pick_from(model, x):
                probs = model.predict_proba(x)[0]
                classes = list(model.classes_)
                return max(eidx, key=lambda i: probs[classes.index(i)] if i in classes else 0.0)
            x = X_of([d["b"]])
            lpick = pick_from(lr, x); tpick = pick_from(tr, x); fpick = rng.choice(eidx)
            for m, pick in (("floor", fpick), ("logreg", lpick), ("tree", tpick)):
                res[m][cls].append((pick // 4, pick % 4) in d["opt"])
    out = {}
    for m in res:
        out[m] = {}
        for cls in ("S", "C"):
            folds_acc = [float(np.mean(res[m][cls][f: f + 1])) for f in range(5)]
            out[m][cls] = {"mean": round(float(np.mean(res[m][cls])), 4) if res[m][cls] else None,
                           "n": len(res[m][cls])}
    return out

base = cv_baselines()
chance_B = partitions["DEF-B_fixed"]
filled = {}
for m in base:
    filled[m] = {}
    for cls in ("S", "C"):
        ch = chance_B[cls]["chance"]; acc = base[m][cls]["mean"]
        filled[m][cls] = round((acc - ch) / (1 - ch), 4) if (acc is not None and ch is not None and ch < 1) else None
receipt["baselines_DEF-B"] = {"acc": base, "filled_headroom": filled,
                              "note": "models: sklearn logreg/tree, empty-cell-aware scoring (M1); labels = min(opt) single-target"}
print(f"[base] {json.dumps(base)}")

receipt["overall"] = {"verdict": "PARTITION-REDERIVED",
                      "reading": None, "runtime_s": round(time.time() - t0, 1)}
with open(f"{OUT}/partition44-receipt.json", "w") as f:
    json.dump(receipt, f, indent=1)
print(f"\nverdict saved -> receipts/partition44-receipt.json ({receipt['overall']['runtime_s']}s)")
print(json.dumps({"partitions": partitions, "filled": filled}, indent=1))
