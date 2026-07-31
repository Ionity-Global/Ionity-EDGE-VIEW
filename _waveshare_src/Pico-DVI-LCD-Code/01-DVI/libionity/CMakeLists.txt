# SDK-Ionity: Custom library for Station Pico
# WiFi provisioning, data streaming, pixel enhancements, branding

add_library(libionity INTERFACE)

target_sources(libionity INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/ionity_wifi.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_wifi.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_stream.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_stream.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_pixels.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_pixels.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_brand.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_brand.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_http.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_http.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_time.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_time.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_weather.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_weather.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_game.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_game.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_qr.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_qr.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_data.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_data.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_scene.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_scene.h
	${CMAKE_CURRENT_LIST_DIR}/ionity_mirror.c
	${CMAKE_CURRENT_LIST_DIR}/ionity_mirror.h
)

target_include_directories(libionity INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(libionity INTERFACE
	pico_stdlib
	pico_cyw43_arch_lwip_threadsafe_background
	libgui
)

# ── WiFi credentials ─────────────────────────────────────────────────
# Read from an optional wifi_config.cmake at the project root, or from
# environment variables.  This keeps credentials out of the public repo.
# See wifi_config.cmake.example in the project root.
#
#   cmake -DWIFI_SSID="MySSID" -DWIFI_PASS="MyPass" ..
#   or  set IONITY_WIFI_SSID=MySSID && set IONITY_WIFI_PASS=MyPass
#
# ──────────────────────────────────────────────────────────────────────

# 1. Try cmake -D flags (highest priority)
set(WIFI_SSID "" CACHE STRING "WiFi SSID (set via -D or wifi_config.cmake)")
set(WIFI_PASS "" CACHE STRING "WiFi password (set via -D or wifi_config.cmake)")

# 2. Try wifi_config.cmake in the project root (next to 01-DVI/CMakeLists.txt)
set(_wifi_config "${CMAKE_CURRENT_LIST_DIR}/../wifi_config.cmake")
if(EXISTS "${_wifi_config}")
	include("${_wifi_config}")
	set(WIFI_SSID "${WIFI_SSID}" CACHE STRING "WiFi SSID" FORCE)
	set(WIFI_PASS "${WIFI_PASS}" CACHE STRING "WiFi password" FORCE)
endif()

# 3. Fall back to environment variables
if(NOT WIFI_SSID AND DEFINED ENV{IONITY_WIFI_SSID})
	set(WIFI_SSID "$ENV{IONITY_WIFI_SSID}")
endif()
if(NOT WIFI_PASS AND DEFINED ENV{IONITY_WIFI_PASS})
	set(WIFI_PASS "$ENV{IONITY_WIFI_PASS}")
endif()

# Warn if still empty
if(NOT WIFI_SSID)
	message(WARNING "WIFI_SSID is empty — WiFi will not connect. "
	                "Set via -DWIFI_SSID=..., environment variable, "
	                "or wifi_config.cmake")
endif()

target_compile_definitions(libionity INTERFACE
	IONITY_WIFI_SSID="${WIFI_SSID}"
	IONITY_WIFI_PASS="${WIFI_PASS}"
	IONITY_STREAM_PORT=4242
)