/* Faux pico-sdk, juste ce qu'il faut pour compiler banc.c sur PC et simuler
 * la carte : un modèle électrique simple de chaque broche. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int uint;

#define GPIO_IN 0
#define GPIO_OUT 1
enum gpio_drive_strength {
    GPIO_DRIVE_STRENGTH_2MA,
    GPIO_DRIVE_STRENGTH_4MA,
    GPIO_DRIVE_STRENGTH_8MA,
    GPIO_DRIVE_STRENGTH_12MA
};

void gpio_init(uint g);
void gpio_set_dir(uint g, bool sortie);
void gpio_set_pulls(uint g, bool haut, bool bas);
void gpio_set_drive_strength(uint g, enum gpio_drive_strength f);
void gpio_put(uint g, bool v);
bool gpio_get(uint g);
uint32_t gpio_get_all(void);
void busy_wait_us(uint32_t us);
uint32_t time_us_32(void);
uint32_t get_rand_32(void);
void multicore_launch_core1(void (*f)(void));
void adc_select_input(uint e);
uint16_t adc_read(void);

static inline void tight_loop_contents(void) {}
static inline void __dmb(void) {}
#define __not_in_flash_func(f) f

typedef struct {
    volatile uint32_t gpio_in, gpio_out, gpio_togl;
} faux_sio_t;
extern faux_sio_t faux_sio;
#define sio_hw (&faux_sio)
