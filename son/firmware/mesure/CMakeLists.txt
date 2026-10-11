cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(mesure_firmware C CXX ASM)

pico_sdk_init()

add_executable(mesure
    src/main.cpp
)

target_include_directories(mesure PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Pas de pilote WiFi : la radio reste éteinte, et ce qu'on mesure est le son de
# la console seul.
target_link_libraries(mesure PRIVATE
    pico_stdlib
    hardware_adc
    hardware_dma
)

# La console passe par l'USB, comme sur les autres firmwares du projet.
pico_enable_stdio_usb(mesure 1)
pico_enable_stdio_uart(mesure 0)

# Un printf ne doit jamais bloquer plus de 10 ms : au-delà de 42 ms, l'anneau
# du DMA serait réécrit avant d'être relu. Une ligne que le PC ne lit pas à
# temps est perdue ; la mesure, elle, continue.
target_compile_definitions(mesure PRIVATE
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000
)

pico_add_extra_outputs(mesure)
