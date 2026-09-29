# Journal du nœud WiFi autonome (v1)

Mesures, recettes et enquêtes de mise au point de ce firmware, reprises telles qu'elles ont
été consignées. Pour savoir ce que fait le firmware et comment l'utiliser, voir le
[README](README.md).

## Le double tampon abandonné

> Une première version ajoutait un double tampon par-dessus. Il n'apportait rien et
> était un piège : `present()` basculait vers un tampon que l'appelant n'avait pas
> rempli, si bien qu'afficher deux fois la même image alternait **image / noir** —
> scintillement à 60 Hz. Corrigé le 18/09/2026.

## La réentrance de `update_bgr()`

La démo amont publie à 100 Hz, donc une construction tient dans 10 ms : c'est le
plancher retenu (`display::PERIODE_MIN_US`).

Le piège se manifeste quand les publications ne sont **pas régulières** — chez nous
elles sont déclenchées par l'arrivée du dernier paquet, et le WiFi livre par rafales.
Deux trames peuvent se terminer à 2 ms d'intervalle. D'où le symptôme : des
clignotements intermittents, jamais périodiques.

La parade retenue est un accesseur `occupe()` **ajouté au pilote vendorisé**
(cf. son `PROVENANCE.txt`) : on publie quand le pilote a fini, pas après un délai
deviné. Une première version utilisait un plancher de 10 ms repris de la cadence
de la démo amont — mesure faite, **une construction prend 2,23 ms en moyenne et
5,8 ms au pire** : le plancher était 4,5 fois trop conservateur et écartait des
trames sans raison, d'où des chutes de cadence visibles par à-coups.

## Recette — résultats du 18/09/2026

Une dalle 64×64, `clk_sys` 266 MHz (horloge pixel 29,6 MHz), 10 plans BCM,
canaux CIE séparés, luminosité de base 6.

| Critère du plan §5 | Mesure | |
|---|---|---|
| Mire fixe affichée | sonde : `AFFICHE`, adresses actives 96 % | ✅ |
| Rafraîchissement ≥ 150 Hz | **788 Hz** | ✅ 5× la cible |
| Cœur 0 saturé ne dégrade pas | **788 Hz, min = max**, au repos comme sous charge | ✅ |
| Damier 1 px sans ghosting | grain fin régulier, aucune traînée | ✅ |
| Absence de scintillement | stable dans les deux régimes | ✅ |

Le rafraîchissement est **rigoureusement constant** entre les trois phases de la
recette : au repos, cœur 0 saturé par du calcul continu, et publication à 60 Hz. C'est
la démonstration objective que l'affichage est autonome.

> La mesure vient du compteur de trames du pilote, adossé à la fin de transfert DMA —
> pas d'un oscilloscope sur `/OE` comme l'envisageait le plan. C'est la même grandeur,
> relevée en interne.

## Le bon instrument pour un écran noir ou clignotant

Deux mesures ont servi, et l'une est trompeuse :

- **`/OE`** pilote l'activation globale de la dalle, **pas le contenu**. Une trame
  entièrement noire a exactement le même rapport cyclique qu'une trame pleine. Mesurer
  `/OE` ne dit donc rien d'un problème d'image — vérifié : 58,6 % contre 58,5 % entre
  un régime sain et un régime qui clignotait visiblement.
- **Les lignes de données R1..B2** portent le contenu : c'est là qu'il faut regarder.
- **Les lignes d'adresse A..E** distinguent « le pilote ne balaie pas » de « il balaie
  du noir ».

Et le compteur de trames du pilote ne prouve rien : il a annoncé 788 Hz parfaitement
stables pendant que l'écran était noir, puis pendant qu'il clignotait.

## Régime nominal

Publication à 60 Hz d'une image statique, relevé toutes les 15 s :

```
  regime nominal   donnees 85 %  adresses 96 %  -> AFFICHE
  regime nominal   allumage moyen 58,3 %  min 57,8  max 59,3  ecart 1,5 pt
  900 trames publiees
```

900 trames en 15 s : la cadence est tenue exactement. En phase 2, c'est la réception
d'une trame réseau qui déclenchera `present()` à la place du réveil périodique.

## Empreinte

114 ko de RAM sur 520, 47 ko de flash, pour une dalle en 10 plans.
À `CHAIN_LEN = 3` les plans de bits triplent : prévoir ~370 ko. Ça passe, sans marge
confortable — le passage à 8 plans est le repli si nécessaire.
