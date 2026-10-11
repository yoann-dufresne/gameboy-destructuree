/**
 * Module SON — firmware de mesure, configuration
 *
 * Le banc de la phase 0 : un Pico 2 W, l'étage d'entrée de
 * docs/prise-son-mgb.md §3 monté sur plaque d'essai, et ce firmware. Il lit
 * les deux fils du son et rend compte de ce qu'il voit, en volts.
 *
 * La radio n'est jamais allumée : ce que ce firmware mesure est le son de la
 * console, sans le bruit du WiFi.
 */
#pragma once

/* ─────────────────────────────────────────────────────────────── brochage
 *
 * GP26 à GP28 sont les seules entrées de l'ADC sorties sur le Pico 2 W (GP29
 * sert à la radio). Elles NE tolèrent PAS le 5 V : jamais plus de 3,3 V. */
#define PIN_VOIE_A     26  /* ADC0, broche 31 — fil de VR1 broche 5 : la gauche, d'après le schéma */
#define PIN_VOIE_B     27  /* ADC1, broche 32 — fil de VR1 broche 4 : la droite, d'après le schéma */
#define ADC_ENTREE_A    0
#define ADC_ENTREE_B    1
/* Masse du montage : AGND, broche 33. Polarisation : ADC_VREF, broche 35. */

/* ──────────────────────────────────────────────────────────────── cadence
 *
 * L'ADC a sa propre horloge de 48 MHz. Une conversion toutes les 125
 * périodes, alternativement A et B : 384 000 conversions par seconde, soit
 * 192 000 par voie. C'est la cadence prévue pour la capture définitive
 * (plan §4) : le banc voit ce que verra la version finale. */
#define ADC_HORLOGE_HZ  48000000u
#define ADC_PERIODE          125u   /* en périodes d'horloge, 96 au minimum */
#define F_VOIE_HZ       (ADC_HORLOGE_HZ / ADC_PERIODE / 2)   /* 192 000 */

/* ────────────────────────────────────────────────────────── anneau du DMA
 *
 * Le DMA écrit sans fin dans un anneau de 2^15 octets, le plus grand que
 * permette le RP2350 : 16 384 échantillons, 42,7 ms. La boucle principale
 * doit le relire plus vite qu'il ne se remplit ; un retard plus long est
 * compté (`retards`), car les mesures de la fenêtre en cours sont alors
 * fausses. */
#define ANNEAU_BITS  15

/* Une mesure toutes les 50 ms : 9 600 échantillons par voie. Assez court pour
 * suivre un son bref, assez long pour qu'un son grave y fasse une période. */
#define FENETRE_MS   50

/* L'attente maximale de l'USB est réglée dans CMakeLists.txt : un printf ne
 * doit jamais bloquer assez longtemps pour que l'anneau déborde. */

/* ────────────────────────────────────────────────── conversion en volts
 *
 * Pleine échelle de l'ADC : ADC_VREF, le 3,3 V du Pico filtré. La datasheet
 * du Pico 2 W lui prête environ 30 mV de moins : sans importance ici. */
#define ADC_VREF_V      3.3f
#define ADC_MAX         4095

/* Gain du montage du banc, Rb / (Rs + Rb), pour donner une idée de la
 * tension au point de prise sur la console. Banc de la phase 0 :
 * Rs = Rb = 10 kΩ. Si Rb change, changer ce gain ; le script
 * tools/mesure_son.py, lui, prend Rs et Rb en option. */
#define GAIN_MONTAGE    0.5f
