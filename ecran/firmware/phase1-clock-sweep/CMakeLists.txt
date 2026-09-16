cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Carte cible")
set(PICO_PLATFORM rp2350 CACHE STRING "Plateforme")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include(pico_sdk_import.cmake)

project(ecran_phase1 C CXX ASM)

pico_sdk_init()

# Coeur du pilote HUB75 de JuPfu, vendorise sans les bibliotheques Pimoroni.
include(${CMAKE_CURRENT_LIST_DIR}/../vendor/hub75-jupfu/hub75-jupfu.cmake)

# Une cible par frequence testee : le diviseur est un constexpr, donc le pilote
# est initialise proprement a cette cadence-la et rien n'est touche ensuite.
foreach(MHZ 28 26 24 22 20 18 16 14 12 10)
    set(NOM phase1_clk_${MHZ}mhz)
    add_executable(${NOM} clock_test.cpp)
    target_compile_definitions(${NOM} PRIVATE PIXEL_CLOCK_KHZ=${MHZ}000)
    target_link_libraries(${NOM} PRIVATE pico_stdlib pico_multicore hub75_jupfu)
    # GP0/GP1 sont pris par R1/G1 : console sur USB.
    pico_enable_stdio_usb(${NOM} 1)
    pico_enable_stdio_uart(${NOM} 0)
    pico_add_extra_outputs(${NOM})
endforeach()
