# pixelpush — émetteur PXL1

Pousse des images vers le module ÉCRAN en UDP.

```bash
./pixelpush.py --source anim                          # plasma animé, 60 img/s
./pixelpush.py --source horloge --fps 10              # horloge à aiguilles
./pixelpush.py --source gif --fichier boucle.gif      # GIF animé
./pixelpush.py --source image --fichier photo.png
./pixelpush.py --source ecran --region 100,100,512,512
./pixelpush.py --source video --fichier film.mp4      # nécessite ffmpeg
```

## Les sources

| Source | Dépendance | Note |
|---|---|---|
| `anim` | — | plasma + **curseur qui fait le tour du cadre** : le meilleur juge de la régularité, un œil repère une saccade bien mieux sur un point qui se déplace |
| `mire` | — | cadre, dégradé, damier, barres de couleur |
| `horloge` | — | aiguilles ; source lente mais toujours vivante, pour laisser la dalle allumée longtemps |
| `image` | Pillow | image fixe, redimensionnée |
| `gif` | Pillow | GIF animé, décodé une fois puis rejoué. **Pas besoin de ffmpeg** |
| `ecran` | Pillow | capture X11. ⚠️ plein écran = 50 à 100 ms par image, soit ~10 img/s ; `--region` ramène près des 60 |
| `video` | **ffmpeg** | décodage et redimensionnement délégués à ffmpeg, rien de lourd côté Python |

## Éprouver le réassemblage du firmware

```bash
./pixelpush.py --source anim --perte 2      # 2 % de paquets jamais émis
./pixelpush.py --source anim --desordre 5   # 5 % de paquets retardés après la dernière tranche
```

`--desordre` place les paquets retardés **après** la tranche marquée « dernière » : le
firmware doit les traiter comme du désordre, pas comme le début d'une nouvelle trame.
C'est le cas limite du réassemblage.

Les compteurs du firmware (`incompletes`, `ecartees`) doivent alors refléter ce qu'on a
injecté. Si l'image reste correcte à 2 % de perte, le réassemblage tient.

## La disposition

Le fichier TOML décrit l'image complète et le rectangle de chaque nœud. **Le protocole
étant tuile-conscient, passer de 1 à 3 nœuds ne change que ce fichier** — ni le
firmware, ni ce script. `layout-3x3.toml` est déjà écrit pour la phase 5.

L'adresse IP est celle que le firmware annonce sur sa console au démarrage.

## Le format

`PXL1_FMT_BGR888` : trois octets par pixel, ordre **B, G, R**. C'est l'ordre qu'attend
la dalle, ce qui permet au firmware d'écrire la charge utile directement dans son
tampon — sans conversion ni recopie intermédiaire.

Les tranches font un multiple de 3 octets pour ne jamais couper un pixel en deux. Une
trame 64×64 fait 12 288 octets, soit 9 paquets de 1398 octets utiles au plus.

| Résolution | Format | à 60 img/s |
|---|---|---|
| 64×64 | BGR888 | **5,95 Mbit/s** (mesuré) |
| 192×192 sur 3 nœuds | BGR888 | 53 Mbit/s — trop |
| 192×192 sur 3 nœuds | IDX8 | 17,7 Mbit/s |
| 192×192 sur 3 nœuds | IDX2 | 4,4 Mbit/s |

Le 3×3 imposera un format indexé : prévu par le protocole, pas encore implémenté.

## L'émetteur surveille ses propres décrochages

Chaque relevé donne le **pire écart** entre deux envois et un compteur de
**décrochages** (écart supérieur à trois périodes). C'est ce qui permet de distinguer
un problème de cette machine d'un problème de la carte : si l'émetteur affiche
60,0 img/s et zéro décrochage, le PC est hors de cause.

## Si rien n'arrive

Vérifier la **route** avant de soupçonner le firmware :

```bash
ip route get <ip-carte>
ping -c3 <ip-carte>
```

Un VPN actif installe volontiers des routes plus spécifiques que celle du réseau local
et capte le trafic vers la carte — arrivé ici le 18/09/2026, avec deux routes `/25` qui
découpaient le `/24` local. Symptôme : l'émetteur émet, le firmware compte **zéro
paquet**.

Contournement si l'on veut garder le VPN :

```bash
sudo ip route add <ip-carte>/32 dev <interface-wifi> scope link src <ip-pc>
```

Piste suivante : l'**isolation des clients** sur la box.
