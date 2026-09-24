# Rollback console commands — reference manual

Stable list of the console commands added by this branch (`k_rollback.c`,
unless noted otherwise). All are registered as **debug commands**
(`COM_AddDebugCommand`), so they show up in the game's pause menu without
having to type them. Unlike `ROLLBACK.md` (closed journal) and `WORLDWIDE.md`
(current state + measurement journal), this file tells no story: it describes
what each command does, today, and is kept up to date as things change. Entry
point for the whole doc set: `docs/README.md` in the private notes repository.

This file, `WORLDWIDE.md` and `ROADMAP.md` are kept **identical** in the
public code repository and in the private notes repository (`docs/` on both
sides).

**Up to date as of 2026-09-23** — 23 commands, checked against
`K_RegisterRollbackStuff` in `k_rollback.c`. Two are **obsolete**
(`rollback_loop`, `rollback_pace`) and are kept only for comparison.

⚠ Reminder: **none of these commands is ever launched in a race without the
project owner's explicit go-ahead**, every time (rule 1 of the docs entry
point).

Two distinct families:

- **Diagnostic** — measure or verify, changing nothing a player sees in a
  normal race.
- **Live netcode** — turn on or tune an actual network behaviour (prediction,
  correction, delay).

---

## Diagnostic — offline or solo checks

### `rollback_test`
Takes a snapshot of the current state, perturbs it (RNG), restores it, takes a
second snapshot, and compares the two **byte for byte**.

- **Proves**: that everything the archiver writes survives a round trip intact
  — every field of every archived mobj/thinker/sector/player.
- **Does not prove**: that a field the archive skips is missing when it
  matters (it would be absent from both snapshots, so wrongly "equal"). For
  that, see `rollback_resim`.
- A difference in the Lua archive is **normal** (`lua_next`'s order depends on
  a table's internal layout, not a bug).
- Also prints the snapshot's size and the cost (µs) of each step.

### `rollback_resim [tics]`
4 tics by default. Saves the state, replays it twice on **the same frozen
inputs** (one "real" pass, one restored pass), then compares.

- Answers the question `rollback_test` cannot ask: does a restored world
  behave like the live world on the same inputs?
- Blind to anything **event-triggered** (a button pressed once during the
  frozen window was never "pressed" in the second pass).

### `rollback_leak [tics]`
4 tics by default. Three snapshots: a reference (B), a pass on the **real**
inputs (A1), a pass on **perturbed** inputs (A2, a neutral input rather than an
extreme one — that is what a client genuinely predicts for someone doing
nothing).

- `A1 != B` → any extra pass pollutes the state, whatever it simulated.
- `A1 == B` and `A2 != B` → it is a **wrong** pass that leaves a trace: exactly
  the network prediction case, reproduced on one machine, with no network.
- On a grid that is entirely at rest (no non-neutral input), the check
  **refuses** rather than passing: an `A2` that does not differ from `B` would
  prove nothing, and a test that cannot fail is worse than no test.
- Exists because `rollback_resim`/the usual soak can stay clean 330 times in a
  row while a real race still drifts — they only test what is in the archive
  (players + mobjs), not file statics or other hidden state.

### `rollback_soak [interval] [tics] [leak]`
Runs `rollback_resim` (or `rollback_leak` if the 3rd argument is nonzero) in
the background, once every `interval` tics, silently unless a check fails.

- No argument: prints the current state (on/off, number of checks and
  failures).
- `interval` at `0`: stops the soak and gives the final tally.
- Expensive (two resimulations + two restores per check): keep it for a
  dedicated server with nobody on it, not a played race.
- Prints the context (map, mode) once at the start — otherwise a run that never
  finds anything gives an unusable number ("500 checks, on which map?").

### `rollback_replay [tics]`
4 tics by default. Rewinds this many tics and **replays the inputs that
actually happened** (read from `netcmds`, where the netcode keeps 512 tics of
history) — unlike the soak, which replays frozen inputs.

- This is literally the operation a rollback performs, triggered on demand
  rather than in response to a network packet.
- Needs `rollback_keep 1` beforehand so the targeted tic is still in the ring.

### `rollback_keep <0|1>`
Keeps a snapshot of **every** tic (instead of none), so `rollback_replay` can
restart from any of the last `ROLLBACK_TICS - 1`. Costs one save per tic; off
by default.

### `rollback_blame [0|1]`
Records, every tic, what the checksum (`Consistancy()`) was actually looking
at: position + object type of every player, plus the sum of the synchronised
RNG seeds — in one text line kept for the last 512 tics.

- **Run on both machines** (client and server). When the server refuses a tic,
  it prints its own line; the client prints its around the same moment. The
  two are compared tic by tic.
- **Also prints `rollback_blame: SAMPLE tic N: ...` once a second** (every
  confirmed tic that is a multiple of 35, in a level), on both machines,
  refused or not (`WORLDWIDE.md` 8.43). While `rollback_correct` is on at
  the server, these are the only lines it prints: the ones above come only on
  the way to a full-state resend, which the correction channel suppresses
  (8.42). `cleancmds_report.py` compares the two logs' samples per window, and
  counts the server's "consistency mismatch ... resend suppressed" notices.
- Used to decide between three hypotheses in a single line of text: position
  diverges, an item diverges, or the RNG seed diverges.

### `rollback_damagelog [0|1]`
**Run on both machines.** Prints one line per *resolved* damage event (not
attempted — only when `P_DamageMobj` returns true) on a confirmed tic, with a
running hash.

- The two logs are diffed tic by tic; the first line where the hashes diverge
  is the first hit the two machines judged differently.
- Does not count refused attempts (a kart invincible here, hit there): the two
  machines constantly refuse different things without that being a bug, so
  counting attempts would give a misleading nonzero number.

### `rollback_detect`
Report only, no setting. Says what the network has told this machine about
tics **already run**: how many inputs arrived late for a tic already executed,
how many contradicted what was used, how many arrived too late for the ring —
and, if any, the oldest tic still waiting for a replay.

⚠ Under `rollback_twoclock`, the confirmed clock never runs ahead: this
counter can then only see a **late resend** of an already-run tic, and reads 0
by construction on a clean link (`WORLDWIDE.md` 8.17). A 0 proves nothing in
that mode.

### `rollback_inputlog [0|1]`
**Run on both machines.** Counts the ticcmds actually consumed by a confirmed
tic and keeps a running hash of them; the bare command prints the state, the
count and the hash. The two logs are compared tic by tic: the first tic where
the counts agree but the **hashes** diverge is a tic where the two machines
ran different inputs without noticing.

⚠ The hash folds the **tic number** into every round, so an identical input
filed under two different numbers reads as differing content. That is
deliberate (it is what makes a relabelling visible), but it means a hash
mismatch alone does not prove the input bytes differ — see `rollback_relabel`.

### `rollback_relabel`
**Server only, and it needs a real remote client** (a local loopback
relabels nothing interesting). Report only. Histogram of
`faketic - realstart`: by how many tics the server **moves the label** of an
arriving ticcmd, relative to the number the client tagged it with. This is
vanilla mechanics (`PT_CLIENTCMD`,
`faketic = maketic + max(0, wantdelay - timegap)`), not something this branch
added.

How to read it — the algebra reduces to two cases, and the histogram is often
bimodal because **several senders** are mixed into it:

- packet arrived **within its budget** (`timegap < wantdelay`) → the offset is
  exactly `wantdelay`, a **narrow spike**;
- packet arrived **late** → the offset is `timegap`, the raw transit time, so
  a **spread** as wide as the jitter.

⚠ A listen server sends itself its own packets too (`CL_SendClientCmd()` is
also called under `if (server)`), so the total histogram counts **the host and
the clients together**. Since 2026-09-21, the report adds four lines that
split the packets: **host or remote client**, **during a race or outside
one**. Hypothesis to test (`WORLDWIDE.md` 8.32): the `+2` cluster comes from
the host **outside a race**, where the delay exemption does not apply.

### `rollback_lagcheck` — not a command
Looked for as a command, it is not one: it is an **automatic print**, edge
triggered, inside `UpdatePingTable` (`d_clisrv.c`). It emits a line
**whenever `target_lag` changes**, prefixed `[client]` or `[server]` depending
on the branch, with the terms that decide the exemption. Nothing to turn on:
the line shows up in the `latest-log.txt` of the machine in question.

---

## Live netcode — change actual network behaviour

### `rollback_loop [tics]` — ❌ OBSOLETE
Replaced by `rollback_twoclock` on 2026-09-10; kept for comparison only. It
advances the confirmed clock on guesses, which fights the game's own
consistency check (`AUDIT_20260909.md`).

**The old prediction loop** (before the two-clock pivot). Off by default: a
build that carries it plays exactly like a stock build until somebody asks for
it. The given value is the number of tics the client may run ahead of the
server (capped by `K_RollbackPredictAhead()`).

- Automatically turns on `rollback_keep` (running ahead with no way back would
  be worse than not predicting at all).
- Mutually exclusive with `rollback_twoclock` **by construction**: one
  advances the confirmed clock (`gametic`), the other refuses to.
- No argument: prints a full telemetry report (predicted tics, min/max/mean
  lead, corrections received, replays, tics handed back to the real loop
  because a message landed on them, etc.) — useful to see *whether* the
  prediction ever got a chance to fire.
- Removes the fixed input delay (see the box under `rollback_twoclock`) while
  it is on — via `K_RollbackPays()`, commit `2026-09-14`.

### `rollback_pace [0|1]` — ❌ OBSOLETE
Only useful with `rollback_loop`, itself obsolete.

Caps the loop (`rollback_loop`) to **one predicted tic per pass**, instead of
predicting as many as the depth allows. Off by default, and deliberately kept
separate from `rollback_loop`: `rollback_loop`'s counters are read once with
pacing off and once with it on, in the same race — otherwise one is comparing
two different evenings.

### `rollback_twoclock [tics]`
**The pivot** — replaces `rollback_loop`. Runs a speculation of `tics` tics
**on top of** the confirmed world, without advancing the authoritative clock
itself.

- Turning `rollback_twoclock` on sets `rollback_loop` to 0 automatically (and
  turns on `rollback_keep`); the two can never run together.
- Turning it off (value ≤ 0) cleanly restores the confirmed world if a
  speculation was in progress — otherwise it would stay for good.
- No argument: report (speculation passes built, tics they ran, time spent
  undoing/redoing the speculation per pass against a whole tic's budget,
  network messages refused because they were raised inside a speculation — a
  netxcmd sent during a speculation cannot be taken back).
- Removes the fixed input delay while it is on (see the box below).

> #### ⚠️ 2026-09-14 fix: `rollback_twoclock` finally removes the fixed delay
>
> The server-side "gentleman's delay" (`UpdatePingTable`, `d_clisrv.c`) and the
> client-side profile `mindelay` were only disabled if
> `K_RollbackPredictAhead() > 0` — that is, only for the old `rollback_loop`.
> But `rollback_twoclock` sets `g_loopahead` to 0 when it turns on (the two are
> mutually exclusive "by construction"): so until this fix existed, **turning
> the pivot on did not remove the input delay** — only the old, deprecated loop
> did. Every measurement in the journal (`ROLLBACK.md`) taken under two-clock
> was therefore taken with that fixed delay still billed on top of the
> speculation.
>
> Fixed with a new function `K_RollbackPays()` (`k_rollback.c`/`.h`) that
> answers true if **`rollback_loop` OR `rollback_twoclock`** is active, and
> replaces `K_RollbackPredictAhead() > 0` at the two places in `d_clisrv.c`
> where the delay was computed (both the server branch *and* the client branch
> of `UpdatePingTable`).
>
> **⚠ Completed 2026-09-20: on the host side, this fix did nothing.** On a
> listen server, `K_RollbackTwoClock()` returns 0 (`client` is `!server`), and
> the host never turns on `rollback_twoclock` anyway, since it is a client-side
> setting. The host kept charging itself 6 to 7 tics, i.e. 170-200 ms, on its
> own input. `K_RollbackPays()` now also asks `K_RollbackCorrectingHere()`
> (`rollback_correct` active on this machine): that is a **proxy**, until the
> server advertises its WORLDWIDE mode (`ROADMAP.md`, *Compatibility* section).
> Measured: the host's `target_lag` stays at 0 for the whole race
> (`WORLDWIDE.md` 8.24-8.27).
>
> **Concrete consequence**: the player profile's "Minimum Input Delay" setting
> (`cv_mindelay`, accessibility menu — until now described as "Practice for
> online play!", i.e. meant to be calibrated offline since online the network
> delay applied on top anyway) **genuinely goes away online as soon as
> `rollback_twoclock` runs**: `target_lag` drops to `0` on the client side (and
> on the host side since the 2026-09-20 addition) instead of staying pinned at
> the `cv_mindelay.value` floor. This is **not** a new setting appearing
> online — it is the removal of a double-count: before this fix, the fixed
> delay stayed billed on top of the speculation, cancelling out part of the
> benefit the pivot is supposed to bring.
>
> **Left out of scope for this fix**, noted in `ROADMAP.md` (section
> *Client-local input delay knob*): a genuine GGPO-style *local* delay knob
> (the player chooses to keep some buffer even with prediction active, without
> it becoming a `wantdelay` sent to the server again — which is exactly the bug
> this fix closes). If the idea is to reuse the existing `cv_mindelay` slider
> for that, it is new work, not an automatic consequence of what is fixed
> here.

### `rollback_cleancmds [0|1]`
**Client side, two-clock mode. On by default** since 2026-09-21 (`WORLDWIDE.md`
8.35, 8.36). When it is on, the speculation no longer touches tics the server
has **already sent**.

Without it, the `netticbuffer` reserve stops the confirmed loop one tic short
of `neededtic`, and the speculation writes your input *of the moment* over the
one the server had assigned to that tic. It also recomputes **every bot's**
input on that tic from this machine's world, because a bot's ticcmd never
carries `TICCMD_RECEIVED`. The tic is then played as confirmed with the wrong
inputs (`WORLDWIDE.md` 8.31, 8.33) -- confirmed at race scale: 64-85% of the
local kart's confirmed tics without the switch, 0-0.1% with it (8.35, 8.37).
Whether it is the drift's source is **not settled**: the two races read
opposite ways, and their off/on/off protocol cannot tell, because an on window
inherits what the off window before it put out of step (8.38). The race that
can is one with the switch on throughout. Costs nothing felt -- what renders is
the speculation above `neededtic`, which the switch never touches, so a driver
reported no difference between it off and on (8.36).

- No argument: the state, and two counters that run **even when off** — how
  many local inputs were written over an already-received tic, and how many
  differed from the server's. The second one should read 0 when nobody is
  driving.
- ⚠ The counters cover **your own inputs only**. The switch also stops the
  bots' recomputation, but nothing here counts it: to see it, compare the
  `rollback_inputlog` lines of both machines tic by tic (the private notes'
  harness has a script for this).
- Changing the value resets the counters, so the same race can be read off
  then on.

### `rollback_history [maxdepth]`
**Client side, two-clock mode. Off by default** (`0`). With a depth above 0,
the speculation replays **your own inputs still in flight** -- sent, but not
yet applied by the server -- one per tic, in the order you made them, instead
of repeating your newest input over every speculated tic (`WORLDWIDE.md` 8.39).
It finds which input the server applied on the newest tic it has sent by the
leveltime stamp every ticcmd carries, and replays everything you sent after
it. The speculation then reaches the tic your newest input will land on --
never below `rollback_twoclock`, never above `maxdepth` (capped at 34).

That tic moves with the network's jitter, so the command does not chase it:
it holds the drawn tic's **lead over the clock** at the largest one asked for
in the last second, raises it at once, and lowers it by one tic a second at
most. The picture then advances one tic per tic instead of jumping with every
jitter (`WORLDWIDE.md` 8.40, 8.41).

- Meant to change only what is **drawn**, never the confirmed world. ⚠ In its
  first run the confirmed world parted inside the window where it was on,
  not yet explained (`WORLDWIDE.md` 8.46): **leave it off** outside a test.
- About doubles the speculation's cost at 171 ms: measured 1.6 to 1.9 times,
  at 8 tics deep on average (8.46).
- Needs `rollback_cleancmds` on (the default); with it off, it does nothing
  and says so.
- No argument: the state; how often the drawn world moved against the
  clock (on or off -- an off window is the control); the share of passes that
  found the applied input; the inputs in flight on average (the round trip,
  in tics); the average depth, with how often the cap cut it short; and how
  many times the lead was raised and lowered.
- Setting it resets those counts, so the same race can be read off then on.
- Suggested value: `12`.

### `rollback_nullspec [0|1]`
Saves and restores the frontier on **every pass** without speculating
anything. Isolates a single question: does the plain round trip through the
archive, on its own, inside the real game loop, suffice to make the server
react — with none of the speculation's own noise.

### `rollback_lag [tics]`
**Testing only.** Delays every packet received from a peer by this many tics.
A local loopback has no latency at all, so without this command a client never
runs short of confirmed tics and never has anything to predict — which is
what let the rollback loop stay on without ever triggering.

- Warns if packets were lost (queue full): an artificial delay then becomes
  artificial packet loss, and the two would look alike in the results without
  this warning.

### `rollback_maxdepth [tics]`
How far back a rollback is allowed to rewind. Beyond this depth, the latency
has to be paid with classic input delay instead of a replay. Capped at the
ring's size (`ROLLBACK_TICS`).

### `rollback_smooth [0|1]`
Off by default. When on, a correction **glides visually** from the kart's old
position to the new one instead of snapping instantly. Deliberately kept
separate from `rollback_loop`/`rollback_twoclock`: a race is read once to
learn whether the number of resyncs dropped (a number), and a second time to
learn whether the visual snapping is gone (that, only a human can say).

### `rollback_correct [tics] [suppress]`
**Server side.** Asks the server to send every client a light state correction
every `tics` tics. `0` turns it off (stock behaviour: only the full resend
corrects).

The packet (`statekart_pak`, `d_clisrv.h`) is **56 bytes per kart**, i.e. 896
for a grid of 16:
- **38 bytes applied**: position, speed, angle, hitlag, rings, item;
- **18 bytes of diagnostics**, measured and printed but **never applied**:
  `spinouttimer`, `nocontrol`, `flashing`, `spinouttype`, `tumbleBounces`,
  `wipeoutslow`, `justbumped`, `offroad`, `speed` (`WORLDWIDE.md` 8.7).

On a listen server, turning this command on also exempts the host from the
gentleman's delay (see the box under `rollback_twoclock`).

- 2nd argument (`suppress`, default `1` when `tics` is given): distinguishes
  **measuring** from **replacing**.
  - `rollback_correct N 0` — sends the corrections *in addition to* the stock
    full resend. This is the control: the resync count stays comparable to
    everything measured before the channel existed.
  - `rollback_correct N` (or `N 1`) — the corrections **replace** the full
    resend. This is the real change: it is what a server in WORLDWIDE mode
    does. On compatibility, **the server decides**
    (decision of 2026-09-21, `ROADMAP.md`, *Compatibility* section).

### `rollback_drift [0|1]`
**Client side.** Reports the measured gap between this client's confirmed
world and the server's, from every correction received (mean and worst case,
in game units — a kart is about 40 units wide).

- The argument decides whether the corrections are **also applied**
  (`rollback_drift 1`) or only measured (default): measuring and correcting in
  the same race would produce a number that says nothing about either.
- Every kart sample also compares the kart's state with the server's:
  spinout, flashing, `justbumped`, hitlag, offroad, speed, item and a few
  more. A sample past 4 units prints a `SPIKE` line with them. From builds
  after `a1df8bb85`, a sample below that whose state differs prints a
  `STATE` line (the first 40 after each reset), and the report says how
  many samples differed and the tic of the first (`WORLDWIDE.md` 8.49). A
  world that agrees prints no such line.

### `rollback_delay`
Report only. Shows both halves of the latency trade-off: the game's input
delay as it currently runs (the `mindelay` floor, the engine's ceiling, the
delay actually applied to this player), and what a rollback at the current
depth would cost against a tic's budget — based on the times measured by
`rollback_test`/`rollback_resim`. Suggests running those two commands first if
nothing has been measured yet.

---

## Usage cheat sheet

- **Setting up a WORLDWIDE race** (as the `correct` scenarios of
  `playtest.sh` do, `WORLDWIDE.md` 8.4):
  - server: `rollback_correct 4`;
  - client: `rollback_twoclock 4` and `rollback_drift 1` (it is that `1` that
    applies the corrections; without it, the client only measures them).
    `rollback_cleancmds` is already on by default. Optionally
    `rollback_history 12`, to draw your inputs still in flight (not yet judged
    in a race);
  - to simulate 171 ms of latency locally: `rollback_lag 6` on the client.
- **Measuring the switches**: an off/on/off race cannot measure a switch that
  changes the confirmed world, since an on window inherits what the off window
  put out of step (`WORLDWIDE.md` 8.38) -- give each setting its own race. It
  is fine for a switch that only changes what is drawn, like
  `rollback_history`.

  The control is run with `rollback_correct 4 0` on the server (the full
  resend stays active). Today these settings are made by hand on each
  machine: the client's automatic switch into WORLDWIDE mode based on the
  server is still to be built (`ROADMAP.md`, *Compatibility* section).
- For a solo diagnostic with no network: `rollback_test`, then
  `rollback_resim`, then, if both are clean but a real race still drifts,
  `rollback_leak` (see also `soak_leak.cfg`, which runs
  `rollback_soak <interval> <tics> 1` on the network test map).
- To investigate a desync in a real race, on both machines at once:
  `rollback_blame 1` (position/item/RNG) and `rollback_damagelog 1` (damage)
  run together and are read tic by tic.
- Known trap (see `ROLLBACK.md`): running `rollback_test` **in the middle** of
  a measurement scenario changes the race that follows (a measured side
  effect). Keep measurement scenarios and diagnostic commands apart.
- `rollback_loop` and `rollback_twoclock` never run together; the second
  replaced the first (see the end of `ROLLBACK.md`, *The pivot landed*) and
  `rollback_loop` is obsolete.
