add_executable(demo_bounce main.c)
target_compile_options(demo_bounce PRIVATE -Wall)
target_compile_definitions(demo_bounce PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=3)
target_link_libraries(demo_bounce pico_stdlib pico_multicore pico_util libdvi libgui)
pico_add_extra_outputs(demo_bounce)
