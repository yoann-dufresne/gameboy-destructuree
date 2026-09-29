# Journal du nœud (v2)

Mesures, recettes et enquêtes de mise au point de ce firmware. Pour savoir ce que fait le
firmware et comment l'utiliser, voir le [README](README.md).

## Banc de la phase 5b — 29/09/2026

Le montage des phases 0 à 4, inchangé côté dalle : un Pico 2 W câblé en HUB75 sur une dalle
64 × 64. Il reçoit le firmware du nœud. Une tête en mode banc (`-DBANC_UNE_DALLE=ON`, canevas
64 × 64) lui est reliée par des **fils volants** — D0, D1, CLK, CS, VSYNC, RDY et trois
masses —, sans les résistances série prévues. Liaison à 16 MHz, soit 32 Mbit/s. Source :
`pixelpush` depuis le PC, en IDX8, via la box.

**Premier message :** `HELLO : rangee 0, canevas de 64 px`, reçu sans erreur au premier
démarrage.

**40 s, dalle éteinte, puis 3 minutes d'animation, dalle allumée :** 600 images reçues par la
tête et 600 publiées par le nœud par tranche de 10 s ; 0 erreur de CRC, 0 resynchronisation,
0 refus de validation, 0 VSYNC sans image. À l'œil : le point qui fait le tour du cadre
avance d'un pas régulier, l'animation est fluide. Aller-retour médian vu du PC : 5,4 ms.

**Critère de la liaison, 10 minutes :**

| | |
|---|---|
| Images publiées par le nœud | 34 573 |
| Erreurs de CRC | **0** |
| Resynchronisations, refus, VSYNC vides | 0, 0, 0 |
| Abandons de synchronisation côté tête | 0 |

**Aucune erreur sur la liaison en 10 minutes, à 16 MHz, sur des fils volants sans
résistances série.** Le critère de la phase 5b sur la liaison est atteint. Reste à relever la
latence à l'analyseur logique (GP18 de la tête, GP17 du nœud).

**Mais des gels visibles** pendant ces 10 minutes, qui ne venaient ni du nœud ni de la
liaison : la radio, entre le PC et la tête, via la box. Enquête et décision dans le
[journal de la tête](../tete/JOURNAL.md) : l'écran émet désormais son propre réseau WiFi.
