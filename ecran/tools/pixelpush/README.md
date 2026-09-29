# pixelpush — émetteur d'images pour PC

Envoie des images au module écran en UDP : animations, mires, horloge, images fixes, GIF,
vidéo ou capture d'une partie de l'écran du PC. C'est le banc d'essai du module : il sert à
l'alimenter sans console, et à éprouver sa réception en injectant des pertes.

Dépendances : Python 3.11 ou plus récent et `numpy`. `Pillow` pour les sources `image`, `gif`
et `ecran`, `ffmpeg` pour la source `video`.

## Parler à la tête (PXL2)

Avec `--cible`, `pixelpush` parle PXL2 à la [tête](../../firmware/tete/README.md) : il lui
demande la taille de son canevas (`PING`, réponse `PONG`) puis lui envoie l'image entière.
C'est la tête qui la place et la répartit.

La tête émet son propre réseau WiFi (« ecran-led » par défaut) : le PC doit le rejoindre, et
la tête est alors en 192.168.4.1. Ce réseau n'a pas d'accès à Internet ; pour garder le sien,
relier le PC à la box par un câble Ethernet.

```bash
nmcli dev wifi connect ecran-led password '<mot de passe>'      # ECRAN_MOT_DE_PASSE de la tête
./pixelpush.py --cible 192.168.4.1 --sonder                        # l'écran se décrit
./pixelpush.py --cible 192.168.4.1 --source anim --format idx8
./pixelpush.py --cible 192.168.4.1 --taille 160x144 --format idx2  # simule la Game Boy
./pixelpush.py --cible 192.168.4.1 --format bgr888 --fps 25
```

- `--taille` fixe la taille de l'image émise ; par défaut, celle du canevas. Plus petite,
  l'écran la centre.
- Si l'écran ne répond pas au `PING`, `pixelpush` émet quand même, en 192 × 192.

## Parler aux nœuds v1 (PXL1)

Sans `--cible`, `pixelpush` parle PXL1 au [nœud WiFi autonome](../../firmware/ecran/README.md).
Il découpe alors lui-même l'image selon un fichier de disposition, `layout-1x1.toml` par
défaut (ou `--layout`), qui donne la taille de l'image et, pour chaque nœud, son adresse IP
et son rectangle. L'adresse est celle que le nœud annonce sur sa console au démarrage.
`layout-3x3.toml` décrit la grille complète telle que la v1 la prévoyait.

```bash
./pixelpush.py --source anim
./pixelpush.py --source horloge --fps 10
```

## Sources

`--source` choisit ce qui est émis, pour les deux protocoles.

| Source | Dépendance | Contenu |
|---|---|---|
| `anim` | — | plasma animé et un curseur qui fait le tour du cadre : l'œil repère une saccade bien mieux sur un point qui se déplace. Source par défaut |
| `mire` | — | cadre, dégradé, damier, barres de couleur |
| `horloge` | — | horloge à aiguilles : lente mais toujours vivante, pour laisser la dalle allumée longtemps |
| `image` | Pillow | image fixe, redimensionnée (`--fichier`) |
| `gif` | Pillow | GIF animé, décodé une fois puis rejoué (`--fichier`) |
| `ecran` | Pillow | capture de l'écran X11. En plein écran, une capture prend 50 à 100 ms, soit une dizaine d'images/s ; `--region x,y,largeur,hauteur` rapproche des 60 |
| `video` | ffmpeg | vidéo décodée et redimensionnée par ffmpeg (`--fichier`) |

Autres options : `--fps` (60 par défaut), `--duree` en secondes (0, par défaut, pour ne
jamais s'arrêter), `--luminosite` de 1 à 255.

## Formats

| `--format` | Contenu | Protocoles |
|---|---|---|
| `bgr888` | 3 octets par pixel, sans perte. Par défaut | PXL1, PXL2 |
| `idx8` | 1 octet par pixel, palette fixe (cube 6 × 6 × 6 plus 40 gris), débit divisé par 3 | PXL1, PXL2 |
| `idx2` | 4 gris, 4 pixels par octet, pixel de gauche dans les bits de poids fort : exactement ce qu'émet le sniffer | PXL2 seulement |

Les palettes voyagent par paquets de commande, renvoyés toutes les 2 s pour qu'un firmware
redémarré les retrouve seul. Ce que chaque format coûte en débit, et pourquoi l'indexé rend
la chaîne bien plus robuste sur un WiFi dégradé : [§4.4 et §4.5 du plan](../../docs/plan-firmware.md).

## Éprouver la réception

```bash
./pixelpush.py --source anim --perte 2      # 2 % des paquets ne sont jamais émis
./pixelpush.py --source anim --desordre 5   # 5 % des paquets sont retardés
```

`--desordre` place les paquets retardés **après** la tranche marquée « dernière » : c'est le
cas limite du réassemblage. Face au nœud v1, les compteurs du firmware (`incompletes`,
`ecartees`) doivent refléter ce qu'on a injecté, et l'image doit rester correcte à 2 % de
perte. Face à la tête, qui complète une image dès que tous ses octets sont là, dans
n'importe quel ordre, `--desordre` ne coûte plus d'image : seul `--perte` en coûte.

## Lire les relevés

Chaque relevé donne le **pire écart** entre deux envois et un nombre de **décrochages**
(écarts de plus de trois périodes). Ils permettent de distinguer un problème du PC d'un
problème de la carte : si l'émetteur affiche 60,0 images/s et zéro décrochage, le PC est
hors de cause. Quand l'écran renvoie des accusés, le relevé donne aussi l'aller-retour
jusqu'à l'affichage.

## Si rien n'arrive

Vérifier la **route** avant de soupçonner le firmware :

```bash
ip route get <ip-carte>
ping -c3 <ip-carte>
```

Un VPN actif installe volontiers des routes plus spécifiques que celle du réseau local et
capte le trafic vers la carte. C'est arrivé le 18/09/2026, avec deux routes `/25` qui
découpaient le `/24` local. Symptôme : l'émetteur émet, le firmware compte **zéro paquet**.
Pour garder le VPN :

```bash
sudo ip route add <ip-carte>/32 dev <interface-wifi> scope link src <ip-pc>
```

Autre piste : l'**isolation des clients** WiFi sur la box.
