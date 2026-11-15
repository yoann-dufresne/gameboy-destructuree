cmake_minimum_required(VERSION 3.13)

# Le noeud n'utilise pas le WiFi : la carte pico2 convient a un Pico 2 comme a
# un Pico 2 W, dont la puce radio reste simplement eteinte.
set(PICO_BOARD pico2 CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(noeud_firmware C CXX ASM)

pico_sdk_init()

# Coeur du pilote HUB75 de JuPfu, vendorise sans les bibliotheques Pimoroni.
include(${CMAKE_CURRENT_LIST_DIR}/../vendor/hub75-jupfu/hub75-jupfu.cmake)

add_executable(noeud
    src/main.cpp
    src/reception.cpp
    ../commun/affichage/display.cpp
)

pico_generate_pio_header(noeud ${CMAKE_CURRENT_LIST_DIR}/src/lien_rx.pio)

target_include_directories(noeud PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../commun            # liaison.h
    ${CMAKE_CURRENT_LIST_DIR}/../commun/affichage  # display.hpp
)

# Avertissements sur nos sources seulement : l'en-tete genere du pilote
# vendorise, inclus par display.cpp, en produit qu'on ne corrigera pas.
set_source_files_properties(src/main.cpp src/reception.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_link_libraries(noeud PRIVATE
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hub75_jupfu
)

# GP0/GP1 sont pris par R1/G1 : la console passe par l'USB.
pico_enable_stdio_usb(noeud 1)
pico_enable_stdio_uart(noeud 0)

# Dalles par chaine : 1 au banc de la phase 5b, 3 en 5c (-DCHAIN_LEN=3).
set(CHAIN_LEN 1 CACHE STRING "Nombre de dalles de la chaine")
# Diviseur de l'horloge pixel, fractionnaire possible : 1 = 29,6 MHz, 1.25 = 23,7 MHz,
# 1.5 = 19,7 MHz, 2 = 14,8 MHz. Mesure du 26/10/2026 sur une chaine de 3 dalles :
# net jusqu'a 23,7 MHz, parasites a 26,9 MHz, image detruite a 29,6. Une dalle
# seule reste nette a 29,6 MHz (16/09/2026).
if(CHAIN_LEN GREATER 1)
    set(HUB75_CLKDIV_DEFAUT 1.5)
else()
    set(HUB75_CLKDIV_DEFAUT 1)
endif()
set(HUB75_CLKDIV ${HUB75_CLKDIV_DEFAUT} CACHE STRING "Diviseur de l'horloge pixel des dalles")
# Mire d'attente remplacee par des bandes de couleur : diagnostic des colonnes.
option(MIRE_COLONNES "Mire de diagnostic des colonnes" OFF)
target_compile_definitions(noeud PRIVATE CHAIN_LEN=${CHAIN_LEN} HUB75_CLKDIV=${HUB75_CLKDIV}
    MIRE_COLONNES=$<BOOL:${MIRE_COLONNES}>)

pico_add_extra_outputs(noeud)
