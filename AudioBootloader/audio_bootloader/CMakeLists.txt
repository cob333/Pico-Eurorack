cmake_minimum_required(VERSION 3.19)

set(PICO_BOARD pico2 CACHE STRING "RP2350 board")

if(NOT DEFINED PICO_SDK_PATH)
  if(DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
  elseif(EXISTS "$ENV{HOME}/Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk/pico_sdk_init.cmake")
    set(PICO_SDK_PATH "$ENV{HOME}/Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk")
  endif()
endif()

if(NOT DEFINED PICO_TOOLCHAIN_PATH)
  file(GLOB _pqt_gcc_roots "$ENV{HOME}/Library/Arduino15/packages/rp2040/tools/pqt-gcc/*")
  list(SORT _pqt_gcc_roots COMPARE NATURAL ORDER DESCENDING)
  list(LENGTH _pqt_gcc_roots _pqt_gcc_count)
  if(_pqt_gcc_count GREATER 0)
    list(GET _pqt_gcc_roots 0 PICO_TOOLCHAIN_PATH)
  endif()
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
  message(FATAL_ERROR "Set PICO_SDK_PATH to a valid pico-sdk checkout")
endif()

if(NOT EXISTS "${PICO_TOOLCHAIN_PATH}/bin/arm-none-eabi-gcc")
  message(FATAL_ERROR "Set PICO_TOOLCHAIN_PATH to the Arduino-Pico pqt-gcc root")
endif()

get_filename_component(ARDUINO_RP2040_ROOT "${PICO_SDK_PATH}" DIRECTORY)
set(UZLIB_DIR "${ARDUINO_RP2040_ROOT}/ota/uzlib/src")

if(NOT EXISTS "${UZLIB_DIR}/uzlib.h")
  message(FATAL_ERROR "Expected Arduino-Pico uzlib sources at ${UZLIB_DIR}")
endif()

set(PICO_GCC_TRIPLE arm-none-eabi CACHE STRING "GNU Arm embedded triple")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(audio_bootloader C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(audio_bootloader
  src/main.cpp
  src/fsk_codec.cpp
  src/ws2812_driver.cpp
  ${UZLIB_DIR}/adler32.c
  ${UZLIB_DIR}/crc32.c
  ${UZLIB_DIR}/tinflate.c
  ${UZLIB_DIR}/tinfzlib.c
)

pico_generate_pio_header(audio_bootloader ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(audio_bootloader PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${UZLIB_DIR}
)

target_link_libraries(audio_bootloader PRIVATE
  cmsis_core
  pico_stdlib
  hardware_adc
  hardware_dma
  hardware_flash
  hardware_pio
  hardware_sync
  hardware_watchdog
)

pico_enable_stdio_uart(audio_bootloader 0)
pico_enable_stdio_usb(audio_bootloader 0)

pico_add_extra_outputs(audio_bootloader)
