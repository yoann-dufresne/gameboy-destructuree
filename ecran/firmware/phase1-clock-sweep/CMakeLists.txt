cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(ecran_phase1 C CXX ASM)

pico_sdk_init()

# Coeur du pilote HUB75 de JuPfu, vendorise sans les bibliotheques Pimoroni.
include(${CMAKE_CURRENT_LIST_DIR}/../vendor/hub75-jupfu/hub75-jupfu.cmake)

add_executable(phase1_clock_sweep clock_sweep.cpp)

target_link_libraries(phase1_clock_sweep PRIVATE
    pico_stdlib
    pico_multicore
    hub75_jupfu
)

# GP0/GP1 sont pris par R1/G1 : console sur USB.
pico_enable_stdio_usb(phase1_clock_sweep 1)
pico_enable_stdio_uart(phase1_clock_sweep 0)

pico_add_extra_outputs(phase1_clock_sweep)
