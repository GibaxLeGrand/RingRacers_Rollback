# What it takes to get to a playable alpha

Written 2026-09-10, after the two-clock pivot. Everything here is measured or read
from source; guesses are marked. `ROLLBACK.md` is the journal and
`AUDIT_20260909.md` is the comparison with SRB2 NetPlus and Odamex. This file is
the only one that answers "what is left, in what order, and why".

## Where the project actually is

**The hard half is done.** Input lag is gone -- "ça répond tout de suite", reported
by a person on five separate races and again after the architecture changed under
it. That took finding `cv_netticbuffer`, and nothing since has cost it.

**The world still desynchronises**, seven to nine full state resends in a
two-minute race, and that is the single thing between here and an alpha. It is not
a feel problem; each one is a savegame downloaded and loaded mid-race.

**The architecture is now the right one.** `gametic` runs only confirmed tics, the
speculation lives above it on a snapshot and is rebuilt every pass. That is SRB2
NetPlus's shape, arrived at after measuring the alternative rather than by
copying. It costs **4.94 ms of a 28.6 ms tic** at two players, bounded and constant.

## The one blocker

**Nothing else matters until the desync is named.**

Three theories have been written, built and refuted, at one CI build and one
played race each. What survives is four measurements, all taken with a person
driving:

| | |
|---|---|
| restore alone, 1400 round trips | **0 resyncs** -- the snapshot machinery is exact |
| speculation on, nobody driving | **0 resyncs** |
| speculation on, driving | **7 to 9 resyncs** |
| netxcmds raised inside a speculation | **0, ever** |

So a speculated tic changes something that reaches the server, and only when
somebody is at the controls.

`rollback_blame` (in `e28b1f1`) prints what the checksum was looking at, tic by
tic, on both ends. `Consistancy()` hashes each player's `x`, `y` and `itemtype`
and the synchronised RNG seeds, and `MOBJCONSISTANCY` is not defined here -- so
the answer is a position, an item, or a seed. **Run it on both windows and compare
by tic number.** Three different bugs, one line of text apart.

⚠ Every number above is **n=1**. None has been repeated. The 0-against-9 contrast
is wide enough to act on and narrow enough to deserve a second sample.

## What a playable alpha requires

| | state |
|---|---|
| **No desync in a normal race** | **blocked** -- the above |
| **Cost that fits at a full grid** | **unmeasured.** Every figure is two players. At sixteen karts a snapshot is 150-190 KiB and a restore was 8.6 ms late in a race, so the pivot could be near 19 ms of 28.6. Measure before optimising. |
| Items, respawns, hitlag, finish line, results | **never exercised under prediction.** The soak replays frozen inputs and is structurally blind to anything edge-triggered. |
| Grand Prix, Battle, Encore, a full race start to finish | **not started.** Phase 2 breadth. |
| Chat, cvars, map votes during play | **probably fixed for free by the pivot** -- the authoritative loop now runs every tic normally, `ExtraDataTicker` included, so nothing lands on a tic that gets replaced. **Verify rather than assume.** |
| Correction smoothing | written, **not measured**. "Maybe ça marche." Either measure it or drop it. |
| Remote karts staying smooth | **unknown.** The speculation re-predicts other karts every pass; whether that reads as smooth or as jitter is a thing only eyes report. Odamex answers it with position history (`cv_netsteadyplayers`); we have nothing. |
| A capability check in the menus | not started. The numbers exist; what is missing is a menu entry and a verdict in plain language. |
| Alpha logistics | not started. A CI build exists per commit; what is missing is a short list of what to report and a way to collect logs. |

## Order, and what blocks what

1. **Name the desync** with `rollback_blame`, fix it, and confirm with a repeated
   measurement rather than one race. Everything below is unreadable until the
   world stops being resent.
2. **Re-measure cost at sixteen karts, late in a race.** If it does not fit, two
   known answers, in this order: **predict less** (Odamex restores one player and
   the moving sectors, not two thousand objects -- this is also the larger win)
   and **amortise** (NetPlus re-simulates only every N live tics).
3. **Breadth**: a full race start to finish, then Battle, Encore, Grand Prix, with
   items and respawns used deliberately. This is where a played race finds what no
   soak can.
4. **Feel**: measure or drop the smoothing; decide whether remote karts need
   history-based smoothing.
5. **Capability check**, so "it stutters for me" arrives as a number.
6. **Alpha**: a report template and a log collection path.

## What the pivot made unnecessary

Worth writing down, because it is a large amount of work that no longer needs
finishing and it should not be resurrected by accident:

- **Self-misprediction, and the whole depth setpoint.** The offset histogram, the
  line through four depths, the `mindelay` floor -- all of that existed because a
  prediction was carried forward and had to be right. The speculation is now
  rebuilt from scratch every pass on the newest inputs, so there is nothing to
  carry and nothing to be wrong about.
- **Detect and correct.** No pending rollback, no `ROLLBACK_RECONCILE_EVERY` rate
  limiter -- which never deferred once in any race ever run.
- **The netxcmd hand-back path.** Predicted tics no longer replace real ones.

Those paths still exist behind `rollback_loop`, which `rollback_twoclock` disables
when it enables itself. They are kept for comparison, not for use.

## The compatibility budget: do not spend it yet

Breaking compatibility with stock servers was authorised, and the obvious thing to
buy with it is labelled inputs plus a server-side input buffer -- the half SRB2
NetPlus marks "not yet implemented" and the half the Rocket League slides say
matters.

**It is not needed any more.** That purchase existed to kill self-misprediction,
and self-misprediction is what the pivot deleted. Spending it now would be paying
for a fix to a problem that no longer exists, and giving up the ability to talk to
an unmodified server on the way.

Keep it for the one thing that might still want it: if remote karts turn out to
need real state rather than prediction -- the Odamex answer -- that is a wire
change, and it is worth the card. Decide that after step 4, with eyes on a screen,
not before.

## Risks, named

- **Every desync number is one race.** The whole diagnosis rests on unrepeated
  measurements.
- **All cost figures are two players on one map.** The grid that matters is
  sixteen.
- **The soak cannot see this class of bug.** Its oracle compares archives, so
  anything the simulation reads that the archive does not carry cannot fail it --
  which is exactly where the last three theories went looking.
- **A person is required.** The desync does not appear without somebody driving,
  and an undriven race produced a clean zero that would have been read as a fix if
  the driver had not said so.
