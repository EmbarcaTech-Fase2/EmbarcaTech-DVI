# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(hdmi C CXX ASM)

pico_sdk_init()

add_executable(hdmi 
	hdmi.c
	#teclado.c
	tmds_encode_font_2bpp.S
	tmds_encode_font_2bpp.h
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

target_link_libraries(${PROJECT_NAME}
        pico_stdlib)


target_compile_definitions(${PROJECT_NAME} PRIVATE
	DVI_VERTICAL_REPEAT=1
	)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	PICO_CORE1_STACK_SIZE=0x200
	)

add_subdirectory(libdvi)
add_subdirectory(libsprite)

target_link_libraries(${PROJECT_NAME}
	pico_stdlib
	pico_multicore
	libdvi
	hardware_adc
	hardware_uart
)

pico_add_extra_outputs(${PROJECT_NAME})
