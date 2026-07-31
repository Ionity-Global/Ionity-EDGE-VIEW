#include "ionity_scene.h"

#include <string.h>
#include <stdio.h>

typedef struct {
    char            name[IONITY_SCENE_NAME_LEN];
    ionity_panel_fn fn;
    bool            used;
} panel_entry_t;

typedef struct {
    ionity_rect_t rect;
    char          panel[IONITY_SCENE_NAME_LEN];
    bool          visible;
} slot_entry_t;

static panel_entry_t panels[IONITY_SCENE_MAX_PANELS];
static slot_entry_t  slots[IONITY_SLOT_COUNT];

static const char *slot_names[IONITY_SLOT_COUNT] = {
    "header", "info_a", "info_b", "info_c", "stage",
    "side",   "marquee", "ticker", "stats", "footer",
};

static panel_entry_t *find_panel(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < IONITY_SCENE_MAX_PANELS; i++) {
        if (panels[i].used && strncmp(panels[i].name, name, IONITY_SCENE_NAME_LEN - 1) == 0)
            return &panels[i];
    }
    return NULL;
}

void ionity_scene_init(void) {
    memset(panels, 0, sizeof(panels));
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < IONITY_SLOT_COUNT; i++) slots[i].visible = true;
}

void ionity_scene_set_rect(ionity_slot_t slot, int x, int y, int w, int h) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return;
    slots[slot].rect.x = (int16_t)x;
    slots[slot].rect.y = (int16_t)y;
    slots[slot].rect.w = (int16_t)w;
    slots[slot].rect.h = (int16_t)h;
}

ionity_rect_t ionity_scene_get_rect(ionity_slot_t slot) {
    ionity_rect_t empty = {0, 0, 0, 0};
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return empty;
    return slots[slot].rect;
}

bool ionity_scene_register(const char *panel_name, ionity_panel_fn fn) {
    if (!panel_name || !panel_name[0] || !fn) return false;

    panel_entry_t *e = find_panel(panel_name);
    if (!e) {
        for (int i = 0; i < IONITY_SCENE_MAX_PANELS; i++) {
            if (!panels[i].used) { e = &panels[i]; break; }
        }
        if (!e) return false;
        e->used = true;
        snprintf(e->name, IONITY_SCENE_NAME_LEN, "%s", panel_name);
    }
    e->fn = fn;
    return true;
}

bool ionity_scene_bind(ionity_slot_t slot, const char *panel_name) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return false;

    if (!panel_name || !panel_name[0] || strcmp(panel_name, "blank") == 0) {
        slots[slot].panel[0] = '\0';
        return true;
    }
    if (!find_panel(panel_name)) return false;

    snprintf(slots[slot].panel, IONITY_SCENE_NAME_LEN, "%s", panel_name);
    return true;
}

ionity_slot_t ionity_scene_slot_from_name(const char *slot_name) {
    if (!slot_name) return IONITY_SLOT_COUNT;
    for (int i = 0; i < IONITY_SLOT_COUNT; i++) {
        if (strcmp(slot_names[i], slot_name) == 0) return (ionity_slot_t)i;
    }
    return IONITY_SLOT_COUNT;
}

const char *ionity_scene_slot_name(ionity_slot_t slot) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return "?";
    return slot_names[slot];
}

bool ionity_scene_bind_by_name(const char *slot_name, const char *panel_name) {
    ionity_slot_t s = ionity_scene_slot_from_name(slot_name);
    if (s >= IONITY_SLOT_COUNT) return false;
    return ionity_scene_bind(s, panel_name);
}

const char *ionity_scene_bound_panel(ionity_slot_t slot) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return "";
    return slots[slot].panel;
}

void ionity_scene_set_visible(ionity_slot_t slot, bool visible) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return;
    slots[slot].visible = visible;
}

bool ionity_scene_is_visible(ionity_slot_t slot) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return false;
    return slots[slot].visible;
}

void ionity_scene_render_slot(ionity_slot_t slot) {
    if (slot < 0 || slot >= IONITY_SLOT_COUNT) return;
    if (!slots[slot].visible) return;
    if (slots[slot].rect.w <= 0 || slots[slot].rect.h <= 0) return;

    panel_entry_t *p = find_panel(slots[slot].panel);
    if (p && p->fn) p->fn(&slots[slot].rect);
}

void ionity_scene_render(void) {
    for (int i = 0; i < IONITY_SLOT_COUNT; i++)
        ionity_scene_render_slot((ionity_slot_t)i);
}

int ionity_scene_describe(char *out, int out_len) {
    if (!out || out_len < 8) return 0;

    int n = snprintf(out, out_len, "{\"type\":\"layout\",\"slots\":{");
    for (int i = 0; i < IONITY_SLOT_COUNT && n < out_len; i++) {
        n += snprintf(out + n, out_len - n, "%s\"%s\":\"%s\"",
                      i ? "," : "", slot_names[i],
                      slots[i].panel[0] ? slots[i].panel : "blank");
    }
    if (n < out_len) n += snprintf(out + n, out_len - n, "},\"panels\":[");
    bool first = true;
    for (int i = 0; i < IONITY_SCENE_MAX_PANELS && n < out_len; i++) {
        if (!panels[i].used) continue;
        n += snprintf(out + n, out_len - n, "%s\"%s\"", first ? "" : ",", panels[i].name);
        first = false;
    }
    if (n < out_len) n += snprintf(out + n, out_len - n, "]}\n");
    return (n < out_len) ? n : out_len - 1;
}
