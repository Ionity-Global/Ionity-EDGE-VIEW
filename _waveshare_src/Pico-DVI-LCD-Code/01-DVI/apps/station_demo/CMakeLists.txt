add_executable(station_demo
	main.c
)

target_compile_options(station_demo PRIVATE -Wall)

target_compile_definitions(station_demo PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=3
	)

target_link_libraries(station_demo
	pico_stdlib
	pico_multicore
	pico_util
	libdvi
	libgui
	libionity
)

pico_add_extra_outputs(station_demo)
