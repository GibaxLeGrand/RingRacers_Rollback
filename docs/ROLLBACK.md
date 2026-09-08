# Rollback netcode — state, measurements and plan

This file is the project's memory. Its predecessor was never committed and was
lost with the session that held it, which cost a day of rediscovering things
that were already known. Everything here is either measured or read from the
source; guesses are marked as such.

## Where this stands

The snapshot machinery works and is measured. **Nothing is wired into the tic
loop**: the game plays exactly as a stock build, and the rollback code only runs
when one of its console commands is typed.

What exists:

- `src/k_rollback.c` — a ring of full world snapshots, the tests, the soak, and
  the delay/depth policy.
- Small changes to `p_saveg`, `d_clisrv`, `d_netcmd`, `g_game`, listed under
  "Changes to shared code" below.

## The tools

All are debug commands, so they also appear in the pause menu's command list.

| command | what it answers |
|---|---|
| `rollback_test` | does a state survive a trip through the archive unchanged, and where does a restore spend its time |
| `rollback_resim [tics]` | do the same tics, replayed from a restored state, produce the same world |
| `rollback_soak <interval> <tics>` | the same question, over and over, through a whole race — silent unless something disagrees |
| `rollback_delay` | what the input delay is doing, and what a rollback costs against a tic's budget |
| `rollback_maxdepth [tics]` | how far back a rollback may rewind before latency must be paid for with input delay |

Every measurement line names the map, the grid and the game mode, because a
number without that context has already been misread twice.

Scenarios live in the game folder and are run with `+exec`:
`rollback.cfg` (one pass of every test), `soak_client.cfg`, `precip_ab.cfg`.

## What has been measured

Northern District, Match Race, on a client that is drawing the game.

| | 8 karts | 16 karts |
|---|---|---|
| snapshot | 100 KB | 119 KB |
| save | 0.8 ms | 0.9 ms |
| restore, before the precipitation fix | 10.5 ms | 12.0 ms |
| restore, after it | — | **5.6 ms** |
| restore, with the chain order carried | — | 7.1 ms |
| resimulation | 0.9 ms/tic | 1.7-2.1 ms/tic |

Most of a snapshot is the map, about 1.2 KB per kart on top. Doubling the grid
barely moves the snapshot but doubles the cost of simulating a tic.

A rollback of eight tics at a full grid therefore costs about 19 ms of a 28.6 ms
tic, down from 25 ms before the precipitation fix.

The restore now breaks down as: relink pointers 3.7 ms, thinkers 1.3 ms, the
rest under 0.3 ms each. **`P_RelinkPointers` is the next target.**

## What has been found and fixed

**References the archive could not keep.** The archiver set a diff bit for any
non-NULL mobj pointer without checking that its target was archived too. Those
came back NULL — or worse, resolved to whatever object had since been handed
that mobjnum. Now gated on the target actually being in the save. This also
affects joins and resyncs, not only rollback.

**Per-viewport visibility.** Renderflags are stripped of the `RF_DONTDRAWPx`
bits before going over the wire, which is right, but a local snapshot needs
them back. `P_SaveNetGame` takes a `local` flag.

**The weather.** Precipitation is never archived, so a restore purged it and
rebuilt every raindrop: 4.3 ms of a 12 ms restore, measured by A/B against
`drawdist_precip`. A local restore now keeps its own rain. This also removed
2 ms from the thinker purge, since each raindrop cost a linear scan of the
renderer's interpolator list.

**Decoration is not deterministic, by design.** The soak found item debris
diverging between two replays: `rotate3d` picks its rollangle with
`M_RandomKey`, the game's *unsynced* generator, which draws from the C library
and is never archived. That is not a desync — nothing else agreed on that angle
either — but it made the oracle cry wolf, so both passes now seed the C library
identically. A real rollback will respin debris that was already in flight.

**Determinism is not closed.** The soak, resimulating one tic and checking
every ten, fails on roughly 5 percent of checks: a kart is hit on the live pass
and not on the replay, and the two restored passes agree with each other, so the
replay repeats and the restore is what loses something. Established so far:

- Not randomness, not the thinker list, not the blockmap or sector chains. The
  chain order genuinely changed across a restore and now does not; the failure
  rate did not move, so ordering was a real difference but not the cause.
- `oldcmd` was missing from the archive, which the gameplay code reads to tell a
  button press from a button held. Carrying it helped, but a sample worth
  trusting puts the rate at 5.5 percent against 7.6 before, so it was not the
  dominant cause either.
- The player structures come back correct. Comparing player_t in memory either
  side of a restore -- which sees what comparing two archives cannot, since a
  field the archive skips is equal on both sides by construction -- leaves only
  alignment padding and the drawing angles.

The objects come back correct too, compared the same way: only the thinker
bookkeeping and old_scale2 differ. And the comparison now runs *inside* a
resimulation check rather than at an idle moment, holding its findings back and
printing them only when that check fails — at which point the players differ
only in karthud, the HUD timers, and their interpolation angles.

So a restore reproduces the simulation state. What differs is what happens next:
a hit trace, recording every time the damage path counts a hit, takes it back, or
judges a player invincible, shows the two passes agreeing on every event they
share and the replay having **more of them**. Ten against twenty in one sample,
two against four in another.

That "more of them" was an instrument lying: the third pass was still recording
into the replay's tally, so every figure was exactly double. Corrected, the
collision counter rules itself out — on failing checks both passes examine the
same pairs (24194, 37946, 48686) in the same order, after a full tic of
simulation, which also buries the ordering theory for good.

So: identical state, identical pairs in identical order, identical damage
verdicts, different outcome. What differs is a value computed during the tic,
and the memory comparison now runs at the far end of both passes rather than at
the restore. The failure rate is 2.7 percent over 590 checks, down from 7.6.

That reading has now been taken -- out of the last soak's log, which had it all
along; the crash killed the run, not the reading. Twenty failures in 120 checks,
and every report named the same four offsets, none of which mattered, because
the instrument was throwing away what did. It kept six findings and dropped the
rest silently; it scanned from player zero with a quota of eight, so the six
that survived were always the interpolation and HUD counters of players 0, 1 and
2, which differ on every check because no archive carries them; it reported a
run only when it did not begin on a multiple of eight, meaning to skip pointers,
which discarded every field that begins on one and reported pointer bytes as
fields whenever their low byte happened to match; and its crib line asked one
`sizeu` buffer for two of its values, so `cmd` and `faultflash` were both
reported at 892. The players whose archived record actually diverged, eleven and
thirteen, were never looked at.

Decoded by hand from the same log, the archived divergences are two named
fields:

- **`player->tilt`**, the camera lean. It is archived, and it is computed during
  the tic: `DoABarrelRoll` calls `R_GetPitchRollAngle`, which for anyone who is
  not a local display player reads `viewx`/`viewy` -- renderer globals the frame
  interpolator writes, at frame rate, from wherever the camera was when the last
  frame was drawn. Nothing in the simulation reads tilt; only the camera does.
  The same call is made by `K_trickPanelTimingVisual`, which uses it to place
  the MT_THOK sprites it spawns -- so local view state reaches the positions of
  archived objects. That is the third of this family, after the render flags and
  the per-screen visibility bits.
- **`player->timeshitprev`**. This one is a real divergence. `timeshit` counts
  the hits taken during a tic and is copied into `timeshitprev` at the end of
  it, so a difference of one means one pass took a hit the other did not.

The build that names fields then ran a 500-check soak, and it answers the
question the hand decoding could only sample: **34 failures in 500 checks (6.8
percent), all 34 of them "the restore loses something the simulation uses" and
not one a non-repeatable replay -- and 26 of the 34 name `timeshit` or
`timeshitprev`.** Twenty-nine of the differences are in the players block and
five in the thinkers block. `tilt` did not appear once. So the hit counter is
not one of several remaining faults; it is very nearly the whole of what is
left, and it is a restore fault.

That run also priced two things nobody had measured:

- **A snapshot grows with the race.** 120 KiB is what one weighs seconds after
  the start; three minutes into the same race it was 318 KiB, because the world
  accumulates objects as it is played. The ring's guard caught it in the slack
  and said so. Every size and timing figure in this document is an
  opening-lap figure and wants re-measuring late in a race -- the restore cost
  in particular, since it was 11 ms at 120 KiB.
- **The object comparison had the same alignment fault as the player one**, so
  every `MT_RING` line it printed was `bprev` or `touching_sectorlist`: half of
  what a failing check said was pointer noise. And `P_LocatePlayerField` did not
  know where the players block ends, so a difference in the thinkers block was
  reported as "player 15, 128934 bytes into their record". Both fixed.

### What three soaks then settled

Three 500-check soaks on RR_NorthernDistrict, sixteen karts, told apart by one
thing: whether a person was driving.

| | idle | **driving** | idle, cameras archived |
|---|---|---|---|
| failures | 34 / 500 | 144 / 550 | 36 / 500 |
| replay not repeatable | 0 | **121** | 0 |
| `tilt` named | 0 | **242** | **0** |
| crash | the ring's size guard | -- | none |

Driving makes the failure rate quadruple and turns almost every failure into a
replay that is not repeatable -- two restored passes disagreeing with each
other, which no amount of *archived* state can explain. Idle, it never happens.

The difference is the camera. `player->tilt` is archived and is computed during
the tic by `DoABarrelRoll`, through `R_PointToAnglePlayer`, which answers from
the local camera; the camera is moved by the tic and was restored by nothing, so
each pass started from wherever the pass before had left it. Standing still the
camera does not move and nothing diverges. So the cameras now go into a local
snapshot -- not into a netgame savegame, where no other machine has any use for
them, but the tic reads them, which makes them state.

The same runs fixed the crash that had been ending soaks all along. It was never
about how much memory anything takes: `P_NetUnArchivePlayers` read the archived
capacity of the item roulette list straight into the structure, destroying the
size of the block the player already held, and then called `Z_Realloc`
unconditionally. Sixteen lists, two restores per check, hundreds of checks. The
list only grows now, and a 500-check soak finishes without dying.

What is left is `timeshitprev`, 31 of the 36 remaining failures, and always the
same shape: `timeshit` zero on both sides, `timeshitprev` 1 on the live pass and
0 on the replay, across a dozen different players. Both passes agree on all
sixteen traced copy decisions, hitlag and nullHitlag included -- so they differ
over *what was copied*, not over whether to copy, and the trace now carries the
values as well as the decision.

### And what the ordering fix did to it

Measured on the binary, not on the branch: the exe carries `<branch> <short
sha> <subject>`, so a run is only counted if `grep` finds the commit's own sha
in the file that ran. That check exists because a run was once reported against
a stale binary -- the artifact of the *previous* commit, installed because
`gh run list --limit 1` answers with the last finished run when the new one does
not exist yet.

**500 checks, 2 failures. 0.4 percent, from 7.2, with no crash and not one
unrepeatable replay.** Nothing at all in the players block any more.

Both survivors are the same thing: `MT_SHADOW`, the drop shadow, one byte going
from 1 to 0, identical diff masks on both sides, and only late in a race
(leveltime 5140 and 5210). Decoration -- but the oracle counts it, so it counts.

What is left to close it is the object-side twin of `P_NamePlayerField`: the
mobj record is written under diff masks, so naming a field inside one means
walking those masks the way the archiver does.

### The played race, after the cameras went in

| | driving, before | driving, after |
|---|---|---|
| failures | 144 / 550 | 149 / 530 |
| replay not repeatable | **121** | **14** |
| restore loses something | 23 | **135** |
| `tilt` named | 242 | 141 |

The cameras did what they were archived for and nothing more. Replays that
were not repeatable collapse, 121 to 14 -- two restored passes now agree,
because they now start from the same camera. But `tilt` did not go away; it
changed category. It is now a difference between the live pass and the
restored one.

And the restore is not what loses it: the post-restore comparison, which reads
the structures with no tic in between, does not report offset 84 for any
player. So both passes start with the same tilt, the same camera and the same
frozen inputs, and one tic later they disagree.

The players it names are bots -- 1, 5, 3, 2, 4, 9 -- and a bot is not a display
player, so `R_PointToAnglePlayer` answers it from `viewx`/`viewy` rather than
from a camera. Those are renderer globals written by the frame interpolator,
and no frame is drawn between the passes, so they cannot move. Something the
tic reads still differs and it is not yet known what: the next instrument
should record `viewx`, `viewy` and the camera at the moment `DoABarrelRoll`
runs, in both passes, rather than reasoning about which of them could have
moved.

### Zero

Taking tilt out of a local snapshot, and the cameras with it, ends the
sequence: **470 checks, 0 failures, in a played race.**

The four steps that got there, each measured on a played race of its own:

| | failures | replay not repeatable |
|---|---|---|
| before any of it | 144 / 550 | 121 |
| cameras archived | 149 / 530 | 14 |
| tilt only for the player being looked at | 23 / 530 | 21 |
| tilt and cameras out of the snapshot | **0 / 470** | **0** |

The middle two are the same mistake at two levels: archiving one piece of
presentation to stabilise another. The camera was archived so tilt would
reproduce; then the cameras themselves became 43 of the 44 remaining
differences, because a camera is driven by a local view that nothing archives
either. What ended it was deciding that neither belongs in a snapshot at all.

The check that says the run counted: no "the world did not advance", the soak
counting its way up from ten, and the commit's own sha in the binary that ran.

With the cameras restored again -- beside the archive, not inside it, so they
are put back without being compared -- a played race gives **2 failures in 500,
0.4 percent**, the same rate as idle, and the camera stops jerking.

Both are the same thing: a bit in the player record's `flags` word, which says
which objects are attached. 0x20d8 became 0x22d8 -- `WHIP`. So the whip object
is attached on one pass and not on the other, which is the first survivor that
is not decoration.

Three techniques worth keeping:

- Compare structures in memory, not archives, when hunting for state the archive
  does not carry. Pointers differ legitimately; they sit on multiples of eight.
- Field offsets come from the compiler, not from counting: one deliberately
  invalid declaration per field (`char (*p)[offsetof(player_t, x)] = 1;`) makes
  it report every offset in its error messages. 350 fields mapped at once,
  without a local build of the game.
- Better still, from the build itself. The CI publishes a `.pdb` beside the
  Windows exe, and the game leaves a `.dmp` when it dies, so
  `cdb -z <dump> -y <game folder> -c ".reload /f; dt <module>!player_t"` prints
  the exact layout of the build that wrote the log -- 352 fields, the module
  name's hyphens turned into underscores. It is trustworthy rather than merely
  plausible because the five offsets the game prints itself agree with it.

## Traps that have cost time

- `map <name>` **forces a Grand Prix**, whose grid is hardcoded to eight karts.
  A Match Race needs `map <name> -match`, and `maxplayers` only means anything
  in the latter.
- `-match` **cannot be passed on the command line**: the argument collector
  stops at the first word starting with `-`. Use `+exec` with a cfg.
- A dedicated server with no client connected **falls back to RR_TESTRUN and
  freezes leveltime**. Measurements taken there are not measurements of a race,
  and a soak checks nothing at all.
- `.gitattributes` covers `/src/*.c` and `/src/*.h` but **not `*.cpp`**, so a
  line-ending conversion of `p_saveg.cpp` gets committed verbatim.
- **Do not size anything on `NETSAVEGAMESIZE`.** It is 768 KB, for the worst a
  netgame savegame can be; a real sixteen-kart snapshot is 120 KB. Slots sized
  on it made the ring reserve 20 MB to carry 2.5, and the game died on "not
  enough memory for item roulette list" — an allocation with nothing to do with
  any of this.
- **`sizeu1` to `sizeu5` are five buffers, not five formats.** Asking the same
  one twice in a single call prints the same number twice, without a warning
  from anything.
- **A new instrument lies.** Six times now: a profile from a map I had
  not checked, a soak on a server that was not racing, an order comparison
  against an empty list, an offset extractor pairing names to the wrong
  offsets, a third pass counting into the replay's tally so every figure came
  out exactly double, and a comparison that discarded most of what it found and
  spent the rest of its quota on players nobody had asked about. A diagnostic
  must state the size of what it examined, a suspiciously round ratio is an
  instrumentation fault until proven otherwise, and whatever a diagnostic
  filters out it must count and report.
- **Playing during a soak** stutters and misfires anything edge-triggered — ring
  usage, item throws — because the replayed passes use frozen inputs. Use
  `play.cfg`, or `rollback_soak 0`.

## Changes to shared code

Everything else is in `k_rollback.c/h`, which nothing calls unless a command is
typed.

- `d_clisrv.c/h` — `Consistancy()` is no longer static.
- `d_netcmd.c` — registers the commands.
- `g_game.c` — one call after `P_Ticker`, which returns immediately unless the
  soak is on.
- `p_saveg.cpp/h` — the `local` flag on save and load, the reference gating, the
  precipitation handling, the load profiling, and two diagnostic helpers.
- `typedef.h`, `src/CMakeLists.txt` — one line each.

The archive format does not change, only which diff bits are set, so a build
with these changes stays compatible with a stock client or server.

## Plan

**1. Finish the restore breakdown.** `P_RelinkPointers` is 3.7 ms of 5.6.
Find out what it spends it on before deciding whether to optimise it.

**2. Soak the determinism.** Run the check through whole races, on many maps
and modes — items, hitlag, respawns, finish lines, Encore, Battle. This is what
finds a field nobody archives, and it needs no people. It runs on a client; a
dedicated server needs a client connected to it before it will race.

**3. The rollback loop, behind a switch that is off by default.** Save each tic
(0.8 ms, affordable), predict remote inputs, and on an input that contradicts a
prediction, restore and replay. The tests become the regression net.

**4. Two instances on one machine, with latency.** The game has `-server` and
`connect`, so a server and a client can be scripted locally. It has no
artificial lag setting, so testing "rollback at 80 ms" means adding a debug one
— a dozen lines, off by default — or using an external shaper.

**5. Against a stock build.** Our client to an unmodified server and back, to
prove the compatibility claim above rather than assert it.

**6. A machine capability test, in the menus.** A short calibration race
against bots that measures the restore and resimulation costs on the player's
own machine and settings, and answers the only question they care about: can
this computer run rollback without the game stuttering. The measurements exist
already — `rollback_delay` prints the verdict as a percentage of a tic's budget
— so what this needs is a menu entry, a representative course to run, and a
recommendation rather than microseconds. It should also propose settings:
a rollback depth this machine can sustain, with the input delay that covers
whatever latency is left over. Worth doing before any wider test, because it
turns "it stutters for me" into a number the player can report.

**7. Alpha with people.** Only after the rest. A CI build already exists per
commit; what is missing is a short list of what to report and a way to collect
logs.

## Where to pick this up

The crash a soak keeps dying on -- "not enough memory for item roulette list"
-- is not about how much memory anything takes. player->itemRoulette.itemList is
a heap-allocated list, and P_NetUnArchivePlayers reallocates it on every
restore. A soak restores twice per check, so hundreds of checks mean thousands
of reallocations across sixteen players, and the allocator eventually refuses;
the roulette is just where it happens to land. Cutting our own footprint from
30 MB to under 10 changed nothing, as it could not.

Which is a design lesson worth more than the crash: a restored state should not
contain dynamically allocated sub-objects, or every rollback pays for
reallocating them. Fixed capacity, or a pool.

The comparison at the far end of both passes has now been read, and the
instrument that was hiding it has been fixed: it no longer discards a finding
without counting it, it starts at the player the archive comparison blamed, and
`P_NamePlayerField` names the field rather than giving its distance into a
record. What that reading found is above -- `tilt` and `timeshitprev`.

Three ways on from here:

1. Run a soak on the build that names fields and read the list. Everything so
   far was decoded by hand out of one log; the tool should now produce the same
   answer by itself, on every failing check, for every player. That is also the
   check on the naming table, which is a copy of the archiver and can drift from
   it: if it names a field whose value makes no sense, it has drifted.
2. Decide what to do about `tilt`. Nothing in the simulation reads it, so the
   honest fix is to keep local view state out of what a local snapshot compares,
   the way the render flags were handled -- but that is treating a symptom until
   it is understood why two passes with no frame drawn between them, and with
   identical objects, arrive at different values. `viewx` and `viewy` cannot
   move during a resimulation. Something else does.
3. `timeshitprev` is the one that matters: a hit landed in one pass and not in
   the other, which is a real divergence and not a cosmetic one. The hit trace
   already exists and already agrees on every event the passes share, so what to
   trace is the attempt that only one pass makes.

Step 3 -- hooking the tic loop behind a switch -- remains available and may be
the better revealer: snapshots cost 0.8 ms, restores 6 ms, determinism holds on
97 percent of checks, and a real rollback replaying real inputs would not
misfire everything edge-triggered the way the frozen-input test does.
