#ifndef IONITY_WEATHER_H
#define IONITY_WEATHER_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default location: Centurion, South Africa (Ionity Global HQ). */
#ifndef IONITY_WEATHER_LAT
#define IONITY_WEATHER_LAT "-25.86"
#endif
#ifndef IONITY_WEATHER_LON
#define IONITY_WEATHER_LON "28.19"
#endif

/* Conditions match the classic 6-icon set used by ionity_brand. */
enum {
    IONITY_WX_SUNNY = 0,
    IONITY_WX_CLOUDY,
    IONITY_WX_RAIN,
    IONITY_WX_STORM,
    IONITY_WX_SNOW,
    IONITY_WX_FOG,
};

/* Init (after WiFi up). Fetches immediately, then every 15 min. */
void ionity_weather_init(void);

/* Poll — call from the main loop. */
void ionity_weather_poll(void);

/* True once any reading (fetched or pushed) is available. */
bool ionity_weather_valid(void);

/* Latest reading. */
int ionity_weather_temp_c(void);
int ionity_weather_condition(void);

/* Push a reading from the stream (Studio app fallback). Overrides fetch. */
void ionity_weather_set(int temp_c, int condition);

#ifdef __cplusplus
}
#endif

#endif
