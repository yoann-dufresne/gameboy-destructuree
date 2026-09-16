cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

include(pico_sdk_import.cmake)

project(ecran_phase0 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# GP0/GP1 sont pris par R1/G1 : l'UART par defaut est inutilisable,
# la console passe par l'USB pour toutes les cibles.
function(ecran_cible nom source)
    add_executable(${nom} ${source})
    target_link_libraries(${nom} PRIVATE pico_stdlib pico_multicore hardware_pio)
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()

# 1. Suite de mires de validation (le cas nominal)
ecran_cible(phase0_bringup bringup.c)
pico_generate_pio_header(phase0_bringup ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

# 2. Diagnostic : quels bits d'adresse A-E arrivent reellement a la dalle
ecran_cible(phase0_diag_adresse diag_adresse.c)
pico_generate_pio_header(phase0_diag_adresse ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

# 3. Diagnostic : continuite fil par fil, au multimetre
ecran_cible(phase0_walk_gpio walk_gpio.c)
