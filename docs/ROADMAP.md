# Roadmap to a playable alpha

Rewritten 2026-09-21. It replaces the 2026-09-10 original and the three
revision notes that had been stacked on top of it (2026-09-10, 2026-09-10
evening, 2026-09-20). Nothing they said was dropped: their conclusions are
folded into the phases below, and the order is brought up to date. The
evidence for every line lives in `WORLDWIDE.md`; this file only says what is
left, in what order, and what each step has to prove.

The old phase numbering (1 to 7, in `ROLLBACK.md`) was retired on 2026-09-10:
it described an architecture where the authoritative clock ran ahead.

## Where this starts from

- **Solved, from the client's seat: input lag.** "Ça répond tout de suite", five
  races, and again after the architecture changed underneath it.
- **Architecture settled.** `gametic` runs only confirmed tics; the speculation
  runs above it on a snapshot and is rebuilt every pass. A light correction
  channel replaces the stock full-state resend: **0 resends a race**, against
  7 to 9 without it.
- **Unattended bench.** Six bots move the world without a driver, so a
  measurement can be repeated instead of being n=1.
- **Open:** the cause of the residual drift, the cost at sixteen karts, the
  relabel histogram's `+2` cluster, and vanilla compatibility (see the dedicated
  section).

## Ground rules for every step

- **No launch without Alex's explicit go-ahead, every time**: the game, a
  `playtest.sh` scenario, a soak, the bench. Writing code and scenarios is
  free; running them is asked for.
- Measure on a binary verified by its sha. Write the prediction before the run.
  Keep a control in the same session.
- ⚠ The harness (`playtest.sh`, the `*.cfg` scenarios) lives only in the game
  folder of the original machine and is not in this repository. Versioning it
  is a prerequisite for measuring from anywhere else.

## Next, in order

1. **Make the savegame vanilla again** -- **done in code** (`WORLDWIDE.md`
   8.29): the four roulette fields are in local snapshots only. Still to
   verify, each a launch to be asked for: `soak_leak.cfg` stays at 0, then a
   join in each direction against a stock build of the same base.
2. **Close the gap between 8.19 and 8.20** (8.28, point 2): the one live lead on
   the drift. One instrument, three values per tic, one driven race.
3. **Somebody hosts and judges.** The host's 170-200 ms delay was removed on
   2026-09-20 (8.27) and nobody has played from the host's seat since. One race,
   and only a person can do it.
4. **Phase B's first measurement**: a pass at sixteen karts, late in a race.
5. **Still owed from 2026-09-10**: four more unattended repeats of
   `playtest.sh correct` (the driven repeat is done: 8.16, 8.18), and the
   correction-rate sweep, `rollback_correct 8`, `16`, `35` -- at 0.25 units of
   residual per four tics, one correction every four tics is probably more than
   needed, and each halving is free bandwidth.

---

## Phase A -- Understand the drift

**No longer the gate for the alpha.** The correction channel absorbs the
divergence (0 resends, mean residual 0.25-0.86 units, a kart being about 40
wide). Phase A is now what lowers the correction rate and the residual, and
what makes the stock consistency check agree again.

**Excluded, each by measurement:** the restore (0 contamination over 1400 round
trips, with and without bots); the archive (0/522 leak checks after the
roulette fix, 8.15); tic determinism (0/330 resim checks); the synchronised RNG
(identical until positions have already drifted, 8.2); the damage path (same
hits, same hashes, 8.8); the confirmed clock running on a guess (8.9, 8.17,
8.20); late resends (0 arrivals, 8.17).

**Live lead:** 8.19 shows the client's confirmed world running different inputs
than the server's for the human kart. 8.20 attributes it to the server's
`faketic` relabelling, but relabelling alone should not make two confirmed
worlds disagree (8.28, point 2). Settle that first.

**Also open:** the relabel histogram's `+2` cluster (2557 of 6403 packets,
8.27). The instrument is named: log `lagDelay` where it goes on the wire
(`netbuffer->u.clientpak.wantdelay = lagDelay` in `CL_SendClientCmd`), tagged
by sender.

**Done when:** zero `Game state reloaded` with the resend **not** suppressed
(`rollback_correct N 0`) over five unattended races and two driven ones -- or,
failing that, the drift explained down to a mechanism and its residual stated.

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
`P_RelinkPointers`, 4.7 ms of an 8.6 ms restore, resolves every pointer with a
linear scan of all mobjs. An index from `mobjnum` to object makes it linear
instead of quadratic, without changing what it computes.

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
- three maps with geometry the test maps lack: steep slopes, water, a big drop.

**Item policy:** do not predict the roulette's result. Let the reel spin under
speculation and commit the pick on a confirmed tic (`WORLDWIDE.md` §4). Prove it
with a deliberately driven item test, not with the soak.

**Done when:** each runs with no resends and no divergence that has not been
read and understood.

---

## Compatibility and capability advertising

**Policy, decided by Alex on 2026-09-21: the server decides.**

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
- `old_z` is never restored, and `K_HandleLapIncrement` reads `old_x`/`old_y` as
  simulation after a restore (`WORLDWIDE.md` 8.3).
- Out-of-bounds read in the bot-overwrite search: the array is indexed before the
  bound is checked (`d_clisrv.c:4120`). Upstream code.
- On Windows `latest-log.txt` ignores `-home`/`-logdir`, so two instances in one
  folder share a log (`ROLLBACK.md`, two-instance harness). Upstream code.
- **No upstream reporting** (Alex, 2026-09-21): bugs in upstream code are not
  reported to Kart Krew. They are fixed here only when they hurt WORLDWIDE.
- The harness is not versioned (see the ground rules).

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
