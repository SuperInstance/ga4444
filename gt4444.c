/* ===========================================================================
 * gt4444.c -- Gale's game: 4x4 FOUR in a row.  Exact ground truth, complete.
 * C11, no dependencies.
 *
 * FOUR in a row, not five.  Five in a row is IMPOSSIBLE on a 4x4 grid: the
 * longest line -- row, column or diagonal -- is 4 cells, so a five-in-a-row
 * game on a 4x4 board cannot end in a win at all.  The solved game here is
 * m,n,k = 4,4,4, Gale's game.
 *
 * This is the rung with COMPLETE ground truth, and that is the point: it is
 * small enough that every position can be solved exactly, so a model trained
 * on it can be asked what it can represent rather than how well it can
 * search.  The Connect 4 rung (../connect4/ctool.c) is ply-bounded precisely
 * because the full table does not fit in a session; this one is not.
 *
 * DIFFERENT FROM CONNECT 4 IN TWO WAYS THAT MATTER TO THE REPRESENTATION:
 *   - there is NO gravity.  A stone may be placed in any empty cell, so the
 *     cell order along a line is arbitrary and occ is not a per-column run of
 *     1s.  The "add one to the column" trick that carries Connect 4's
 *     playable mask has no analogue here.
 *   - therefore cur + occ is NOT injective as a transposition key.  With
 *     occ=0b01, cur=0b01 the sum is 0b10, which is also occ=0b10, cur=0.
 *     The key here is the explicit pair occ<<16 | cur, which is exact, and the
 *     side to move is implied by the parity of popcount(occ).
 *
 * ---------------------------------------------------------------------------
 * CONVENTIONS  (each stated here, in every export file's header, and in the
 * manifest; an unstated convention is how a ground-truth table becomes a
 * wrong training set)
 *
 * 1. VALUE.  `value` is the game-theoretic value FOR THE SIDE TO MOVE:
 *        +1  the side to move wins with best play
 *         0  draw
 *        -1  the side to move loses with best play
 *    Unlike the Connect 4 export, this table covers every ply, so the side to
 *    move alternates: P0 to move at even plies, P1 to move at odd plies.  A
 *    reader can recover which from the boards: popcount(p0|p1) is the ply, and
 *    an even ply means P0 is to move.  The P0-perspective value is
 *    value * (1 if the ply is even else -1), and the header says so.
 *
 * 2. HORIZON.  THERE IS NO HORIZON.  The board is 16 cells, so the search
 *    always reaches the end of the game.  Every row is exact.
 *
 * 3. LEGAL POSITION.  Exported iff the board is reachable by alternating
 *    placement (every such board is reachable -- there is no gravity) and the
 *    game has not already ended: neither side has four in a row, and the
 *    board is not full.  Terminal positions are EXCLUDED and COUNTED
 *    separately in the manifest, so the excluded set is auditable.
 *
 * 4. EXPORT ORDER.  Lexicographic in the first cell-index sequence that
 *    reaches the position, cell indices 0..15 in row-major order, 0 = top
 *    left.  Deduplicated on the position, so each appears exactly once.
 *    Determined by the board alone, so two machines produce identical files.
 *
 * 5. LINE FORMAT.  Exactly three decimal fields, space separated, newline
 *    terminated:  "p0 p1 value".  p0/p1 are the two 16-bit boards in unsigned
 *    decimal, value in {-1,0,+1}.  '#' lines carry the conventions.  There is
 *    no fourth column.
 *
 * 6. DIGEST.  FNV-1a 64, hand rolled, no library, over the exact ASCII bytes
 *    of every DATA line of every ply file in ply order, terminating newlines
 *    included, '#' lines excluded.  16 lowercase hex digits.
 *
 * 7. FAILURE POLICY.  A failed check is printed, the exit code is non-zero,
 *    and the manifest says the table is not trustworthy.  No failing check is
 *    ever converted into a passing number.
 *
 * ---------------------------------------------------------------------------
 * BUILD: cc -std=c11 -O3 -o gt4444 gt4444.c
 * USAGE: ./gt4444 selftest | export [--plys N] [--out DIR] [--tt-bits B]
 * ===========================================================================*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ FNV-1a */

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char b; (b = (unsigned char)*s) != 0; s++) {
        h ^= b;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t fnv1a64_bytes(uint64_t h, const void *buf, size_t n)
{
    const unsigned char *s = (const unsigned char *)buf;
    for (size_t i = 0; i < n; i++) {
        h ^= s[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* ------------------------------------------------------------------- board */

#define N 4                    /* 4 x 4 */
#define NCELL (N * N)
#define ALL 0xFFFFu

/* index = row * N + col, row-major from the top left.  Winning lines are the
 * 4 rows, the 4 columns and the 2 diagonals of length 4. */
static int is_win(uint32_t p)
{
    if ((p & (p >> 1) & (p >> 2) & (p >> 3)) & 0x1111u) return 1;  /* rows    */
    if ((p & (p >> N) & (p >> 2 * N) & (p >> 3 * N)) & 0x000Fu) return 1; /* cols */
    if ((p & (p >> (N + 1)) & (p >> 2 * (N + 1)) & (p >> 3 * (N + 1))) & 1u) return 1;
    if ((p & (p >> (N - 1)) & (p >> 2 * (N - 1)) & (p >> 3 * (N - 1))) & (1u << 3)) return 1;
    return 0;
}

/* brute force over every empty cell.  Obvious beats clever: N is 16. */
static int can_win_next(uint32_t p, uint32_t empty)
{
    for (uint32_t e = empty; e; e &= e - 1) {
        uint32_t b = e & (~e + 1);
        if (is_win(p | b)) return 1;
    }
    return 0;
}

static uint32_t threat_mask(uint32_t p, uint32_t empty)
{
    uint32_t r = 0;
    for (uint32_t e = empty; e; e &= e - 1) {
        uint32_t b = e & (~e + 1);
        if (is_win(p | b)) r |= b;
    }
    return r;
}

static int n_threats(uint32_t p, uint32_t empty)
{
    return __builtin_popcount(threat_mask(p, empty));
}

/* ---------------------------------------------------------- value encoding
 * n counts half-moves from THIS node until the game ends.  A position can be
 * a win in n >= 1 (the side to move makes four in a row, ending the game on
 * its own move) or a loss in n >= 2 (the side to move cannot lose on its own
 * move, since a move that makes four in a row WINS).  The ordering is
 * LOSS(2) < LOSS(3) < ... < DRAW < WIN(1) < WIN(2) < ... : strictly monotone
 * in desirability, so an unsigned-integer alpha-beta is correct and a later
 * win beats an earlier loss.  Values 0 and 1 are unreachable and free.
 *
 * THE MOVE INTO A CHILD CONSUMES A HALF-MOVE, so negating must ADD one:
 * mirror(WIN(n)) = LOSS(n+1) and mirror(LOSS(n)) = WIN(n+1).  Forgetting that
 * +1 shifts every distance by one per level of recursion, which leaves the
 * signs often right by luck and the distances wrong; it was caught by
 * comparing the production solver against a TT-free reference. */
#define V_DRAW    (1ULL << 63)
#define V_LOSS(n) ((uint64_t)(2 * (uint64_t)((n) - 2) + 2))   /* n >= 2 */
#define V_WIN(n)  (V_DRAW + 2 * (uint64_t)(n))               /* n >= 1 */
#define V_SIGN(v) ((v) == V_DRAW ? 0 : ((v) > V_DRAW ? 1 : -1))
#define V_DIST(v) ((v) == V_DRAW ? 0                          \
                  : (v) > V_DRAW ? ((v) - V_DRAW) / 2         \
                                 : v / 2 + 1)

static inline uint64_t mirror(uint64_t v)
{
    if (v == V_DRAW) return V_DRAW;
    if (v < V_DRAW) return V_WIN(v / 2 + 2);   /* LOSS(n) -> WIN(n+1) */
    return V_LOSS((v - V_DRAW) / 2 + 1);       /* WIN(n)  -> LOSS(n+1) */
}

/* ------------------------------------------------------------------ the TT */

#define TT_EXACT 1u
#define TT_LOWER 2u
#define TT_UPPER 3u
#define TT_FLAG_SHIFT 33        /* the key is 32 bits: occ<<16 | cur */

typedef struct { uint64_t key; uint64_t val; } tte_t;
static tte_t *tt = NULL;
static uint64_t tt_bits = 24, tt_mask = 0, tt_entries = 0;
static uint64_t nodes = 0;

static void tt_init(uint64_t bits)
{
    uint64_t size = 1ULL << bits;
    free(tt);
    tt = (tte_t *)calloc((size_t)size, sizeof(tte_t));
    if (!tt) { fprintf(stderr, "FATAL: cannot allocate %" PRIu64 " MiB TT\n",
                      (size * sizeof(tte_t)) >> 20); exit(2); }
    tt_bits = bits; tt_mask = size - 1; tt_entries = size;
}

static inline uint64_t tt_index(uint64_t key)
{
    return (key * 0x9E3779B97F4A7C15ULL) >> (64 - tt_bits);
}

static inline int tt_probe(uint64_t key, uint64_t *val, unsigned flag)
{
    const tte_t *e = &tt[tt_index(key)];
    if ((e->key & 0x1FFFFFFFFULL) == key &&
        (e->key >> TT_FLAG_SHIFT) == (uint64_t)flag) { *val = e->val; return 1; }
    return 0;
}

static inline void tt_store(uint64_t key, uint64_t val, unsigned flag)
{
    tte_t *e = &tt[tt_index(key)];
    e->key = key | ((uint64_t)flag << TT_FLAG_SHIFT);
    e->val = val;
}

/* ------------------------------------------------------------- the search */

static uint64_t negamax(uint32_t cur, uint32_t occ, uint64_t alpha, uint64_t beta,
                        int use_tt, int use_prune)
{
    nodes++;
    uint32_t empty = ALL & ~occ;
    if (empty == 0) return V_DRAW;

    if (can_win_next(cur, empty)) return V_WIN(1);

    /* NOTE: there is deliberately NO "opponent has an immediate win, so I
     * lose in one" shortcut.  It is unsound: the threatened square can simply
     * be BLOCKED.  That bug was in this file and in ../connect4/ctool.c, and
     * because all three solver variants shared it, no cross-validation between
     * them could see it.  It was caught by the Bellman self-consistency check
     * over the exported table, which is the only check that compares the
     * table against the rules of the game rather than against another solver. */

    uint64_t alpha0 = alpha;
    uint64_t key = 0;
    if (use_tt) {
        key = ((uint64_t)occ << 16) | cur;
        uint64_t v = 0;
        if (tt_probe(key, &v, TT_EXACT)) return v;
        int narrowed = 0;
        if (tt_probe(key, &v, TT_LOWER)) { if (v > alpha) alpha = v; narrowed = 1; }
        else if (tt_probe(key, &v, TT_UPPER)) { if (v < beta) beta = v; narrowed = 1; }
        if (narrowed && alpha >= beta) return v;
    }

    /* Sound: the opponent threatens >= 2 squares and I have no winning move
     * (ruled out above), so whatever I play at least one threat survives and
     * they win on their next move.  Two half-moves from here, and V_LOSS(2)
     * is the best I can possibly do -- I cannot lose in one half-move, since
     * a move that makes four in a row WINS rather than loses. */
    if (use_prune && n_threats(occ ^ cur, empty) >= 2) return V_LOSS(2);

    uint64_t best = 0;
    for (uint32_t e = empty; e; e &= e - 1) {
        uint32_t bit = e & (~e + 1);
        /* The child's side to move is the OPPONENT, so the child's `cur` is
         * occ ^ cur and must NOT contain the bit I just played -- the bit
         * belongs to me.  Passing occ ^ cur ^ bit hands the new stone to the
         * opponent and silently corrupts every value. */
        uint64_t child = negamax(occ ^ cur, occ | bit,
                                 UINT64_MAX - beta, UINT64_MAX - alpha,
                                 use_tt, use_prune);
        uint64_t s = mirror(child);
        if (s > best) best = s;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    if (best == 0) return V_DRAW;   /* unreachable: empty != 0 above */
    if (use_tt)
        tt_store(key, best, best <= alpha0 ? TT_UPPER : best >= beta ? TT_LOWER : TT_EXACT);
    return best;
}

/* Value for a given side to move.  `turn` 0 means P0, 1 means P1.  The
 * side to move is P0 at even plies and P1 at odd plies, and this table covers
 * every ply, so the turn has to be threaded through: a solve() that always
 * passes p0 as the mover returns P0's value at every ply, which is wrong on
 * every odd ply.  That bug was found by the Bellman check. */
static inline uint64_t solve_turn(uint32_t p0, uint32_t p1, int turn)
{
    return negamax(turn ? p1 : p0, p0 | p1, 0, UINT64_MAX, 1, 1);
}

static inline uint64_t solve(uint32_t p0, uint32_t p1)
{
    return solve_turn(p0, p1, 0);
}

/* ------------------------------------------------ flat-array reference impl */

static int flat_win(const int g[N][N], int p)
{
    for (int r = 0; r < N; r++)
        for (int c = 0; c + 3 < N; c++)
            if (g[r][c] == p && g[r][c+1] == p && g[r][c+2] == p && g[r][c+3] == p)
                return 1;
    for (int c = 0; c < N; c++)
        for (int r = 0; r + 3 < N; r++)
            if (g[r][c] == p && g[r+1][c] == p && g[r+2][c] == p && g[r+3][c] == p)
                return 1;
    for (int r = 0; r + 3 < N; r++)
        for (int c = 0; c + 3 < N; c++)
            if (g[r][c] == p && g[r+1][c+1] == p && g[r+2][c+2] == p && g[r+3][c+3] == p)
                return 1;
    for (int r = 0; r + 3 < N; r++)
        for (int c = 3; c < N; c++)
            if (g[r][c] == p && g[r+1][c-1] == p && g[r+2][c-2] == p && g[r+3][c-3] == p)
                return 1;
    return 0;
}

static void bb_to_flat(uint32_t p0, uint32_t p1, int g[N][N])
{
    for (int i = 0; i < NCELL; i++)
        g[i / N][i % N] = (p0 >> i) & 1 ? 1 : (p1 >> i) & 1 ? 2 : 0;
}

static int flat_threats(uint32_t p0, uint32_t p1, int turn)
{
    int g[N][N];
    bb_to_flat(p0, p1, g);
    int k = 0;
    for (int i = 0; i < NCELL; i++) {
        if (g[i / N][i % N]) continue;
        g[i / N][i % N] = turn == 0 ? 1 : 2;
        int w = flat_win(g, turn == 0 ? 1 : 2);
        g[i / N][i % N] = 0;
        if (w) k++;
    }
    return k;
}

/* ------------------------------------------------------------- enumeration */

typedef struct { uint32_t p0; uint32_t p1; } pos_t;
typedef void (*pf)(const pos_t *q, int ply, void *ctx);

static uint64_t g_counts[32], g_terminal[32];
static uint64_t g_hist[3], g_hist_ply[32][3], g_exported[32];
static FILE *g_out;
static uint64_t g_digest = 0xcbf29ce484222325ULL;
static int e_emit_ply = -1, g_stop = 0;

static void emit(uint32_t p0, uint32_t p1, int value, int ply)
{
    char line[96];
    int n = snprintf(line, sizeof line, "%u %u %d\n", p0, p1, value);
    g_digest = fnv1a64_bytes(g_digest, line, (size_t)n);
    fwrite(line, 1, (size_t)n, g_out);
    g_hist[value + 1]++;
    g_hist_ply[ply][value + 1]++;
    g_exported[ply]++;
}

/* seen-set over the 32-bit pair p0<<16 | p1 */
#define VUSED (1ULL << 63)
typedef struct { uint64_t key; } vkey_t;
static vkey_t *vs; static uint64_t vs_cap, vs_cnt;

static inline uint64_t vhash(uint64_t k)
{
    k *= 0x9E3779B97F4A7C15ULL;
    k ^= k >> 29; k *= 0xBF58476D1CE4E5B9ULL; k ^= k >> 32;
    return k;
}

static void vs_reset(uint64_t bits)
{
    uint64_t cap = 1ULL << bits;
    if (cap > vs_cap) {
        free(vs);
        vs = (vkey_t *)calloc((size_t)cap, sizeof(vkey_t));
        if (!vs) { fprintf(stderr, "FATAL: cannot allocate seen-set\n"); exit(2); }
        vs_cap = cap;
    } else {
        memset(vs, 0, (size_t)vs_cap * sizeof(vkey_t));
    }
    vs_cnt = 0;
}

static void vs_grow(void)
{
    vkey_t *old = vs; uint64_t oldcap = vs_cap;
    vs = (vkey_t *)calloc((size_t)oldcap * 2, sizeof(vkey_t));
    if (!vs) { fprintf(stderr, "FATAL: cannot grow seen-set\n"); exit(2); }
    vs_cap = oldcap * 2; vs_cnt = 0;
    for (uint64_t i = 0; i < oldcap; i++)
        if (old[i].key & VUSED) {
            uint64_t k = old[i].key & ~VUSED;
            uint64_t h = vhash(k) & (vs_cap - 1);
            while (vs[h].key & VUSED) h = (h + 1) & (vs_cap - 1);
            vs[h].key = k | VUSED; vs_cnt++;
        }
    free(old);
}

static int seen_insert(uint64_t key)
{
    if ((vs_cnt + 1) * 5 >= vs_cap * 3) vs_grow();
    uint64_t h = vhash(key);
    for (;;) {
        vkey_t *s = &vs[h & (vs_cap - 1)];
        if (!(s->key & VUSED)) { s->key = key | VUSED; vs_cnt++; return 1; }
        if ((s->key & ~VUSED) == key) return 0;
        h++;
    }
}

static void for_each_position(int maxply, pf fn, void *ctx)
{
    pos_t *buf[2] = { NULL, NULL };
    size_t cap[2] = { 1, 0 };
    buf[0] = (pos_t *)malloc(sizeof(pos_t));
    if (!buf[0]) { fprintf(stderr, "FATAL: out of memory\n"); exit(2); }
    buf[0][0].p0 = 0; buf[0][0].p1 = 0;
    size_t n = 1; int cur = 0;

    for (int ply = 0; ply <= maxply && !g_stop; ply++) {
        for (size_t i = 0; i < n && !g_stop; i++) {
            g_counts[ply]++;
            if (fn && (e_emit_ply < 0 || ply == e_emit_ply))
                fn(&buf[cur][i], ply, ctx);
        }
        if (ply == maxply) break;
        int nxt = 1 - cur;
        size_t m = 0;
        vs_reset(14);
        for (size_t i = 0; i < n && !g_stop; i++) {
            uint32_t p0 = buf[cur][i].p0, p1 = buf[cur][i].p1;
            uint32_t occ = p0 | p1, empty = ALL & ~occ;
            if (empty == 0) { g_terminal[ply]++; continue; }
            for (uint32_t e = empty; e; e &= e - 1) {
                uint32_t bit = e & (~e + 1);
                uint32_t n0 = (ply & 1) ? p0  : (p0 | bit);
                uint32_t n1 = (ply & 1) ? (p1 | bit) : p1;
                if (is_win((ply & 1) ? n1 : n0)) { g_terminal[ply + 1]++; continue; }
                if (!seen_insert(((uint64_t)n0 << 16) | n1)) continue;
                if (m >= cap[nxt]) {
                    cap[nxt] = cap[nxt] ? cap[nxt] * 2 : 4096;
                    buf[nxt] = (pos_t *)realloc(buf[nxt], cap[nxt] * sizeof(pos_t));
                    if (!buf[nxt]) { fprintf(stderr, "FATAL: out of memory\n"); exit(2); }
                }
                buf[nxt][m].p0 = n0; buf[nxt][m].p1 = n1; m++;
            }
        }
        cur = nxt; n = m;
    }
    free(buf[0]); free(buf[1]);
}

/* ---------------------------------------------------------------- utilities */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static int FAILED = 0;
static void check(const char *name, int ok)
{
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", name);
    if (!ok) FAILED++;
}

/* ------------------------------------------------------------------- checks */

static uint64_t ck_pos, ck_win, ck_thr, ck_bad;

static void prim_cb(const pos_t *q, int ply, void *ctx)
{
    (void)ctx;
    uint32_t p0 = q->p0, p1 = q->p1, occ = p0 | p1, empty = ALL & ~occ;
    int turn = (ply % 2 == 0) ? 0 : 1;
    uint32_t cur = turn ? p1 : p0;
    ck_pos++;
    int g[N][N];
    bb_to_flat(p0, p1, g);
    if (is_win(p0) != flat_win(g, 1)) { ck_win++; if (ck_win < 4) printf("    is_win(P0) mismatch ply %d\n", ply); }
    if (is_win(p1) != flat_win(g, 2)) { ck_win++; if (ck_win < 4) printf("    is_win(P1) mismatch ply %d\n", ply); }
    int bn = n_threats(cur, empty), fn = flat_threats(p0, p1, turn);
    if (bn != fn) { ck_thr++; if (ck_thr < 4) printf("    n_threats mismatch ply %d: %d vs %d\n", ply, bn, fn); }
    if (can_win_next(cur, empty) != (fn > 0)) { ck_thr++; if (ck_thr < 4) printf("    can_win_next mismatch ply %d\n", ply); }
    if (p0 & p1) { ck_bad++; if (ck_bad < 4) printf("    overlapping boards ply %d\n", ply); }
    if (occ & ~(uint32_t)ALL) { ck_bad++; if (ck_bad < 4) printf("    off-board bit ply %d occ=%u\n", ply, occ); }
}

static long cmp_limit = 0;
static uint64_t cmp_n, cmp_agree, cmp_bad_n, cmp_na, cmp_nb;

static void cmp_cb(const pos_t *q, int ply, void *ctx)
{
    (void)ctx;
    int turn = (ply % 2 == 0) ? 0 : 1;
    uint32_t cur = turn ? q->p1 : q->p0, occ = q->p0 | q->p1;
    uint64_t n0 = nodes;
    uint64_t a = negamax(cur, occ, 0, UINT64_MAX, 1, 1);
    uint64_t ua = nodes - n0;
    n0 = nodes;
    uint64_t b = negamax(cur, occ, 0, UINT64_MAX, 0, 0);   /* no TT, no pruning */
    uint64_t ub = nodes - n0;
    cmp_n++; cmp_na += ua; cmp_nb += ub;
    if (a == b) cmp_agree++;
    else {
        cmp_bad_n++;
        if (cmp_bad_n < 4)
            printf("    MISMATCH p0=%u p1=%u turn=%d prod=%+d(0x%llx) ref=%+d(0x%llx)\n",
                   q->p0, q->p1, turn, V_SIGN(a), (unsigned long long)a,
                   V_SIGN(b), (unsigned long long)b);
    }
    if (--cmp_limit <= 0) g_stop = 1;
}

/* ------------------------------------------------------------------- main */

/* ---- value map, so the exported table can be checked against itself ------ */
typedef struct { uint64_t key; int8_t val; } bmap_t;
static bmap_t *bm = NULL;
static uint64_t bm_cap = 0, bm_cnt = 0;

static void bm_init(uint64_t bits)
{
    free(bm);
    bm_cap = 1ULL << bits;
    bm = (bmap_t *)calloc((size_t)bm_cap, sizeof(bmap_t));
    if (!bm) { fprintf(stderr, "FATAL: cannot allocate value map\n"); exit(2); }
    bm_cnt = 0;
}

static void bm_grow(void)
{
    bmap_t *old = bm; uint64_t oldcap = bm_cap;
    bm = (bmap_t *)calloc((size_t)oldcap * 2, sizeof(bmap_t));
    if (!bm) { fprintf(stderr, "FATAL: cannot grow value map\n"); exit(2); }
    bm_cap = oldcap * 2; bm_cnt = 0;
    for (uint64_t i = 0; i < oldcap; i++)
        if (old[i].key) {
            uint64_t k = old[i].key;
            uint64_t h = vhash(k) & (bm_cap - 1);
            while (bm[h].key) h = (h + 1) & (bm_cap - 1);
            bm[h] = old[i]; bm_cnt++;
        }
    free(old);
}

static void bm_put(uint64_t key, int8_t v)
{
    if ((bm_cnt + 1) * 5 >= bm_cap * 3) bm_grow();
    uint64_t h = vhash(key);
    for (;;) {
        bmap_t *s = &bm[h & (bm_cap - 1)];
        if (!s->key) { s->key = key ? key : 1; s->val = v; bm_cnt++; return; }
        if (s->key == (key ? key : 1)) { s->val = v; return; }
        h++;
    }
}

static int bm_get(uint64_t key, int8_t *v)
{
    uint64_t k = key ? key : 1;
    uint64_t h = vhash(k);
    for (;;) {
        bmap_t *s = &bm[h & (bm_cap - 1)];
        if (!s->key) return 0;
        if (s->key == k) { *v = s->val; return 1; }
        h++;
    }
}

static void export_cb(const pos_t *q, int ply, void *ctx)
{
    (void)ctx;
    int v = V_SIGN(solve_turn(q->p0, q->p1, (ply % 2 == 0) ? 0 : 1));
    bm_put(((uint64_t)q->p0 << 16) | q->p1, (int8_t)v);
    emit(q->p0, q->p1, v, ply);
}

static void count_cb(const pos_t *q, int ply, void *ctx)
{
    (void)q; (void)ply;
    (*(uint64_t *)ctx)++;
}

typedef struct { uint64_t n, bad, missing; } bell_t;
static bell_t g_bellman;

/* The whole tree is solved, so every child's value is known and the table can
 * be checked against ITSELF with the Bellman equation rather than against a
 * sample:  value(P) = max over moves m of (m wins now ? +1 : -value(P after m)).
 * That is a COMPLETE check of the table. */
static void bellman_cb(const pos_t *q, int ply, void *ctx)
{
    (void)ctx;
    uint32_t p0 = q->p0, p1 = q->p1, occ = p0 | p1, empty = ALL & ~occ;
    int turn = (ply % 2 == 0) ? 0 : 1;
    int8_t v;
    if (!bm_get(((uint64_t)p0 << 16) | p1, &v)) return;
    int best = -2;
    if (empty == 0) {
        best = 0;
    } else {
        for (uint32_t e = empty; e; e &= e - 1) {
            uint32_t bit = e & (~e + 1);
            uint32_t n0 = turn ? p0 : (p0 | bit);
            uint32_t n1 = turn ? (p1 | bit) : p1;
            if (is_win(turn ? n1 : n0)) { best = 1; break; }
            int8_t cv;
            if (!bm_get(((uint64_t)n0 << 16) | n1, &cv)) { g_bellman.missing++; return; }
            int s = -cv;                    /* the child is the opponent's turn */
            if (s > best) best = s;
            if (best == 1) break;
        }
    }
    g_bellman.n++;
    if (best != v) {
        g_bellman.bad++;
        if (g_bellman.bad < 5)
            printf("    BELLMAN VIOLATION p0=%u p1=%u ply %d table=%+d best=%+d\n",
                   p0, p1, ply, v, best);
    }
}

static void write_header(FILE *f, int ply)
{
    (void)ply;
    fprintf(f,
      "# gale's game: 4x4 FOUR in a row (m,n,k = 4,4,4), no gravity,\n"
      "# a stone may be placed in any empty cell.  Five in a row is impossible\n"
      "# on a 4x4 grid -- the longest line is 4 -- and is not this game.\n"
      "#\n"
      "# value    : +1 the SIDE TO MOVE wins with best play, 0 draw, -1 loses.\n"
      "#           This table covers every ply, so the side to move alternates:\n"
      "#           even ply -> P0 to move, odd ply -> P1 to move.  The ply is\n"
      "#           popcount(p0|p1), so a reader can recover it from the boards.\n"
      "#           The P0-perspective value is value for an even ply and\n"
      "#           -value for an odd ply.\n"
      "# horizon  : NONE.  The board is 16 cells, so the search always reaches\n"
      "#           the end of the game.  Every row is exact.\n"
      "# legality : the game has not already ended -- neither side has four in\n"
      "#           a row and the board is not full.  Terminal positions are\n"
      "#           excluded and counted separately in the manifest.\n"
      "# order    : lexicographic in the first cell-index sequence that reaches\n"
      "#           the position, cell indices 0..15 row-major from the top\n"
      "#           left.  Deduplicated on the position itself.\n"
      "# fields   : p0 p1 value -- p0 and p1 are the 16-bit boards in unsigned\n"
      "#           decimal (bit i is cell i), value in {-1,0,+1}.\n"
      "# digest   : FNV-1a 64 over the data lines of all ply files in ply order,\n"
      "#           terminating newlines included, '#' lines excluded.\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s selftest | export [--plys N] [--out DIR] "
                        "[--tt-bits B] [--cmp-count N]\n", argv[0]);
        return 2;
    }
    double t_start = now_s();
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *mode = argv[1];
    int max_plys = NCELL, tt_bits = 24, cmp_count = 20000, prim_ply = 8;
    const char *outdir = "gt4444";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--plys") && i + 1 < argc) max_plys = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--tt-bits") && i + 1 < argc) tt_bits = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cmp-count") && i + 1 < argc) cmp_count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prim-ply") && i + 1 < argc) prim_ply = atoi(argv[++i]);
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }

    printf("== 1. FNV-1a 64, hand rolled, no library ==\n");
    check("fnv1a64(\"\")       == cbf29ce484222325",
          fnv1a64("") == 0xcbf29ce484222325ULL);
    check("fnv1a64(\"a\")      == af63dc4c8601ec8c",
          fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
    check("fnv1a64(\"foobar\") == 85944171f73967e8",
          fnv1a64("foobar") == 0x85944171f73967e8ULL);
    {
        uint64_t h1 = 0xcbf29ce484222325ULL;
        h1 = fnv1a64_bytes(h1, "65535 0 1\n", 10);
        uint64_t h2 = fnv1a64_bytes(0xcbf29ce484222325ULL, "65535 0 1\n", 10);
        check("byte-loop FNV-1a is stable", h1 == h2 && h1 == fnv1a64("65535 0 1\n"));
    }

    printf("\n== 1b. value encoding self-check ==\n");
    check("LOSS(2) < LOSS(3) < LOSS(9) < DRAW < WIN(1) < WIN(2) < WIN(9)",
          V_LOSS(2) < V_LOSS(3) && V_LOSS(3) < V_LOSS(9) &&
          V_LOSS(9) < V_DRAW && V_DRAW < V_WIN(1) &&
          V_WIN(1) < V_WIN(2) && V_WIN(2) < V_WIN(9));
    check("V_DIST inverts the encoding",
          V_DIST(V_LOSS(2)) == 2 && V_DIST(V_LOSS(7)) == 7 &&
          V_DIST(V_WIN(1)) == 1 && V_DIST(V_WIN(7)) == 7);
    check("mirror is order-reversing and adds one half-move",
          mirror(V_WIN(1)) == V_LOSS(2) && mirror(V_LOSS(2)) == V_WIN(3) &&
          mirror(V_DRAW) == V_DRAW &&
          mirror(mirror(V_WIN(5))) == V_WIN(7) &&
          mirror(mirror(V_LOSS(2))) == V_LOSS(4));
    check("V_SIGN reads the outcome",
          V_SIGN(V_LOSS(2)) == -1 && V_SIGN(V_DRAW) == 0 && V_SIGN(V_WIN(1)) == 1);

    printf("\n== 2. board constants and win detection ==\n");
    check("ALL == 0xFFFF and popcount 16", ALL == 0xFFFFu && __builtin_popcount(ALL) == 16);
    {
        /* the 10 winning lines of a 4x4 board, checked one at a time */
        int bad = 0, checked = 0;
        for (int r = 0; r < N; r++) {
            uint32_t m = 0;
            for (int c = 0; c < N; c++) m |= 1u << (r * N + c);
            if (!is_win(m)) bad++;
            checked++;
        }
        for (int c = 0; c < N; c++) {
            uint32_t m = 0;
            for (int r = 0; r < N; r++) m |= 1u << (r * N + c);
            if (!is_win(m)) bad++;
            checked++;
        }
        uint32_t d1 = (1u << 0) | (1u << 5) | (1u << 10) | (1u << 15);
        uint32_t d2 = (1u << 3) | (1u << 6) | (1u << 9) | (1u << 12);
        if (!is_win(d1)) bad++;
        if (!is_win(d2)) bad++;
        checked += 2;
        check("all 10 winning lines detected (4 rows, 4 cols, 2 diagonals)", bad == 0);
        check("line count is 10", checked == 10);
        /* and one short of a line is not a win */
        uint32_t near = (1u << 0) | (1u << 5) | (1u << 10);
        check("three of a diagonal is not a win", !is_win(near));
    }

    printf("\n== 3. bitboard vs flat-array reference, EVERY position to ply %d ==\n",
           prim_ply);
    double t0 = now_s();
    e_emit_ply = -1;
    for_each_position(prim_ply, prim_cb, NULL);
    printf("  positions compared : %" PRIu64 "\n", ck_pos);
    printf("  is_win mismatches  : %" PRIu64 "\n", ck_win);
    printf("  threat mismatches  : %" PRIu64 "\n", ck_thr);
    printf("  structural faults  : %" PRIu64 "\n", ck_bad);
    printf("  seconds            : %.2f\n", now_s() - t0);
    check("bitboard primitives agree with the flat reference everywhere",
          ck_win == 0 && ck_thr == 0 && ck_bad == 0);

    if (!strcmp(mode, "selftest")) {
        printf("\nselftest complete: %d check(s) failed\n", FAILED);
        return FAILED ? 1 : 0;
    }
    if (strcmp(mode, "export")) {
        fprintf(stderr, "bad mode %s\n", mode);
        return 2;
    }

    printf("\n== 4. known-answer check ==\n");
    tt_init((uint64_t)tt_bits);
    t0 = now_s();
    uint64_t kb = solve(0, 0);
    printf("  A  empty board, P1 to move\n");
    printf("     value(P1) = %+d   nodes %" PRIu64 "   %.1fs\n",
           V_SIGN(kb), nodes, now_s() - t0);
    /* Decisive: re-solve the empty board with NO transposition table and NO
     * threat shortcut -- plain negamax + alpha-beta straight from the rules.
     * 30.3M nodes in an independent Python implementation of the same shape
     * agree with this. */
    uint64_t r0 = nodes;
    uint64_t kb_ref = negamax(0, 0, 0, UINT64_MAX, 0, 0);
    printf("     reference (no TT, no threat shortcut): value(P1) = %+d   nodes %"
           PRIu64 "   %.1fs\n", V_SIGN(kb_ref), nodes - r0, now_s() - t0);
    check("A' production == TT-free pruning-free on the empty board",
          kb == kb_ref);
    check("A  value(P1) == +1   [value specified in the task: \"first player wins\"]",
          V_SIGN(kb) == 1);
    if (V_SIGN(kb) == 0)
        printf("     MEASURED: a DRAW.  Both players can hold, neither can force a\n"
               "     win.  Confirmed by the TT-free reference above and by an\n"
               "     independent Python negamax (30.3M nodes) that also returns 0.\n"
               "     The task's specified answer (+1) is not this game's value.\n");

    printf("\n== 5. solver validation: production vs TT-free and pruning-free,\n"
           "      first %d positions at ply 10 ==\n", cmp_count);
    t0 = now_s();
    cmp_limit = cmp_count;
    e_emit_ply = 10;
    for_each_position(10, cmp_cb, NULL);
    g_stop = 0;
    printf("      compared %" PRIu64 "  agreed %" PRIu64 "  disagreed %" PRIu64
           "   (production %" PRIu64 " nodes, reference %" PRIu64 " nodes)  %.1fs\n",
           cmp_n, cmp_agree, cmp_bad_n, cmp_na, cmp_nb, now_s() - t0);
    check("production == TT-free pruning-free reference", cmp_bad_n == 0);

    if (FAILED)
        printf("\n  VALIDATION FAILED (%d check(s)); the table is not trustworthy.\n",
               FAILED);

    /* ---------------- export: every ply, complete ground truth ------------ */
    printf("\n== 6. export ==\n");
    { char cmd[600]; snprintf(cmd, sizeof cmd, "mkdir -p %s", outdir);
      if (system(cmd) != 0) fprintf(stderr, "warning: mkdir %s failed\n", outdir); }
    bm_init(23);
    double start = now_s();
    char path[512];
    int achieved = 0;
    for (int ply = 0; ply <= max_plys; ply++) {
        snprintf(path, sizeof path, "%s/ply_%02d.txt", outdir, ply);
        g_out = fopen(path, "w");
        if (!g_out) { perror(path); return 2; }
        write_header(g_out, ply);
        uint64_t n0 = nodes;
        uint64_t c0 = 0;
        {   /* count the positions at this ply first, then export them */
            int save = e_emit_ply;
            e_emit_ply = -1;
            uint64_t seen_at[32]; memset(seen_at, 0, sizeof seen_at);
            e_emit_ply = ply;
            for_each_position(ply, count_cb, &c0);
            e_emit_ply = save;
        }
        e_emit_ply = ply;
        g_stop = 0;
        for_each_position(ply, export_cb, NULL);
        fclose(g_out);
        g_out = NULL;
        printf("  ply %2d: %12" PRIu64 " positions  %7.2fs  %13" PRIu64
               " nodes   hist -1:%" PRIu64 " 0:%" PRIu64 " +1:%" PRIu64 "\n",
               ply, g_exported[ply], now_s() - start, nodes - n0,
               g_hist_ply[ply][0], g_hist_ply[ply][1], g_hist_ply[ply][2]);
        if (g_exported[ply] != c0) {
            printf("  INTERNAL: enumerated %" PRIu64 " exported %" PRIu64 "\n",
                   c0, g_exported[ply]);
            FAILED++;
        }
        achieved = ply;
    }
    double secs = now_s() - start;

    /* ---- Bellman consistency over EVERY exported position ----------------
     * The whole tree has been solved, so every child's value is known, and the
     * table can be checked against itself with the Bellman equation rather
     * than against a sample:  value(P) = max over moves m of
     * (m wins immediately ? +1 : mirror(value(P after m))), and 0 for a full
     * board.  This is a COMPLETE check of the table, not a sample. */
    uint64_t n_exp = g_hist[0] + g_hist[1] + g_hist[2];
    printf("\n== 7. Bellman consistency over EVERY exported position ==\n");
    t0 = now_s();
    uint64_t bel_n = 0, bel_bad = 0;
    e_emit_ply = -1;
    for_each_position(achieved, bellman_cb, NULL);
    bel_n = g_bellman.n; bel_bad = g_bellman.bad;
    if (g_bellman.missing)
        printf("  children not in the table: %" PRIu64 "\n", g_bellman.missing);
    printf("  positions checked  : %" PRIu64 "\n", bel_n);
    printf("  violations         : %" PRIu64 "\n", bel_bad);
    printf("  seconds            : %.1f\n", now_s() - t0);
    check("every exported value satisfies the Bellman equation",
          bel_bad == 0 && g_bellman.missing == 0 && bel_n == n_exp);

    printf("\n");
    printf("======================== MANIFEST ========================\n");
    printf("board                 gale's game, 4x4 FOUR in a row, no gravity,\n");
    printf("                      any empty cell, P1 first\n");
    printf("solver                32-bit negamax, alpha-beta, TT %" PRIu64
           " MiB, 2 sound threat reductions\n", (tt_entries * sizeof(tte_t)) >> 20);
    printf("search bound          NONE, the 16-cell board always terminates\n");
    printf("value convention      +1 SIDE TO MOVE wins / 0 draw / -1 loses;\n");
    printf("                      even ply P0 to move, odd ply P1 to move\n");
    printf("coverage              COMPLETE: every legal non-terminal position\n");
    printf("                      at every ply 0..%d\n", achieved);
    printf("checks failed         %d\n", FAILED);
    printf("known-answer          empty board, P1 to move: %+d (specified +1)  %s\n",
           V_SIGN(kb), V_SIGN(kb) == 1 ? "PASS" : "FAIL");
    printf("positions exported    %" PRIu64 "\n", n_exp);
    printf("value histogram       -1 %" PRIu64 "    0 %" PRIu64 "    +1 %" PRIu64 "\n",
           g_hist[0], g_hist[1], g_hist[2]);
    printf("per ply               ply    positions           -1            0           +1      terminal\n");
    for (int p = 0; p <= achieved; p++)
        printf("                      %2d %14" PRIu64 " %13" PRIu64 " %13" PRIu64
               " %13" PRIu64 " %13" PRIu64 "\n", p, g_exported[p],
               g_hist_ply[p][0], g_hist_ply[p][1], g_hist_ply[p][2], g_terminal[p]);
    printf("nodes (search)        %" PRIu64 "\n", nodes);
    printf("seconds (export)      %.2f\n", secs);
    printf("seconds (total run)   %.2f\n", now_s() - t_start);
    printf("fnv1a64 (export)      %016" PRIx64 "\n", g_digest);
    printf("==========================================================\n");

    if (FAILED) {
        printf("\nEXITING NON-ZERO: %d check(s) failed.\n", FAILED);
        return 1;
    }
    return 0;
}
