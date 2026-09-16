cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

include(pico_sdk_import.cmake)

project(ecran_phase0 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(phase0_bringup bringup.c)

pico_generate_pio_header(phase0_bringup ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

target_link_libraries(phase0_bringup PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pio
)

# GP0/GP1 sont pris par R1/G1 : l'UART par defaut est inutilisable.
# La console passe donc par l'USB.
pico_enable_stdio_usb(phase0_bringup 1)
pico_enable_stdio_uart(phase0_bringup 0)

pico_add_extra_outputs(phase0_bringup)
