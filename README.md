# IHM Frigo — C++ / Qt

Application native tactile 1024 × 600. L’interface a été redessinée en C++ avec QPainter à partir des dimensions, couleurs et dispositions du HTML fourni : bandeau de 56 px, navigation de 84 px, panneaux, boutons ±, clavier numérique et écrans de maintenance séparés. Les polices IBM Plex Sans / Mono sont embarquées dans l’exécutable et fonctionnent hors ligne. Le HTML et son runtime `support.js` ne sont pas exécutés.

## Essayer sous Windows

Double-cliquer sur `Lancer simulation.cmd` après compilation locale. Le mode simulation est explicitement signalé à l’écran et n’envoie aucune commande au matériel. L’exécutable local est `build/frigo.exe` ; il utilise Qt installé dans `C:\Qt\6.11.2\mingw_64`.

En simulation, le code **1234** ouvre les réglages, comme dans la maquette. En utilisation réelle, le premier accès demande de créer puis confirmer un code personnel à quatre chiffres. Le code est stocké sous forme de hash salé dans QSettings ; c’est un verrou d’interface, pas une protection contre un administrateur de la machine.

Les réglages se modifient avec les boutons ± ; toucher la valeur ouvre le clavier. Faire glisser verticalement le panneau, ou utiliser la molette, pour atteindre le bouton Enregistrer lorsque la liste dépasse l’écran. Le statut CAN détaillé et la commande « Relire la carte » sont accessibles en touchant le texte d’état dans le bandeau supérieur. Les vues sont mises à l’échelle uniformément en plein écran pour conserver les proportions.

## Ubuntu 22.04.5 LTS

Qt 5.15 est pris en charge pour utiliser les paquets de cette distribution ; le même code compile avec Qt 6.

```bash
sudo apt update
sudo apt install build-essential cmake qtbase5-dev libqt5serialbus5-dev libqt5serialbus5-plugins network-manager can-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/frigo --simulate
```

Configuration CAN, avec le débit confirmé de **250 kbit/s** :

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000 restart-ms 100
sudo ip link set can0 up
./build/frigo --fullscreen --interface can0 \
  --alarm-start /chemin/absolu/alarm_gpio.sh \
  --alarm-stop /chemin/absolu/stop_alarm_gpio.sh
```

Remplacer les chemins par les scripts exécutables réels. La commande d’arrêt GPIO n’a pas encore été fournie : l’application accepte un exécutable configurable, sans interprétation par un shell. Sans cette option, l’acquittement reste visuel et ne peut pas arrêter le GPIO. L’application doit tourner dans une session graphique utilisateur ; ne pas l’exécuter en root. Le système configure le débit CAN avant son démarrage.

### Diagnostic CAN : permission refusée / débit 500000

Le plugin SocketCAN de Qt 5.15 ajoute par défaut `BitRateKey = 500000`. L’application supprime maintenant ce réglage avec `setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant())` **avant** `connectDevice()`. Cela conserve la configuration de Linux et évite la tentative de changement de débit qui produit `RTNETLINK answers: Operation not permitted` / `Cannot apply parameter: 4 with value: 500000`. Ne pas remplacer cette ligne par un débit de 250000 : Qt tenterait toujours une opération privilégiée.

Après transfert du code corrigé sur Ubuntu, recompiler puis vérifier :

```bash
cmake --build build -j2
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0
timeout 10s candump -e can0
./build/frigo --fullscreen --interface can0
```

`timeout` peut retourner 124 lorsque les dix secondes sont écoulées, ce qui n’est pas une erreur CAN. Vérifier `bitrate 250000`, le drapeau `UP` et l’état CAN (normalement `ERROR-ACTIVE`). L’absence de trames dans `candump` peut venir d’une carte silencieuse ou du bus : elle ne permet pas à elle seule de conclure à une panne de l’IHM. Si des trames sont présentes mais aucune température ne s’affiche, relever les IDs et DLC : la moyenne attend les trois IDs standard `0x100`, `0x101`, `0x102`, chacun sur deux ou quatre octets (entier signé little-endian divisé par 10).

Référence : [code SocketCAN Qt 5.15](https://github.com/qt/qtserialbus/blob/5.15/src/plugins/canbus/socketcan/socketcanbackend.cpp).

## Comportement CAN

- CAN classique, identifiants standard ; les trames RTR, erreurs, étendues et échos locaux ne sont pas décodés comme des données.
- Températures `0x100`–`0x103` : `int16 LE / 10` sur deux octets ou `int32 LE / 10` sur quatre octets. Le firmware observé transmet quatre octets (`1F 01 00 00` = 28,7 °C). Batterie `0x104` : deux octets uniquement, divisés par 1000.
- CAP1–CAP3 : moyenne des trois sondes, sans appliquer deux fois les offsets. Si une mesure manque ou date de plus de 6 secondes, affichage `—`. EVA et batterie ont également une détection de péremption. La batterie reste une tension supposée d’après votre documentation.
- Démarrage : lecture RTR `0x30F`, comparaison avec un cache complet. Sans cache ou si la signature diffère, lecture de **tous les paramètres `0x300` à `0x30C`**, offsets inclus. Seul un jeu complet et cohérent est accepté. Délais et deux tentatives supplémentaires pour les lectures.
- Sauvegarde : seules les valeurs brutes modifiées sont émises ; trames espacées de 100 ms par timer non bloquant. Températures et offsets ×10 ; heures ×3600 ; minutes ×60. Signature calculée sur les 13 valeurs brutes, modulo **65535**, puis envoyée sur deux octets. Une lecture RTR supplémentaire vérifie la signature avant de persister le cache et réactiver les sauvegardes.
- Les entiers signés négatifs sont sommés comme des valeurs signées. Le reste négatif est normalisé dans `[0, 65534]`. Cette convention devra être confrontée au firmware si celui-ci additionne des représentations `uint32_t`.
- Calibrage : bouton distinct dans Maintenance, envoi des seuls offsets modifiés puis signature incluant la configuration entière.
- Maintenance : commande `0x30E` ; l’interface attend le retour de la carte pour afficher le mode actif. Les commandes relais nécessitent ce mode et la réception préalable du masque du pack, afin de préserver les autres bits. Les relais restent désactivés si la carte ne publie pas leur état.
- Codes d’erreur fournis décodés et journalisés (500 entrées en mémoire). `0x52` active le script GPIO ; porte `0x02` arrête le script et retire l’alarme porte affichée. L’acquittement utilisateur est local : aucun ID CAN d’acquittement n’a été inventé. RTC `0x30D` n’est pas envoyé.
- En cas d’échec, aucune sauvegarde automatique n’est retentée. Utiliser « Relire la carte » après correction ; après déconnexion du périphérique, relancer l’application.

La signature seule n’est pas un accusé de réception transactionnel et peut avoir des collisions, conformément au protocole fourni. Les bornes de saisie reflètent les types CAN et la contrainte min ≤ max ; les limites métier spécifiques à votre équipement restent à préciser.

## Réseau et clavier

Le réseau est géré de façon asynchrone avec NetworkManager (`nmcli`). Recherche des SSID, connexion Wi-Fi ouverte ou personnelle, activation/désactivation radio, affichage des interfaces et adresses MAC/IP. Le mot de passe passe par l’entrée standard de `nmcli --ask`, jamais dans les arguments du processus ou les logs de l’application. Les autorisations de la session NetworkManager/Polkit doivent permettre la connexion.

Clavier tactile intégré pour les valeurs, codes et mots de passe ; le clavier physique reste utilisable. Les réseaux d’entreprise 802.1X et la configuration IP statique ne sont pas implémentés. Les caractères absents du clavier tactile peuvent être saisis au clavier physique.

L’historique propose les vues 1h / 24h / 7j de la maquette et conserve jusqu’à sept jours de mesures pendant la session (sans persistance après fermeture). En simulation uniquement, une courbe de démonstration reproduit celle de la maquette. En utilisation réelle, seules les mesures reçues sont tracées. Les détails non exposés par le protocole, comme la version firmware, le nombre de dégivrages et la date du dernier dégivrage, sont affichés avec `—`.

## Validation

Compilation Windows avec Qt 6.11.2 / MinGW 13.1 et tests QtTest : formats signés/non signés, rejet des DLC incorrects, modulo exact, moyenne et péremption, alarme porte, envoi différentiel, commit et lecture des offsets. Capture reproductible :

```bash
./build/frigo --simulate --screenshot apercu.png
```

Les tests d’interface vérifient aussi le PIN tactile, les thèmes, la navigation, l’enregistrement après défilement, le calibrage, la correspondance des relais et les cibles tactiles après mise à l’échelle. Des captures des écrans se trouvent dans `captures/`. Pour régénérer un écran :

```bash
./build/frigo --simulate --preview-page cal --screenshot captures/cal.png
```

Écrans : `temp`, `hist`, `alarms`, `reg`, `deg`, `alm`, `net`, `mnt`, `cal`, `fan`, `act`, `pwd`, `pin`. La sélection directe pour capture fonctionne uniquement en simulation et ne déverrouille pas l’accès réel.

Les valeurs CAN, l’indicateur de simulation et les champs non disponibles peuvent différer des données fictives du HTML. La validation réelle de SocketCAN, des permissions Wi-Fi et des scripts GPIO doit se faire sur Ubuntu avec la carte connectée.

Polices : IBM Plex, licence SIL Open Font License ; textes de licence dans `assets/fonts/OFL-Sans.txt` et `assets/fonts/OFL-Mono.txt`.

Références API : [Qt CAN Bus](https://doc.qt.io/qt-6/qcanbusdevice.html), [NetworkManager nmcli](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nmcli.html).
