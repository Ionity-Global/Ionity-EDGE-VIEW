#include "ionity_data.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char     key[IONITY_DATA_KEY_LEN];
    char     value[IONITY_DATA_VAL_LEN];
    uint32_t stamp_ms;
    bool     used;
} data_entry_t;

static data_entry_t table[IONITY_DATA_SLOTS];
static uint32_t     last_push_ms = 0;
static bool         had_push = false;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static data_entry_t *find(const char *key) {
    for (int i = 0; i < IONITY_DATA_SLOTS; i++) {
        if (table[i].used && strncmp(table[i].key, key, IONITY_DATA_KEY_LEN - 1) == 0)
            return &table[i];
    }
    return NULL;
}

void ionity_data_init(void) {
    memset(table, 0, sizeof(table));
    last_push_ms = 0;
    had_push = false;
}

bool ionity_data_set(const char *key, const char *value) {
    if (!key || !key[0]) return false;
    if (!value) value = "";

    data_entry_t *e = find(key);
    if (!e) {
        for (int i = 0; i < IONITY_DATA_SLOTS; i++) {
            if (!table[i].used) { e = &table[i]; break; }
        }
        /* Table full: recycle the least recently updated entry. */
        if (!e) {
            e = &table[0];
            for (int i = 1; i < IONITY_DATA_SLOTS; i++)
                if (table[i].stamp_ms < e->stamp_ms) e = &table[i];
        }
        e->used = true;
        snprintf(e->key, IONITY_DATA_KEY_LEN, "%s", key);
    }

    snprintf(e->value, IONITY_DATA_VAL_LEN, "%s", value);
    e->stamp_ms = now_ms();
    last_push_ms = e->stamp_ms;
    had_push = true;
    return true;
}

const char *ionity_data_get_fresh(const char *key) {
    data_entry_t *e = find(key);
    if (!e) return NULL;
    if (now_ms() - e->stamp_ms > IONITY_DATA_STALE_MS) return NULL;
    return e->value;
}

const char *ionity_data_get(const char *key, const char *fallback) {
    const char *v = ionity_data_get_fresh(key);
    return (v && v[0]) ? v : fallback;
}

int ionity_data_get_int(const char *key, int fallback) {
    const char *v = ionity_data_get_fresh(key);
    if (!v || !v[0]) return fallback;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return fallback;
    return (int)n;
}

uint32_t ionity_data_age_ms(const char *key) {
    data_entry_t *e = find(key);
    if (!e) return UINT32_MAX;
    return now_ms() - e->stamp_ms;
}

bool ionity_data_have_server(void) {
    return had_push && (now_ms() - last_push_ms) <= IONITY_DATA_STALE_MS;
}

uint32_t ionity_data_server_age_ms(void) {
    if (!had_push) return UINT32_MAX;
    return now_ms() - last_push_ms;
}

int ionity_data_count(void) {
    int n = 0;
    for (int i = 0; i < IONITY_DATA_SLOTS; i++) if (table[i].used) n++;
    return n;
}

void ionity_data_clear(void) {
    ionity_data_init();
}
