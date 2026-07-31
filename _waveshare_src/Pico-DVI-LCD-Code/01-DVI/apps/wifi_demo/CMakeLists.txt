add_executable(wifi_demo
    main.c
)

target_compile_options(wifi_demo PRIVATE -Wall)

target_compile_definitions(wifi_demo PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=3
    PARAM_ASSERTIONS_ENABLED_ALL=1
    PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
    CYW43_LWIP=1
)

target_include_directories(wifi_demo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Central WiFi credentials (set by wifi_config.cmake via libionity processing)
if(WIFI_SSID)
    target_compile_definitions(wifi_demo PRIVATE
        IONITY_WIFI_SSID="${WIFI_SSID}"
        IONITY_WIFI_PASS="${WIFI_PASS}"
    )
endif()

target_link_libraries(wifi_demo
    pico_stdlib
    pico_multicore
    pico_util
    pico_cyw43_arch_lwip_threadsafe_background
    libdvi
    libgui
)

pico_add_extra_outputs(wifi_demo)
