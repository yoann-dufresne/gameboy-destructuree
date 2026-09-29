cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(tete_firmware C CXX ASM)

# La liaison SPI de la puce WiFi est en PIO, cadencee depuis clk_sys. A 266 MHz
# le diviseur par defaut (2) la ferait partir a 133 MHz : « [CYW43] STALL ».
#   266 / 4 = 66,5 MHz, un peu sous les 75 MHz nominaux : marge saine.
# A recalculer si CLK_SYS_KHZ change dans include/config.h.
add_compile_definitions(CYW43_PIO_CLOCK_DIV_INT=4)

# Nom annonce au DHCP : la tete se retrouve dans la liste des baux de la box.
add_compile_definitions(CYW43_HOST_NAME="ecran")

# Banc de la phase 5b : une liaison, un noeud, une dalle — canevas de 64 x 64.
option(BANC_UNE_DALLE "Canevas reduit a une seule dalle, une seule liaison" OFF)
add_compile_definitions(BANC_UNE_DALLE=$<BOOL:${BANC_UNE_DALLE}>)

# Horloge de la liaison vers les noeuds, en kHz (defaut dans config.h : 16000).
set(LIEN_HORLOGE_KHZ "" CACHE STRING "Horloge de la liaison, en kHz")
if(LIEN_HORLOGE_KHZ)
    add_compile_definitions(LIEN_HORLOGE_KHZ=${LIEN_HORLOGE_KHZ})
endif()

pico_sdk_init()

add_executable(tete
    src/main.cpp
    src/lien.cpp
    src/net/reseau.cpp
)

pico_generate_pio_header(tete ${CMAKE_CURRENT_LIST_DIR}/src/lien.pio)

target_include_directories(tete PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../commun  # pxl1.h, pxl2.h
    ${CMAKE_CURRENT_LIST_DIR}/src/net    # lwipopts.h
)

target_compile_options(tete PRIVATE -Wall -Wextra)

target_link_libraries(tete PRIVATE
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_pio
    # API raw de lwIP, pile servie en arriere-plan : pas de sockets, pas de RTOS.
    pico_cyw43_arch_lwip_threadsafe_background
)

# La console passe par l'USB, comme sur les noeuds : GP0/GP1 y sont pris par la
# liaison L0.
pico_enable_stdio_usb(tete 1)
pico_enable_stdio_uart(tete 0)

pico_add_extra_outputs(tete)
