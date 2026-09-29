# pixelpush — émetteur PXL2 / PXL1

Pousse des images vers le module ÉCRAN en UDP.

## PXL2 : l'écran n'est qu'une adresse

Depuis la révision du 29/09/2026, l'écran se présente comme **une tête** : une IP, un
canevas. `pixelpush` lui demande sa taille (`PING` → `PONG`) et lui envoie l'image
entière ; c'est la tête qui la place et la répartit sur les rangées.

```bash
./pixelpush.py --cible 192.168.1.50 --sonder                   # l'écran se décrit
./pixelpush.py --cible 192.168.1.50 --source anim --format idx8
./pixelpush.py --cible 192.168.1.50 --taille 160x144 --format idx2   # simule la Game Boy
./pixelpush.py --cible 192.168.1.50 --format bgr888 --fps 25
```

- `--taille` choisit la taille de l'image émise ; par défaut, celle du canevas annoncé.
  Plus petite, l'écran la **centre**.
- `--format idx2` : quatre gris, quatre pixels par octet, pixel de gauche en poids fort
  — exactement ce qu'émet le sniffer. PXL2 seulement.
- Si l'écran ne répond pas au `PING`, `pixelpush` émet quand même, en 192×192.

## PXL1 : les nœuds WiFi autonomes de la v1

Sans `--cible`, c'est le protocole v1 du firmware `firmware/ecran/`, avec un fichier de
disposition (`layout-1x1.toml` par défaut). Rien n'a changé pour lui :

```bash
./pixelpush.py --source anim                          # plasma animé, 60 img/s
./pixelpush.py --source horloge --fps 10              # horloge à aiguilles
./pixelpush.py --source gif --fichier boucle.gif      # GIF animé
./pixelpush.py --source image --fichier photo.png
./pixelpush.py --source ecran --region 100,100,512,512
./pixelpush.py --source video --fichier film.mp4      # nécessite ffmpeg
```

Toutes les sources, l'injection de défauts et la mesure d'aller-retour valent pour les
deux protocoles.

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

> La tête v2 complète une image dès que tous ses octets sont là, dans n'importe quel
> ordre : face à elle, `--desordre` ne coûte plus d'image — seul `--perte` en coûte.

## La disposition (PXL1 seulement)

Le fichier TOML décrit l'image complète et le rectangle de chaque nœud. En PXL1,
l'émetteur découpe lui-même : c'est ce que la v2 a supprimé. `layout-3x3.toml` reste
comme trace de l'architecture v1 ; en PXL2 la disposition vit dans le firmware de la
tête (`firmware/tete/include/config.h`).

L'adresse IP est celle que le firmware annonce sur sa console au démarrage.

## Les deux formats

```bash
./pixelpush.py --format bgr888      # 3 octets par pixel, sans perte
./pixelpush.py --format idx8        # 1 octet + palette, débit divisé par 3
./pixelpush.py --luminosite 12      # commande hors du flux de pixels
```

`idx8` quantifie sur un **cube 6×6×6 plus 40 gris** : palette fixe, envoyée une fois
puis rafraîchie toutes les 2 s pour qu'un firmware redémarré la retrouve seul. La
quantification se réduit à une division — **0,02 ms par trame 64×64** — et l'erreur
moyenne est de **3,3 %**.

### Ce que l'indexé apporte vraiment

Mesuré dos à dos le 18/09/2026, sur un lien WiFi **dégradé** (ping passé de 3,4 à
8,3 ms, 2,4 GHz encombré), même émetteur à 60 img/s :

| | BGR888 | IDX8 |
|---|---|---|
| Débit émis | 5,95 Mbit/s | 1,98 Mbit/s |
| Paquets par trame | 9 | 3 |
| **Trames reçues par le firmware** | **10 img/s** | **60 img/s** |
| Assemblage d'une trame | 19,3 ms | 7,8 ms |

**Diviser le débit par 3 multiplie par 6 la résistance à un lien dégradé.** Une trame
de 9 paquets n'arrive entière que si les neuf passent ; à 3 paquets, la probabilité est
tout autre. C'est l'argument décisif pour le 3×3 — et il est mesuré, plus supposé.

> ⚠️ Les latences ne se comparent **qu'à conditions de lien identiques**. Une mesure
> prise avant la dégradation ne se compare pas à une mesure prise après : le lien
> change au fil de la journée, et c'est lui qui domine.

## Le format

`PXL1_FMT_BGR888` : trois octets par pixel, ordre **B, G, R**. C'est l'ordre qu'attend
la dalle, ce qui permet au firmware d'écrire la charge utile directement dans son
tampon — sans conversion ni recopie intermédiaire.

Les tranches font un multiple de 3 octets pour ne jamais couper un pixel en deux. Une
trame 64×64 fait 12 288 octets, soit 9 paquets de 1398 octets utiles au plus.

| Résolution | Format | à 60 img/s |
|---|---|---|
| 64×64 | BGR888 | **5,95 Mbit/s** (mesuré) |
| 192×192 | BGR888 | 53 Mbit/s — trop ; ✅ jusqu'à 27 img/s |
| 192×192 | IDX8 | 17,7 Mbit/s ✅ **implémenté** |
| 160×144 (Game Boy) | IDX2 | 2,8 Mbit/s ✅ **implémenté** (PXL2) |

En v2, la tête reçoit tout : ces débits se comparent aux **24,6 Mbit/s** qu'un Pico 2 W
encaisse (phase 4).

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
