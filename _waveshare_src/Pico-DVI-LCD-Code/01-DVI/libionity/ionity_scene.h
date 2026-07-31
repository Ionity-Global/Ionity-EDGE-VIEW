/* ionity_scene — interchangeable screen slots and panels.
 *
 * The screen is a set of fixed rectangular SLOTS. Any PANEL can be bound to
 * any slot by name, at runtime, from the server:
 *
 *     {"type":"layout","slot":"stage","panel":"weather"}
 *
 * Panels are registered by the application, so nothing here is tied to a
 * particular dashboard - swap the bindings and the same firmware becomes a
 * different screen.
 */
#ifndef IONITY_SCENE_H
#define IONITY_SCENE_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IONITY_SCENE_MAX_PANELS 24
#define IONITY_SCENE_NAME_LEN   16

typedef enum {
    IONITY_SLOT_HEADER = 0,
    IONITY_SLOT_INFO_A,
    IONITY_SLOT_INFO_B,
    IONITY_SLOT_INFO_C,
    IONITY_SLOT_STAGE,
    IONITY_SLOT_SIDE,
    IONITY_SLOT_MARQUEE,
    IONITY_SLOT_TICKER,
    IONITY_SLOT_STATS,
    IONITY_SLOT_FOOTER,
    IONITY_SLOT_COUNT
} ionity_slot_t;

typedef struct {
    int16_t x, y, w, h;
} ionity_rect_t;

/* A panel draws itself inside the rectangle it is given. */
typedef void (*ionity_panel_fn)(const ionity_rect_t *r);

void ionity_scene_init(void);

/* Define where a slot lives on screen. */
void ionity_scene_set_rect(ionity_slot_t slot, int x, int y, int w, int h);
ionity_rect_t ionity_scene_get_rect(ionity_slot_t slot);

/* Make a panel available for binding. Re-registering a name replaces it. */
bool ionity_scene_register(const char *panel_name, ionity_panel_fn fn);

/* Bind a panel to a slot. Both accept the names used on the wire.
 * Panel name "blank" (or an empty string) clears the slot. */
bool ionity_scene_bind(ionity_slot_t slot, const char *panel_name);
bool ionity_scene_bind_by_name(const char *slot_name, const char *panel_name);

const char *ionity_scene_bound_panel(ionity_slot_t slot);
ionity_slot_t ionity_scene_slot_from_name(const char *slot_name);
const char *ionity_scene_slot_name(ionity_slot_t slot);

/* Draw every bound slot. */
void ionity_scene_render(void);

/* Draw one slot (useful for partial refreshes). */
void ionity_scene_render_slot(ionity_slot_t slot);

/* Slots can be hidden without losing their binding. */
void ionity_scene_set_visible(ionity_slot_t slot, bool visible);
bool ionity_scene_is_visible(ionity_slot_t slot);

/* Serialise the current layout as JSON for the server to read back. */
int ionity_scene_describe(char *out, int out_len);

#ifdef __cplusplus
}
#endif

#endif
