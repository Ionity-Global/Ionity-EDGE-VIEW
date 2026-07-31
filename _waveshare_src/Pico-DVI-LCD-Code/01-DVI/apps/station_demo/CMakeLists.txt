add_executable(station_demo
	main.c
)

target_compile_options(station_demo PRIVATE -Wall)

target_compile_definitions(station_demo PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=3
	PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
	CYW43_LWIP=1
	)

target_include_directories(station_demo PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../libionity
	)

target_link_libraries(station_demo
	pico_stdlib
	pico_multicore
	pico_util
	pico_lwip
	libdvi
	libgui
	libionity
)

pico_add_extra_outputs(station_demo)
