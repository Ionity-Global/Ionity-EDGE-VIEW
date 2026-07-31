#include "ionity_mirror.h"

#include <string.h>
#include "lwip/tcp.h"
#include "pico/time.h"

#define MIRROR_MAGIC 0x31464D49u  /* 'IMF1' */
#define HEADER_BYTES 12

static struct tcp_pcb *server_pcb = NULL;
static struct tcp_pcb *viewer = NULL;

static uint8_t packed[IONITY_MIRROR_PACKED];
static uint8_t encoded[IONITY_MIRROR_PACKED + HEADER_BYTES];

static uint8_t  target_fps = 5;
static uint32_t last_send_ms = 0;
static uint32_t frames_sent = 0;
static uint32_t frames_dropped = 0;

bool ionity_mirror_active(void) { return viewer != NULL; }
uint32_t ionity_mirror_frames_sent(void) { return frames_sent; }
uint32_t ionity_mirror_frames_dropped(void) { return frames_dropped; }

void ionity_mirror_set_fps(uint8_t fps) {
    if (fps < 1) fps = 1;
    if (fps > 15) fps = 15;
    target_fps = fps;
}

static void drop_viewer(void) {
    if (!viewer) return;
    struct tcp_pcb *pcb = viewer;
    viewer = NULL;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_close(pcb);
}

static void mirror_err(void *arg, err_t err) {
    (void)arg; (void)err;
    viewer = NULL;  /* lwIP already freed the pcb */
}

static err_t mirror_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (p == NULL) { drop_viewer(); return ERR_OK; }

    /* One byte from the viewer sets the frame rate; anything else is ignored. */
    uint8_t b = 0;
    if (pbuf_copy_partial(p, &b, 1, 0) == 1 && b >= 1 && b <= 15)
        ionity_mirror_set_fps(b);

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t mirror_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || new_pcb == NULL) return ERR_VAL;

    /* One viewer at a time — the newest wins so a stale socket cannot block it. */
    if (viewer) drop_viewer();

    viewer = new_pcb;
    tcp_recv(new_pcb, mirror_recv);
    tcp_err(new_pcb, mirror_err);
    tcp_nagle_disable(new_pcb);
    frames_sent = frames_dropped = 0;
    last_send_ms = 0;
    return ERR_OK;
}

bool ionity_mirror_init(uint16_t port) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, port) != ERR_OK) { tcp_close(pcb); return false; }

    server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!server_pcb) { tcp_close(pcb); return false; }
    tcp_accept(server_pcb, mirror_accept);
    return true;
}

/* Sample one pixel out of each 4x4 block straight from the three bit planes. */
static void downscale(const uint8_t *fb, uint16_t fb_w, uint16_t fb_h) {
    const uint32_t width_bytes = fb_w / 8u;
    const uint32_t plane = width_bytes * fb_h;
    const uint16_t step_x = fb_w / IONITY_MIRROR_WIDTH;
    const uint16_t step_y = fb_h / IONITY_MIRROR_HEIGHT;

    uint8_t *out = packed;
    for (uint16_t oy = 0; oy < IONITY_MIRROR_HEIGHT; oy++) {
        const uint32_t row = (uint32_t)(oy * step_y) * width_bytes;
        for (uint16_t ox = 0; ox < IONITY_MIRROR_WIDTH; ox += 2) {
            uint8_t pair = 0;
            for (uint8_t half = 0; half < 2; half++) {
                const uint16_t sx = (uint16_t)((ox + half) * step_x);
                const uint32_t addr = row + (sx >> 3);
                const uint8_t mask = (uint8_t)(1u << (sx & 7u));
                uint8_t v = 0;
                if (fb[addr] & mask)             v |= 1u;   /* blue  */
                if (fb[addr + plane] & mask)     v |= 2u;   /* green */
                if (fb[addr + 2u * plane] & mask) v |= 4u;  /* red   */
                pair = (uint8_t)((pair << 4) | v);
            }
            *out++ = pair;
        }
    }
}

/* Returns encoded length, or 0 if RLE would not be smaller than raw. */
static uint16_t rle_encode(uint8_t *dst, uint16_t cap) {
    uint16_t o = 0;
    uint32_t i = 0;
    while (i < IONITY_MIRROR_PACKED) {
        const uint8_t v = packed[i];
        uint32_t run = 1;
        while (run < 255 && i + run < IONITY_MIRROR_PACKED && packed[i + run] == v) run++;
        if (o + 2 > cap) return 0;
        dst[o++] = (uint8_t)run;
        dst[o++] = v;
        i += run;
    }
    return o;
}

void ionity_mirror_tick(const uint8_t *framebuf, uint16_t fb_width, uint16_t fb_height) {
    if (!viewer || !framebuf) return;

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint32_t interval = 1000u / target_fps;
    if (now - last_send_ms < interval) return;

    /* Never queue behind a busy socket — drop the frame instead. */
    if (tcp_sndbuf(viewer) < IONITY_MIRROR_PACKED + HEADER_BYTES) {
        frames_dropped++;
        return;
    }

    last_send_ms = now;
    downscale(framebuf, fb_width, fb_height);

    uint8_t *payload = encoded + HEADER_BYTES;
    uint16_t len = rle_encode(payload, IONITY_MIRROR_PACKED);
    uint8_t format = 0;
    if (len == 0) {
        memcpy(payload, packed, IONITY_MIRROR_PACKED);
        len = IONITY_MIRROR_PACKED;
        format = 1;
    }

    encoded[0] = (uint8_t)(MIRROR_MAGIC & 0xFF);
    encoded[1] = (uint8_t)((MIRROR_MAGIC >> 8) & 0xFF);
    encoded[2] = (uint8_t)((MIRROR_MAGIC >> 16) & 0xFF);
    encoded[3] = (uint8_t)((MIRROR_MAGIC >> 24) & 0xFF);
    encoded[4] = (uint8_t)(IONITY_MIRROR_WIDTH & 0xFF);
    encoded[5] = (uint8_t)(IONITY_MIRROR_WIDTH >> 8);
    encoded[6] = (uint8_t)(IONITY_MIRROR_HEIGHT & 0xFF);
    encoded[7] = (uint8_t)(IONITY_MIRROR_HEIGHT >> 8);
    encoded[8] = format;
    encoded[9] = 0;
    encoded[10] = (uint8_t)(len & 0xFF);
    encoded[11] = (uint8_t)(len >> 8);

    if (tcp_write(viewer, encoded, (uint16_t)(len + HEADER_BYTES), TCP_WRITE_FLAG_COPY) == ERR_OK) {
        tcp_output(viewer);
        frames_sent++;
    } else {
        frames_dropped++;
    }
}
