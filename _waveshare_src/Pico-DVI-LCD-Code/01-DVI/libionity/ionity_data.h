/* ionity_data — server-streamed key/value store.
 *
 * The EDGE-VIEW screen is driven by the Studio server, not by the Pico. The
 * server pushes values with {"type":"data","key":"...","value":"..."} and
 * panels read them back by key. Every entry records when it last arrived so a
 * panel can fall back to on-device generation when the server goes quiet.
 */
#ifndef IONITY_DATA_H
#define IONITY_DATA_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IONITY_DATA_SLOTS   32
#define IONITY_DATA_KEY_LEN 24
#define IONITY_DATA_VAL_LEN 192

/* Values older than this are considered stale and panels use local fallback. */
#define IONITY_DATA_STALE_MS (5 * 60 * 1000u)

void ionity_data_init(void);

/* Store/overwrite a value. Returns false only if the table is full. */
bool ionity_data_set(const char *key, const char *value);

/* Returns the stored value, or fallback when missing or stale. */
const char *ionity_data_get(const char *key, const char *fallback);

/* Returns the stored value only while fresh, else NULL. */
const char *ionity_data_get_fresh(const char *key);

/* Integer form. Returns fallback when missing, stale or non-numeric. */
int ionity_data_get_int(const char *key, int fallback);

/* Milliseconds since the key last arrived, or UINT32_MAX if never. */
uint32_t ionity_data_age_ms(const char *key);

/* True once any value has arrived from the server. */
bool ionity_data_have_server(void);

/* Milliseconds since the most recent push from the server. */
uint32_t ionity_data_server_age_ms(void);

int ionity_data_count(void);
void ionity_data_clear(void);

#ifdef __cplusplus
}
#endif

#endif
