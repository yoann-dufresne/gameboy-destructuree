cmake_minimum_required(VERSION 3.13)

# Les deux cibles sont construites pour la carte pico2, sans radio : celle de la
# tete tourne aussi sur son Pico 2 W, dont elle ne touche jamais GP23 a GP25 ni
# GP29, les broches de la puce WiFi.
set(PICO_BOARD pico2 CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)

include(pico_sdk_import.cmake)

project(recette_carte C CXX ASM)

pico_sdk_init()

function(recette_cible nom source)
    add_executable(${nom} ${source} src/banc.c)
    target_include_directories(${nom} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    target_link_libraries(${nom} PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        hardware_adc
    )
    # GP0 et GP1 sont pris par la liaison ou la nappe : la console passe par l'USB.
    pico_enable_stdio_usb(${nom} 1)
    pico_enable_stdio_uart(${nom} 0)
    pico_add_extra_outputs(${nom})
endfunction()

# Avertissements sur nos sources seulement.
set_source_files_properties(src/banc.c src/tete.c src/noeud.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# 1. Pour le Pico 2 W de la tete, support U1
recette_cible(recette_tete src/tete.c)

# 2. Pour les Pico 2 des noeuds, supports U2 a U4
recette_cible(recette_noeud src/noeud.c)
