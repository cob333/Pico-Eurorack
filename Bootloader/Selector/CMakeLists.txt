cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
  set(PICO_BOARD pico2 CACHE STRING "Pico SDK board target")
endif()

if(NOT DEFINED PICO_SDK_PATH)
  set(_ARDUINO_PICO_SDK "$ENV{HOME}/Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk")
  if(EXISTS "${_ARDUINO_PICO_SDK}/pico_sdk_init.cmake")
    set(PICO_SDK_PATH "${_ARDUINO_PICO_SDK}" CACHE PATH "Local Pico SDK path")
  endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_eurorack_selector C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_selector
  ${CMAKE_CURRENT_LIST_DIR}/../boot_config.c
  ${CMAKE_CURRENT_LIST_DIR}/../Calibration/calibration.c
  selector.c
)
set_target_properties(pico_selector PROPERTIES OUTPUT_NAME Pico_Firmware)

pico_generate_pio_header(pico_selector ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(pico_selector ${CMAKE_CURRENT_LIST_DIR}/../Calibration/i2s_lsbj.pio)

target_include_directories(pico_selector PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${CMAKE_CURRENT_LIST_DIR}/../Calibration
  ${CMAKE_CURRENT_LIST_DIR}
)

if(PICO_PLATFORM MATCHES "rp2350")
  target_compile_definitions(pico_selector PRIVATE PICO_BOOT_TARGET_RP2350=1)
else()
  target_compile_definitions(pico_selector PRIVATE PICO_BOOT_TARGET_RP2040=1)
endif()

target_link_libraries(pico_selector PRIVATE
  pico_stdlib
  pico_multicore
  hardware_adc
  hardware_flash
  hardware_pio
  hardware_sync
  hardware_watchdog
)

pico_enable_stdio_usb(pico_selector 0)
pico_enable_stdio_uart(pico_selector 0)
pico_add_extra_outputs(pico_selector)
