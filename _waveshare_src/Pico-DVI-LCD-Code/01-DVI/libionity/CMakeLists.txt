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
)

target_include_directories(libionity INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(libionity INTERFACE
	pico_stdlib
	pico_cyw43_arch_lwip_threadsafe_background
	libgui
)

target_compile_definitions(libionity INTERFACE
	IONITY_WIFI_SSID="Antwerp Ionity"
	IONITY_WIFI_PASS="Antwerp1990!"
	IONITY_STREAM_PORT=4242
)