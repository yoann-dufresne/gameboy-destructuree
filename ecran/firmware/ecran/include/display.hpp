/**
 * Module ÉCRAN — façade d'affichage
 *
 * Cache entièrement le pilote HUB75 vendorisé. Le reste du firmware ne parle que
 * de « un tampon arrière, on le remplit, on le publie ».
 *
 * Découpage des cœurs, conforme au plan §6.5 :
 *   cœur 0 — remplit le tampon arrière (réseau, mise à l'échelle) puis publie ;
 *   cœur 1 — possède le pilote : création, interruptions, conversion en plans
 *            de bits. Le flux vers la dalle est en PIO + DMA, donc ~0 % de CPU.
 */
#pragma once

#include <cstdint>

namespace display {

/* Démarre le pilote sur le cœur 1 et attend qu'il soit opérationnel.
 * À appeler depuis le cœur 0, une seule fois. */
void init();

/* Tampon arrière au format BGR, DISPLAY_W x DISPLAY_H, 3 octets par pixel,
 * balayage ligne par ligne. Valide jusqu'au prochain present(). */
uint8_t *backbuffer();

/* Publie le tampon interne. */
void present();

/* Publie un tampon externe (celui de la réception, par exemple). Le tampon doit
 * rester stable pendant l'appel. */
void present(const uint8_t *bgr);

/* Vrai tant que la construction des plans de bits amorcée par la dernière
 * publication n'est pas terminée.
 *
 * present() n'est synchrone que pour le remaniement des pixels : la construction
 * se poursuit par interruption à travers toute la séquence BCM, et le pilote n'a
 * aucun garde-fou de réentrance. **Ne jamais publier tant que ceci est vrai.**
 * Un plancher temporel ne suffit pas : la durée réelle d'une construction n'est
 * pas garantie. */
bool occupe();

/* Luminosité de base, 1 à 255. Plus haut = plus lumineux et moins rafraîchi. */
void set_brightness(uint8_t basis);

/* Identité du nœud lue sur les straps (0 à NODE_COUNT-1). */
uint8_t node_id();

} // namespace display
