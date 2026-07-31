/* ionity_qr.c — minimal QR generator for the EDGE-VIEW broadcast link.
 *
 * Fixed profile: Version 2 (25x25), ECC level L (34 data + 10 ECC codewords,
 * single block), byte mode, mask pattern 0. Follows ISO/IEC 18004; layout
 * logic mirrors the public-domain reference algorithm (Project Nayuki).
 */
#include "ionity_qr.h"
#include "ionity_pixels.h"
#include "GUI_Paint.h"
#include <string.h>

#define QR_N        IONITY_QR_SIZE   /* 25 */
#define DATA_CW     34
#define ECC_CW      10
#define TOTAL_CW    (DATA_CW + ECC_CW)

/* ---- GF(256) arithmetic, poly 0x11D ---- */
static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static bool gf_ready = false;

static void gf_init(void) {
    if (gf_ready) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_ready = true;
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    if (!a || !b) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

/* Reed-Solomon: compute ECC_CW remainder bytes of data. */
static void rs_compute(const uint8_t *data, int len, uint8_t *ecc) {
    uint8_t gen[ECC_CW + 1];
    gf_init();
    /* Build generator polynomial (x-α^0)...(x-α^(ECC_CW-1)). */
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    for (int i = 0; i < ECC_CW; i++) {
        for (int j = i + 1; j > 0; j--)
            gen[j] = gen[j - 1] ^ gf_mul(gen[j], gf_exp[i]);
        gen[0] = gf_mul(gen[0], gf_exp[i]);
    }
    memset(ecc, 0, ECC_CW);
    for (int i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ ecc[0];
        memmove(ecc, ecc + 1, ECC_CW - 1);
        ecc[ECC_CW - 1] = 0;
        for (int j = 0; j < ECC_CW; j++)
            ecc[j] ^= gf_mul(gen[ECC_CW - 1 - j], factor);
    }
}

/* ---- Module matrix helpers ---- */
static uint8_t is_func[QR_N][QR_N];

static void set_func(uint8_t m[QR_N][QR_N], int x, int y, bool dark) {
    if (x < 0 || x >= QR_N || y < 0 || y >= QR_N) return;
    m[y][x] = dark ? 1 : 0;
    is_func[y][x] = 1;
}

static void draw_finder(uint8_t m[QR_N][QR_N], int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                       ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < QR_N && y >= 0 && y < QR_N)
                set_func(m, x, y, dist != 2 && dist != 4);
        }
}

static void draw_alignment(uint8_t m[QR_N][QR_N], int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                       ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            set_func(m, cx + dx, cy + dy, dist != 1);
        }
}

static void draw_format_bits(uint8_t m[QR_N][QR_N]) {
    /* ECC L (01), mask 0 (000): data=01000b, BCH-protected. */
    int data = 8;
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;   /* = 0x77C4 */

    for (int i = 0; i <= 5; i++) set_func(m, 8, i, (bits >> i) & 1);
    set_func(m, 8, 7, (bits >> 6) & 1);
    set_func(m, 8, 8, (bits >> 7) & 1);
    set_func(m, 7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) set_func(m, 14 - i, 8, (bits >> i) & 1);

    for (int i = 0; i < 8; i++)  set_func(m, QR_N - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) set_func(m, 8, QR_N - 15 + i, (bits >> i) & 1);
    set_func(m, 8, QR_N - 8, true);             /* dark module */
}

bool ionity_qr_encode(const char *text, uint8_t out[QR_N][QR_N]) {
    size_t len = strlen(text);
    if (len > IONITY_QR_MAX_TEXT) return false;

    /* ---- Build codewords: byte mode ---- */
    uint8_t cw[TOTAL_CW];
    memset(cw, 0, sizeof(cw));
    int bitpos = 0;
    /* append_bits helper (MSB first) */
    #define APPEND(val, n) do { \
        for (int b = (n) - 1; b >= 0; b--, bitpos++) \
            if (((val) >> b) & 1) cw[bitpos >> 3] |= 0x80 >> (bitpos & 7); \
    } while (0)
    APPEND(4, 4);                    /* byte mode */
    APPEND((int)len, 8);             /* char count (v1-9) */
    for (size_t i = 0; i < len; i++) APPEND((int)(uint8_t)text[i], 8);
    int term = DATA_CW * 8 - bitpos; if (term > 4) term = 4;
    APPEND(0, term);
    if (bitpos & 7) APPEND(0, 8 - (bitpos & 7));
    #undef APPEND
    for (int i = bitpos / 8, alt = 0; i < DATA_CW; i++, alt ^= 1)
        cw[i] = alt ? 0x11 : 0xEC;

    rs_compute(cw, DATA_CW, cw + DATA_CW);

    /* ---- Function patterns ---- */
    memset(out, 0, QR_N * QR_N);
    memset(is_func, 0, sizeof(is_func));
    /* timing */
    for (int i = 0; i < QR_N; i++) {
        set_func(out, 6, i, i % 2 == 0);
        set_func(out, i, 6, i % 2 == 0);
    }
    draw_finder(out, 3, 3);
    draw_finder(out, QR_N - 4, 3);
    draw_finder(out, 3, QR_N - 4);
    draw_alignment(out, QR_N - 7, QR_N - 7);    /* (18,18) */
    draw_format_bits(out);

    /* ---- Zigzag data placement ---- */
    int i = 0, total_bits = TOTAL_CW * 8;
    for (int right = QR_N - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < QR_N; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? QR_N - 1 - vert : vert;
                if (!is_func[y][x] && i < total_bits) {
                    out[y][x] = (cw[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
        }
    }

    /* ---- Mask 0: invert where (x+y) even, non-function only ---- */
    for (int y = 0; y < QR_N; y++)
        for (int x = 0; x < QR_N; x++)
            if (!is_func[y][x] && ((x + y) & 1) == 0)
                out[y][x] ^= 1;

    return true;
}

bool ionity_qr_draw(const char *text, uint16_t x, uint16_t y, uint8_t scale) {
    static uint8_t m[QR_N][QR_N];
    if (!ionity_qr_encode(text, m)) return false;
    if (scale == 0) scale = 1;
    int quiet = 2 * scale;
    int span = QR_N * scale + 2 * quiet;
    ionity_fill_rect_fast(x, y, x + span - 1, y + span - 1, 0x7 /* white */);
    for (int r = 0; r < QR_N; r++)
        for (int c = 0; c < QR_N; c++)
            if (m[r][c])
                ionity_fill_rect_fast(x + quiet + c * scale, y + quiet + r * scale,
                                      x + quiet + c * scale + scale - 1,
                                      y + quiet + r * scale + scale - 1, 0x0);
    return true;
}
