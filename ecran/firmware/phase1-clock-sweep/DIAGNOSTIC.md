# Pourquoi l'affichage restait noir — mesure du 18/09/2026

Quatre écarts séparaient `smoke.cpp` (qui affiche) de ma première version (noire).
`probe.cpp` les isole **un par un et mesure le résultat sans regarder l'écran**.

## Méthode

Les six lignes de données R1 G1 B1 R2 G2 B2 et les cinq lignes d'adresse A–E sont
pilotées par le PIO, mais leurs pads restent lisibles par le CPU (`gpio_get_all`).
On échantillonne 200 ms et on compte les états non nuls :

- **adresses toujours à 0** → le pilote ne balaie pas ;
- **adresses actives, données toujours à 0** → il balaie mais ne diffuse que du noir ;
- **les deux actifs** → il affiche.

Verdict objectif, sans œil humain dans la boucle.

## Résultats

| Sonde | Écart testé | Données | Adresses | Verdict |
|---|---|---|---|---|
| `reference` | aucun | 100 % | 96 % | ✅ affiche |
| `plans8` | 8 plans BCM | 100 % | 96 % | ✅ affiche |
| `cie_communs` | canaux CIE communs | 100 % | 96 % | ✅ affiche |
| `clk252` | `clk_sys` 252 MHz | 100 % | 96 % | ✅ affiche |
| **`coeur1`** | **pilote sur le cœur 1** | 100 % | **0 %** | 🔴 **ne balaie pas** |
| `tous_ecarts` | les quatre ensemble | 100 % | 0 % | 🔴 ne balaie pas |
| **`coeur1_kick`** | **cœur 1 + correctif** | 100 % | **97 %** | ✅ **affiche** |

**Un seul coupable : le pilote sur le cœur 1.** Les trois autres écarts sont
innocents pris isolément, et le cumul des quatre ne fait pas pire que le cœur 1 seul.

> ℹ️ `B1` apparaît toujours à 0 : la mire de la sonde est en quadrants, et sa moitié
> haute (rouge / vert) ne contient aucun bleu. Ce n'est pas un défaut de câblage —
> les mires de la phase 0 avaient validé B1.

## Mécanisme

Explication cohérente avec les mesures, déduite du code du pilote — pas instrumentée
directement :

`create()` remplit `row_cmd_buffer2_` tandis que le DMA diffuse `row_cmd_buffer1_`,
qui n'a jamais été rempli. La bascule entre les deux dépend d'une interruption qui ne
survient pas au démarrage quand le pilote vit sur le cœur 1. Le PIO de ligne ne reçoit
alors que des zéros : **adresse 0 en permanence, `lit_cycles` à 0**. Le panneau reste
noir — mais le compteur de trames, lui, continue de tourner.

D'où le piège qui m'a coûté plusieurs essais : **`frame_rate_debug` annonçait un
rafraîchissement parfaitement stable (1138 Hz) alors que rien n'était affiché.** Un
rafraîchissement sain ne prouve pas qu'on affiche quelque chose.

## Correctif

Appeler `setBasisBrightness()` juste après `start()` : la reconstruction des commandes
de ligne provoque la bascule attendue.

```cpp
static void core1_entry(void) {
    driver.create();
    driver.start();
    driver.setBasisBrightness(6);  // <- sans cela, adresses figées a 0
    pilote_pret = true;
    while (true) tight_loop_contents();
}
```

`setIntensity()` doit produire le même effet : les deux passent par
`build_row_cmd_buffer()`.

## Pourquoi ça compte

Le plan prévoit le **cœur 1 pour l'affichage** (§3.1) et le cœur 0 pour le réseau.
Ce correctif est donc nécessaire, pas anecdotique. À reprendre en phase 1.

## Rejouer la mesure

```bash
cmake --build build                  # construit les 7 sondes
python3 <runner>                     # flashe et releve chaque verdict
```

Chaque sonde s'annonce sur la console USB puis imprime trois relevés et un verdict.
