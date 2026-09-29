cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(ecran_firmware C CXX ASM)

# La puce WiFi CYW43 est pilotee par une liaison SPI en PIO dont le diviseur est
# dimensionne pour un clk_sys de 150 MHz (valeur par defaut : 2, soit 75 MHz de
# PIO). On tourne a 266 MHz pour l'horloge pixel : le diviseur doit suivre, sinon
# la liaison part a 133 MHz et la puce ne repond plus — « [CYW43] STALL: timeout ».
#   266 / 4 = 66,5 MHz, soit un peu en dessous des 75 MHz nominaux : marge saine.
# A recalculer si CLK_SYS_KHZ change dans include/config.h.
add_compile_definitions(CYW43_PIO_CLOCK_DIV_INT=4)

pico_sdk_init()

# Coeur du pilote HUB75 de JuPfu, vendorise sans les bibliotheques Pimoroni.
include(${CMAKE_CURRENT_LIST_DIR}/../vendor/hub75-jupfu/hub75-jupfu.cmake)

add_executable(ecran
    src/main.cpp
    src/display.cpp
    src/net/reseau.cpp
)

target_include_directories(ecran PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../commun  # pxl1.h
    ${CMAKE_CURRENT_LIST_DIR}/src/net   # lwipopts.h
)

target_link_libraries(ecran PRIVATE
    pico_stdlib
    pico_multicore
    hub75_jupfu
    # API raw de lwIP, pile servie en arriere-plan : pas de sockets, pas de RTOS.
    pico_cyw43_arch_lwip_threadsafe_background
)

# GP0/GP1 sont pris par R1/G1 : l'UART par defaut est inutilisable.
pico_enable_stdio_usb(ecran 1)
pico_enable_stdio_uart(ecran 0)

pico_add_extra_outputs(ecran)
