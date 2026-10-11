cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(banc_reseau C CXX ASM)

pico_sdk_init()

set(COMMUN ${CMAKE_CURRENT_LIST_DIR}/commun)
# Le dossier de secrets.h ; à changer seulement pour compiler sans toucher au sien.
set(BANC_SECRETS ${CMAKE_CURRENT_LIST_DIR}/include CACHE PATH "Dossier de secrets.h")
# Le serveur DHCP vendorisé et la référence de PXL1 viennent du module écran :
# reseau.md §4 les désigne pour les quatre réseaux.
set(DHCP ${CMAKE_CURRENT_LIST_DIR}/../../ecran/firmware/vendor/dhcpserver)
set(PXL ${CMAKE_CURRENT_LIST_DIR}/../../ecran/firmware/commun)

# ─────────────────────────────────────────────── R1 et R2 : la radio
#
# Un exécutable par rôle : le réseau est fixé à la compilation, le canal et le
# numéro se règlent par la console et sont gardés en flash.
function(banc_radio nom source)
    add_executable(${nom}
        ${source}
        ${COMMUN}/radio.cpp
        ${COMMUN}/outils.cpp
        ${COMMUN}/reglages.cpp
        ${DHCP}/dhcpserver.c
    )
    target_include_directories(${nom} PRIVATE
        ${BANC_SECRETS}                     # secrets.h
        ${COMMUN}                           # banc.h, lwipopts.h…
        ${DHCP}
        ${PXL}
    )
    set_source_files_properties(${source} ${COMMUN}/radio.cpp ${COMMUN}/outils.cpp
        ${COMMUN}/reglages.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    target_compile_definitions(${nom} PRIVATE ${ARGN}
        # Une ligne que le PC ne lit pas à temps est perdue : le trafic, lui,
        # ne doit jamais attendre l'USB.
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000
        # Le second cœur ne sert pas : la flash peut s'écrire sans le bloquer.
        PICO_FLASH_ASSUME_CORE1_SAFE=1
    )
    target_link_libraries(${nom} PRIVATE
        pico_stdlib
        hardware_flash
        pico_flash
        # API raw de lwIP, pile servie en arrière-plan, comme les modules.
        pico_cyw43_arch_lwip_threadsafe_background
    )
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()

banc_radio(banc_ap_image         radio/point_acces.cpp BANC_RESEAU=0)
banc_radio(banc_ap_son           radio/point_acces.cpp BANC_RESEAU=1)
banc_radio(banc_ap_manettes      radio/point_acces.cpp BANC_RESEAU=2)
banc_radio(banc_ap_cartouche     radio/point_acces.cpp BANC_RESEAU=3)
banc_radio(banc_client_image     radio/client.cpp      BANC_RESEAU=0)
banc_radio(banc_client_son       radio/client.cpp      BANC_RESEAU=1)
banc_radio(banc_client_manettes  radio/client.cpp      BANC_RESEAU=2)
banc_radio(banc_client_cartouche radio/client.cpp      BANC_RESEAU=3)
banc_radio(banc_faux_ecran       radio/faux_ecran.cpp)

# ─────────────────────────────────────────────── R3 : le bus de la console
function(banc_bus nom source)
    add_executable(${nom} ${source} ${COMMUN}/outils.cpp)
    target_include_directories(${nom} PRIVATE ${COMMUN} ${CMAKE_CURRENT_LIST_DIR}/bus)
    set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    target_compile_definitions(${nom} PRIVATE ${ARGN})
    target_link_libraries(${nom} PRIVATE pico_stdlib hardware_i2c pico_i2c_slave)
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()

banc_bus(banc_bus_maitre            bus/maitre.cpp)
banc_bus(banc_bus_esclave_sniffer   bus/esclave.cpp BUS_ADRESSE=BUS_ADRESSE_SNIFFER)
banc_bus(banc_bus_esclave_son       bus/esclave.cpp BUS_ADRESSE=BUS_ADRESSE_SON)
banc_bus(banc_bus_esclave_doublure  bus/esclave.cpp BUS_ADRESSE=BUS_ADRESSE_DOUBLURE)
