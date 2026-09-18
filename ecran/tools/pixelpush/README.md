# pixelpush — émetteur PXL1

Pousse des images vers le module ÉCRAN en UDP.

```bash
./pixelpush.py --layout layout-1x1.toml --source anim          # animation, 60 img/s
./pixelpush.py --layout layout-1x1.toml --source mire --fps 1  # mire fixe
./pixelpush.py --layout layout-1x1.toml --source image --fichier photo.png
```

## La disposition

Le fichier TOML décrit l'image complète et le rectangle de chaque nœud. **Le
protocole étant tuile-conscient, passer de 1 à 3 nœuds ne change que ce
fichier** — ni le firmware, ni ce script. `layout-3x3.toml` est déjà écrit pour
la phase 5.

L'adresse IP est celle que le firmware annonce sur sa console au démarrage.

## Le format

`PXL1_FMT_BGR888` : trois octets par pixel dans l'ordre **B, G, R**. C'est l'ordre
qu'attend la dalle, ce qui permet au firmware d'écrire la charge utile directement
dans le tampon d'affichage — sans conversion ni recopie intermédiaire.

Les tranches font un multiple de 3 octets pour ne jamais couper un pixel en deux.
Une trame 64×64 fait 12 288 octets, soit 9 paquets de 1398 octets utiles au plus.

## Débit

| Résolution | Format | à 60 img/s |
|---|---|---|
| 64×64 | BGR888 | **5,95 Mbit/s** (mesuré) |
| 192×192 réparti sur 3 nœuds | BGR888 | 53 Mbit/s au total — trop |
| 192×192 réparti sur 3 nœuds | IDX8 | 17,7 Mbit/s |
| 192×192 réparti sur 3 nœuds | IDX2 | 4,4 Mbit/s |

Le passage au 3×3 imposera un format indexé : c'est prévu par le protocole, pas
encore implémenté.

## Si rien n'arrive

Vérifier d'abord la **route**, avant de soupçonner le firmware :

```bash
ip route get <ip-de-la-carte>
ping -c3 <ip-de-la-carte>
```

Un VPN actif installe volontiers des routes plus spécifiques que celle de ton
réseau local et capte le trafic vers la carte — c'est arrivé ici le 18/09/2026,
avec deux routes `/25` qui découpaient le `/24` local et le renvoyaient dans le
tunnel. Symptôme : l'émetteur émet, le firmware compte **zéro paquet**.

Route hôte de contournement, si l'on veut garder le VPN :

```bash
sudo ip route add <ip-carte>/32 dev <interface-wifi> scope link src <ip-pc>
```

Piste suivante si la route est bonne mais que rien n'arrive : l'**isolation des
clients** sur la box, qui empêche deux appareils WiFi de se parler.
