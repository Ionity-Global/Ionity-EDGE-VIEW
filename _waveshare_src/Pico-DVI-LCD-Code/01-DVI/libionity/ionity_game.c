/* ionity_game.c — self-playing attract-mode games for IO-nity EDGE-VIEW.
 *
 * "Local AI" that populates the screen: BFS-pathfinding autopilots play
 * Pac-Man (4 ghost personalities, scatter/chase/frightened) or Snake
 * (food-seek with tail-chase safety) entirely on the RP2350. Mode is
 * switchable at runtime via the Studio app ("mode" stream command).
 */
#include "ionity_game.h"
#include "ionity_pixels.h"
#include "GUI_Paint.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define C_BLACK   0x0
#define C_BLUE    0x1
#define C_GREEN   0x2
#define C_CYAN    0x3
#define C_RED     0x4
#define C_MAGENTA 0x5
#define C_YELLOW  0x6
#define C_WHITE   0x7

/* ---- Play area ---- */
static uint16_t ax, ay, aw, ah;
static ionity_game_mode_t mode = IONITY_GAME_PACMAN;
static uint32_t score = 0;

/* ================= PAC-MAN ================= */

#define PM_W 28
#define PM_H 15
static const char *pm_maze[PM_H] = {
    "############################",
    "#o........#......#........o#",
    "#.######.#.######.#.######.#",
    "#..........................#",
    "####.##.####.##.####.##.####",
    "#......##....##....##......#",
    "#.####.##.##.##.##.##.####.#",
    "#........#..HHHH..#........#",
    "#.####.##.##.##.##.##.####.#",
    "#......##....##....##......#",
    "####.##.####.##.####.##.####",
    "#..........................#",
    "#.######.#.######.#.######.#",
    "#o........#......#........o#",
    "############################",
};

static uint8_t pm_cell[PM_H][PM_W];   /* 0 empty, 1 wall, 2 pellet, 3 power */
static int pm_ts;                     /* tile size px */
static int pm_ox, pm_oy;              /* pixel origin of maze */
static int pm_pellets;
static int pm_lives;
static int pm_level;
static uint32_t pm_frame;
static int fright_timer;              /* frames of frightened mode */

typedef struct {
    int px, py;        /* pixel pos relative to maze origin */
    int dir;           /* 0=R 1=D 2=L 3=U, -1 idle */
    int home_tx, home_ty;
    int respawn;       /* frames until ghost leaves home */
    uint8_t color;
} pm_actor_t;

static pm_actor_t pac;
static pm_actor_t ghosts[4];
static const int DX[4] = {1, 0, -1, 0};
static const int DY[4] = {0, 1, 0, -1};

static inline int pm_tx(const pm_actor_t *a) { return (a->px + pm_ts / 2) / pm_ts; }
static inline int pm_ty(const pm_actor_t *a) { return (a->py + pm_ts / 2) / pm_ts; }
static inline bool pm_wall(int tx, int ty) {
    if (tx < 0 || tx >= PM_W || ty < 0 || ty >= PM_H) return true;
    return pm_cell[ty][tx] == 1;
}
static inline bool pm_aligned(const pm_actor_t *a) {
    return (a->px % pm_ts) == 0 && (a->py % pm_ts) == 0;
}

static void pm_reset_actors(void) {
    pac.px = 13 * pm_ts; pac.py = 11 * pm_ts; pac.dir = 0; pac.respawn = 0;
    static const uint8_t gcol[4] = {C_RED, C_MAGENTA, C_CYAN, C_GREEN};
    for (int i = 0; i < 4; i++) {
        ghosts[i].px = (12 + i) * pm_ts;
        ghosts[i].py = 7 * pm_ts;
        ghosts[i].dir = (i & 1) ? 0 : 2;
        ghosts[i].color = gcol[i];
        ghosts[i].respawn = i * 90;          /* staggered release */
        ghosts[i].home_tx = (i & 1) ? PM_W - 2 : 1;
        ghosts[i].home_ty = (i & 2) ? PM_H - 2 : 1;
    }
    fright_timer = 0;
}

static void pm_reset_maze(void) {
    pm_pellets = 0;
    for (int y = 0; y < PM_H; y++) {
        for (int x = 0; x < PM_W; x++) {
            char c = pm_maze[y][x];
            uint8_t v = 0;
            if (c == '#') v = 1;
            else if (c == '.') { v = 2; pm_pellets++; }
            else if (c == 'o') { v = 3; pm_pellets++; }
            pm_cell[y][x] = v;
        }
    }
    pm_reset_actors();
}

static void pm_reset_game(void) {
    score = 0;
    pm_lives = 3;
    pm_level = 1;
    pm_frame = 0;
    pm_ts = aw / PM_W < ah / PM_H ? aw / PM_W : ah / PM_H;
    if (pm_ts & 1) pm_ts--;                   /* even tile => 2px steps stay aligned */
    if (pm_ts < 6) pm_ts = 6;
    pm_ox = ax + (aw - PM_W * pm_ts) / 2;
    pm_oy = ay + (ah - PM_H * pm_ts) / 2;
    pm_reset_maze();
}

/* BFS over maze tiles. blocked[] marks extra no-go tiles (near ghosts).
 * Returns first-step direction from (sx,sy) toward nearest goal, or -1. */
static int8_t bfs_dist[PM_H][PM_W];
static uint16_t bfs_queue[PM_W * PM_H];

static int pm_bfs_dir(int sx, int sy, const uint8_t blocked[PM_H][PM_W],
                      bool (*is_goal)(int, int)) {
    memset(bfs_dist, -1, sizeof(bfs_dist));
    int qh = 0, qt = 0;
    bfs_queue[qt++] = (uint16_t)(sy * PM_W + sx);
    bfs_dist[sy][sx] = 0;
    int goal = -1;
    while (qh < qt) {
        int cur = bfs_queue[qh++];
        int cx = cur % PM_W, cy = cur / PM_W;
        if (is_goal(cx, cy) && !(cx == sx && cy == sy)) { goal = cur; break; }
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (pm_wall(nx, ny) || bfs_dist[ny][nx] >= 0) continue;
            if (blocked && blocked[ny][nx] && !(is_goal(nx, ny))) continue;
            bfs_dist[ny][nx] = (int8_t)(bfs_dist[cy][cx] + 1);
            bfs_queue[qt++] = (uint16_t)(ny * PM_W + nx);
        }
    }
    if (goal < 0) return -1;
    /* Walk back from goal to the step adjacent to start. */
    int cx = goal % PM_W, cy = goal / PM_W;
    while (bfs_dist[cy][cx] > 1) {
        bool found_parent = false;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx >= 0 && nx < PM_W && ny >= 0 && ny < PM_H &&
                bfs_dist[ny][nx] == bfs_dist[cy][cx] - 1) {
                cx = nx; cy = ny; found_parent = true; break;
            }
        }
        if (!found_parent) return -1;
    }
    for (int d = 0; d < 4; d++)
        if (sx + DX[d] == cx && sy + DY[d] == cy) return d;
    return -1;
}

static bool pm_goal_pellet(int x, int y) { return pm_cell[y][x] == 2 || pm_cell[y][x] == 3; }
static bool pm_goal_fright_ghost(int x, int y) {
    if (fright_timer <= 0) return false;
    for (int i = 0; i < 4; i++)
        if (ghosts[i].respawn == 0 &&
            pm_tx(&ghosts[i]) == x && pm_ty(&ghosts[i]) == y) return true;
    return false;
}

static uint8_t pm_blocked[PM_H][PM_W];

static void pm_pick_pac_dir(void) {
    int sx = pm_tx(&pac), sy = pm_ty(&pac);
    /* Danger map: tiles within 2 of an active, non-frightened ghost. */
    memset(pm_blocked, 0, sizeof(pm_blocked));
    if (fright_timer <= 0) {
        for (int i = 0; i < 4; i++) {
            if (ghosts[i].respawn > 0) continue;
            int gx = pm_tx(&ghosts[i]), gy = pm_ty(&ghosts[i]);
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = gx + dx, ny = gy + dy;
                    if (nx >= 0 && nx < PM_W && ny >= 0 && ny < PM_H &&
                        abs(dx) + abs(dy) <= 2)
                        pm_blocked[ny][nx] = 1;
                }
        }
    }
    int d = -1;
    if (fright_timer > 0)
        d = pm_bfs_dir(sx, sy, NULL, pm_goal_fright_ghost);     /* hunt ghosts */
    if (d < 0)
        d = pm_bfs_dir(sx, sy, pm_blocked, pm_goal_pellet);     /* safe pellet */
    if (d < 0)
        d = pm_bfs_dir(sx, sy, NULL, pm_goal_pellet);           /* any pellet */
    if (d >= 0 && !pm_wall(sx + DX[d], sy + DY[d])) {
        pac.dir = d;
    } else if (pm_wall(sx + DX[pac.dir], sy + DY[pac.dir])) {
        for (int t = 0; t < 4; t++)
            if (!pm_wall(sx + DX[t], sy + DY[t])) { pac.dir = t; break; }
    }
}

static void pm_pick_ghost_dir(int gi) {
    pm_actor_t *g = &ghosts[gi];
    int sx = pm_tx(g), sy = pm_ty(g);
    int ptx = pm_tx(&pac), pty = pm_ty(&pac);
    bool scatter = ((pm_frame / 60) % 27) < 7;   /* 7 s scatter / 20 s chase */
    int tx, ty;
    if (fright_timer > 0) {                       /* frightened: random walk */
        int opts[4], n = 0;
        for (int d = 0; d < 4; d++) {
            if (d == (g->dir + 2) % 4) continue;
            if (!pm_wall(sx + DX[d], sy + DY[d])) opts[n++] = d;
        }
        if (n == 0) g->dir = (g->dir + 2) % 4;
        else g->dir = opts[rand() % n];
        return;
    }
    if (scatter) { tx = g->home_tx; ty = g->home_ty; }
    else switch (gi) {
        case 0: tx = ptx; ty = pty; break;                          /* chaser */
        case 1: tx = ptx + DX[pac.dir] * 4;                         /* ambusher */
                ty = pty + DY[pac.dir] * 4; break;
        case 2: tx = (int)(rand() % PM_W); ty = (int)(rand() % PM_H); break; /* wildcard */
        default: {                                                  /* shy */
            int dist = abs(ptx - sx) + abs(pty - sy);
            if (dist > 8) { tx = ptx; ty = pty; }
            else { tx = g->home_tx; ty = g->home_ty; }
        } break;
    }
    /* Greedy: legal dir (no reverse) minimizing distance to target. */
    int best = -1; long bestd = 0x7fffffff;
    for (int d = 0; d < 4; d++) {
        if (d == (g->dir + 2) % 4) continue;
        int nx = sx + DX[d], ny = sy + DY[d];
        if (pm_wall(nx, ny)) continue;
        long dd = (long)(nx - tx) * (nx - tx) + (long)(ny - ty) * (ny - ty);
        if (dd < bestd) { bestd = dd; best = d; }
    }
    g->dir = best >= 0 ? best : (g->dir + 2) % 4;
}

static void pm_draw_actor_pac(void) {
    int cx = pm_ox + pac.px + pm_ts / 2, cy = pm_oy + pac.py + pm_ts / 2;
    int r = pm_ts / 2 - 1; if (r < 2) r = 2;
    Paint_DrawCircle(cx, cy, r, C_YELLOW, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    if ((pm_frame / 8) & 1) {                     /* mouth open: notch in dir */
        int mw = r, mh = r / 2 + 1;
        int x0 = cx, y0 = cy;
        switch (pac.dir) {
        case 0: ionity_fill_rect_fast(x0, y0 - mh / 2, x0 + mw, y0 + mh / 2, C_BLACK); break;
        case 2: ionity_fill_rect_fast(x0 - mw, y0 - mh / 2, x0, y0 + mh / 2, C_BLACK); break;
        case 1: ionity_fill_rect_fast(x0 - mh / 2, y0, x0 + mh / 2, y0 + mw, C_BLACK); break;
        default: ionity_fill_rect_fast(x0 - mh / 2, y0 - mw, x0 + mh / 2, y0, C_BLACK); break;
        }
    }
}

static void pm_draw_actor_ghost(const pm_actor_t *g) {
    int x0 = pm_ox + g->px + 1, y0 = pm_oy + g->py + 1;
    int s = pm_ts - 2;
    uint8_t body = g->color;
    if (fright_timer > 0) body = (fright_timer < 120 && (pm_frame / 10) & 1) ? C_WHITE : C_BLUE;
    /* dome + skirt */
    Paint_DrawCircle(x0 + s / 2, y0 + s / 2 - 1, s / 2, body, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    ionity_fill_rect_fast(x0, y0 + s / 2 - 1, x0 + s, y0 + s, body);
    /* wavy hem */
    for (int i = 0; i < s; i += 3)
        Paint_SetPixel(x0 + i + ((pm_frame / 6) & 1), y0 + s, body);
    /* eyes */
    int ey = y0 + s / 3;
    Paint_SetPixel(x0 + s / 3, ey, C_WHITE);
    Paint_SetPixel(x0 + 2 * s / 3, ey, C_WHITE);
}

static void pm_tick(void) {
    pm_frame++;
    int speed = pm_ts >= 8 ? 2 : 1;

    /* --- Pac --- */
    if (pm_aligned(&pac)) {
        int tx = pm_tx(&pac), ty = pm_ty(&pac);
        if (pm_cell[ty][tx] == 2) { pm_cell[ty][tx] = 0; score += 10; pm_pellets--; }
        else if (pm_cell[ty][tx] == 3) {
            pm_cell[ty][tx] = 0; score += 50; pm_pellets--;
            fright_timer = 6 * 60;
        }
        if (pm_pellets <= 0) { pm_level++; pm_reset_maze(); return; }
        pm_pick_pac_dir();
    }
    if (!pm_wall(pm_tx(&pac) + ((pac.px % pm_ts) ? 0 : DX[pac.dir]),
                 pm_ty(&pac) + ((pac.py % pm_ts) ? 0 : DY[pac.dir])) || !pm_aligned(&pac)) {
        pac.px += DX[pac.dir] * speed;
        pac.py += DY[pac.dir] * speed;
    }

    /* --- Ghosts --- */
    if (fright_timer > 0) fright_timer--;
    for (int i = 0; i < 4; i++) {
        pm_actor_t *g = &ghosts[i];
        if (g->respawn > 0) { g->respawn--; continue; }
        int gspeed = speed;
        if (fright_timer > 0 && (pm_frame & 1)) gspeed = 0;       /* frightened 50% */
        else if ((pm_frame & 7) == 7) gspeed = 0;                 /* normal 87%, grid-aligned */
        if (gspeed == 0) continue;
        if (pm_aligned(g)) pm_pick_ghost_dir(i);
        if (!pm_wall(pm_tx(g) + ((g->px % pm_ts) ? 0 : DX[g->dir]),
                     pm_ty(g) + ((g->py % pm_ts) ? 0 : DY[g->dir])) || !pm_aligned(g)) {
            g->px += DX[g->dir] * gspeed;
            g->py += DY[g->dir] * gspeed;
        }
        /* collision */
        if (pm_tx(g) == pm_tx(&pac) && pm_ty(g) == pm_ty(&pac)) {
            if (fright_timer > 0) {
                score += 200;
                g->px = 13 * pm_ts; g->py = 7 * pm_ts;
                g->respawn = 180;
            } else {
                pm_lives--;
                if (pm_lives <= 0) { pm_reset_game(); return; }
                pm_reset_actors();
                return;
            }
        }
    }
}

static void pm_draw(void) {
    /* walls + pellets */
    for (int y = 0; y < PM_H; y++) {
        for (int x = 0; x < PM_W; x++) {
            int px = pm_ox + x * pm_ts, py = pm_oy + y * pm_ts;
            switch (pm_cell[y][x]) {
            case 1:
                ionity_fill_rect_fast(px + 1, py + 1, px + pm_ts - 1, py + pm_ts - 1, C_BLUE);
                break;
            case 2:
                ionity_fill_rect_fast(px + pm_ts / 2 - 1, py + pm_ts / 2 - 1,
                                      px + pm_ts / 2, py + pm_ts / 2, C_WHITE);
                break;
            case 3:
                if ((pm_frame / 12) & 1)
                    Paint_DrawCircle(px + pm_ts / 2, py + pm_ts / 2, 2,
                                     C_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                break;
            default: break;
            }
        }
    }
    for (int i = 0; i < 4; i++)
        if (ghosts[i].respawn < 120) pm_draw_actor_ghost(&ghosts[i]);
    pm_draw_actor_pac();
    /* HUD */
    char hud[48];
    snprintf(hud, sizeof(hud), "PAC-AI L%d  %06lu  x%d",
             pm_level, (unsigned long)score, pm_lives);
    Paint_DrawString_EN(ax + 4, ay + 1, hud, &Font12, C_YELLOW, C_BLACK);
}

/* ================= SNAKE ================= */

#define SN_MAX_W 44
#define SN_MAX_H 24
static int sn_ts, sn_gw, sn_gh, sn_ox, sn_oy;
static uint8_t sn_grid[SN_MAX_H][SN_MAX_W];       /* 0 free, 1 body */
static uint16_t sn_body[SN_MAX_W * SN_MAX_H];     /* ring buffer of cells */
static int sn_head, sn_tail, sn_len;              /* indices into sn_body */
static int sn_food_x, sn_food_y;
static int sn_dir;
static int sn_grow;
static uint32_t sn_frame;

static int16_t sn_dist[SN_MAX_H][SN_MAX_W];
static uint16_t sn_queue[SN_MAX_W * SN_MAX_H];

static void sn_place_food(void) {
    for (int tries = 0; tries < 500; tries++) {
        int x = rand() % sn_gw, y = rand() % sn_gh;
        if (!sn_grid[y][x]) { sn_food_x = x; sn_food_y = y; return; }
    }
    sn_food_x = sn_food_y = 0;
}

static void sn_reset(void) {
    sn_ts = 10;
    sn_gw = aw / sn_ts; if (sn_gw > SN_MAX_W) sn_gw = SN_MAX_W;
    sn_gh = (ah - 14) / sn_ts; if (sn_gh > SN_MAX_H) sn_gh = SN_MAX_H;
    sn_ox = ax + (aw - sn_gw * sn_ts) / 2;
    sn_oy = ay + 14 + (ah - 14 - sn_gh * sn_ts) / 2;
    memset(sn_grid, 0, sizeof(sn_grid));
    sn_head = sn_tail = 0; sn_len = 0;
    int cx = sn_gw / 2, cy = sn_gh / 2;
    for (int i = 2; i >= 0; i--) {                 /* 3-cell start, heading right */
        sn_body[sn_head] = (uint16_t)(cy * sn_gw + (cx - i));
        sn_grid[cy][cx - i] = 1;
        sn_head = (sn_head + 1) % (int)(sizeof(sn_body) / sizeof(sn_body[0]));
        sn_len++;
    }
    sn_dir = 0; sn_grow = 0; sn_frame = 0; score = 0;
    sn_place_food();
}

/* BFS from head; target = food (or tail when unsafe). Returns dir or -1. */
static int sn_bfs_dir(int tgt_x, int tgt_y, bool ignore_tail) {
    int hb = (sn_head - 1 + (int)(sizeof(sn_body) / sizeof(sn_body[0]))) %
             (int)(sizeof(sn_body) / sizeof(sn_body[0]));
    int hx = sn_body[hb] % sn_gw, hy = sn_body[hb] / sn_gw;
    int tx0 = sn_body[sn_tail] % sn_gw, ty0 = sn_body[sn_tail] / sn_gw;
    memset(sn_dist, -1, sizeof(sn_dist));
    int qh = 0, qt = 0;
    sn_queue[qt++] = (uint16_t)(hy * sn_gw + hx);
    sn_dist[hy][hx] = 0;
    bool found = false;
    while (qh < qt && !found) {
        int cur = sn_queue[qh++];
        int cx = cur % sn_gw, cy = cur / sn_gw;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || nx >= sn_gw || ny < 0 || ny >= sn_gh) continue;
            if (sn_dist[ny][nx] >= 0) continue;
            bool solid = sn_grid[ny][nx] != 0;
            if (ignore_tail && nx == tx0 && ny == ty0) solid = false;
            if (solid && !(nx == tgt_x && ny == tgt_y)) continue;
            sn_dist[ny][nx] = (int16_t)(sn_dist[cy][cx] + 1);
            sn_queue[qt++] = (uint16_t)(ny * sn_gw + nx);
            if (nx == tgt_x && ny == tgt_y) { found = true; break; }
        }
    }
    if (!found) return -1;
    /* backtrack */
    int cx = tgt_x, cy = tgt_y;
    while (sn_dist[cy][cx] > 1) {
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx >= 0 && nx < sn_gw && ny >= 0 && ny < sn_gh &&
                sn_dist[ny][nx] == sn_dist[cy][cx] - 1) { cx = nx; cy = ny; break; }
        }
    }
    for (int d = 0; d < 4; d++)
        if (hx + DX[d] == cx && hy + DY[d] == cy) return d;
    return -1;
}

static void sn_tick(void) {
    sn_frame++;
    if (sn_frame % 5) return;                      /* move every 5 frames */
    int cap = (int)(sizeof(sn_body) / sizeof(sn_body[0]));
    int hb = (sn_head - 1 + cap) % cap;
    int hx = sn_body[hb] % sn_gw, hy = sn_body[hb] / sn_gw;

    int d = sn_bfs_dir(sn_food_x, sn_food_y, false);
    if (d < 0) {                                    /* no safe food path: chase tail */
        int tx0 = sn_body[sn_tail] % sn_gw, ty0 = sn_body[sn_tail] / sn_gw;
        d = sn_bfs_dir(tx0, ty0, true);
    }
    if (d < 0) {                                    /* fallback: any open cell */
        for (int t = 0; t < 4; t++) {
            int nx = hx + DX[t], ny = hy + DY[t];
            if (nx >= 0 && nx < sn_gw && ny >= 0 && ny < sn_gh && !sn_grid[ny][nx]) { d = t; break; }
        }
    }
    if (d < 0) { sn_reset(); return; }              /* boxed in: restart */
    sn_dir = d;

    int nx = hx + DX[d], ny = hy + DY[d];
    if (nx < 0 || nx >= sn_gw || ny < 0 || ny >= sn_gh || sn_grid[ny][nx]) { sn_reset(); return; }

    sn_body[sn_head] = (uint16_t)(ny * sn_gw + nx);
    sn_head = (sn_head + 1) % cap;
    sn_grid[ny][nx] = 1;
    sn_len++;

    if (nx == sn_food_x && ny == sn_food_y) {
        score += 25;
        sn_grow += 2;
        sn_place_food();
        if (sn_len > (sn_gw * sn_gh * 3) / 5) { sn_reset(); return; }
    }
    if (sn_grow > 0) { sn_grow--; }
    else {
        int tc = sn_body[sn_tail];
        sn_grid[tc / sn_gw][tc % sn_gw] = 0;
        sn_tail = (sn_tail + 1) % cap;
        sn_len--;
    }
}

static void sn_draw(void) {
    /* border */
    ionity_draw_panel(sn_ox - 2, sn_oy - 2, sn_ox + sn_gw * sn_ts + 1,
                      sn_oy + sn_gh * sn_ts + 1, C_GREEN, C_BLACK);
    int cap = (int)(sizeof(sn_body) / sizeof(sn_body[0]));
    for (int i = sn_tail, n = 0; n < sn_len; i = (i + 1) % cap, n++) {
        int cx = sn_body[i] % sn_gw, cy = sn_body[i] / sn_gw;
        int px = sn_ox + cx * sn_ts, py = sn_oy + cy * sn_ts;
        bool is_head = (n == sn_len - 1);
        ionity_fill_rect_fast(px + 1, py + 1, px + sn_ts - 2, py + sn_ts - 2,
                              is_head ? C_WHITE : C_GREEN);
    }
    if ((sn_frame / 8) & 1) {
        int px = sn_ox + sn_food_x * sn_ts, py = sn_oy + sn_food_y * sn_ts;
        Paint_DrawCircle(px + sn_ts / 2, py + sn_ts / 2, sn_ts / 2 - 2,
                         C_RED, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }
    char hud[48];
    snprintf(hud, sizeof(hud), "SNAKE-AI  %06lu  LEN %d", (unsigned long)score, sn_len);
    Paint_DrawString_EN(ax + 4, ay + 1, hud, &Font12, C_GREEN, C_BLACK);
}

/* ================= BOUNCE ================= */

typedef struct { int x, y, dx, dy, w, h; uint8_t c; } bounce_t;
static bounce_t bb[6];

static void bounce_reset(void) {
    static const uint8_t cols[6] = {C_RED, C_GREEN, C_BLUE, C_YELLOW, C_CYAN, C_MAGENTA};
    for (int i = 0; i < 6; i++) {
        bb[i].w = 18 + (i * 5) % 20;
        bb[i].h = 14 + (i * 7) % 16;
        bb[i].x = ax + 10 + (i * 61) % (aw - 60);
        bb[i].y = ay + 10 + (i * 37) % (ah - 50);
        bb[i].dx = (i & 1) ? 2 : -1;
        bb[i].dy = (i & 2) ? 1 : -2;
        bb[i].c = cols[i];
    }
}

static void bounce_tick_draw(void) {
    for (int i = 0; i < 6; i++) {
        bounce_t *s = &bb[i];
        ionity_fill_rect_fast(s->x, s->y, s->x + s->w, s->y + s->h, s->c);
        s->x += s->dx; s->y += s->dy;
        if (s->x <= (int)ax + 2 || s->x + s->w >= (int)(ax + aw) - 2) { s->dx = -s->dx; s->x += s->dx; }
        if (s->y <= (int)ay + 2 || s->y + s->h >= (int)(ay + ah) - 2) { s->dy = -s->dy; s->y += s->dy; }
    }
}

/* ================= Public API ================= */

void ionity_game_init(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    ax = x; ay = y; aw = w; ah = h;
    pm_reset_game();
    sn_reset();
    bounce_reset();
}

void ionity_game_set_mode(ionity_game_mode_t m) {
    if (m == mode) return;
    mode = m;
    switch (m) {
    case IONITY_GAME_PACMAN: pm_reset_game(); break;
    case IONITY_GAME_SNAKE:  sn_reset(); break;
    case IONITY_GAME_BOUNCE: bounce_reset(); break;
    default: break;
    }
}

bool ionity_game_set_mode_name(const char *name) {
    if (!name) return false;
    if (strstr(name, "pac"))    { ionity_game_set_mode(IONITY_GAME_PACMAN); return true; }
    if (strstr(name, "snake"))  { ionity_game_set_mode(IONITY_GAME_SNAKE);  return true; }
    if (strstr(name, "bounce")) { ionity_game_set_mode(IONITY_GAME_BOUNCE); return true; }
    if (strstr(name, "off"))    { ionity_game_set_mode(IONITY_GAME_OFF);    return true; }
    return false;
}

ionity_game_mode_t ionity_game_get_mode(void) { return mode; }

const char *ionity_game_mode_name(void) {
    switch (mode) {
    case IONITY_GAME_PACMAN: return "PAC-MAN";
    case IONITY_GAME_SNAKE:  return "SNAKE";
    case IONITY_GAME_BOUNCE: return "BOUNCE";
    default:                 return "OFF";
    }
}

uint32_t ionity_game_score(void) { return score; }

void ionity_game_tick(uint32_t frame) {
    (void)frame;
    switch (mode) {
    case IONITY_GAME_PACMAN: pm_tick(); pm_draw(); break;
    case IONITY_GAME_SNAKE:  sn_tick(); sn_draw(); break;
    case IONITY_GAME_BOUNCE: bounce_tick_draw();   break;
    default: break;
    }
}
