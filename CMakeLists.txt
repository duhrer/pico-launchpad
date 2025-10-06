cmake_minimum_required(VERSION 3.12)

set(NAME pico-launchpad)

include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Adapted from:  https://github.com/wiredopposite/OGX-Mini/blob/master/Firmware/RP2040/CMakeLists.txt
set(LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/libs)

# We have to use a newer TinyUSB because the Pico SDK doesn't yet include the MIDI host callbacks.
set(PICO_TINYUSB_PATH ${LIB_DIR}/tinyusb)

set(PICO_PIO_USB_PATH ${LIB_DIR}/Pico-PIO-USB)

# Initialize the SDK
pico_sdk_init()

add_subdirectory(${PICO_PIO_USB_PATH} ${CMAKE_BINARY_DIR}/Pico-PIO-USB)

# Add your source files
add_executable(${NAME}
    src/pico-launchpad.c
    src/usb_descriptors.c
    src/launchpad.c
)

# use tinyusb implementation
target_compile_definitions(${NAME} PRIVATE PIO_USB_USE_TINYUSB)

# Really not sure if this is necessary/advisable.
#target_compile_definitions(${NAME} PRIVATE PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/preprocessor/include
)

# Link required libraries
target_link_libraries(
    ${NAME}
    pico_stdlib
    tinyusb_device
    tinyusb_host
    pico_pio_usb
    #pico_cyw43_arch_none
    pico_bootsel_via_double_reset
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)

# Disable USB serial and enable UART serial
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
