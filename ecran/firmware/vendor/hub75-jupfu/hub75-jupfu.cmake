# Cible du cœur du pilote HUB75 de JuPfu, sans les bibliothèques Pimoroni.
# Ajout local — ce fichier ne vient pas de l'amont. Voir PROVENANCE.txt.

add_library(hub75_jupfu STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/hub75.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/fm6126a.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/rul6024.cpp
)

pico_generate_pio_header(hub75_jupfu ${CMAKE_CURRENT_LIST_DIR}/src/hub75.pio)

target_include_directories(hub75_jupfu PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Neutralise la dépendance à pico_graphics de Pimoroni : on alimente le
# pilote par update_bgr() et non par update(PicoGraphics*).
target_compile_definitions(hub75_jupfu PUBLIC USE_PICO_GRAPHICS=0)

target_link_libraries(hub75_jupfu PUBLIC
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
)
