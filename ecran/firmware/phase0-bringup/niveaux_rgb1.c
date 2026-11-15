/**
 * Module ÉCRAN — trois niveaux fixes sur R1, G1, B1, lisibles au multimètre
 *
 * Diagnostic d'une moitié haute de dalle allumée en continu : on sait alors
 * qu'un fil de couleur arrive mal, sans savoir lequel ni pourquoi. Ce test ne
 * demande pas de suivre la console : chaque broche porte un niveau différent,
 * stable, qu'on reconnaît au premier coup d'œil.
 *
 *   GP0 → R1 (trou 1) : signal carré à 50 %, lu ≈ 1,6 V par un multimètre
 *   GP1 → G1 (trou 2) : 0 V
 *   GP2 → B1 (trou 3) : 3,3 V
 *
 * G1 est à 0 V exprès : une ligne qui ne sait plus redescendre (court-circuit
 * vers le 3,3 V, broche abîmée) ne se voit qu'ainsi. C'est le cas d'une
 * moitié de dalle verte en continu.
 *
 * Mesurer côté Pico (broches 1, 2, 4), puis côté dalle : un écart entre les
 * deux désigne le fil ; un mauvais niveau dès le Pico désigne le Pico. Une
 * entrée de dalle qui ne reçoit rien lit environ 3 V, tirée par la dalle.
 *
 * /OE reste à 3,3 V (sorties éteintes) : aucune LED ne s'allume.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define OEN_PIN 13

int main(void) {
    stdio_init_all();

    for (uint i = 0; i <= 13; ++i) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
    gpio_put(OEN_PIN, 1);

    gpio_put(1, 0);
    gpio_put(2, 1);

    /* GP0 : PWM, 50 % à ~10 kHz, bien plus vite que ce qu'un multimètre suit. */
    gpio_set_function(0, GPIO_FUNC_PWM);
    const uint tranche = pwm_gpio_to_slice_num(0);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 15.0f);
    pwm_config_set_wrap(&cfg, 999);
    pwm_init(tranche, &cfg, true);
    pwm_set_gpio_level(0, 500);

    while (true) {
        printf("R1 (trou 1, GP0) = ~1,6 V   G1 (trou 2, GP1) = 0 V   "
               "B1 (trou 3, GP2) = 3,3 V   /OE = 3,3 V\n");
        sleep_ms(2000);
    }
}
