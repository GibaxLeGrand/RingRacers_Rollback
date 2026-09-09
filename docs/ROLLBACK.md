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

Measured on the binary, not on the branch: the exe carries `<branch>\0<short
sha>\0<subject>`, so a run is only counted if `grep` finds the commit's own sha
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

### Three maps, and the end of the player-state failures

Writing a player's attachment only when its object is going into the save --
the mobjnum-reuse rule that had been applied to object-to-object pointers and
never to player-to-object ones -- ends every failure the players block had.

| map | before | after |
|---|---|---|
| RR_NORTHERNDISTRICT | 2 / 500 | **0 / 520** |
| RR_GREENHILLS | 8 / 590 | 4 / 580 |
| RR_SONICSPEEDWAY | 2 / 560 | 4 / 560 |

Not one `flags` failure anywhere afterwards, and no unrepeatable replay on any
map. Each run is checked against the map name the resim prints, so a soak on the
wrong map cannot be reported as a soak on the right one.

What is left is 0.7 percent on two of the three, entirely in the thinkers block
and entirely decoration: `MT_SHADOW` seven times, `GREENHILLSTREE` twice,
`MT_SPRAYCAN` once. Same record length, same diff masks, one byte flipping
between 0 and 1. Which field that is has **not** been established -- naming a
field inside a mobj record needs the object-side twin of `P_NamePlayerField`,
and those records are written under diff masks rather than in a fixed order.

So: player state is clean on three maps, and the residue is scenery. Recorded as
identified rather than explained.

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

Measured on one configuration -- RR_NorthernDistrict, Match Race, sixteen karts,
one human and fifteen bots. **Idle: 2 failures in 500. Driving: 2 in 500.** Both
0.4 percent, no crash over a full run, no unrepeatable replay. Nothing is wired
into the tic loop; the game still plays as a stock build.

⚠ **Read that rate as a rate and nothing more.** It comes from a byte-for-byte
`memcmp`, so the pass/fail verdict is sound -- but every *description* of what a
failure consisted of, anywhere in this document before the phase 3 section, was
produced by instruments since caught reporting a first offset only, a cap of
three with no count, and a per-object walk that lined up unrelated objects. "Two
failures in five hundred" does **not** mean two bytes, two objects, or two
fields. One failure measured properly turned out to be a four-byte shift that
made eight thousand bytes differ. When reading an old figure here, check whether
the instrument behind it could count.

### Resuming this, mechanically

Nothing here can be built locally: Ring Racers needs SDL3, which the WSL
toolchain does not have. The only path is the repository's GitHub Actions
build, about four minutes, whose Windows job publishes a playable exe as an
artifact. A single `.c` file can still be syntax-checked without it:

```
gcc -fsyntax-only -Wall -std=gnu11 -I <a folder holding a stub config.h> -I src -DHAVE_SDL src/k_rollback.c
```

**Never measure against a binary without checking it is the one you built.**
The exe carries `<branch>\0<seven-character sha>\0<commit subject>` in plain
text, so:

```
grep -q "$(git rev-parse --short=7 HEAD)" ringracers_rollback-netcode.exe
```

and refuse to run if it is missing. `gh run list --limit 1` answers with the
*previous* run while the new one is still being created, which is how a whole
measurement was once taken against the artifact of the commit before -- take
the run by `headSha` instead.

The game folder is `D:\RingRacers - 24 - Copie`. The scenarios live there and
end in `quit`, so each run stops by itself:

| file | what it does |
|---|---|
| `soak_northern.cfg`, `soak_greenhills.cfg`, `soak_speedway.cfg` | a soak of about 500 checks on one map, unattended |
| `replay_test.cfg` | `rollback_keep` on, then `rollback_replay` at 1, 4, 8 and 16 tics |
| `soak_client.cfg` | the soak a human plays under |
| `netserver_min.cfg`, `netclient_min.cfg` | the two-instance harness, **run and working** |

A soak needs nobody: the two failures that ended phase 2 were on bot players,
and fifteen bots exercise items, damage, respawns and the finish line. A human
is needed only to judge feel, and to see things no log records -- which is how
the camera jerk was found.

### Where each phase stands

| phase | state | what it is waiting on |
|---|---|---|
| 1. Restore breakdown | done enough | `P_RelinkPointers` is 3.7 ms of 5.6 and has never been broken down further. Not blocking anything. |
| 2. Soak the determinism | **done enough** -- three maps, player state clean, residue is scenery | nothing blocking |
| 3. Rollback loop behind a switch | **started** -- the ring fills during play and a replay runs on real inputs | the packet half, which needs two instances |
| 4. Two instances with latency | not started | needs 3, and a lag knob the game does not have |
| 5. Against a stock build | not started | needs 3, and a wire-format audit that has never been done |
| 6. Capability test in the menus | not started | needs 3 for figures that mean anything |
| 7. Alpha with people | not started | needs 3 through 6 |

### What is left in phase 2

**One open defect.** The last two failures of a played race are a bit in the
player record's `flags` word -- `WHIP` -- so the whip object is attached on one
pass and not the other. Every survivor before it was presentation: shadows,
interpolation angles, camera lean. This one is an attack, so it is worth
explaining rather than excluding.

**Breadth, which is the real gap.** Everything above is one map and one mode.
What has never been soaked at all:

- **Other maps.** At least three, chosen for geometry the current one does not
  have: steep slopes, water, a big drop.
- **Grand Prix.** Its grid is hardcoded to eight karts and it runs bots
  differently -- `map <name>` without `-match` is enough to get one.
- **Battle**, and **Encore**.
- **A full race, start to finish**, rather than a soak dropped into the middle
  of one: the grid, the finish line, the results screen.
- **Respawns and item use**, deliberately, since the frozen-input test misfires
  anything edge-triggered and cannot exercise them on its own.

Target for each: 300 checks or more, zero failures or only ones that have been
read and understood.

**And a re-measurement.** Every cost in this document is an opening-lap figure.
A snapshot was 120 KiB seconds after the start and 318 KiB three minutes in,
because the world accumulates objects, so the 11 ms restore wants taking again
late in a long race. That number is the whole budget question.

### Before phase 3 -- the rollback loop

1. The whip explained, or shown to be presentation and excluded on purpose.
2. Two more maps and Battle, at 300 checks each, with nothing unexplained.
3. ~~The restore cost re-measured late in a race~~ -- **done**: 8.6 ms against
   6.8 early, `P_RelinkPointers` 4.7 of it, and a sixteen-tic replay 31 to 49 ms
   on top. A deep rollback does not fit in a tic; the cap is the answer.

Not more than that. A soak replays frozen inputs, which is structurally blind to
everything edge-triggered; a real rollback replaying real inputs is a strictly
better detector, and the soak becomes the regression net behind it. Phase 2 is
finished when it stops being the best instrument available, not when it is
perfect.

### Phase 3, where it stands

**Done and measured.** `rollback_keep` fills the ring during ordinary play, one
save a tic, off by default. `rollback_replay <n>` rewinds that many tics and
runs them again with the inputs the netcode recorded -- `netcmds` holds 512 of
them -- then checks the world arrived where it already was, prices the replay
per tic, and puts the world back either way. It is the first thing in this
project that replays real input rather than frozen input, which is the blindness
the soak could never fix.

**Cost, measured at four depths on RR_NORTHERNDISTRICT with sixteen karts:**
1.7 to 2.6 ms per replayed tic, so a sixteen-tic rollback costs about 30 to
40 ms of replay on top of a 6 ms restore. Against a 28.6 ms tic that is the
budget question phase 6 exists to answer, and it says a deep rollback cannot
be paid for inside one tic on this machine.

**Four faults, in the order they were found**, each by the instrument rather
than by reading:

1. It wrote its snapshot through a null pointer and took the game down. The
   slots the tests compare in were allocated inside the resimulation check, so
   any other command that borrowed them found nothing. Allocated on first use
   now, by whoever needs them.
2. It replayed one tic too many. A console command runs at the top of
   `TryRunTics`, **before** the tic loop, so `gametic` is the tic about to run
   and the world is the one the tic before it left.
3. **The restore rewinds `gametic`, and `P_Ticker` never advances it** --
   `TryRunTics` does, and a replay that calls `P_Ticker` directly goes around
   it. Every replayed world was stamped with the tic it started from. Worth
   carrying into the real loop: replaying tics by hand means keeping `gametic`
   by hand.
4. The loop feeds each tic the input that ran, so it leaves `cmd` holding the
   last one and `oldcmd` the one before. Both are carried by a local snapshot,
   so the comparison reported them faithfully. Bookkeeping rather than a world
   that went somewhere else -- and the first attempt at putting them back read
   them from the **wrong side of the restore**, so it wrote back the input of
   the tic the replay starts from. Fixed by moving the capture up to before
   `K_LoadGameState`, and the comparison now names the ticcmd field instead of
   leaving a byte offset: `K_NameTiccmdDifferences` walks the eleven fields of a
   ticcmd, and the count prints even when it is zero, because a difference that
   is put back silently survived two builds exactly that way.
5. **The replay copied `netcmds` straight into `players[].cmd`, and that step is
   not a copy.** A ticcmd arrives carrying the `leveltime` it was built at
   (`g_build_ticcmd.cpp:171`), and `G_Ticker` turns that stamp into the control
   lag the simulation reads -- or zero, for a bot. The replay skipped the
   conversion and handed the simulation the stamp: **latency 130 where the live
   tic had 2**, on every one of six replays. Nothing diverged on it, because both
   readers clamp (`min(latency, 6)` for drift leniency in `p_user.c:2416`, the
   item roulette's fudge in `k_roulette.c:2041`), so it is a hazard rather than a
   failure -- and the clamp is why six identical worlds did not catch it. The
   step is now a function of its own, `G_MoveTiccmdsIntoPlayers`, called by
   `G_Ticker` and by the replay, so there is only one of it. **General lesson for
   the real loop: replay a tic through the live loop's own steps, not through a
   reconstruction of them.**

**Measured, each binary verified by its sha:**

| build | replays | identical | input differences named |
|---|---|---|---|
| `9382bf1` and before | 6 | **0** | the inputs, every time |
| `758e7ca` -- capture moved before the restore | 6 | **5** | 2 a replay (`latency`) |
| `e495c33` -- replay goes through the live input step | 12 | **8** | **0** |

**Cost, and the shape of it.** Early in a race a replayed tic is 1.6 to 1.9 ms
and a snapshot 137 to 152 KiB. Late in the same race -- `leveltime` past 1900 --
it is **2.3 to 3.2 ms** and 152 to 161 KiB, because the world has accumulated
objects. So a sixteen-tic rollback late in a race is **40 to 52 ms of replay**
against a 28.6 ms tic, where the early figure is 26 to 30 ms. Both are past a
tic's budget; the depth cap is the answer, and phase 6 is where it gets picked.

**What is left is late in the race, and the instrument that was supposed to
explain it was lying.** Four or five of the twelve replays differ, every one of
them a deep replay past `leveltime` 1900, and the snapshot puts its first
difference around byte 136000 of 150000 -- in the thinkers block, about ninety
percent of the way in.

The per-object pass was reported as naming four objects -- rings, a spring, an
arrow sign. It was showing the first three of a list it never counted. Once it
counted, on `6b2c796`:

```
rollback_replay: 2070 objects compared, 714 differed, 3 shown in full
rollback_replay: 2031 objects compared, 1036 differed, 3 shown in full
rollback_replay: 1964 objects compared, 1111 differed, 3 shown in full
```

**Seven hundred to eleven hundred objects out of two thousand cannot have changed
in a snapshot whose first differing byte is ninety percent of the way through
it.** The pass walks both captures by position and trusts that position N is the
same object on both sides. It has a type check for exactly that, but a shift
inside a run of hundreds of `MT_RING`s is `MT_RING` against `MT_RING`, so the
check passes and every ring after the shift "differs". The four named objects
were the first three entries of a misaligned walk, twice. **Discard that table;
it was never evidence.** The records now carry `mobjnum` and the pass says how
many positions hold the same object on both sides, and says plainly that its
count means nothing when they do not.

**The memory comparison is the one that held up.** It keys on `mobjnum` rather
than on position, and with `K_NameMobjField` it now names the field. What it
reports, over the whole run:

| field | what it is |
|---|---|
| `old_x`, `old_y`, `old_z`, `old_x2`, `old_y2`, `old_angle2`, `old_scale2` | interpolation: where the renderer should draw from |
| `resetinterp` | the flag that tells the renderer not to interpolate |
| `whiteshadow`, `shadowcolor` | shadow appearance |

Every one of those is presentation, and the same family as the renderflags, the
per-viewport visibility bits, `tilt` and the camera. None is in the archive, so
the round-trip stays `IDENTICAL` while the structures differ -- which is exactly
what "the restore does not rebuild interpolation state" looks like, and it costs
the simulation nothing.

It also reported an `MT_RING`'s `x`, `y` and `z` as having moved. That one is
most likely **not real**: `mobjnum` is handed out afresh by every save and never
cleared, so a number can name two different rings either side of a restore, and
the comparison had no type check. It has one now, and counts what it skips.

**Then the comparison was asked how big the difference was, and the answer
changed the shape of the problem.** On `dfe236c`, twelve replays, nine identical,
and the three failures read:

```
8838 bytes differ in 2003 runs, from byte 138421 to byte 169360 of 169361
  replay DIFFERS -- the replay is 169361 bytes, the world as it was was 169365
80 bytes differ in 20 runs, from byte 139719 to byte 179613 of 181427
10285 bytes differ in 2178 runs, from byte 145080 to byte 184103 of 184104
```

**Four bytes.** The replayed world archives to four bytes less than the world it
is compared against, and from the first difference the two streams are offset by
four, so they disagree in two thousand places all the way to the last byte. The
eight thousand differing bytes are the *signature of a four-byte shift*, not two
thousand changed values -- and without the count, the single offset that used to
be reported could not tell the two apart. One record lost one field.

That also explains the per-object pass: a four-byte shift in the stream is not
what misaligned it, but the object list genuinely drifting is, and both were
invisible behind a first-difference-only report and a cap of three.

The byte at the divergence reads `0x33333330` against `0xaaaaaaa0`; the earlier
sighting was `0x77777770` against `0x2aaaaaa8`. All four are exact multiples of
twelve degrees as `angle_t` -- 72, 240, 168 and 60 -- which says an angle that
advances in fixed steps, not a position.

**So the instrument that was still missing is a way to name the record a
snapshot offset falls in**, and the archive has no index. `K_LocateSnapshotRecord`
does it without one: the two snapshots are identical up to the first difference,
so the bytes just before it are a fingerprint, and the capture of the living world
holds those same bytes split one record per object. Finding the fingerprint in the
capture names the object and the offset inside its record, with no knowledge of
the layout. It reports the window length it matched on and how many records
matched, because a fingerprint that matches twice names nothing. **Not yet
measured.**

### Closed: twelve replays out of twelve, byte for byte

On `ce17cc5`, binary verified by its sha, on a scenario with no restore of its
own in it: **twelve replays, twelve IDENTICAL, no input differences, clean
exit.** Depths 1, 4, 8 and 16, carried from `leveltime` 902 out to 3718 -- over a
hundred seconds into the race, not an opening lap.

| | early in the race | late in the race |
|---|---|---|
| snapshot | 152 KiB | 156 to 187 KiB |
| a replayed tic | 1.85 ms | 1.97 to 3.06 ms |
| a 16-tic replay | -- | 31 to 49 ms |
| restore, whole | 6.8 ms | **8.6 ms** |
| of which `P_RelinkPointers` | 3.5 ms | **4.7 ms** |

So a sixteen-tic rollback late in a race is about **54 ms**, or one and nine
tenths of a tic. It does not fit, and the depth cap is what answers that -- phase
6's question, now with numbers that are not opening-lap numbers.

**That is the third of the three things phase 3 was waiting on.** The restore
cost late in a race is measured, and `P_RelinkPointers` is still more than half
of it.

The memory comparison still reports fifteen hundred to two thousand field
differences per check. All of it is interpolation, shadows, and the `rollangle`
the archive no longer carries -- none of it is in the archive, which is why the
archive is clean. That is not a failure; it is the restore declining to rebuild
state the renderer owns.

### Closed while driving too: twelve out of twelve

`1b4c4a5`, binary verified, a person drifting and using items: **twelve replays,
twelve IDENTICAL, no failures.** The same score as bots idle. The played-race
divergence below is closed, and the section after it records how.

The provenance line still reports a disagreement every time, and that is the
point of it rather than a fault:

```
16 tics checked, 0 not recorded; netcmds disagreed with what really ran
  on 240 player-tics, replayed on the record instead
```

240 is 15 of the 16 replayed tics times 16 players; the shallower depths give
3 of 4 and 7 of 8. **Exactly one tic short every time** -- the most recent one,
not yet acknowledged and so not yet cleared. That is a measurement of
`D_Clearticcmd`'s reach: everything older than one tic has lost its flags. It is
also the size of the problem phase 3's *detect* gesture has to solve, so the line
is worth keeping as a number.

### What it looked like before that: driving broke it somewhere bots never touched

Same build, `ce17cc5`, twelve replays -- but with a person at the controls
drifting, using items and hitting trick panels instead of a human sitting still.
**Eight identical, four failures, and not one of them resembles a bot failure.**

| | bots idle | a person driving |
|---|---|---|
| first differing byte | 136 000 to 145 000 | **562 to 671** |
| which block | thinkers | **players** |
| size | 8 000 to 29 000 bytes, a length shift | 2, 61, 134, 317 bytes, no shift |
| named | `MT_ITEM_DEBRIS`, `MT_SHADOW` | **`steering`, `speed`, on player 0** |

```
2 bytes differ in 1 runs, from byte 562 to byte 563 of 183068
  that is player 0 (Comodore), 21 bytes into their record -- steering
317 bytes differ in 143 runs, from byte 647 to byte 183078 of 186846
  that is player 0 (Comodore), 106 bytes into their record -- speed
```

`steering` is `-1` against `0`. `speed` is 15.76 against 15.79. Small, real, and
in the simulation rather than beside it -- the first residue of this whole
project that is neither decoration nor bookkeeping.

**The inputs are not the cause**: zero input differences on all twelve, so `cmd`
and `oldcmd` agree at the end of every replay. And the locator correctly said it
could place none of it in an archived object, because none of it is in one.

**Where it must come from.** `p_user.c` computes a human's steering by solving
towards the angle their ticcmd carries, and that solver reads `cmd.latency`
twice:

```c
if (player->drift && abs(player->drift) < 5 && player->cmd.latency)   // 2394
angle_t leniency = leniency_base * min(player->cmd.latency, 6);       // 2416
```

Both are dead code for a bot, because `G_Ticker` sets a bot's latency to zero.
That is exactly the pair this document predicted a soak could never reach, and
one played race reached them.

**What has not been shown yet** is *why* the two passes disagree, and the honest
answer is that the current instruments cannot say. `K_ReportReplayInputs`
compares the inputs the replay *ends* holding; nothing checks that each replayed
tic was fed the same input the live tic used. By construction it should be --
both read `netcmds[t]` -- unless `netcmds` for the local player is rewritten
after its tic has run, which on a listen server is not obviously impossible.

**And the instrument answered on its first played race: it had never once been
fed the right inputs.** Twelve replays, every player, every tic:

```
16 tics checked against the inputs they really used, 0 not recorded, 240 disagreed
  tic 1049, player 0 fed something else -- flags 1 vs 0
  tic 1049, player 1 fed something else -- flags 128 vs 0
```

240 is sixteen tics times fifteen players. **Only `flags` ever differs**, and the
reason is four lines of the netcode:

```c
static void D_Clearticcmd(tic_t tic)
{
	D_FreeTextcmd(tic);
	for (i = 0; i < MAXPLAYERS; i++)
		netcmds[tic%BACKUPTICS][i].flags = 0;
}
```

called from `TryRunTics` as `for (; tictoclear < firstticstosend; tictoclear++)`
-- "clear only when acknowledged". It zeroes **exactly and only the flags**,
which is exactly and only what disagreed. `netcmds` is not a record of the past;
it is a mailbox, and the netcode empties the acknowledged slots.

**Which is the whole mechanism of the steering divergence.** `p_user.c:2371`
branches on `!(player->cmd.flags & TICCMD_RECEIVED)` -- the "missed a single tic"
path -- and sets `player->steering = targetsteering` directly instead of running
the camera-angle solver below it. A tic replayed out of `netcmds` arrives with
flags 0, so the replay takes the dropped-input branch where the live tic did not.
Different steering, then different speed.

**And why a bot could never show it**: `K_PlayerUsesBotMovement` is tested
*first*, at line 2363, so a bot takes its own branch and never reaches the flag.
Losing `TICCMD_BOT` costs it nothing. The one field the netcode erases is the one
field only a human's code path reads. Five hundred bot checks could not have
found this, and one played race did.

Fixed for `rollback_replay` by replaying on the recorded inputs rather than on
`netcmds`, which is what a verification command should do anyway.

⚠ **This is a constraint on phase 3, not just on a test.** A real rollback that
rewinds past an acknowledged tic cannot recover that tic's flags from `netcmds`
either -- they are gone. The loop has to keep them itself, exactly as the ring
slot now does. Found by an instrument built to check something else, which is the
argument for building the instrument.

### How the bot-side residue was closed: `MT_ITEM_DEBRIS` and an unsynchronised die

The locator answered on its first run. Five of six failures placed the first
differing byte inside a named object, each on a unique 32-byte window:

| object | its masks |
|---|---|
| `MT_ITEM_DEBRIS`, 90 bytes into a 118-byte record | `diff 8800303e diff2 00045008` |
| `MT_ITEM_DEBRIS`, 89 into 117 | `diff 8800303e diff2 00044008` |
| `MT_ITEM_DEBRIS`, 90 into 118 | `diff 8800303e diff2 00045008` |
| `MT_ITEM_DEBRIS`, 94 into 122 | `diff 8800303e diff2 00045028` |
| `MT_SHADOW`, 53 into 75 | `diff 80100006 diff2 00808000` |

Every `diff2` there has `MD2_ROLLANGLE`. And `src/objects/item-debris.c`:

```c
static void rotate3d (mobj_t *debris)
{
	const uint8_t steps = 30;
	debris->rollangle = M_RandomKey(steps) * (ANGLE_MAX / steps);
}
```

`ANGLE_MAX / 30` is `0x08888888`, twelve degrees. Every value this residue has
ever shown is an exact multiple of it: `0x33333330` is six of them, `0xaaaaaaa0`
twenty, `0x77777770` fourteen, `0x2aaaaaa8` five. That is not a resemblance, it
is the arithmetic.

**`M_RandomKey` is the unsynchronised generator** -- `m_random.c` says so in its
own comment, "as with all M_Random functions, not synched in netgames" -- and it
appears nowhere in `p_saveg.cpp`. Its state is never archived, and HUD drawing
consumes it between tics, so it is not even a function of the tic count. A
replayed tic therefore rolls a different number.

**And that is why four bytes go missing.** The archiver writes the field only
`if (mobj->rollangle)`. One draw in thirty is zero. When the replay rolls the
zero and the live pass did not, the record is four bytes shorter, every byte
after it is offset, and the comparison reports nineteen thousand differing bytes
in four thousand runs -- for one cosmetic die roll on a piece of debris that
lives about a second.

**Fixed by leaving it out of a local snapshot**, gated on `localsnapshot` exactly
as `tilt` is. It stays on the wire, where a peer has to be told a value it cannot
compute for itself. The cost is that debris loses its roll across a rollback,
which is a sub-second particle.

⚠ **The tempting fix is the wrong one.** Making `rotate3d` draw from
`P_RandomKey(PR_DECORATION, ...)` -- a *synchronised* class, and one literally
named for decoration -- would make the roll deterministic and archived, with no
visible pop. It would also consume from a synchronised generator that a stock
peer does not consume from, so our RNG stream would walk away from theirs and
every later synchronised draw would differ. That is a desync against stock
builds, which is the exact thing phase 5 exists to protect. **Left alone, and
recorded as an upstream observation instead:** a net-synchronised object takes an
archived field from an unsynchronised die, so two stock clients already archive
different `rollangle` for the same debris.

Fourth member of the family, after the render flags, the per-viewport visibility
bits, and `tilt` with the camera: **a presentation value driven by something the
simulation does not own, reaching archived state.**

⚠ **A measurement hazard found the same way.** `rollback_test` performs a
restore, and a restore does not put interpolation state back -- so dropping a
`rollback_test` into the middle of a scenario changes the race that follows it.
The twelve-replay runs of `e495c33` and `6b2c796` are therefore **not**
comparable run to run (eight identical against seven), because the second
scenario had a `rollback_test` in it. Keep measurement scenarios apart from
scenarios that restore.

Two of those took two turns of reasoning each and were settled by a number in
one: the count of tics actually replayed exposed a loop that looked timed but
had run nothing, and three readings of `leveltime` refuted both competing
explanations at once. `P_NamePlayerField` named `cmd` and then `oldcmd` without
anybody decoding a hex window, which is what that table was written for.

**The packet half, located.** In the client's reception path (d_clisrv.c:5925)
the server's tics are copied into `netcmds[i % BACKUPTICS]`, and the code
already carries the line `if (i >= gametic) // Don't copy old net commands`. So
receiving a tic older than the present is a case the netcode has already thought
about; today it is harmless because a client never runs ahead. That is exactly
where a correction belongs. Three gestures, all inside functions that exist:

1. **Predict** -- relax `while (neededtic > gametic)` in `TryRunTics` so the
   client advances on the last inputs it knows.
2. **Detect** -- in that copy loop, compare what arrives for `i < gametic`
   against what was used, and keep the oldest tic that disagrees.
3. **Correct** -- `K_LoadGameState` that tic and replay to the present, which is
   what `rollback_replay` already does.

**The harness for testing it**, written and not yet run: `netserver.cfg` and
`netclient.cfg`, a scripted server and client on one machine, the client given
its own `-home` so the two do not overwrite each other's log. That was the
phase 4 prerequisite; it turns out phase 3's second half needs it first.

### Phase 3, as the code actually presents it

Read out of the source rather than from netcode in general, so the plan names
real things:

- `netcmds[BACKUPTICS][MAXPLAYERS]` (d_clisrv.c:240) is the input ring, indexed
  by tic modulo its length.
- `G_Ticker` copies `netcmds[buf][i]` into `players[i].cmd` (g_game.c:2015).
  That is the one place a tic learns what anybody pressed.
- A client's inputs arrive into `netcmds[faketic % BACKUPTICS][player]` and are
  marked `TICCMD_RECEIVED` (d_clisrv.c:5632). So the code already distinguishes
  "this input is real" from "this slot is whatever was left in it".
- `TryRunTics` runs `while (neededtic > gametic)` (d_clisrv.c:6748), which is
  where a tic begins and where a rollback has to interrupt.

Which gives four pieces, in order of risk:

1. **Predict.** When a tic is about to run and a player's slot is not
   `TICCMD_RECEIVED`, fill it by repeating that player's last known input and
   record that the tic was predicted for them. Repeat-last is the standard
   prediction and the right first one: it is correct whenever nobody changed
   what they were holding, which is most tics.
2. **Snapshot.** `K_SaveGameState(gametic)` before each tic. Measured at 0.8 ms
   against a 28.6 ms budget, and the ring already holds twenty.
3. **Correct.** When a real input arrives for a tic already run and it differs
   from what was predicted, `K_LoadGameState` that tic, write the truth into
   `netcmds`, and re-run forward to the present. The cost is the restore plus
   one resimulated tic per tic rewound -- 6 ms plus roughly 1 ms each, so the
   depth cap is a budget question, not a correctness one.
4. **Cap.** `K_RollbackMaxDepth` already exists and `rollback_delay` already
   prices a rollback as a percentage of a tic. Past the cap, pay the latency
   with input delay instead of rewinding.

Behind `cv_rollback`, off by default, so a build with it compiled in still
plays exactly as a stock one until somebody turns it on. The soak stays on as
the regression net: a rollback that breaks determinism will show up there
first.

### What needs somebody at the controls

Most of this runs unattended: a scenario ends in `quit`, fifteen bots exercise
items, damage, respawns and the finish line, and the two failures that ended
phase 2 were on bot players. What bots **cannot** stand in for, so far:

- **Anything the simulation only reads for a human.** `cmd.latency` is the clear
  case: `G_Ticker` sets it to zero for any player using bot movement, so the
  drift leniency and the roulette fudge that read it are **only ever nonzero for
  a person driving**. A bots-only soak cannot exercise the path fault 5 above was
  about -- it can only prove the worlds still agree, not that the value reaching
  the simulation is right.
- **Edge-triggered input.** A soak replays frozen inputs, so a button that
  matters on its front -- item use, trick panels, spindash, e-brake, bail,
  respawn -- is never pressed during a check. This is structural, not a gap in
  coverage, and it is the blindness a real rollback loop fixes by replaying real
  input. Until phase 3's packet half exists, only a person pressing buttons
  reaches those.
- **What no log records.** The camera jerk was found by watching, after four
  builds of clean logs. Any change touching the camera, `tilt`, or interpolation
  wants eyes on it before it is believed.
- **Phase 4 onward.** Two instances with latency, a stock peer, and the
  capability test's "does it stutter for me" are all judgements a person makes.

So the division is: **measurements and regression runs are unattended, and a
played race is asked for when a change touches what only a human drives** --
named above, rather than asked for by reflex.

### The two-instance harness, run at last

It had been written and never started. Started, it failed twice, and both
failures are worth more than the run that worked.

**A client cannot join a Match Race that has already begun.** The client dies on

```
I_Error(): assert failed: newplayernum < MAXPLAYERS, d_clisrv.c:4094
```

which is this document's own `maxplayers` trap seen from the other end:
`K_UpdateMatchRaceBots` fills the grid *up to `maxplayers`*, so by the time a
level is running there is no free slot and there never will be. The harness
therefore starts the server, lets the client connect **while the server is still
in the menu**, and only then runs `map`. With `maxplayers 8` and `bots 4` the
client lands in player 8 and the log says so.

⚠ **Upstream, worth reporting with the others.** The code that looks for a bot to
overwrite reads the array before checking the bound:

```c
while (playeringame[nobotoverwrite]
&& players[nobotoverwrite].bot
&& nobotoverwrite < MAXPLAYERS)
```

An out-of-bounds read, and the assert underneath only notices the damage
afterwards.

**Two instances in one folder share one log.** On Windows `i_main.cpp` ends at
`fopen("latest-log.txt", "wt+")` -- a path relative to the process, not to
`-home`, which it ignores. So the second instance overwrites the first, and a
"server" log came back talking about the client's config file. It computes a
`-logdir`/`-logfile` path just above and then does not use it on this platform.

The harness gives the client **its own copy of the executable** in `clienthome`,
with `RINGRACERSWADDIR` pointing back at the game data: nothing is duplicated but
the exe, and each instance gets its own `latest-log.txt`. Fixing `i_main.cpp`
would be one line but would move `latest-log.txt` for every recipe in this
document, so the copy is cheaper.

**And then the rollback was run on the client**, which is the first time any of
this has touched a world it does not own. Everything before it was a listen
server, where the local world *is* the authority.

`rollback_test` on the client: **round-trip IDENTICAL** over 123341 bytes, and
`consistancy before=10477 perturbed=27593 afterload=10477 -- PASS`. The restore
is 7.9 ms there, of which `P_RelinkPointers` is 3.9.

Five `rollback_replay`, at depths 4, 8, 16, 16, 16:

| | |
|---|---|
| IDENTICAL | **3 of 5** |
| input provenance | **0 disagreed**, every time |
| the other 2 | **one byte**, at byte 460 both times, in the `misc` block |
| replayed tic | 1.09 to 1.36 ms (8 racers, against 1.85 to 3.06 at sixteen) |
| the client | left on its own scripted `quit`; no desync, no resynch, no assert |

**Nothing threw the client off the server.** A local restore rewinds `gametic`
and `leveltime` underneath a client whose clock the server owns, and the fear was
that this alone would get it dropped for inconsistency. It does not.

**The residue is one byte and it repeats exactly** -- same offset, same
transition, `0x15` to `0x16`, at two different points of the race. A value
landing on the same pair twice is not a tic counter, and the misc archiver
answers what it is by being read rather than instrumented: byte 459 is the
`0x2e`/`0x2f` paused marker, which was sitting in the hex window all along, and
the field after it is `livestudioaudience_timer`. 21 against 22, off by one, in
the direction of the replay having decremented it one time fewer.

`TryRunTics` decrements it, in the tic loop, in the same block as
`Schedule_Run`:

```c
if (Playing() && netgame && (gametic % TICRATE == 0))
{
	Schedule_Run();
	if (cv_livestudioaudience.value)
		LiveStudioAudience();
}
```

A replay drives `P_Ticker` by hand and never reaches that block. Sixteen tics
cross a multiple of thirty-five at most once, so the miss is **always exactly
one and only sometimes** -- which is the entire shape of what was measured. And
it needs `netgame`, so a listen server never showed it: this could only appear
on the client.

**Third of a kind.** `gametic` was the first thing `TryRunTics` does per tic that
a hand-rolled replay missed, the input step was the second, and this is the
third. The lesson has to be stated more strongly than it was: *replaying a tic by
hand means reproducing everything the tic loop does around `P_Ticker`, and that
list is not short.*

It is a laugh track, and reproducing it would mean calling a netcode block that
also runs scheduled commands -- real side effects that must not happen twice. So
a local snapshot leaves it out, gated like `tilt` and the debris roll. Fifth
member of the presentation family.

### The breadth soaks, run at last -- and two of the three do not count yet

`soak_battle.cfg`, `soak_gp.cfg` and `soak_encore.cfg` had been written and never
started. Started:

| | checks | failures | context |
|---|---|---|---|
| Battle | 390 | **1** | Battle mode, but on **RR_TESTRUN** |
| Grand Prix | 520 | 0 | **not verified** |
| Encore | 530 | 0 | **not verified** |

**Battle switched mode but not map.** `map -gametype Battle -random` moved the
gametype -- the log says `WARNING: No Deathmatch starts in this map!`, which is a
Battle-rules complaint -- and then landed on `RR_TESTRUN`. Battle *rules* were
soaked; a Battle *arena* was not. `forcebots 1` worked: sixteen racers, fifteen
bots, in a gametype whose rules carry no `GTR_BOTS`.

Its one failure is named: `MT_DASHRING`, 71 bytes into an 81-byte record,
`0x1d` to `0x17`, same diff masks both sides. First `MT_DASHRING` this project
has seen, and the first failure found on a map other than Northern District.

**Grand Prix and Encore cannot be counted, and that is an instrument fault.**
`K_PrintGrid` prints the map, grid and mode, but only from a *verbose* check --
so a run that never fails never says what it ran on. Five hundred and twenty
clean checks against an unnamed map and an unnamed mode is a number nobody can
use, and this document says as much two sections above about somebody else's
numbers. `rollback_soak` announces its context at the start now. Both want
re-running before they are believed.

⚠ **Worth keeping separately**: a scenario that switches mode with `-random` may
not end up on the map its filename promises. The context line is the only thing
that would ever say so.

### Before phase 4 -- two instances with latency

- Phase 3 working behind its switch, with the soak still passing.
- An artificial latency knob. The game has none: a dozen lines, off by default,
  or an external shaper.
- A scripted server and client on one machine (`-server` and `connect` exist).

### Before phase 5 -- the wire-format audit, done

Every change to `p_saveg` since the upstream base (`05cca02c9`), sorted.

**Local only.** Gated on `localsnapshot` / `localrestore`, so a netgame stream
never sees them:

| what | where |
|---|---|
| cmd, oldcmd, SPBdistance, itemscale, enteredGame, faultflash | the extras block a local snapshot adds |
| `rollangle` **skipped** when local (still written for the wire) | beside the mirrored flag |
| `tilt` **skipped** when local (still written for the wire) | beside viewrollangle |
| per-viewport render flags kept instead of stripped | the mobj archiver |
| chain order: mobjnum plus blockmap and sector positions | the mobj archiver |
| precipitation not torn down and rebuilt | the thinker loader |
| weather not re-switched | the weather step |
| the cameras | not in the archive at all -- they live beside it, in the ring slot |

**Shared with the wire, and none of it changes the grammar.** The framing --
field order, sizes, diff masks -- is untouched, so a stock peer parses our
stream and we parse theirs. What differs is the meaning of three values, and in
all three cases ours is the correct one:

- `MobjIsArchived` sets fewer diff bits: an object-to-object pointer is written
  only when its target is going into the save. A stock reader is mask-driven and
  reads what it is told; the links it no longer receives are the ones that used
  to be attached to whatever object had inherited that `mobjnum`.
- `followerskin` went from `WRITEUINT8` to `WRITESINT8`. **The same byte, in the
  same place.** Only the sign is read differently -- a stock peer sees 255 where
  we see -1, which is the upstream bug.
- The `onconveyor` read order. The **writer is untouched**, so the bytes are
  identical and the reader consumes the same six either way; ours now assigns
  them the way they were written. A stock reader keeps scrambling them.
- The item roulette list no longer shrinks on load, so the `cap` we write next
  describes the block we hold. A stock reader allocates what it is told.

So phase 5 is plannable, and its risk is not framing but semantics: we and a
stock peer would disagree about three values, always with us correct. The
honest move is to offer all three upstream -- the `onconveyor` order in
particular, since it scrambles `timeshit`, `timeshitprev` and `onconveyor` for
any player who joins a netgame in progress.
