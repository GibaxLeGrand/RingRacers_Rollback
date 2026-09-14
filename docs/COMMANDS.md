# Commandes console du rollback — manuel de référence

Liste stable des commandes console ajoutées par cette branche (`k_rollback.c`,
sauf mention contraire). Toutes sont enregistrées comme **commandes de debug**
(`COM_AddDebugCommand`), donc visibles dans le menu pause du jeu sans avoir à
les taper. Contrairement à `ROLLBACK.md` (journal daté, chronologique) et
`WORLDWIDE.md` (audit), ce fichier ne raconte pas d'histoire : il décrit ce que
chaque commande fait, aujourd'hui, et se met à jour au fil de l'eau.

Deux familles bien distinctes :

- **Diagnostic** — mesurent ou vérifient, ne changent rien à ce que voit un
  joueur en partie normale.
- **Netcode en direct** — activent ou réglent un comportement réseau réel
  (prédiction, correction, délai).

---

## Diagnostic — vérifications hors ligne ou en solo

### `rollback_test`
Prend un instantané de l'état courant, le perturbe (RNG), le restaure, reprend
un second instantané et compare les deux **octet par octet**.

- **Prouve** : que tout ce que l'archiveur écrit survit intact à une
  relecture — chaque champ de chaque mobj/thinker/secteur/joueur archivé.
- **Ne prouve pas** : qu'un champ non archivé manque à l'appel (il serait
  absent des deux instantanés, donc "égal" à tort). Pour ça, voir
  `rollback_resim`.
- Une différence dans l'archive Lua est **normale** (l'ordre de `lua_next`
  dépend de la disposition interne des tables, pas un bug).
- Affiche aussi la taille de l'instantané et le coût (µs) de chaque étape.

### `rollback_resim [tics]`
Par défaut 4 tics. Sauvegarde l'état, le rejoue deux fois sur **les mêmes
entrées gelées** (une passe "vraie", une passe restaurée), puis compare.

- Répond à la question que `rollback_test` ne peut pas poser : est-ce qu'un
  monde restauré se comporte comme le monde vivant sur les mêmes entrées ?
- Aveugle à tout ce qui est **déclenché par un événement** (un bouton
  pressé une seule fois pendant la fenêtre gelée n'a jamais été "pressé"
  dans la seconde passe).

### `rollback_leak [tics]`
Par défaut 4 tics. Trois instantanés : une référence (B), une passe sur les
**vraies** entrées (A1), une passe sur des entrées **perturbées** (A2, un
input neutre plutôt qu'un extrême — c'est ce qu'un client prédit vraiment
pour quelqu'un qui ne fait rien).

- `A1 != B` → n'importe quelle passe supplémentaire pollue l'état, peu importe
  ce qu'elle a simulé.
- `A1 == B` et `A2 != B` → c'est une passe **fausse** qui laisse une trace :
  exactement le cas de la prédiction réseau, reproduit sur une seule machine,
  sans réseau.
- Sur une grille entièrement à l'arrêt (aucune entrée non neutre), la
  vérification **refuse** plutôt que de passer : un `A2` qui ne diffère pas de
  `B` ne prouverait rien, et un test qui ne peut pas échouer est pire que pas
  de test.
- Existe parce que `rollback_resim`/le soak habituel peuvent rester propres
  330 fois de suite alors que la partie réelle dérive — ils ne testent que
  ce qui est dans l'archive (joueurs + mobjs), pas les statics de fichier ou
  autre état caché.

### `rollback_soak [interval] [tics] [leak]`
Lance `rollback_resim` (ou `rollback_leak` si le 3ᵉ argument est non nul) en
tâche de fond, une fois toutes les `interval` tics, silencieusement sauf en cas
d'échec.

- Sans argument : affiche l'état courant (actif/coupé, nombre de vérifications
  et d'échecs).
- `interval` à `0` : coupe le soak et donne le bilan final.
- Coûte cher (deux resimulations + deux restaurations par vérification) : à
  réserver à un serveur dédié sans personne dessus, pas à une partie jouée.
- Affiche le contexte (map, mode) une seule fois au démarrage — sinon un run
  qui ne trouve jamais rien donne un chiffre inexploitable ("500 vérifications,
  sur quelle map ?").

### `rollback_replay [tics]`
Par défaut 4 tics. Rembobine ce nombre de tics et **rejoue les entrées qui se
sont vraiment produites** (lues dans `netcmds`, où le netcode garde 512 tics
d'historique) — contrairement au soak qui rejoue des entrées gelées.

- C'est littéralement l'opération qu'un rollback effectue, déclenchée à la
  demande plutôt qu'en réponse à un paquet réseau.
- Nécessite `rollback_keep 1` au préalable pour que le tic visé soit encore
  dans l'anneau.

### `rollback_keep <0|1>`
Garde un instantané de **chaque** tic (au lieu de rien), pour que
`rollback_replay` puisse repartir de n'importe lequel des `ROLLBACK_TICS - 1`
derniers. Coûte une sauvegarde par tic ; coupé par défaut.

### `rollback_blame [0|1]`
Enregistre, à chaque tic, ce que le checksum (`Consistancy()`) regardait
vraiment : position + type d'objet de chaque joueur, plus la somme des seeds
RNG synchronisés — dans une ligne de texte gardée pour les 512 derniers tics.

- **À lancer sur les deux machines** (client et serveur). Quand le serveur
  refuse un tic, il imprime sa propre ligne ; le client imprime les siennes
  autour du même moment. Les deux se comparent tic par tic.
- Sert à trancher entre trois hypothèses en une seule ligne de texte : la
  position diverge, l'item diverge, ou le seed RNG diverge.

### `rollback_damagelog [0|1]`
**À lancer sur les deux machines.** Imprime une ligne par événement de dégât
*résolu* (pas tenté — seulement quand `P_DamageMobj` retourne vrai) sur un tic
confirmé, avec un hash cumulatif.

- Les deux journaux se diffent tic par tic ; la première ligne où les hashs
  divergent est le premier coup jugé différemment par les deux machines.
- Ne compte pas les tentatives refusées (kart invincible ici, percuté là) :
  les deux machines refusent des choses différentes en permanence sans que ce
  soit un bug, donc compter les tentatives aurait donné un chiffre non-nul
  trompeur.

### `rollback_detect`
Rapport seul, aucun réglage. Dit ce que le réseau a raconté sur des tics
**déjà joués** : combien d'entrées sont arrivées en retard pour un tic déjà
exécuté, combien contredisaient ce qui avait été utilisé, combien sont
arrivées trop tard pour l'anneau — et, s'il y en a, le plus vieux tic encore
en attente d'un rejeu.

---

## Netcode en direct — changent le comportement réseau réel

### `rollback_loop [tics]`
**L'ancienne boucle de prédiction** (avant le pivot two-clock). Coupée par
défaut : une build qui l'embarque joue exactement comme une build stock tant
que personne ne la demande. La valeur donnée est le nombre de tics que le
client peut courir en avance sur le serveur (plafonné par
`K_RollbackPredictAhead()`).

- Active automatiquement `rollback_keep` (courir en avance sans pouvoir
  revenir en arrière serait pire que ne pas prédire du tout).
- Mutuellement exclusif avec `rollback_twoclock` **par construction** : l'une
  avance l'horloge confirmée (`gametic`), l'autre refuse de le faire.
- Sans argument : imprime un rapport de télémétrie complet (tics prédits,
  avance min/max/moyenne, corrections reçues, rejeux, tics rendus à la boucle
  réelle parce qu'un message est arrivé dessus, etc.) — utile pour voir *si*
  la prédiction a jamais eu l'occasion de se déclencher.

### `rollback_pace [0|1]`
Limite la boucle (`rollback_loop`) à **un seul tic prédit par passe**, au lieu
d'en prédire autant que la profondeur le permet. Coupé par défaut, et
volontairement séparé de `rollback_loop` : les compteurs de `rollback_loop` se
lisent une fois avec le pacing coupé et une fois allumé, dans la même
partie — sinon on compare deux soirées différentes.

### `rollback_twoclock [tics]`
**Le pivot** — remplace `rollback_loop`. Fait tourner une spéculation de
`tics` tics **par-dessus** le monde confirmé, sans avancer l'horloge
autoritaire elle-même.

- Allumer `rollback_twoclock` met `rollback_loop` à 0 automatiquement (et
  active `rollback_keep`) ; les deux ne peuvent pas tourner en même temps.
- L'éteindre (valeur ≤ 0) restaure proprement le monde confirmé si une
  spéculation était en cours — sinon elle resterait pour de bon.
- Sans argument : rapport (passes de spéculation construites, tics qu'elles
  ont joués, temps passé à défaire/refaire la spéculation par passe contre le
  budget d'un tic entier, messages réseau refusés parce que levés dans une
  spéculation — un netxcmd envoyé pendant une spéculation ne peut pas être
  repris).

### `rollback_nullspec [0|1]`
Sauvegarde et restaure la frontière à **chaque passe** sans rien spéculer.
Isole une seule question : est-ce que le simple aller-retour par l'archive,
tout seul, dans la vraie boucle de jeu, suffit à faire réagir le serveur —
sans le bruit de la spéculation elle-même.

### `rollback_lag [tics]`
**Test uniquement.** Retarde chaque paquet reçu d'un pair de ce nombre de
tics. Une boucle locale (loopback) n'a aucune latence, donc sans cette
commande un client n'est jamais à court de tics confirmés et n'a jamais rien
à prédire — c'est ce qui permettait à la boucle de rollback de rester
allumée sans jamais se déclencher.

- Prévient si des paquets ont été perdus (file pleine) : un délai artificiel
  devient alors de la perte de paquets artificielle, et les deux se
  ressembleraient dans les résultats sans cet avertissement.

### `rollback_maxdepth [tics]`
Jusqu'où un rollback a le droit de revenir en arrière. Au-delà de cette
profondeur, la latence doit être compensée par du délai d'entrée (input
delay) classique plutôt que par un rejeu. Plafonné à la taille de l'anneau
(`ROLLBACK_TICS`).

### `rollback_smooth [0|1]`
Coupé par défaut. Quand actif, une correction **glisse visuellement** de
l'ancienne position du kart vers la nouvelle au lieu de la faire sauter
instantanément. Séparé volontairement de `rollback_loop`/`rollback_twoclock` :
une partie se lit une fois pour savoir si le nombre de resyncs a baissé (un
chiffre), et une seconde fois pour savoir si les à-coups visuels ont
disparu (ça, seul un humain peut le dire).

### `rollback_correct [tics] [suppress]`
**Côté serveur.** Demande au serveur d'envoyer à chaque client une correction
d'état légère (38 octets par kart : position, vitesse, angle, hitlag,
rings, item) toutes les `tics` tics. `0` coupe (comportement stock : seul le
renvoi complet de partie corrige).

- 2ᵉ argument (`suppress`, défaut `1` si `tics` est donné) : distingue
  **mesurer** de **remplacer**.
  - `rollback_correct N 0` — envoie les corrections *en plus* du renvoi
    complet stock. C'est le témoin : le nombre de resyncs reste comparable à
    tout ce qui a été mesuré avant l'existence du canal.
  - `rollback_correct N` (ou `N 1`) — les corrections **remplacent** le
    renvoi complet. C'est le changement réel, et le point où la
    compatibilité avec un serveur stock est abandonnée.

### `rollback_drift [0|1]`
**Côté client.** Rapporte l'écart mesuré entre le monde confirmé de ce
client et celui du serveur, à partir de chaque correction reçue (moyenne et
pire cas, en unités de jeu — un kart fait ~40 unités de large).

- L'argument décide si les corrections sont **aussi appliquées**
  (`rollback_drift 1`) ou seulement mesurées (défaut) : mesurer et corriger
  dans la même partie donnerait un chiffre qui ne dit rien ni sur l'un ni sur
  l'autre.

### `rollback_delay`
Rapport seul. Affiche les deux moitiés du compromis de latence : le délai
d'entrée du jeu tel qu'il tourne en ce moment (plancher `mindelay`, plafond
moteur, délai réellement appliqué à ce joueur), et ce que coûterait un
rollback à la profondeur actuelle par rapport au budget d'un tic — en se
basant sur les temps mesurés par `rollback_test`/`rollback_resim`. Invite à
lancer ces deux commandes d'abord si rien n'a encore été mesuré.

---

## Pense-bête d'usage

- Pour un diagnostic solo sans réseau : `rollback_test`, puis `rollback_resim`,
  puis si les deux sont propres mais qu'une vraie partie dérive quand même,
  `rollback_leak` (voir aussi `soak_leak.cfg`, qui lance `rollback_soak
  <interval> <tics> 1` sur la map de test réseau).
- Pour investiguer un désync en partie réelle, sur les deux machines à la
  fois : `rollback_blame 1` (position/item/RNG) et `rollback_damagelog 1`
  (dégâts) tournent ensemble et se lisent tic par tic.
- Piège connu (voir `ROLLBACK.md`) : lancer `rollback_test` **au milieu** d'un
  scénario de mesure change la partie qui suit (effet de bord mesuré). Garder
  les scénarios de mesure et les commandes de diagnostic séparés.
- `rollback_loop` et `rollback_twoclock` ne tournent jamais ensemble ; le
  second a remplacé le premier (voir `ROLLBACK.md`, section two-clock) mais
  la commande existe encore pour comparaison.
