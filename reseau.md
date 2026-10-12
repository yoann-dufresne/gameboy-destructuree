# Le réseau de la Game Boy Pocket déstructurée

*Document transversal : comment les modules se parlent, et ce que chacun en attend.*
Version 1 — 11/10/2026 : un réseau par type de périphérique, chacun émis par la console.

Chaque module décrit son protocole et son matériel dans sa propre documentation. Ce document
ne fixe que ce qu'ils partagent : qui émet quel réseau, sur quel canal, à quelles adresses, et
comment les Pico de la console se parlent entre eux.

---

## 0. Décisions

| Sujet | Décision ou proposition | Tranchée par |
|---|---|---|
| **Qui parle à qui** | les périphériques ne parlent qu'à la console ; la console parle à chacun | toi, 11/10/2026 |
| **Liaisons directes** | un seul saut radio entre la console et chaque périphérique : aucun routeur, aucun relais | toi, 11/10/2026 |
| **Un microcontrôleur par type** | sur la console, un microcontrôleur par type de périphérique externe : image, son, manettes, cartouche. **Chacun émet son propre réseau**, dont il est le point d'accès | toi, 11/10/2026 |
| **Plusieurs périphériques par type** | jusqu'à 4 par réseau, la limite d'un Pico 2 W en point d'accès : 4 écrans qui montrent la même partie, 4 manettes qui jouent ensemble… Les conflits entre manettes se régleront plus tard | toi, 11/10/2026 |
| **Plusieurs écrans** | une copie de chaque paquet par écran, pas de vraie diffusion (§2.2) | R2 |
| **Les canaux** | trois canaux, ou un seul pour toute la console : le banc de coexistence tranche (§3) | R1 |
| **Les adresses** | fixes pour les périphériques ; un serveur DHCP sur chaque point d'accès, pour le PC (§4) | — |
| **Entre les Pico de la console** | un bus filaire I²C, l'injecteur en maître (§5) | R3 |

---

## 1. Le principe

```
                          CONSOLE PRINCIPALE
   ┌──────────────────────────────────────────────────────────────┐
   │  sniffer           source de son      injecteur      doublure │
   │  (capture)         (son)              (manette)     (cartouche)│
   │     │                  │                  │               │    │
   │     └──────────────────┴──── bus I²C ─────┴───────────────┘    │
   └─────┼──────────────────┼──────────────────┼───────────────┼────┘
    réseau « image »   réseau « son »   réseau « manettes »  réseau « cartouche »
         │                  │                  │               │
   1 à 4 écrans     1 à 4 haut-parleurs   1 à 4 manettes    le lecteur
```

**Pourquoi des liaisons directes.** Deux sauts radio ont été mesurés, et ils gèlent : en
passant par une box, une image a mis jusqu'à 122 ms à traverser la radio, dans 33 tranches de
10 s sur 57 ([module écran](ecran/docs/plan-firmware.md), §2.7). En direct, le sniffer mesure
1,85 ms d'aller-retour moyen. Un flux relayé occupe aussi l'air deux fois.

**Pourquoi un réseau par type.** Un Pico 2 W en point d'accès n'accepte que 4 clients
(datasheet, §3.8). Un seul réseau pour toute l'installation serait plein dès le premier
exemplaire de chaque périphérique. Un réseau par type laisse 4 places à chacun, et chaque
module garde son réseau à lui : il s'éprouve seul, sans dépendre des autres.

**Le prix** : quatre radios dans la console, à quelques centimètres les unes des autres. C'est
le premier risque, et le banc R1 le mesure avant tout changement dans les modules.

---

## 2. Les quatre réseaux

### 2.1 Vue d'ensemble

| Réseau | Point d'accès | Clients | Protocole, port | Trafic au plus | Canal proposé | Adresses |
|---|---|---|---|---|---|---|
| **image** | le sniffer ([capture](capture/)) | 1 à 4 écrans ([ecran](ecran/)) | `PXL1`, `PXL2` ; 4242 | 4 × 2,8 = **11,2 Mbit/s** | 11 | 192.168.4.0/24 |
| **son** | la source de son ([son](son/)) | 1 à 4 haut-parleurs | `SON1` ; 4243 | 4 × 1,6 = **6,2 Mbit/s** | 1 | 192.168.5.0/24 |
| **manettes** | l'injecteur ([manette](manette/)) | 1 à 4 manettes | `MAN1` ; 4244 | négligeable | 6 | 192.168.6.0/24 |
| **cartouche** | la doublure ([cartouche](cartouche/)) | le lecteur | `CAR1` ; 4245 | 8 Mbit/s pendant 1 s à l'insertion, presque rien sinon | 6 | 192.168.7.0/24 |

Les exigences de chaque flux sont dans le plan de son module ; en bref : l'image veut quelques
millisecondes et aucun gel ; le son, moins de 0,1 % de pertes dans un budget de 30 à 60 ms ; la
manette, 10 ms en médiane et 99 % sous 30 ms, et une connexion en moins de 3 s au réveil ; la
cartouche supporte des secondes, sauf le vibreur.

### 2.2 Une copie par périphérique

Le point d'accès envoie **une copie de chaque paquet à chaque client**. Une vraie diffusion, un
seul paquet pour tous, part en WiFi au débit le plus lent du réseau, sans accusé ni
réémission ; un client en économie d'énergie la retarde jusqu'à sa prochaine balise. Le module
son l'a écartée pour ces raisons.

Quatre copies coûtent quatre fois l'air, mais chacune est fiable. Le réseau image est le plus
chargé : 11,2 Mbit/s, quand le module écran a mesuré qu'un Pico en reçoit 17,7 en continu. Reste
à savoir si le sniffer en **émet** autant pendant qu'il capture : c'est R2. Si l'air manque, on
essaiera la vraie diffusion, à un débit imposé.

### 2.3 Comment le point d'accès connaît ses clients

Même règle partout : **le client se signale, le point d'accès répond à ceux qui se sont
signalés.** Chaque client envoie un premier paquet dès qu'il est connecté, puis un battement ;
le point d'accès tient une table de 4 clients au plus, et oublie celui qu'il n'entend plus
depuis 2 s. Les protocoles existants s'y prêtent déjà : les accusés de `PXL1` et `MAN1`,
l'annonce de `CAR1`. `SON1` gagne un battement du haut-parleur.

### 2.4 Les modules restent autonomes

Chaque réseau appartient à un module, et chaque module s'éprouve sans les autres : on remplace
le point d'accès par un PC, ou un client par un PC. Hors de la Game Boy, la tête de l'écran
garde son mode point d'accès, pour qu'un PC lui envoie des images directement ; dans
l'installation, elle devient cliente du réseau image.

---

## 3. Les canaux

Le Pico 2 W ne connaît que le 2,4 GHz, qui n'a que **trois canaux sans recouvrement** : 1, 6 et
11. Quatre réseaux dans la console, à quelques centimètres les uns des autres : deux plans
possibles.

| Plan | Comment | Le risque |
|---|---|---|
| **Trois canaux** | l'image seule sur le 11 ; le son sur le 1 ; les manettes et la cartouche, peu chargées, ensemble sur le 6 | à quelques centimètres, une antenne qui émet sur le 1 peut assourdir sa voisine qui écoute sur le 6 : les canaux ne se recouvrent pas, mais la puissance reçue d'une voisine si proche dépasse ce que le filtre du récepteur rejette |
| **Un seul canal** | les quatre points d'accès sur le même canal | ils s'entendent et parlent à tour de rôle, sans s'assourdir ; mais tout doit tenir dans un canal : environ 18 Mbit/s, près de ce qu'un Pico a montré |

**Proposition : trois canaux**, et le banc R1 les met à l'épreuve d'abord. Sur le lieu
d'exposition, les canaux se choisissent d'après un relevé des réseaux voisins : ils sont
réglables dans le firmware de chaque point d'accès.

Si les antennes se gênent : les écarter et les orienter, baisser la puissance d'émission
(les périphériques sont à quelques mètres, pas à cinquante), ou passer au plan à un canal.

---

## 4. Les adresses

Sur chaque réseau :

| Adresse | Qui |
|---|---|
| .1 | le point d'accès, dans la console |
| .2 à .5 | les périphériques, **adresses fixes** : le numéro du périphérique, de 1 à 4, réglé dans son firmware |
| .16 à .23 | DHCP : le PC de mise au point, les outils |

Fixes, parce que la manette doit se connecter en moins de 3 s au réveil, et que le point
d'accès reconnaît ainsi chaque périphérique à son adresse. Le serveur DHCP minimal du module
écran (`ecran/firmware/vendor/dhcpserver/`) sert aux quatre réseaux.

**Le PC de mise au point prend une place** : au banc, trois périphériques et le PC.

Noms des réseaux : `gb-image`, `gb-son`, `gb-manettes`, `gb-cartouche`, en WPA2. Les mots de
passe restent dans les `secrets.h`, jamais versionnés.

---

## 5. Le bus de la console

Les quatre microcontrôleurs de la console ne partagent plus de réseau : ils se parlent par fil.

| De | Vers | Quoi |
|---|---|---|
| injecteur | tous | marche, arrêt |
| injecteur | sniffer | la luminosité, que le sniffer transmet aux écrans ; le message « aucune manette », qu'il incruste |
| injecteur | source de son | le volume, que la source transmet aux haut-parleurs |
| doublure | injecteur | le feu vert : la copie de la cartouche est prête, la console peut s'allumer |

**Proposition : un bus I²C**, deux fils et la masse, l'injecteur en maître : c'est lui qui
reçoit les manettes et allume la console. Il interroge chacun 50 fois par seconde, ce qui suffit
largement à des ordres que personne ne remarque à 20 ms près. Le SDK du Pico gère le RP2350 en
esclave I²C. Deux broches par microcontrôleur : sur le sniffer, GP6 et GP7 (I2C1) sont libres
d'après le brochage du module capture ; la doublure monte à 34 broches sur 41.

**Le format est fixé** dans [`bus_console.h`](banc-reseau/firmware/commun/bus_console.h),
qui fait foi et que les quatre firmwares copieront. En bref :

| | |
|---|---|
| Vitesse, cadence | 400 kHz ; 50 interrogations par seconde et par esclave, 7 % du bus |
| Broches | I2C1, GP6 (SDA) et GP7 (SCL) ; une seule paire de tirages de 4,7 kΩ vers 3,3 V pour tout le bus |
| Adresses | sniffer 0x41, source de son 0x42, doublure 0x43 |
| L'ordre, 10 octets | version, numéro, drapeaux (marche ; « aucune manette »), luminosité, volume, secondes d'absence de la manette, secondes avant l'extinction, CRC |
| L'état, 5 octets | version, numéro du dernier ordre compris, drapeaux (prêt ; copie de cartouche prête), ordres refusés, CRC |
| L'intégrité | CRC-8 du SMBus, initialisé à l'adresse de l'esclave : un message à la mauvaise adresse est refusé comme un message abîmé |

L'injecteur écrit le même ordre aux trois esclaves ; chacun y prend ce qui le concerne.

---

## 6. Ce que ça change dans les modules

| Module | Avant | Maintenant |
|---|---|---|
| [capture](capture/) | le sniffer, client du réseau émis par la tête | le sniffer **émet le réseau image**, et envoie une copie par écran |
| [ecran](ecran/) | la tête émet le réseau, décision du 29/09/2026 | la tête **rejoint le réseau image** (mode station, `WIFI_STATION`, qui existe déjà) ; elle garde le mode point d'accès pour être utilisée seule. La liaison reste directe, ce qui était le but du 29/09 |
| [son](son/) | le son sur le sniffer, peut-être ; deux sauts via la tête (T4.1) | la source a **son propre Pico, qui émet le réseau son**. La question « un seul Pico » tombe ; celle des deux sauts aussi. Le bruit de la radio dans l'ADC (T3.1) compte d'autant plus : ce Pico émet jusqu'à 6,2 Mbit/s |
| [manette](manette/) | topologie non décidée | l'injecteur **émet le réseau manettes**, et devient maître du bus de la console |
| [cartouche](cartouche/) | topologie non décidée | la doublure **émet le réseau cartouche**. Le risque T3.5, le WiFi et la ROM dans la même flash, reste entier ; le plan B à deux puces aussi |

---

## 7. Le banc de coexistence — avant tout changement dans les modules

Huit Pico 2 W de rechange : quatre points d'accès posés à l'écart qu'ils auront dans la
console, quatre clients à 3 m. Un firmware de banc qui émet le trafic réel de chaque réseau,
et un outil qui donne le verdict, sur le modèle des outils des modules. Tout est écrit, dans
[`banc-reseau/`](banc-reseau/README.md) : firmwares, déroulé pas à pas, et
`tools/banc.py`, qui règle les cartes, enregistre et donne le verdict. Compilé, éprouvé sur
PC, jamais flashé.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **R1** 🔬 Quatre radios côte à côte | Les quatre réseaux émettent leur trafic au plus : 11,2 Mbit/s d'image, 6,2 de son, des manettes, des rafales de cartouche ; 10 minutes en trois canaux, puis 10 minutes sur un seul | pour chaque réseau, les pertes et les retards, à comparer aux exigences du module. Le plan de canaux qui les tient, ou aucun : alors, écarter les antennes, baisser la puissance, et recommencer |
| **R2** 🔬 Le sniffer et ses quatre écrans | Le vrai sniffer, compilé avec l'option `POINT_ACCES`, capture la console et envoie quatre copies : à la tête, et à trois Pico qui jouent les écrans | aucune tranche perdue en 10 minutes ; la capture ne décroche pas ; la marge de temps du cœur réseau |
| **R3** Le bus de la console | Quatre Pico reliés en I²C, l'injecteur en maître ; une heure | aucun message perdu ni corrompu |

✅ **Critère de sortie :** R1 et R2 tenus. Alors seulement, les modules passent à cette
topologie ; d'ici là, chacun garde son réseau de banc.

---

## 8. Pour plus tard

- **Plusieurs manettes** : qui gagne quand deux joueurs appuient en même temps. La console
  reste allumée tant qu'une manette la veut, et les molettes font la moyenne (décisions du
  11/10/2026, [module manette](manette/docs/plan.md) §7.2).
- **Plusieurs haut-parleurs** dans une même pièce reçoivent le son à quelques millisecondes
  d'écart, et leurs horloges dérivent : cela peut s'entendre comme un écho.
- **Plusieurs écrans** : chacun sa luminosité, ou la même pour tous.
- **L'alimentation de la console** : quatre Pico qui émettent, c'est jusqu'à 1 A en pointe.
  Câblage en étoile, un filtre à l'entrée du Pico du son : le bruit des radios ne doit pas
  arriver à son ADC par l'alimentation ([module son](son/docs/plan.md), §2.2).

---

## 9. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 29/09/2026 | La tête de l'écran émet son propre réseau ; le sniffer s'y connecte en direct | Via une box, l'image gelait jusqu'à 122 ms |
| 11/10/2026 | **Décision de Yoann** : les périphériques ne parlent qu'à la console | Voir le module manette |
| 11/10/2026 | **Décision de Yoann** : des liaisons directes, un microcontrôleur par type de périphérique sur la console, chacun point d'accès de son réseau | Un seul saut radio partout ; jusqu'à 4 écrans, haut-parleurs ou manettes par réseau. Remplace la décision du 29/09 : c'est désormais le sniffer qui émet le réseau image |
