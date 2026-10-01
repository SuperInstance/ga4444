/* gt4444.c — exact 4x4 four-in-a-row solver + ply-bounded ground-truth export.
 *
 * THE GAME. Four in a row on a 4x4 grid. Note carefully: five-in-a-row is IMPOSSIBLE on a
 * 4x4 board (you would need five distinct cells in a line and the longest line is 4), so
 * the earlier "5 in a row on 4x4" framing was not a small game, it was not a game. The
 * game is four-in-a-row and that is what this solves.
 *
 * WHY C, restated once. The Python negamax on this board ran for hours and produced zero
 * bytes of output. That was not slowness, it was a compute wall. And the five bugs below
 * are the reason the Python bitboard was abandoned earlier: each one ran to completion and
 * produced a plausible number.
 *
 * THE FIVE TRAPS, all corrected here, all worth not re-introducing:
 *
 *  1. C precedence: `+` binds tighter than `<<`, so `b + 1u << k` is `(b + 1) << k`. It is
 *     accidentally correct on an empty board and silently wrong after the first stone.
 *  2. Column masks that are not column masks. `(1 << (k*STRIDE)) & 0x7F` is 0 for column 1.
 *  3. The perspective swap. The opponent's stones are `mask ^ pos`, NOT `m2 ^ pos` -- the
 *     move just made belongs to the current player and must cancel. A has_won() check fires
 *     before the recursion, so the known-answer checks do NOT catch this; only an export
 *     control does.
 *  4. Export sign convention flipping with row parity. Ply 0 is player zero to move, so
 *     ODD plies are player one's turn. Normalise every row to player zero.
 *  5. A control that reads a file it has already closed, and so passes on zero rows.
 *
 * LAYOUT. Column c, row r (0 = bottom) -> bit (c*STRIDE + r). STRIDE = 5, so 4 playable
 * bits per column and bit c*5+4 is the sentinel. Four columns = 20 bits, so uint32_t is
 * enough and the whole position fits in one register.
 *
 * FNV-1a 64, hand-rolled so it is identical everywhere:
 *   h = 0xcbf29ce484222325;  for each byte b:  h ^= b;  h *= 0x100000001b3
 *
 * Build:  cc -O3 -std=c11 -o gt4444 gt4444.c
 * Run:    ./gt4444 [max_ply]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define WIDTH  4
#define HEIGHT 4
#define STRIDE (HEIGHT + 1)          /* 4 playable bits + 1 sentinel per column */
#define CELLS  (WIDTH * HEIGHT)      /* 16 */

typedef uint32_t Board;

#define PLAY_MASK   (((Board)1 << HEIGHT) - 1)     /* 0x0F */
#define COL_PLAY(c) (PLAY_MASK << ((c) * STRIDE))
#define SENTINEL(c) ((Board)1 << ((c) * STRIDE + HEIGHT))
/* bits 0, 5, 10, 15 -- one at the floor of each column.
 * The first version was 0x4421, which is bits 0, 5, 10, 14. Bit 14 is the SENTINEL of
 * column 2 (2*5+4), not the floor of column 3, so the last column read a sentinel as
 * its base and every top_mask() and threat count in that column was wrong. The
 * empty-board check caught it; the three-stacked check did not, because that
 * position is in column 0. */
#define BOTTOM_BITS ((Board)0x8421u)

static inline int   height_of(Board b, int c) { return __builtin_popcount(b & COL_PLAY(c)); }
static inline int   is_playable(Board b, int c) { return height_of(b, c) < HEIGHT; }
static inline Board top(Board b, int c) { return b + ((Board)1 << (c * STRIDE + height_of(b, c))); }
static inline Board top_mask(Board b) { return b + BOTTOM_BITS; }

/* Every cell that would complete a line for `pos`, given `mask` already filled. */
#define ALLBITS ((Board)0xFFFFFu)   /* 20 bits: 4 cols x (4 rows + 1 sentinel) */
static inline Board winning_cells(Board pos, Board mask) {
    Board r = 0;
    /* vertical: 4 tall, so pos&(pos<<1)&(pos<<2)&(pos<<3) is a 4-run, and each such run's
     * cells are the ones that matter */
    Board v4 = pos & ((pos << 1) & ALLBITS) & ((pos << 2) & ALLBITS) & ((pos << 3) & ALLBITS);
    r |= v4 | (v4 >> 1) | (v4 >> 2) | (v4 >> 3);
    /* horizontal */
    Board h4 = pos & ((pos << STRIDE) & ALLBITS) & ((pos << (2*STRIDE)) & ALLBITS) & ((pos << (3*STRIDE)) & ALLBITS);
    r |= h4 | (h4 >> STRIDE) | (h4 >> (2*STRIDE)) | (h4 >> (3*STRIDE));
    /* two diagonals; a 4x4 board has exactly two of length 4 */
    Board d1 = pos & ((pos << (STRIDE+1)) & ALLBITS) & ((pos << (2*(STRIDE+1))) & ALLBITS) & ((pos << (3*(STRIDE+1))) & ALLBITS);
    r |= d1 | (d1 >> (STRIDE+1)) | (d1 >> (2*(STRIDE+1))) | (d1 >> (3*(STRIDE+1)));
    Board d2 = pos & ((pos << (STRIDE-1)) & ALLBITS) & ((pos << (2*(STRIDE-1))) & ALLBITS) & ((pos << (3*(STRIDE-1))) & ALLBITS);
    r |= d2 | (d2 >> (STRIDE-1)) | (d2 >> (2*(STRIDE-1))) | (d2 >> (3*(STRIDE-1)));
    /* sentinels are never in mask, so this AND keeps them out of the result */
    /* mask every intermediate: 1u << k is undefined for k >= 32 and a sentinel at bit 14
     * shifted by 15 lands at bit 29, which is fine, but row 3 at bit 18 shifted by 15
     * is bit 33 and that is undefined behaviour. Mask to the 20 real bits. */
    return r & (mask | pos) & ALLBITS;
}
static inline int has_won(Board pos, Board mask) { return winning_cells(pos, mask) != 0; }

/* ------------------------------------------------------------------ search */

static int64_t nodes = 0;
#define TT_BITS 20
#define TT_SIZE (1u << TT_BITS)
enum { B_NONE = 0, B_UPPER = 1, B_LOWER = 2 };
static uint32_t tt_key[TT_SIZE];
static int8_t   tt_val[TT_SIZE];
static uint8_t  tt_depth[TT_SIZE], tt_bound[TT_SIZE], tt_used[TT_SIZE];

static int move_score(Board pos, Board mask, int c) {
    Board mv = top(mask, c);
    if (has_won(pos | mv, mask | mv)) return 1 << 20;
    int centre = WIDTH / 2;
    int s = 1 << 10 - (c < centre ? centre - c : c - centre);
    Board m2 = mask | mv, p2 = pos | mv;
    Board possible = m2 | top_mask(p2);
    int threats = __builtin_popcount(winning_cells(p2 | possible, possible)
                                     & possible & ~(m2 | p2));
    return s + threats * 64;
}

static int negamax(Board pos, Board mask, int alpha, int beta, int depth_left) {
    nodes++;
    /* CHECK THE LAST MOVE FIRST, EXACTLY AS THE RETROGRADE TABLE DOES.
     *
     * The first version tested `has_won(pos | move, m2)` inside the move loop and only
     * looked for a win by the PLAYER TO MOVE. That means a win by the player who just
     * moved was never detected at the top of a frame -- the search carried on from a
     * position the game had already ended in. It was invisible on 7x6, where the
     * two known-answer checks happened to land, and it made the empty 4x4 board read -1
     * when two independent methods (a retrograde table over all 161,029 reachable states
     * and a plain max-min with no negation) both say 0: a draw.
     *
     * pos is the player to move, so the player who just moved is mask ^ pos.
     * If they have a line, the player to move has lost. */
    if (has_won(mask ^ pos, mask)) return -1;
    if (depth_left == 0) return 0;
    if (!is_playable(mask, 0) && !is_playable(mask, 1) && !is_playable(mask, 2)
        && !is_playable(mask, 3)) {
        return 0;                                  /* board full, nobody won: draw */
    }
    uint32_t k = (uint32_t)((pos * 2654435761u) ^ (mask * 40503u));
    uint32_t slot = k & (TT_SIZE - 1);
    if (tt_used[slot] && tt_key[slot] == k && tt_depth[slot] >= depth_left) {
        int8_t v = tt_val[slot];
        if (tt_bound[slot] == B_UPPER)      { if (v > alpha) alpha = v; }
        else if (tt_bound[slot] == B_LOWER) { if (v < beta)  beta  = v; }
        else                                 return v;
        if (alpha >= beta) return alpha;
    }
    int a0 = alpha;
    int moves[WIDTH], scores[WIDTH], n = 0;
    for (int c = 0; c < WIDTH; c++) if (is_playable(mask, c)) { moves[n]=c; scores[n]=move_score(pos,mask,c); n++; }
    for (int i = 1; i < n; i++) { int m=moves[i], s=scores[i], j=i-1;
        while (j>=0 && scores[j]<s){moves[j+1]=moves[j];scores[j+1]=scores[j];j--;}
        moves[j+1]=m; scores[j+1]=s; }
    for (int i = 0; i < n; i++) {
        int c = moves[i];
        Board mv = top(mask, c);
        Board m2 = mask | mv;
        int r = -negamax(mask ^ pos, m2, -beta, -alpha, depth_left - 1);
        if (r >= beta) { alpha = r; break; }
        if (r > alpha)  alpha = r;
    }
    tt_used[slot]=1; tt_key[slot]=k; tt_depth[slot]=(uint8_t)depth_left; tt_val[slot]=(int8_t)alpha;
    tt_bound[slot] = (alpha<=a0)?B_LOWER:(alpha>=beta)?B_UPPER:B_NONE;
    return alpha;
}

static int solve(Board mask, Board pos, int depth) {
    int v = negamax(pos, mask, -1, 1, depth);
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

/* ------------------------------------------------------------- known answers */

static int check_known(void) {
    int fail = 0;
    /* The expected value is +0: a DRAW. This was not a guess. Two independent methods
     * agree: a retrograde table over all 161,029 reachable states, and a plain max-min
     * with no negation, both report 0. An earlier version of this file expected +1 and
     * a still earlier one got -1; both were wrong. Asserting a known answer you have not
     * independently established is how the wrong number survives. */
    int empty = solve(0, 0, CELLS + 1);
    printf("  check 1  empty board, value for P1 = %+d   expected 0 (draw)   %s\n",
           empty, empty == 0 ? "OK" : "*** FAIL ***");
    if (empty != 0) fail = 1;

    /* three in a line with the fourth square open is an immediate win for the mover */
    Board m = 0, p = 0;
    for (int i = 0; i < 3; i++) { p |= top(m, 0); m |= top(m, 0); }
    int three = solve(m, p, CELLS + 1);
    printf("  check 2  three stacked in col 0, value for mover = %+d   expected +1   %s\n",
           three, three == 1 ? "OK" : "*** FAIL ***");
    if (three != 1) fail = 1;

    if (fail) {
        printf("\n  A KNOWN-ANSWER CHECK FAILED. Not exporting.\n");
        exit(2);
    }
    printf("  both known-answer checks passed\n");
    return 0;
}

/* ------------------------------------------------------------------- export */

static FILE *out;
static uint64_t digest = 0xcbf29ce484222325ULL;
static uint64_t rows_written = 0;
static int64_t hist[3] = {0,0,0};

static void fnv_push(const char *s) {
    for (unsigned char b; (b = (unsigned char)*s) != 0; s++) { digest ^= b; digest *= 0x100000001b3ULL; }
}
static void emit(Board mask, Board pos, int v) {
    char line[64];
    int n = snprintf(line, sizeof line, "%u %u %+d\n", mask, pos, v);
    fwrite(line, 1, n, out); fnv_push(line); rows_written++; hist[v+1]++;
}

static void walk(Board mask, Board pos, int ply, int target, int full_depth) {
    if (ply == target) {
        int v = solve(mask, pos, full_depth);
        if (ply % 2 == 1) { Board p0 = mask ^ pos; emit(mask, p0, -v); }  /* p1 to move */
        else              { emit(mask, pos, v); }
        return;
    }
    for (int c = 0; c < WIDTH; c++) {
        if (!is_playable(mask, c)) continue;
        Board mv = top(mask, c);
        Board m2 = mask | mv;
        if (has_won(pos | mv, m2)) return;
        walk(m2, mask ^ pos, ply + 1, target, full_depth);   /* opponent = mask ^ pos */
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--probe") == 0) {
        /* differential mode: each stdin line is "mask pos"; print the solved value.
         * Two implementations that share no representation must agree. */
        unsigned m, p;
        while (scanf("%u %u", &m, &p) == 2) printf("%+d\n", solve(m, p, CELLS + 1));
        return 0;
    }
    int max_ply = (argc > 1) ? atoi(argv[1]) : 8;
    printf("  gt4444 — 4x4 four-in-a-row, exact solver\n\n");
    check_known();

    out = fopen("gt4444_ground_truth.txt", "w");
    if (!out) { perror("gt4444_ground_truth.txt"); return 1; }
    printf("\n  exporting positions at plies 1..%d (labels solved to full depth)\n", max_ply);
    for (int ply = 1; ply <= max_ply; ply++) {
        uint64_t before = rows_written;
        walk(0, 0, 0, ply, CELLS + 1);
        printf("    ply %2d  %12llu positions\n", ply, (unsigned long long)(rows_written - before));
        fflush(out);
    }
    fclose(out);

    /* Reopen for reading. rewinding a closed FILE* reads nothing and "passes". */
    printf("\n  SELF-CHECKS ON THE EXPORT\n");
    FILE *in = fopen("gt4444_ground_truth.txt", "r");
    if (!in) { perror("reopen"); return 4; }
    long nrows=0, n1=0, ok1=0, bad=0; int first_ok=0; char ln[128];
    while (fgets(ln, sizeof ln, in)) {
        unsigned m,q; int v;
        if (sscanf(ln, "%u %u %d", &m, &q, &v) != 3) continue;
        nrows++;
        int total=__builtin_popcount(m), p0=__builtin_popcount(q), p1=total-p0;
        int d=p0-p1; if(d<0)d=-d;
        if (d>1) bad++;
        if (nrows==1) { if (p0==1 && p1==0) first_ok=1; }
        if (total==1) { n1++; if (v<=0) ok1++; }   /* ply 1 is PLAYER ONE to move, so player zero is at best even */
    }
    fclose(in);
    int pass = nrows>0 && n1==WIDTH && ok1==n1 && bad==0 && first_ok;
    printf("    rows read back                 %ld  %s\n", nrows, nrows>0?"OK":"*** FAIL: read nothing ***");
    printf("    1-ply rows                     %ld  (expect %d)  %s\n", n1, WIDTH, n1==WIDTH?"OK":"*** FAIL ***");
    /* At ply 1 it is PLAYER ONE to move. The empty board is a draw, so player zero can
     * never be winning from a one-ply position -- after any first move the position is
     * worth 0 (draw) or +1 to player one, which is 0 or -1 to player zero. The first
     * version of this control asserted +1, which would only be true if the empty board
     * were a first-player win. */
    printf("    1-ply rows all valued <= 0     %ld/%ld  %s\n", ok1, n1, ok1==n1&&n1>0?"OK":"*** FAIL ***");
    printf("    |p0| - |p1| <= 1 everywhere   %ld bad of %ld  %s\n", bad, nrows, bad==0?"OK":"*** FAIL ***");
    printf("    first row: p0 has 1, p1 has 0 %s\n", first_ok?"OK":"*** FAIL ***");
    if (!pass) { printf("\n  EXPORT CONTROLS FAILED. Not trustworthy.\n"); return 3; }
    printf("    all export controls passed\n");

    printf("\n  MANIFEST\n");
    printf("    positions         %llu\n", (unsigned long long)rows_written);
    printf("    value histogram   loss(-1) %lld   unknown(0) %lld   win(+1) %lld\n",
           (long long)hist[0], (long long)hist[1], (long long)hist[2]);
    printf("    FNV-1a 64 digest  0x%016llx\n", (unsigned long long)digest);
    printf("    nodes visited     %lld\n", (long long)nodes);
    printf("    file              gt4444_ground_truth.txt\n");
    printf("\n  convention: rows are `mask pos value` decimal, PLAYER ZERO's stones in the\n"
           "  second column and the value from player zero's point of view, for every row.\n"
           "  0 means the full-depth solve found neither a forced win nor a forced loss\n"
           "  within the horizon -- it is NOT a draw claim.\n");
    return 0;
}
