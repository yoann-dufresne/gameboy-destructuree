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

# Test minimal : usage strictement conforme a la reference amont, coeur 0.
add_executable(phase1_smoke smoke.cpp)
target_link_libraries(phase1_smoke PRIVATE pico_stdlib pico_multicore hub75_jupfu)
pico_enable_stdio_usb(phase1_smoke 1)
pico_enable_stdio_uart(phase1_smoke 0)
pico_add_extra_outputs(phase1_smoke)

# --- Sondes : isolent un par un les ecarts qui rendaient l'affichage noir ---
function(ecran_sonde NOM CORE1 PLANS CIE_SEP CLK_KHZ)
    set(KICK 0)
    if(ARGC GREATER 5)
        list(GET ARGV 5 KICK)
    endif()
    set(CIBLE probe_${NOM})
    add_executable(${CIBLE} probe.cpp)
    target_compile_definitions(${CIBLE} PRIVATE
        PROBE_CORE1=${CORE1}
        PROBE_BITPLANES=${PLANS}
        PROBE_CIE_SEPARATE=${CIE_SEP}
        PROBE_CLK_KHZ=${CLK_KHZ}
        PROBE_NOM="${NOM}"
        PROBE_KICK=${KICK}
    )
    target_link_libraries(${CIBLE} PRIVATE pico_stdlib pico_multicore hub75_jupfu)
    pico_enable_stdio_usb(${CIBLE} 1)
    pico_enable_stdio_uart(${CIBLE} 0)
    pico_add_extra_outputs(${CIBLE})
endfunction()

#              nom             coeur1 plans cie_sep clk_khz
ecran_sonde(reference          0      10    1       266000)
ecran_sonde(coeur1             1      10    1       266000)
ecran_sonde(plans8             0       8    1       266000)
ecran_sonde(cie_communs        0      10    0       266000)
ecran_sonde(clk252             0      10    1       252000)
ecran_sonde(tous_ecarts        1       8    0       252000)
ecran_sonde(coeur1_kick        1      10    1       266000  1)
