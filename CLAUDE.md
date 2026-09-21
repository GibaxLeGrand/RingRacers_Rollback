# Ring Racers Worldwide — consignes pour les agents

Fork de Ring Racers (Kart Krew) qui ajoute un netcode à prédiction côté client.
Branche de travail : `rollback-netcode`.

## À lire avant toute chose

1. [docs/README.md](docs/README.md) — règles de travail, décisions prises, où se
   trouve quoi, état de chaque document.
2. **Les notes privées**, si elles sont clonées à côté de ce dépôt :
   `../RingRacers_Worldwide-notes/README.md`, puis `memoire-agent.md` et la
   dernière note de `sessions/`. Environnement local, mémoire de travail,
   audits, harnais.
3. [docs/WORLDWIDE.md](docs/WORLDWIDE.md), bloc *Current state* en tête.
4. [docs/ROADMAP.md](docs/ROADMAP.md) — la suite.

## Règles non négociables

- **Aucun lancement sans accord explicite du porteur du projet, à chaque fois** :
  exe du jeu, `playtest.sh`, scénario `.cfg`, soak, banc de test. Modifier des
  fichiers ne demande pas d'accord ; lancer le jeu, si.
- **Mesurer, pas supposer** : binaire vérifié par son sha, prédiction écrite avant
  la course, témoin dans la même session. Détails dans `docs/README.md`.
- **Journaux annotés, jamais réécrits** (`WORLDWIDE.md` hors bloc *Current state*,
  `ROLLBACK.md`, `AUDIT_20260909.md`). Chaque nouveau constat va dans
  `WORLDWIDE.md` §8.x, et le bloc *Current state* est mis à jour.
- **Compatibilité : le serveur décide.** Serveur en mode WORLDWIDE : prédiction,
  clients WORLDWIDE seulement. Serveur vanilla : netcode d'origine, et un client
  WORLDWIDE s'y comporte comme un client vanilla.
- **Ce dépôt est public : aucune info personnelle** (chemins locaux, nom
  d'utilisateur, prénom). Elle va dans les notes privées. On écrit « Gibax ».
- On échange avec le porteur du projet **en français**.
