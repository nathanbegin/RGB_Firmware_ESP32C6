# ESP-C6-UNIT02

Firmware ESP-IDF pour le PCB « Contrôleur Zigbee RGBW + 8 rondelles » (ESP32-C6-MINI-1-N4, 4 Mo). Routeur Zigbee. Fabricant `NathanSensors`, modèle `ESP-C6-UNIT02`. Structure reprise de [ESP-C6-UNIT01](https://github.com/nathanbegin/ESP-C6-UNIT01) (OTA Zigbee, portail Wi-Fi, tests hôtes, CI).

Source du matériel : dossier de conception « Contrôleur Zigbee RGBW + 8 rondelles » (sections 3.1, 3.4, 6 et 7).

## Sorties et endpoints

Chaîne par sortie : GPIO → 74AHCT244 (5 V, non inverseur) → 470 Ω → AON7410 côté bas. GPIO haut = charge allumée. Un MOSFET reste bloqué tant que sa sortie vaut 0.

| Endpoint | Type ZCL | GPIO | Connecteur | Clusters |
| --- | --- | --- | --- | --- |
| 10 | Extended Color Light (0x010D) | R=18, G=19, B=20, W=21 | J3 | Basic, Identify, Groups, Scenes, On/Off, Level, Color Control (XY + HS), client OTA |
| 11 | Dimmable Light (0x0101) | P=22 | J5 (rondelles) | On/Off, Level, Groups, Scenes, Identify |

Interface locale : DEL verte IO23, bouton PAIR IO2, BOOT IO9 (`main/board_config.h`).

## Comportement

- **Couleur** : XY ou HS → RGB, niveau, puis courbe CIE 1931. Le ZCL n'a pas de canal blanc : avec l'option « ruban RGBW » (portail), `W = min(R,G,B)` est retranché de R, G, B. Désactivée par défaut (ruban RGB actuel, W reste à 0).
- **Plafond de puissance** : somme R+G+B limitée à 75 % de 3,0 par défaut (≈ 2,9 A si le blanc plein mesure 3,8 A), proportions conservées. À régler après l'essai A1 (courant réel du ruban).
- **PWM** : LEDC, un seul temporisateur, 2 kHz par défaut, réglable de 300 Hz à 20 kHz. La résolution baisse avec la fréquence (13 bits à 2 kHz, 10 bits à 20 kHz).
- **Démarrage** : sorties à 0. L'état (StartUpOnOff, par défaut « précédent ») n'est appliqué qu'une fois le réseau rejoint.
- **Sauvegarde** : le dernier état est écrit en NVS 2 s après la dernière commande (une seule écriture flash par rafale).
- **PAIR** : appui court = DEL selon l'état réseau (2 clignotements lents = rejoint, rapides = pas de réseau). Appui de 5 s = quitte le réseau, efface `zb_storage`, relance l'appairage.
- **DEL d'état** : clignotement rapide (5 Hz) en recherche, fixe 3 s à l'appairage, 1 Hz pendant l'OTA, éteinte sinon.
- **Portail de réglage** : maintenir BOOT (IO9) ≈ 3 s. Se connecter à `NathanSensors-Setup`, ouvrir `http://192.168.4.1`: fréquence PWM, plafond, option RGBW. En mode CONFIG, Zigbee ne démarre pas et les sorties restent à 0.

## OTA et récupération

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Une nouvelle image n'est validée (`esp_ota_mark_app_valid_cancel_rollback`) qu'après avoir rejoint le réseau, 10 s plus tard, et émis un premier rapport. Un plantage ou une réinitialisation avant ce point ramène l'image précédente. Récupération manuelle : BOOT + RESET, puis flash USB-C.

| Partition | Adresse | Taille |
| --- | --- | --- |
| nvs | `0x9000` | `0x6000` |
| phy_init | `0xF000` | `0x1000` |
| ota_0 | `0x10000` | `0x1E0000` |
| ota_1 | `0x1F0000` | `0x1E0000` |
| zb_storage | `0x3D0000` | `0x4000` |
| zb_fct | `0x3D4000` | `0x400` |
| otadata | `0x3D6000` | `0x2000` |

Identité OTA : fabricant `0x131B`, type d'image `0x0201` (`main/ota_client.h`). Augmenter `APP_OTA_FILE_VERSION` avant chaque version publiée.

```bash
idf.py build
python tools/build_ota.py build/zb_rgbw_controller.bin   # .ota + ota-index.json
```

Première installation par USB-C (aucune image antérieure) :

```bash
idf.py -p <PORT> flash monitor
```

## Zigbee2MQTT

Copier `esp_c6_unit02.mjs` dans `data/external_converters/`, redémarrer, puis refaire l'interview. Deux lumières : `rgbw` (couleur, XY et HS) et `rondelles` (gradation).

```json
{"state_rgbw":"ON","brightness_rgbw":200,"color_rgbw":{"x":0.31,"y":0.33}}
```

## Compilation et tests

ESP-IDF **5.5.4**, `esp-zigbee-lib` ~1.6, `esp-zboss-lib` ~1.6, cible `esp32c6`, flash 4 Mo.

```bash
bash tests/run.sh      # moteur de lumière, OTA, empaquetage (hôte)
```

Le workflow CI compile le firmware sous `espressif/idf:v5.5.4`.

## État de validation

- Testé sur PC : conversion couleur, plafond de puissance, option RGBW, courbe CIE 1931, résolution PWM, OTA (blocs fragmentés, erreurs), format `.ota`, table de partitions.
- **Non compilé sur la cible** dans la session de rédaction (pas d'ESP-IDF disponible). Les noms de l'API Zigbee 1.6 (`esp_zb_color_dimmable_light_*`, `esp_zb_level_cluster_*`) sont à confirmer par la première compilation CI.
- **Non testé sur matériel**, ni sous Zigbee2MQTT (convertisseur à valider contre la version installée).
- Non implémenté : effet Identify (DEL), fondu matériel LEDC (les transitions ZCL sont déjà échelonnées par la pile), température de couleur.
- À faire avant la commande du PCB (section 7 du dossier) : essais A1 (courant du ruban), A2 (PWM sur les rondelles Illume), A3 (blocs 12 V flottants).
