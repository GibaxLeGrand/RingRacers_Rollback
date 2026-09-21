# Commandes console du rollback — manuel de référence

Liste stable des commandes console ajoutées par cette branche (`k_rollback.c`,
sauf mention contraire). Toutes sont enregistrées comme **commandes de debug**
(`COM_AddDebugCommand`), donc visibles dans le menu pause du jeu sans avoir à
les taper. Contrairement à `ROLLBACK.md` (journal clos) et `WORLDWIDE.md`
(état courant + journal de mesures), ce fichier ne raconte pas d'histoire : il
décrit ce que chaque commande fait, aujourd'hui, et se met à jour au fil de
l'eau. Point d'entrée de toute la doc : `docs/README.md` du dépôt privé de notes.

Ce fichier, `WORLDWIDE.md` et `ROADMAP.md` sont gardés **identiques** dans le
dépôt de code public et dans le dépôt privé de notes (`docs/` des deux côtés).

**À jour au 2026-09-21** — 22 commandes, vérifiées contre
`K_RegisterRollbackStuff` dans `k_rollback.c`. Deux sont **obsolètes**
(`rollback_loop`, `rollback_pace`) et restent seulement pour comparaison.

⚠ Rappel : **aucune de ces commandes ne se lance dans une partie sans l'accord
explicite du porteur du projet**, à chaque fois (règle 1 du point d'entrée de
la doc).

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

⚠ Sous `rollback_twoclock`, l'horloge confirmée ne court jamais en avance :
ce compteur ne peut alors voir que le **renvoi tardif** d'un tic déjà joué, et
lit 0 par construction sur un lien propre (`WORLDWIDE.md` 8.17). Un 0 ne prouve
rien dans ce mode.

### `rollback_inputlog [0|1]`
**À lancer sur les deux machines.** Compte les ticcmds réellement consommés
par un tic confirmé et en tient un hash cumulatif ; la commande seule affiche
l'état, le compte et le hash. Les deux journaux se comparent tic par tic :
le premier tic où les comptes concordent mais où les **hashs** divergent est
un tic où les deux machines ont joué des entrées différentes sans s'en
apercevoir.

⚠ Le hash replie le **numéro de tic** dans chaque tour, donc une entrée
identique rangée sous deux numéros différents se lit comme un contenu
différent. C'est voulu (c'est ce qui rend visible un réétiquetage), mais ça
veut dire qu'un écart de hash ne prouve pas à lui seul que les octets
d'entrée diffèrent — voir `rollback_relabel`.

### `rollback_relabel`
**Serveur uniquement, et il faut un vrai client distant** (une boucle locale
ne relabellise rien d'intéressant). Rapport seul. Histogramme de
`faketic - realstart` : de combien de tics le serveur **déplace l'étiquette**
d'un ticcmd qui arrive, par rapport au numéro dont le client l'avait marqué.
C'est de la mécanique vanilla (`PT_CLIENTCMD`,
`faketic = maketic + max(0, wantdelay - timegap)`), pas de cette branche.

Comment le lire — l'algèbre se réduit à deux cas et l'histogramme est souvent
bimodal parce que **plusieurs émetteurs** y sont mélangés :

- paquet arrivé **dans son budget** (`timegap < wantdelay`) → l'offset vaut
  exactement `wantdelay`, donc un **pic étroit** ;
- paquet arrivé **après** → l'offset vaut `timegap`, le transit brut, donc un
  **étalement** aussi large que la gigue.

⚠ Un serveur-écoute s'envoie ses propres paquets (`CL_SendClientCmd()` est
appelé sous `if (server)` aussi), donc l'histogramme total compte **le host et
les clients ensemble**. Depuis le 2026-09-21, le rapport ajoute quatre lignes
qui séparent les paquets : **hôte ou client distant**, **pendant une course ou
en dehors**. L'hypothèse à tester (`WORLDWIDE.md` 8.32) : le cluster `+2` vient
de l'hôte **en dehors d'une course**, où l'exemption de délai ne s'applique pas.

### `rollback_lagcheck` — n'existe pas comme commande
Cherché comme commande, il n'y est pas : c'est une **impression
automatique**, à front, dans `UpdatePingTable` (`d_clisrv.c`). Elle sort une
ligne **quand `target_lag` change**, préfixée `[client]` ou `[server]` selon
la branche, avec les termes qui décident de l'exemption. Rien à activer : la
ligne apparaît dans `latest-log.txt` de la machine concernée.

---

## Netcode en direct — changent le comportement réseau réel

### `rollback_loop [tics]` — ❌ OBSOLÈTE
Remplacée par `rollback_twoclock` le 2026-09-10 ; gardée pour comparaison
seulement. Elle avance l'horloge confirmée sur des suppositions, ce qui se bat
avec le contrôle de cohérence du jeu (`AUDIT_20260909.md`).

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
- Retire le délai d'entrée fixe (voir encadré sous `rollback_twoclock`) tant
  qu'il est actif — via `K_RollbackPays()`, commit `2026-09-14`.

### `rollback_pace [0|1]` — ❌ OBSOLÈTE
Ne sert qu'avec `rollback_loop`, elle-même obsolète.

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
- Retire le délai d'entrée fixe tant qu'il est actif (voir encadré ci-dessous).

> #### ⚠️ Correctif du 2026-09-14 : `rollback_twoclock` retire enfin le délai fixe
>
> Le "gentleman's delay" côté serveur (`UpdatePingTable`, `d_clisrv.c`) et le
> `mindelay` du profil joueur côté client ne se désactivaient **que** si
> `K_RollbackPredictAhead() > 0` — c'est-à-dire seulement pour l'ancienne
> boucle `rollback_loop`. Or `rollback_twoclock` met `g_loopahead` à 0 en
> s'activant (les deux sont mutuellement exclusifs "by construction") : donc
> tant que ce correctif n'existait pas, **activer le pivot ne retirait pas le
> délai d'entrée** — seule l'ancienne boucle dépréciée le faisait. Toutes les
> mesures du journal (`ROLLBACK.md`) prises sous two-clock l'ont donc été avec
> ce délai fixe toujours facturé en plus de la spéculation.
>
> Corrigé par une nouvelle fonction `K_RollbackPays()` (`k_rollback.c`/`.h`)
> qui répond vrai si **`rollback_loop` OU `rollback_twoclock`** est actif, et
> qui remplace `K_RollbackPredictAhead() > 0` aux deux endroits de
> `d_clisrv.c` où le délai était calculé (branche serveur *et* branche
> client de `UpdatePingTable`).
>
> **⚠ Complété le 2026-09-20 : côté hôte, ce correctif ne faisait rien.** Sur
> un serveur d'écoute, `K_RollbackTwoClock()` renvoie 0 (`client` vaut
> `!server`), et l'hôte n'active de toute façon jamais `rollback_twoclock`,
> qui est un réglage client. L'hôte continuait donc à se facturer 6 à 7 tics,
> soit 170-200 ms, sur sa propre entrée. `K_RollbackPays()` interroge
> maintenant aussi `K_RollbackCorrectingHere()` (`rollback_correct` actif sur
> cette machine) : c'est un **proxy**, en attendant que le serveur annonce son
> mode WORLDWIDE (`ROADMAP.md`, section *Compatibility*). Mesuré : `target_lag`
> de l'hôte reste à 0 toute la course (`WORLDWIDE.md` 8.24-8.27).
>
> **Conséquence concrète** : le réglage "Minimum Input Delay" du profil
> joueur (`cv_mindelay`, menu accessibilité — jusqu'ici décrit comme
> "Practice for online play!", donc pensé pour être calibré hors-ligne
> puisqu'en ligne le délai réseau s'imposait de toute façon par-dessus)
> **s'efface réellement en ligne dès que `rollback_twoclock` tourne** :
> `target_lag` retombe à `0` côté client (et côté hôte depuis le complément
> du 2026-09-20) au lieu de rester bloqué au plancher `cv_mindelay.value`. Ce n'est **pas** un nouveau réglage qui
> apparaîtrait en ligne — c'est la suppression d'un double-comptage : avant
> ce correctif, le délai fixe restait facturé par-dessus la spéculation,
> annulant une partie du bénéfice que le pivot est censé apporter.
>
> Reste **hors scope de ce correctif**, noté dans `ROADMAP.md` (section
> *Client-local input delay knob*) : un vrai bouton de délai *local* façon GGPO
> (le joueur choisit de garder un peu de tampon même avec la prédiction
> active, sans que ça ne redevienne un `wantdelay` envoyé au serveur — c'est
> précisément le bug que ce correctif referme). Si l'idée est de recycler le
> slider `cv_mindelay` existant pour ça, c'est un nouveau développement, pas
> une conséquence automatique de ce qui est corrigé ici.

### `rollback_cleancmds [0|1]`
**Côté client, mode deux horloges.** Coupé par défaut. Quand il est actif, la
spéculation ne touche plus aux tics que le serveur a **déjà envoyés**.

Sans lui, la réserve `netticbuffer` arrête la boucle confirmée un tic avant
`neededtic`, et la spéculation écrit ton entrée *du moment* par-dessus celle que
le serveur avait assignée à ce tic. Le tic est ensuite joué comme confirmé avec
la mauvaise entrée : c'est la meilleure piste pour la dérive (`WORLDWIDE.md`
8.31).

- Sans argument : l'état, et deux compteurs qui tournent **même coupé** —
  combien d'entrées locales ont été écrites par-dessus un tic déjà reçu, et
  combien différaient de celle du serveur. Le second doit valoir 0 quand
  personne ne pilote.
- Changer la valeur remet les compteurs à zéro, pour lire une même course
  coupé puis actif.

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
d'état légère toutes les `tics` tics. `0` coupe (comportement stock : seul le
renvoi complet de partie corrige).

Le paquet (`statekart_pak`, `d_clisrv.h`) fait **56 octets par kart**, soit
896 pour une grille de 16 :
- **38 octets appliqués** : position, vitesse, angle, hitlag, rings, objet ;
- **18 octets de diagnostic**, mesurés et affichés mais **jamais appliqués** :
  `spinouttimer`, `nocontrol`, `flashing`, `spinouttype`, `tumbleBounces`,
  `wipeoutslow`, `justbumped`, `offroad`, `speed` (`WORLDWIDE.md` 8.7).

Sur un serveur d'écoute, activer cette commande exempte aussi l'hôte du délai
galant (voir l'encadré sous `rollback_twoclock`).

- 2ᵉ argument (`suppress`, défaut `1` si `tics` est donné) : distingue
  **mesurer** de **remplacer**.
  - `rollback_correct N 0` — envoie les corrections *en plus* du renvoi
    complet stock. C'est le témoin : le nombre de resyncs reste comparable à
    tout ce qui a été mesuré avant l'existence du canal.
  - `rollback_correct N` (ou `N 1`) — les corrections **remplacent** le
    renvoi complet. C'est le changement réel : c'est ce que fait un serveur
    en mode WORLDWIDE. Côté compatibilité, **c'est le serveur qui décide**
    (décision du 2026-09-21, `ROADMAP.md`, section *Compatibility*).

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

- **Réglage d'une partie WORLDWIDE** (tel que les scénarios `correct` de
  `playtest.sh` le font, `WORLDWIDE.md` 8.4) :
  - serveur : `rollback_correct 4` ;
  - client : `rollback_twoclock 4` et `rollback_drift 1` (c'est ce `1` qui
    applique les corrections ; sans lui, le client les mesure seulement) ;
  - pour simuler 171 ms de latence en local : `rollback_lag 6` côté client.
  
  Le témoin se lance avec `rollback_correct 4 0` côté serveur (le renvoi
  complet reste actif). Aujourd'hui, ces réglages se font à la main sur chaque
  machine : le passage automatique du client en mode WORLDWIDE selon le
  serveur est à construire (`ROADMAP.md`, section *Compatibility*).
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
  second a remplacé le premier (voir la fin de `ROLLBACK.md`, *The pivot
  landed*) et `rollback_loop` est obsolète.
