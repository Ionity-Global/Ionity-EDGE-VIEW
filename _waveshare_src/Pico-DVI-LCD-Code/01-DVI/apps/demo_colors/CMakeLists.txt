add_executable(demo_colors main.c)
target_compile_options(demo_colors PRIVATE -Wall)
target_compile_definitions(demo_colors PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=3)
target_link_libraries(demo_colors pico_stdlib pico_multicore pico_util libdvi libgui)
pico_add_extra_outputs(demo_colors)
