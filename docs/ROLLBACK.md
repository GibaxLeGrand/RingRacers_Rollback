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
