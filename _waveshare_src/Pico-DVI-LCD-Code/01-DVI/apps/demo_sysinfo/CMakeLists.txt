add_executable(demo_sysinfo main.c)
target_compile_options(demo_sysinfo PRIVATE -Wall)
target_compile_definitions(demo_sysinfo PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=3)
target_link_libraries(demo_sysinfo pico_stdlib pico_multicore pico_util libdvi libgui)
pico_add_extra_outputs(demo_sysinfo)
