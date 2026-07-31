#ifndef IONITY_TIME_H
#define IONITY_TIME_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timezone offset in seconds (default: SAST, UTC+2). Override at compile time. */
#ifndef IONITY_TZ_OFFSET_SECONDS
#define IONITY_TZ_OFFSET_SECONDS 7200
#endif

/* Init (call once after WiFi is up). Starts background NTP sync. */
void ionity_time_init(void);

/* Poll — call from the main loop. Handles NTP request/retry/resync. */
void ionity_time_poll(void);

/* True once a valid time source (NTP or manual) has been applied. */
bool ionity_time_valid(void);

/* Manually set time from a UTC epoch (e.g. pushed by the Studio app). */
void ionity_time_set_epoch(uint32_t utc_epoch);

/* Current local epoch (UTC + TZ offset). 0 if not valid. */
uint32_t ionity_time_local_epoch(void);

/* Local wall-clock components. */
void ionity_time_get_hms(int *h, int *m, int *s);

/* "HH:MM:SS" (9 bytes incl NUL). Returns "--:--:--" when not synced. */
void ionity_time_clock_str(char *out, size_t len);

/* "THU 31 JUL" style local date string. Empty when not synced. */
void ionity_time_date_str(char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif
