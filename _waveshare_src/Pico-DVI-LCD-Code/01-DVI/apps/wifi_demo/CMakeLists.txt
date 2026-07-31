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
    LWIP_HTTPD_CGI=0
    LWIP_HTTPD_SSI=0
    LWIP_HTTPD_SUPPORT_POST=0
)

target_include_directories(wifi_demo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(wifi_demo
    pico_stdlib
    pico_multicore
    pico_util
    pico_cyw43_arch_lwip_threadsafe_background
    libdvi
    libgui
)

pico_add_extra_outputs(wifi_demo)
