add_executable(edgeview
	main.c
)

target_compile_options(edgeview PRIVATE -Wall)

target_compile_definitions(edgeview PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=3
	PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
	CYW43_LWIP=1
	)

target_include_directories(edgeview PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../libionity
	)

target_link_libraries(edgeview
	pico_stdlib
	pico_multicore
	pico_util
	pico_lwip
	libdvi
	libgui
	libionity
)

pico_add_extra_outputs(edgeview)
