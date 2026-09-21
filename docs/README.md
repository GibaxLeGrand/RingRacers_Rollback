# Documentation — Ring Racers Worldwide

Point d'entrée unique. À lire en premier, par un humain ou par un agent, sur
n'importe quel poste. Ce fichier contient ce qu'il faut pour reprendre sans
aucune mémoire locale : règles de travail, décisions prises, où se trouve quoi,
et l'état de chaque document.

Dernière mise à jour : **2026-09-21** (audit + nettoyage des docs).

---

## Reprendre le projet

Prompt type pour une nouvelle conversation ou un autre agent :

```
Je reprends Ring Racers Worldwide (fork de Ring Racers, branche rollback-netcode).
Lis d'abord docs/README.md, puis le bloc "Current state" en tête de docs/WORLDWIDE.md,
puis docs/ROADMAP.md. Ne refais pas les analyses déjà consignées.
Voilà ce que je veux faire : [...]
```

Ordre de lecture :

1. **ce fichier** — règles, décisions, environnement ;
2. **[WORLDWIDE.md](WORLDWIDE.md)**, bloc *Current state* en tête — l'état du
   projet aujourd'hui, puis le journal §8 pour les preuves ;
3. **[ROADMAP.md](ROADMAP.md)** — ce qui reste à faire, dans quel ordre ;
4. **[COMMANDS.md](COMMANDS.md)** — ce que fait chaque commande console.

---

## Règles de travail (valables pour tout agent)

1. **Aucun lancement sans accord explicite, à chaque fois.** Ça couvre
   l'exécutable du jeu, `playtest.sh`, un scénario `.cfg`, un soak, un banc de
   test. Même en cours de tâche, même pour un test « rapide », même si un test
   précédent a été accepté. Modifier du code, des `.cfg` ou des docs ne demande
   pas d'accord ; **démarrer un processus qui fait tourner le jeu, si.**
2. **Mesurer, pas supposer.** Les règles que le projet a payées cher :
   - vérifier que le binaire testé est bien celui du commit
     (voir *Construire et tester*) ;
   - écrire la prédiction **avant** la course, pas après ;
   - toujours un témoin (*control*) dans la même session ;
   - un instrument dit la taille de ce qu'il a examiné, et compte ce qu'il filtre ;
   - un ratio suspicieusement rond est un défaut d'instrument jusqu'à preuve du
     contraire ;
   - un invariant écrit en commentaire à côté du code n'est pas une preuve : il
     s'est révélé faux trois fois sur cette branche.
3. **Les journaux s'annotent, ils ne se réécrivent pas.** `WORLDWIDE.md` (sauf
   son bloc *Current state*), `ROLLBACK.md` et `AUDIT_20260909.md` gardent leur
   texte d'origine : quand une section plus récente la contredit, on ajoute un
   ⚠ qui renvoie vers elle. Les renversements font partie de la preuve.
   `ROADMAP.md`, `COMMANDS.md`, ce fichier et le bloc *Current state* se
   réécrivent pour rester exacts.
4. **Chaque mesure ou constat est consigné au fil de l'eau** dans
   `WORLDWIDE.md` §8.x, et le bloc *Current state* est mis à jour dans la
   foulée.
5. **Langue** : on échange avec le porteur du projet en français. Les docs
   techniques existantes sont en anglais ; on garde la langue de chaque fichier.

---

## Décisions prises — ne pas rediscuter

| Décision | Date | Source |
|---|---|---|
| **Compatibilité : c'est le serveur qui décide.** Un serveur en mode WORLDWIDE fait tourner la prédiction côté client et n'accepte que des clients WORLDWIDE. Un serveur vanilla garde le netcode d'origine (*delay-based*) ; un client WORLDWIDE peut s'y connecter et s'y comporte exactement comme un client vanilla. | 2026-09-21 | Alex |
| Le projet s'appelle **Worldwide** : c'est de la prédiction côté client avec réconciliation serveur, pas du rollback GGPO. | 2026-09-10 | `WORLDWIDE.md` §0 |
| **Deux horloges** : `gametic` ne joue que des tics confirmés ; la spéculation tourne au-dessus sur un snapshot. `G_Ticker` est appelé tel quel pour tous les tics — ne jamais réimplémenter la simulation. | 2026-09-10 | `AUDIT_20260909.md`, `WORLDWIDE.md` §0 |
| **Pas de bibliothèque GGPO** : 4 joueurs max (contre 16), modèle P2P (contre client/serveur), 8 frames de prédiction (229 ms, trop court pour 250-300 ms). La technique est reprise, pas la bibliothèque. | 2026-09-08 | `ETAT_PROJET.md` §11 (local) |
| **Ne pas prédire le résultat de la roulette d'objets** : la roue tourne visuellement pendant la spéculation, le résultat n'est validé que sur un tic confirmé. | 2026-09-10 | `WORLDWIDE.md` §4 |
| Correction légère par kart (`PT_STATECORRECTION`) plutôt que streaming d'état complet façon Odamex. | 2026-09-10 | `WORLDWIDE.md` §5, §8.4 |

---

## Où se trouve quoi

| Quoi | Où |
|---|---|
| Dépôt de travail | `RingRacers_Rollback`, branche `rollback-netcode` (poste d'origine : `C:\Users\ariguet\perso\RingRacers_Rollback`) |
| Fork GitHub | `https://github.com/GibaxLeGrand/RingRacers_Rollback` (remote `origin`) |
| Amont | `KartKrewDev/RingRacers` (remote `upstream`, **push désactivé**) |
| Base amont du diff | `05cca02c9` |
| Code du projet | `src/k_rollback.c`/`.h` (presque tout), plus des changements ciblés dans `d_clisrv.c`, `p_saveg.cpp`, `g_game.c` et quelques autres |
| Identité git | `GibaxLeGrand <48137851+GibaxLeGrand@users.noreply.github.com>` |
| CI | GitHub Actions, `.github/workflows/build.yml`, ~4 min, jobs Linux (Alpine) + Windows (llvm-mingw) |
| Binaire de test | artéfact CI `ringracers-win64-<sha>` : `.exe` jouable + `.pdb` |
| Dossier de jeu de test | `D:\RingRacers - 24 - Copie` — **sur le poste d'origine uniquement** |
| Harnais de test | `playtest.sh` et les scénarios `*.cfg`, dans le dossier de jeu — **⚠ hors git** |
| Projet frère SRB2Kart | `SRB2Kart_Rollback` (poste d'origine), avec son propre état |
| Références étudiées | SRB2 NetPlus et Odamex, clonés localement pour étude, hors dépôt (`AUDIT_20260909.md`) |

⚠ **Le harnais de test n'est pas versionné.** `playtest.sh` et tous les
scénarios (`playclient_correct.cfg`, `playserver_correct.cfg`,
`soak_leak.cfg`, …) vivent dans le dossier de jeu du poste d'origine. Sur un
autre poste, **aucune** mesure de `WORLDWIDE.md` ne peut être rejouée tant
qu'ils n'ont pas été copiés, ou mieux, ajoutés au dépôt.

---

## Construire et tester

- **Pas de build local** : Ring Racers demande SDL3, absent de la toolchain WSL.
  Le seul build est la CI GitHub Actions, déclenchée par un push.
- Vérification syntaxique d'un fichier isolé, sans build :
  ```
  gcc -fsyntax-only -Wall -std=gnu11 -I <dossier avec un config.h factice> -I src -DHAVE_SDL src/k_rollback.c
  ```
- **Vérifier le binaire avant toute mesure.** L'exe contient
  `<branche>\0<sha court>\0<sujet du commit>` en clair :
  ```
  grep -q "$(git rev-parse --short=7 HEAD)" ringracers_rollback-netcode.exe
  ```
  Refuser de mesurer si le sha n'y est pas. Prendre l'artéfact par `headSha` ou
  par id de run : `gh run list --limit 1` renvoie le run **précédent** tant que
  le nouveau n'est pas créé.
- **Réglage d'une partie WORLDWIDE** : voir le pense-bête en fin de
  [COMMANDS.md](COMMANDS.md).
- Rappel de la règle 1 : **chaque lancement se demande.**

---

## État des documents

| Fichier | Statut | Rôle |
|---|---|---|
| `docs/README.md` | ✅ à jour | Ce fichier : point d'entrée, règles, décisions, environnement. |
| `docs/WORLDWIDE.md` | ✅ à jour (bloc *Current state*) + journal | État courant en tête ; §0-§7 : analyse du 2026-09-10, en partie dépassée (chaque passage concerné est marqué ⚠) ; §8 : journal de mesures, le plus récent en bas. |
| `docs/ROADMAP.md` | ✅ à jour (réécrit le 2026-09-21) | Ce qui reste, dans quel ordre, avec les critères de fin de chaque phase. |
| `docs/COMMANDS.md` | ✅ à jour | Référence des 21 commandes console `rollback_*`. |
| `docs/AUDIT_20260909.md` | 📜 historique | Comparaison avec NetPlus et Odamex. Ses recommandations ont été appliquées (pivot deux horloges). Toujours utile pour le *pourquoi*. |
| `docs/ROLLBACK.md` | 📜 journal clos, **obsolète comme état** | Journal du 08 au 10/09 : snapshots, déterminisme, ancienne boucle. Les phases 1-7 qu'il décrit sont retirées. Ses *pièges* restent valables. |
| `ETAT_PROJET.md` (racine) | ❌ **obsolète**, local, hors git | Ancien document de reprise du 08/09. Remplacé par ce fichier. |
| `README.md` (racine) | amont + note du fork | README de Kart Krew, avec un renvoi ici en tête. |
| `CLAUDE.md` (racine) | ✅ à jour | Consignes chargées automatiquement par Claude Code : renvoie ici. |
| `docs/udmf.txt`, `docs/logo.png`, `.gitlab/`, `thirdparty/` | amont | Hors périmètre du projet. |
