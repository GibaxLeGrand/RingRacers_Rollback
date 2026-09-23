# Roadmap to a playable alpha

Rewritten 2026-09-21. It replaces the 2026-09-10 original and the three
revision notes that had been stacked on top of it (2026-09-10, 2026-09-10
evening, 2026-09-20). Nothing they said was dropped: their conclusions are
folded into the phases below, and the order is brought up to date. The
evidence for every line lives in `WORLDWIDE.md`; this file only says what is
left, in what order, and what each step has to prove.

The old phase numbering (1 to 7, in `ROLLBACK.md`) was retired on 2026-09-10:
it described an architecture where the authoritative clock ran ahead.

This file, `WORLDWIDE.md` and `COMMANDS.md` are kept **identical** in the public
code repository and in the private notes repository (`docs/` in both). The
docs entry point and `ROLLBACK.md` live in the private notes only.

## Where this starts from

- **Solved, from the client's seat: input lag.** "Ça répond tout de suite", five
  races, and again after the architecture changed underneath it.
- **Architecture settled.** `gametic` runs only confirmed tics; the speculation
  runs above it on a snapshot and is rebuilt every pass. A light correction
  channel replaces the stock full-state resend: **0 resends a race**, against
  7 to 9 without it.
- **Unattended bench.** Six bots move the world without a driver, so a
  measurement can be repeated instead of being n=1.
- **Found on one map: the drift's cause.** With `rollback_cleancmds` on
  throughout, a driven race reads 0.000 units and 0 checksum refusals, like
  `nospec` (8.44), and its same-session control diverges as predicted
  (8.45). The other maps are owed.
- **Open:** the cost at sixteen karts, and vanilla compatibility (see the
  dedicated section).

## Ground rules for every step

- **No launch without Gibax's explicit go-ahead, every time**: the game, a
  `playtest.sh` scenario, a soak, the bench. Writing code and scenarios is
  free; running them is asked for.
- Measure on a binary verified by its sha. Write the prediction before the run.
  Keep a control in the same session.
- The harness (`playtest.sh`, `soak.sh`, the `*.cfg` scenarios) is versioned in
  the private notes repository (`harnais/`), never the public one: it carries
  local paths. The game folder comes from an environment variable.

## Next, in order

State on 2026-09-23. The 2026-09-21 session on the measuring machine ran the
leak soak and two driven cleancmds races (8.34-8.37). The audit of 2026-09-22
found that those races' off/on/off protocol cannot measure the drift (8.38),
and built `rollback_history` for the feel (8.39). The audit of 2026-09-23 found
that every driven race ran on one plain map, made the harness run any map, and
found that `rollback_history` as built probably judders (8.40). On the
measuring machine, the same day, the steps that need no launch are done, and
found that the blame lines never print with the correction channel on (8.42);
they now print once a second on both machines (8.43). Then the first launches
since 2026-09-21: `correct_on`, which read like `nospec` on every count
(8.44), and its same-session control `correct`, which diverged as predicted
(8.45). Then `history`, whose confirmed world parted inside its window
(8.46), and the leak soak at its depth, which found no archive leak (8.47).
**Every launch is asked for first.**

1. ~~**Code everything that needs no launch.**~~ Done on 2026-09-21, one commit
   each: roulette fields local-only (8.29), indexed relink (8.30),
   `rollback_cleancmds` (8.31), relabel split (8.32), bot-overwrite bound check,
   `old_z` restored on load, release-config exe in CI.
2. ~~**Take the dev artifact of the latest commit and check its sha.**~~ Done
   on 2026-09-21; the harness now prints the check before every run.
3. ~~**Prepare the harness.**~~ Done (8.33): three 1000-tic windows,
   `cleancmds_report.py` comparing every player's input tic by tic.
4. ~~**The 2026-09-21 test session.**~~ Done (8.34-8.37):
   - leak soak: `itemRoulette.playing`/`.exiting` were never archived, found and
     fixed, 259/261 then **1/260** (the survivor is the harmless
     `itemList.cap`). The relink step fell from 4.7 ms to **under 0.1 ms**.
   - two driven `correct` races: `rollback_cleancmds` takes wrong-input tics
     from 64-85% (local) and 23-80% (bots) to **0-0.1%**; no felt difference
     (8.36); now on by default. Their drift readings disagreed -- and cannot be
     trusted either way (8.38).
5. ~~**Without a launch, on the measuring machine.**~~ Done on 2026-09-23
   (8.42):
   - the dev artifact of `2b58e1d53` is installed, its sha checked;
   - `maps.py`: the same 152 race maps as 8.40, the shortlist stands;
   - `rngsum`: there is none to read. No blame line prints while the
     correction channel suppresses resends. What the logs do show: the
     server refused the client's checksum at every five-second sample from
     window 0 to the end, the on window included; `nospec` refused none.
6. **The next test session, one launch at a time, each asked for:**
   - ~~`playtest.sh correct_on`, driven.~~ Done on 2026-09-23 (8.44), on
     `a1df8bb85`: 0.000 units on 6741 kart samples, 0 refusals, 87 of 87
     blame samples identical, 0 wrong inputs -- every clause of the
     prediction. The relabel split is written down: its `+2` falls before
     the measurement windows.
   - ~~**`playtest.sh correct`, driven, on the same build: the same-session
     control.**~~ Done on 2026-09-23 (8.45): refusals from 12 tics into
     window 0 to the end, 77-84% wrong local inputs in the off windows, blame
     samples that part and never agree again -- every clause of the
     prediction. The driven kart parts first, then a bot, then the seeds.
   - ~~`playtest.sh history`, driven.~~ Run on 2026-09-23 (8.46): the display
     side works as designed, but **the confirmed world parted inside the
     history window** -- one refusal, two bots out by 15 and 22 units, a bump
     on the server and not on the client -- and window 2 inherited it. Not
     yet explained.
   - ~~`soak.sh leak12`, unattended.~~ Done on 2026-09-23 (8.47): 2 failures
     in 264, both the harmless `itemList.cap` -- no leak through the archive
     at 12 tics.
   - **`playtest.sh depth12`, can run without a driver**: plain speculation at
     12 tics, history off, in the middle window. Prediction in 8.47. It parts:
     depth. It holds: history's replay, or too rare an event for one window,
     and a second `history` race is the check.
   - **A race played from the host's seat**, to judge the feel now the host's
     delay is gone (8.27). Only a person can do this one.
   - **Other maps** (8.40), map by map from its shortlist: `soak.sh leak
     map=<lump>` and `soak.sh ww map=<lump>` first, unattended; then
     `playtest.sh correct_on map=<lump>` driven, with `playtest.sh nospec
     map=<lump>` as its control. Northern District first, then the maps
     with water, executors, polyobjects and ACS.
7. **Depending on 6:**
   - `correct_on` reads like `nospec` (it did, 8.44) and the control shows
     the instruments see a divergence (it did, 8.45): **Phase A's mechanism
     is found on Skyscraper Leaps**, which meets the second half of its
     *Done when*. The first half -- zero reloads with the resend not
     suppressed -- is not yet run (see Phase A). Then the correction-rate
     sweep (`rollback_correct 8`, `16`, `35`), owed since 2026-09-10 -- with
     no drift, far fewer corrections should do.
   - `correct_on` still drifts: there is a second leak, and the memory-hash
     instrument below is next.
   - A map breaks the leak soak or drifts where Skyscraper Leaps does not: the
     block `P_LocateSnapshotBlock` names is the second leak.
   - `history` feels better **and the leak of 8.46 is found and fixed**: turn
     it on by default, and fold its cost into Phase B. Until then it stays
     off.
8. **Then** the compatibility work (section below), and Phase B's big lever.

---

## Phase A -- Understand the drift

**No longer the gate for the alpha.** The correction channel absorbs the
divergence (0 resends, mean residual 0.13-0.86 units in the off/on/off
races, a kart being about 40 wide). Phase A is now what lowers the correction
rate and the residual, and what makes the stock consistency check agree
again. **On Skyscraper Leaps it does both: 0.000 units and 0 refusals with
`rollback_cleancmds` on throughout (8.44, 8.45).**

**Excluded, each by measurement:** the restore (0 contamination over 1400 round
trips, with and without bots); the archive (0/522 leak checks after the
roulette fix, 8.15; 1/260 after the second one, 8.34); tic determinism (0/330
resim checks); the synchronised RNG as a cause (it parts only after positions
have, 8.2); the damage path (same hits, same hashes, 8.8); the confirmed clock
running on a guess (8.9, 8.17, 8.20); late resends (0 arrivals, 8.17).

**Fixed, and the source on Skyscraper Leaps (8.31, 8.33, 8.35-8.38, 8.44,
8.45):** the speculation started on a tic the server had already sent -- the
netticbuffer reserve stopped the confirmed loop one short -- and overwrote the
local player's input in it with the current one, and every bot's input with
one recomputed from the client's world; the next pass ran that tic as
confirmed. `rollback_cleancmds` fixes it (64-85% wrong-input tics down to
0-0.1%), and is on by default. With it on from the first tic, a driven race
reads 0.000 units, 0 refusals, and blame samples identical on both machines
(8.44). The same-session control shows the order in which the worlds part
without it: the driven kart first, by thousandths of a unit, then a bot, then
the seed sum, which the channel never repairs (8.45). The off/on/off races
could not measure it, as 8.38 said: their on windows inherited the off
windows' divergence.

**Measured on one map only** (8.40): every driven race ran on
`RR_SkyscraperLeaps`, which has no water, no polyobject, no linedef executor
and no ACS. The exclusions above hold there. The other maps are in *Next, in
order*.

**The relabel histogram's `+2` cluster** (8.27), read in both races of
2026-09-23: it falls before the measurement windows -- the host's before
`rollback_correct` is set, the client's before its `rollback_lag 6` --
harmless on the counts. 8.32's split mislabels it, because the waiting map
`RR_TESTRUN` is a level (8.44, 8.45).

**If another map drifts where Skyscraper Leaps does not, the next
instrument:** hash the program's global memory (the exe's `.data`/`.bss`)
just before a speculation and just after the restore, narrow a difference down
to an address, name it with the `.pdb` -- the blind spot every archive-based
check shares, since none of them look outside the archive.

**Done when:** zero `Game state reloaded` with the resend **not** suppressed
(`rollback_correct N 0`) over five unattended races and two driven ones -- or,
failing that, the drift explained down to a mechanism and its residual stated.
**Status on 2026-09-23:** the second is met on Skyscraper Leaps (mechanism
8.31, residual 0.000, 8.44-8.45). The first has not been run. It is now
expected to pass -- a race whose checksum never disagrees never fires a
resend -- which makes it a cheap confirmation, and the form the other maps'
check could take.

---

## Phase B -- Make it fit at a real grid

**The gate for the alpha.** Every cost figure is two to nine karts, mostly early
in a race. A Ring Racers grid is sixteen, and a snapshot grows from 120 KiB at
the start to 318 KiB three minutes in.

Measured: 4.94 ms a pass at two karts, 8.33 at eight, **9.5 ms at nine with
somebody driving -- 33% of a 28.6 ms tic**, already past the line below. The
restore alone was 8.6 ms at sixteen karts late in a race. **Assume it does not
fit, and measure.**

**A cheap lever first, found by reading** (`WORLDWIDE.md` 8.30):
`P_RelinkPointers`, 4.7 ms of an 8.6 ms restore, resolved every pointer with a
linear scan of all mobjs. **Now indexed** by `mobjnum` (2026-09-21), same
answer, and **measured under 0.1 ms** (8.34).

⚠ **`rollback_history` pulls the other way** (8.39): replaying the inputs in
flight takes the speculation from 4 tics to about 8 at 171 ms, an estimated 14
to 16 ms a pass at nine karts, and it grows with latency. If it stays on, this
phase has to pay for it.

**Then two structural levers, in this order:**

1. **Predict less.** Odamex restores one player and the moving sectors, not two
   thousand objects; Rocket League separates the car from the ball. The larger
   win, and it also delivers partial correction, which the design asks for.
2. **Amortise.** NetPlus re-simulates only every N live tics
   (`cv_siminaccuracy`), trading freshness for a smoother CPU profile.

**Done when:** a pass fits in **30% of a tic** at sixteen karts, late in a race,
at the depth Phase D settles on.

---

## Phase C -- Breadth under prediction

**Needs B**: testing feel and correctness through a stutter tells you about the
stutter.

Nothing here has run under prediction yet:

- a full race, start to finish: grid, finish line, results screen;
- **Battle**, **Encore**, **Grand Prix** (grid hardcoded to eight, bots run
  differently);
- items and respawns used on purpose -- the soak replays frozen inputs and is
  blind to anything edge-triggered;
- maps with what the test map lacks: water, polyobjects, linedef executors, ACS
  (`harnais/maps.py` lists them; shortlist in `WORLDWIDE.md` 8.40). The
  harness runs any scenario on any map (`map=<lump>`).

**Item policy:** do not predict the roulette's result. Let the reel spin under
speculation and commit the pick on a confirmed tic (`WORLDWIDE.md` §4). Prove it
with a deliberately driven item test, not with the soak.

**Done when:** each runs with no resends and no divergence that has not been
read and understood.

---

## Compatibility and capability advertising

**Policy, decided by Gibax on 2026-09-21: the server decides.**

- A server in **WORLDWIDE mode** runs client-side prediction and the correction
  channel, and accepts **WORLDWIDE clients only**.
- A **vanilla server** runs the stock delay-based netcode. A WORLDWIDE client may
  join it and then behaves **exactly as a vanilla client**.

This supersedes every earlier statement that stock compatibility was "abandoned"
(`WORLDWIDE.md` 8.4, 8.14) or "already satisfied" (this file before
2026-09-21).

**Already there, and to keep:**

- `packetversion`, `version`, `subversion` and `application` are untouched, so
  both kinds of server stay visible to both kinds of client in the browser
  (`d_clisrv.c:1681-1691`). Keep it that way.
- `PT_STATECORRECTION` is appended at the end of the packet enum
  (`d_clisrv.h:144`), so no stock packet number moved.
- `serverinfo_pak.kartvars` is a flag byte with three free bits (`0x04`, `0x08`,
  `0x10`), exchanged before any join; a vanilla server reports them unset.
- `askinfo_pak.time` already measures the ping before joining.

**Fixed in code, not yet verified:** the savegame. The roulette fields of 8.14
were written unconditionally, so a WORLDWIDE build and a stock one would pass
the version check and then misread each other's savegame by 16 bytes a player
(`WORLDWIDE.md` 8.28). They are now local-only, and the savegame is stock
grammar again (8.29).

**A prerequisite nobody had written down** (8.30): CI builds are `DEVELOP`, so
their `VERSION`/`SUBVERSION` are 0 and they cannot see a public server at all;
and the branch sits on upstream's development line (`v2.4-106`), not on a
release tag. "A WORLDWIDE client on a vanilla server" needs a release-config
build on the release base the public servers run.

**The work, in order:**

1. ~~Roulette fields in local snapshots only~~ -- done in code (8.29).
2. **One server-side meaning of "WORLDWIDE mode".** Today it is two switches on
   two machines (`rollback_twoclock` on the client, `rollback_correct` on the
   server) and the host's delay exemption infers it from `g_correctrate`
   (8.26). Give the server one switch and derive the rest from it.
3. **Advertise it**: an `SV_PREDICTION` bit in `kartvars`.
4. **Refuse vanilla clients cleanly on a WORLDWIDE server.** The client has to
   declare itself at join; a vanilla client, which cannot, gets a readable
   refusal (`SV_SendRefuse`) instead of a broken session. How to declare without
   upsetting a vanilla server that receives the same join is to be read in the
   join path before anything is written.
5. **Switch the client automatically**: two-clock and applied corrections on
   against a server advertising the bit, everything off otherwise.
6. `K_RollbackPays()` asks the bit instead of `g_correctrate`.
7. Optionally, a few bytes appended to `serverinfo_pak` (depth, correction rate)
   so the pre-join delay menu can recommend a value -- read against the length
   that actually arrived, never past it.

**Must not:** touch the four version fields, or read an appended field without
checking the packet was long enough to carry it.

**Done when:** a WORLDWIDE client joins a vanilla server and plays delay-based
without incident; joins a WORLDWIDE server and predicts; a vanilla client is
refused by a WORLDWIDE server with a readable message; a WORLDWIDE build hosting
in vanilla mode accepts vanilla clients.

---

## Client-local input delay knob (small, not scheduled)

A dial the **player** sets, kept entirely local: how many tics of buffer to hold
between their input and what gets simulated, even while two-clock covers the
round trip. GGPO calls it a local delay frame; it is the honest option for a
player on a bad line who prefers a stable picture to the last 60 ms.

**It must never reach the wire as `wantdelay`.** That was the original
`cv_mindelay` bug: the client asked the server to hold its own input, then could
not predict it. Closed by `K_RollbackPays()` (client side 2026-09-14, host side
2026-09-20 -- see `COMMANDS.md`, `rollback_twoclock`). Reusing the `cv_mindelay`
slider is an option, not a given.

**Depends on:** nothing but the two-clock pivot. **Done when:** a player can
hold N tics of their own buffer under two-clock, and the packet -- not the
setting -- shows no `wantdelay`.

The pre-join menu that offers this knob with a recommended value is step 7 of
the compatibility section.

---

## Phase D -- Feel

**Needs C**, and no log can finish it.

- **Correction smoothing** (`rollback_smooth`): written, never measured ("maybe
  ça marche"). Measure it against a control in the same session, or drop it.
- **Remote karts**: re-predicted every pass. Smooth or jittery is for eyes to
  say. If it jitters, Odamex's answer is position history and interpolation
  (`cv_netsteadyplayers`, `histx/y/z`), which this branch does not have.
- **The depth**: which lead feels best, which feeds back into B.
- **The inputs still in flight** (`rollback_history`, 8.39): the speculation
  replays every input sent but not yet applied, instead of repeating the
  newest, so quick flicks and releases are drawn as the server will play
  them. Built, off by default; judged in `playtest.sh history`. Holds the
  drawn tic's lead over the clock so the picture does not judder (8.41), and
  counts the drawn world's jumps, as a control with the switch off too.
- **Somebody hosts and judges.** Every reactivity verdict so far was given from
  the client's seat, and the host was paying 170-200 ms until 2026-09-20. The
  bench cannot stand in for this.

**Done when:** a person prefers it to the control on three races, at a stated
latency, **at least one of them while hosting**.

---

## Phase E -- Capability check

**Needs B and D.** A short calibration run in the menus that measures restore and
replay cost on the player's machine and answers the only question a player has:
can this computer run it without stuttering. It proposes a depth; it does not
print microseconds.

**Done when:** "it stutters for me" arrives as a number and a recommended
setting.

---

## Phase F -- Alpha

**Needs all of the above.** What is missing is not code:

- a downloadable build (the CI already makes one per commit);
- a short list of what to report, in a player's words;
- a way to collect logs without asking for file paths;
- a statement of what is known broken, so reports are about the rest.

**Done when:** somebody who is not Gibax has played it and reported something
useful.

---

## Backlog -- latent defects, not blocking

- `botvars.diffincrease` is `int16_t` but archived with `WRITEUINT8`/`READUINT8`
  (`p_saveg.cpp:867`, `:1635`). Grand Prix only, between rounds.
- `K_HandleLapIncrement` reads `old_x`/`old_y` as simulation after a restore
  (`WORLDWIDE.md` 8.3). (`old_z` not being restored is fixed, 2026-09-21.)
- ~~Out-of-bounds read in the bot-overwrite search~~: fixed 2026-09-21.
- On Windows `latest-log.txt` ignores `-home`/`-logdir`, so two instances in one
  folder share a log (`ROLLBACK.md`, two-instance harness). Upstream code.
- **No upstream reporting** (Gibax, 2026-09-21): bugs in upstream code are not
  reported to Kart Krew. They are fixed here only when they hurt WORLDWIDE.
- ~~The harness is not versioned~~: versioned in the private notes, 2026-09-21.

---

## Not on the path

**Labelled inputs with a server-side input buffer.** Planned when the old loop
carried its prediction forward; the two-clock pivot removed the problem it was
for. Under the compatibility policy a WORLDWIDE server may change the wire
between WORLDWIDE peers, so this is no longer a compatibility question -- it is
a cost with nothing to buy. Reopen it only if Phase D shows remote karts need
real state rather than prediction.

**State streaming à la Odamex.** A different netcode, not a bigger version of
this one: per-entity deltas plus relevance, on the scale of everything done so
far. Only worth revisiting if D fails in a way B cannot pay for.

## Risks

- **Phase A may not be one field**: "state the archive does not carry" is a
  family, and the live lead may be an instrument artefact.
- **Phase B may have no answer at sixteen karts** on ordinary hardware. Phase E
  exists to say so out loud.
- **Most numbers in the journal are n=1.** The bench fixes that going forward, not
  retroactively.
- **A person is required for D**, and for every judgement of feel.
