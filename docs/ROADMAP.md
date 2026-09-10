# Phases to a playable alpha

Written 2026-09-10, after the two-clock pivot and after the desync was narrowed to
one statement. `ROLLBACK.md` is the journal, `AUDIT_20260909.md` is the comparison
with SRB2 NetPlus and Odamex. This file says what is left, in what order, and what
each phase has to prove before the next one is worth starting.

The old phase numbering (1 to 7) is retired: it was written for an architecture
where the authoritative clock ran ahead, and that is gone. These phases are the
post-pivot ones.

## Where this starts from

**Solved and holding:** input lag. "Ça répond tout de suite", reported by a person
on five races and again after the architecture changed underneath it.

**The architecture is settled.** `gametic` runs only confirmed tics; the
speculation lives above it on a snapshot and is rebuilt every pass. Costs
**4.94 ms of a 28.6 ms tic** at two players, **8.33 ms** at eight.

**The bench is unattended.** Six bots move the world without a driver, so
measurements can be repeated instead of being n=1. This is what makes the phases
below plannable at all.

**Open:** the desync, narrowed to a single statement -- *a speculated tic modifies
state the archive does not carry, and that state reaches a bot's simulation.*

---

> **Revised 2026-09-10 by `WORLDWIDE.md`.** The project is now Ring Racers
> Worldwide -- client-side prediction with server reconciliation, named for what
> it is. The phases below stand; `WORLDWIDE.md` sections 6 and 7 change Phase A's
> next step, give Phase B a second reason, add an item-prediction policy to
> Phase C, and add a client-local delay knob. Read that file first.

> **Revised again 2026-09-10, evening.** The light correction channel is built
> and measured (`WORLDWIDE.md` sections 8.4 to 8.6): a race ran with **zero
> full-state resends** and a residual position error of **0.25 units**. That
> changes the order below. **Phase A no longer blocks the alpha** -- the channel
> absorbs the divergence -- so it stops being the gate and becomes an
> optimisation: every unit of residual drift it removes lets the correction rate
> come down. What blocks the alpha now is Phase B, the cost at a full grid, which
> the channel does not help with at all.
>
> **The next four things, in order:**
>
> 1. **Repeat `playtest.sh correct` driven.** One command, one race. The run that
>    produced nine resyncs and 330-unit gaps had a person in it; the clean one did
>    not. Until that is repeated driven, nothing above is proven.
> 2. **Four more unattended repeats.** Every number in this project has been
>    n=1 once, and one of those was read as a fix when it was an artefact.
> 3. **Find the correction rate that is actually needed.** `rollback_correct 8`,
>    `16`, `35`. At 0.25 units of residual per four tics, one every four tics is
>    probably far more than necessary, and each halving is free bandwidth.
> 4. **Then Phase B**, which is now the gate: 9.5 ms a pass at nine karts is
>    already 33% of a tic, and a grid is sixteen.

## Phase A -- Close the desync

**Blocks everything.** A full state resend every fifteen seconds makes every other
measurement unreadable, and no amount of tuning shows through it.

What is already excluded, each measured under the same conditions: the restore
(clean over 1400 round trips, with and without bots), the inputs (the server
transmits bot ticcmds and the client copies them unconditionally), the
synchronised RNG (`rngsum` identical on the failing tic), items (`itemtype`
identical), and netxcmds (never once raised inside a speculation).

**The work.** The memory comparison -- `K_NameMobjField` over 125 fields plus
`P_NamePlayerField` -- walks *structures* rather than archives, which is the blind
spot every other oracle here shares, and it is what found the interpolation family.
Point it at the world just after `K_RollbackUnspeculate` against a copy taken just
before `K_RollbackSpeculate`. Anything it names that is not presentation is the bug.

⚠ Read its output knowing it has reported fifteen hundred to two thousand differing
fields per check since the beginning, all filed as "presentation" **on the strength
of their names**, never once checked against what the simulation actually reads.

**Done when:** zero resyncs over **five** unattended bot races and **two** driven
ones. Five because every number in this project so far has been n=1, and one of
those n=1 readings was an undriven race that looked exactly like a fix.

---

## Phase B -- Make it fit at a real grid

**Needs A.** Every cost figure so far is two or eight karts on one map, early in a
race. A Ring Racers grid is sixteen, and a snapshot grows from 120 KiB at the start
to 318 KiB three minutes in.

Measured so far: 4.94 ms a pass at two karts, 8.33 at eight. Straight-lining that
to sixteen puts a pass near half a tic, and the restore was 8.6 ms alone at sixteen
karts late in a race. **Assume it does not fit and measure rather than assuming it
does.**

**Two known levers, in this order:**

1. **Predict less.** Odamex restores one player and the moving sectors, not two
   thousand objects; Rocket League says the same thing differently. This is the
   larger win and it attacks the cost at its root rather than spreading it.
2. **Amortise.** NetPlus re-simulates only every N live tics (`cv_siminaccuracy`),
   which trades freshness for a smoother CPU profile.

**Done when:** a pass fits inside a stated budget -- **30% of a tic** is a
defensible line -- at sixteen karts, late in a race, with the depth that Phase D
turns out to need.

---

## Phase C -- Breadth under prediction

**Needs B**, because testing feel and correctness through a stutter tells you about
the stutter.

Nothing in this list has ever run under prediction:

- a full race start to finish -- grid, finish line, results screen
- **Battle**, **Encore**, **Grand Prix** (its grid is hardcoded to eight and it
  runs bots differently)
- items and respawns used deliberately, since the soak replays frozen inputs and is
  structurally blind to anything edge-triggered
- three maps chosen for geometry the current one lacks: steep slopes, water, a big
  drop

**Done when:** each of those runs with no resyncs and no divergence that has not
been read and understood. The soak becomes the regression net behind it rather than
the front-line instrument.

---

## Phase D -- Feel

**Needs C**, and it is the phase no log can finish.

- **Correction smoothing**: written, currently "maybe ça marche". Measure it against
  a control in the same session, or drop it.
- **Remote karts**: the speculation re-predicts them every pass. Whether that reads
  as smooth or as jitter is a thing only eyes report. If it jitters, Odamex's
  answer is position history and interpolation (`cv_netsteadyplayers`, `histx/y/z`)
  and we have nothing equivalent.
- **The depth**: what lead actually feels best, which then feeds back into B.

**Done when:** a person prefers it to the control, on three races, at a latency
that is stated rather than assumed.

---

## Phase E -- Capability check

**Needs B and D.** A short calibration run in the menus that measures restore and
replay cost on the player's own machine and settings, and answers the only question
a player has: can this computer run it without stuttering. It should propose a
depth, not print microseconds.

**Done when:** "it stutters for me" arrives as a number and a recommended setting
instead of a sentence.

---

## Phase F -- Alpha

**Needs all of the above.** What is missing is not code:

- a build people can download (CI already produces one per commit)
- a short list of what to report, in the language a player uses
- a way to collect logs without asking for file paths
- a statement of what is known broken, so reports are about the rest

**Done when:** somebody who is not Gibax has played it and reported something
useful.

---

## Not on the path

**The wire change.** Breaking compatibility was authorised, and it was going to buy
labelled inputs plus a server-side input buffer -- the half NetPlus marks "not yet
implemented". **The pivot already removed the problem it was for.** Self-
misprediction, the depth setpoint, the offset histogram, detect and correct, the
rate limiter that never deferred once: all of that existed because a prediction was
carried forward and had to be right, and a speculation rebuilt every pass carries
nothing.

Keep the card for one thing only: if Phase D shows remote karts need real state
rather than prediction, that is a wire change and it is worth spending on. Decide
it with eyes on a screen, not before.

**State streaming à la Odamex.** A different netcode, not a bigger version of this
one, and it gives up talking to stock servers. Only worth revisiting if D fails in
a way B cannot pay for.

## Risks, named

- **Phase A may not be one field.** The statement it rests on is sound, but "state
  the archive does not carry" is a family, not a name.
- **Phase B may not have an answer at sixteen karts** on ordinary hardware. That is
  what Phase E exists to say out loud rather than hide.
- **Every measurement before today was n=1.** The unattended bench fixes that going
  forward; it does not retroactively fix the numbers already in the journal.
- **A person is still required for D**, and nothing about the bench changes that.
