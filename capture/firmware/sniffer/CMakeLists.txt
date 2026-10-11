cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(sniffer_firmware C CXX ASM)

# clk_sys reste a 150 MHz, la valeur par defaut : la liaison SPI du CYW43 en
# derive et son diviseur est dimensionne pour ca. Le module ecran, qui tourne a
# 266 MHz, a du le recalculer — une journee perdue sur un « [CYW43] STALL ».
# Rien a ajuster ici tant que CLK_SYS_KHZ vaut 150000 dans include/config.h.

pico_sdk_init()

add_executable(sniffer
    src/main.cpp
    src/capture.cpp
    src/net/reseau.cpp
)

# Le programme PIO est assemblé à la compilation ; capture.pio.h en sort.
pico_generate_pio_header(sniffer ${CMAKE_CURRENT_LIST_DIR}/src/capture.pio)

target_include_directories(sniffer PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/net   # lwipopts.h
)

target_link_libraries(sniffer PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    # API raw de lwIP, pile servie en arriere-plan : pas de sockets, pas de RTOS.
    pico_cyw43_arch_lwip_threadsafe_background
)

# ⚠️ GP0/GP1 portent LD0/LD1 : l'UART par defaut du SDK est inutilisable.
# La console passe donc par l'USB. Meme piege que sur le module ecran, ou
# GP0/GP1 portaient R1/G1.
pico_enable_stdio_usb(sniffer 1)
pico_enable_stdio_uart(sniffer 0)

pico_add_extra_outputs(sniffer)

# ─── Banc R2 du réseau (reseau.md §7, banc-reseau/) ─────────────────────────
# Le sniffer émet lui-même le réseau image, en 192.168.4.1, et envoie une copie
# de chaque tranche à chaque écran, quatre au plus. DÉSACTIVÉ par défaut : le
# module est clos, et ce mode ne sert qu'au banc. Désactivé, rien ne change :
# ni les sources compilées, ni leurs options. Le serveur DHCP est celui que le
# module écran vendorise (reseau.md §4).
option(POINT_ACCES "Le sniffer emet le reseau image (banc R2)" OFF)
if(POINT_ACCES)
    set(DHCP ${CMAKE_CURRENT_LIST_DIR}/../../../ecran/firmware/vendor/dhcpserver)
    target_sources(sniffer PRIVATE ${DHCP}/dhcpserver.c)
    target_include_directories(sniffer PRIVATE ${DHCP})
    target_compile_definitions(sniffer PRIVATE POINT_ACCES=1)
endif()
