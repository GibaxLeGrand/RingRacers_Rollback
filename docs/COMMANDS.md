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

**Up to date as of 2026-10-01** — 32 commands, checked against
`K_RegisterRollbackStuff` in `k_rollback.c`, and one server variable,
`worldwide` (`cvars.cpp`). Two commands are **obsolete** (`rollback_loop`,
`rollback_pace`) and are kept only for comparison. `worldwide` and
`rollback_vanillajoin` exist from `51ba899d6` (`WORLDWIDE.md` 8.80);
`rollback_poolcopy` from `df8ed24e9` (8.82); `rollback_rawsnap` from
`371ca7419` (8.88, merged as `49daf1196`); `rollback_histreal` from
`7a455f6fe` (8.89); `rollback_keepearly` from `6209f1786` (8.98), off by
default from `771bec680` (8.100); `rollback_join` (8.119) and
`rollback_botsashuman` (8.120) from 2026-10-01.

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

From `4adeea840` (`WORLDWIDE.md` 8.86, 8.87), one line per player: samples
filed, filed a tic later than they arrived because the slot was taken, filed
over one already there, and tics that got none and repeated the one before
(`SV_Maketic`). In a level only; cumulative, never reset -- read a window by
the difference between two reports.

### `rollback_objprofile [0|1]`
Off by default. Times the objects' thinker list **object by object** and adds
the time up by object type (`p_tick.c`, `WORLDWIDE.md` 8.68, 8.69). Built to
find what an Opulence tic spends its time on: its decorations (gems and
coins, mace chains, braziers), not the karts.

- No argument: the list's time a tic, then the 15 dearest types -- how many
  think a tic, their time a tic and each -- and every other type together.
- The timing adds its own cost, so the total reads above a tic's real one:
  compare types with each other, not with a race without it.
- Setting it resets the counts.

### `rollback_rawsnap [0|1|2]`
**Client side, and the tests** (`WORLDWIDE.md` 8.88; merged as
`49daf1196`). How snapshots are taken:
- `0`: network snapshots, as before.
- `1` (**default since `WORLDWIDE.md` 8.125**): **raw snapshots** -- the
  level pools copied whole, the heads pointing
  into them, and an archive of the rest (players, world, ACS, Lua...).
  Restored at their own addresses; every reference count is rebuilt. The
  tests and soaks refuse to start in this mode, and a running soak waits:
  they compare archives, which it does not write.
- `2`: raw snapshots **verified** -- each also carries the full archive, and
  each restore is checked against it byte for byte, and its rebuilt
  reference counts against the live ones; the first failures print a
  `VERIFY` line (the archive block, or the object type and both counts).
  Slower: it is a check, not a mode to play in.

No argument: the mode and its counts -- raw saves and their size (archive,
pools, heads) and time, raw restores and their time, restores refused
(another level), restores verified and how many failed each check. Also
printed at the end of `rollback_twoclock`'s report while raw snapshots are
on. Setting it resets the counts; snapshots already taken are read the way
they were written. About 5 MB a snapshot on Opulence. Harness: `soak.sh
leakraw`, `soak.sh wwraw` (mode 2), `playtest.sh keepraw` (mode 1).

Measured on Opulence (`WORLDWIDE.md` 8.94, 8.95): a save 1.1 ms against 2.5
to 2.9, a restore 1.9 ms against about 6.5, a kept pass 5.6 to 6.4 ms
driven; soaks 0 failures, the archive identical after all but 9 of 2805
verified restores. **On by default since 8.125**: the players-block
difference is the item list's capacity, harmless (8.124); the double claim
is counted once; a snapshot taken while an object has a Lua
`floorspriteslope` goes the network way, and the report counts them; the
level interpolators are not rebuilt by either restore. The harness's
scenarios that run the tests set `rollback_rawsnap 0`, so they measure what
they always measured.

### `rollback_poolcopy [times]`
**Diagnostic, in a level** (`WORLDWIDE.md` 8.82). Times a raw copy of the four
level pools every thinker and sector node lives in -- what a raw snapshot
(track B2) would save instead of the network archive's thinker sections --
`times` times (10 by default), puts the last copy back onto the world it was
taken from, and copies again to check the round trip is byte for byte exact.
Changes nothing: the world is put back as it already was. Prints the copy's
size, its mean time, the time to put it back, and `exact` or `NOT EXACT`.

### `rollback_vanillajoin [0|1]`
**Client side, for testing WORLDWIDE mode's refusal** with a WORLDWIDE build
(`WORLDWIDE.md` 8.80). With `1`, the next join leaves out what a WORLDWIDE
client adds to its join request, as a stock client would: a server running
`worldwide on` must turn it away with a readable message, and its log says
`worldwide: refused node`. Off by default. The `vanillajoin` scenario of
`playtest.sh` uses it.

### `rollback_join`
**Client side: the pause menu's *Enter Game*, from the console.** A client
nobody drives stays a spectator, so a scenario run unattended never ran the
join's path (`WORLDWIDE.md` 8.113). It sends the same request as the menu
(`XD_SPECTATE`, join) for this machine's first player, and only when that
player is a spectator who has not already asked: the same request for a
player in the race would make them spectate. Otherwise it sends nothing and
says why. `playtest.sh <scenario> join` calls it from a generated copy of
the client scenario, the windows unmoved.

### `rollback_lagcheck` — not a command
Looked for as a command, it is not one: it is an **automatic print**, edge
triggered, inside `UpdatePingTable` (`d_clisrv.c`). It emits a line
**whenever `target_lag` changes**, prefixed `[client]` or `[server]` depending
on the branch, with the terms that decide the exemption. Nothing to turn on:
the line shows up in the `latest-log.txt` of the machine in question.

---

## Live netcode — change actual network behaviour

### `worldwide [On|Off]` — server variable
**Server side. The one switch of WORLDWIDE mode** (`WORLDWIDE.md` 8.80): the
compatibility policy says the server decides. A console variable, not a
debug command, `Off` by default. **In the menus** since `89aba69fb`:
*Options > Server Options > Advanced... > Network Connection > WORLDWIDE
Mode*, and the host screen shows `(WORLDWIDE: On/Off)` under
`(Public: ...)` (`WORLDWIDE.md` 8.113). **Saved** in the config since then;
it is a server variable, not a netvar, so a client saves only what it set
itself. Set it before hosting, in the menu, in a dedicated server's startup
script, or on the command line (`+worldwide On` -- a `+` command, there is
no `-worldwide`); the command line runs after the saved config and wins.
With anybody else connected, a change is refused and the value put back:
the clients learned the mode when they joined. The test harness starts
every server with `+worldwide off` and lets the WORLDWIDE scenarios turn it
on, or a saved value would put a control scenario in the mode.

A server hosting in WORLDWIDE mode:
- sends a light correction every 4 tics **in place of** the full-state
  resend, as `rollback_correct 4` does (a rate set by `rollback_correct`
  wins over the mode's);
- sets the `SV_WORLDWIDE` bit (`0x04`) of `kartvars` in its server info; a
  stock client never reads that bit;
- refuses a client whose join request does not declare it WORLDWIDE
  ("This server runs the WORLDWIDE netcode...") or declares another
  WORLDWIDE protocol version; its own player on a listen server is never
  refused;
- exempts its host from the gentleman's delay (`K_RollbackPays()`), as
  `rollback_correct` did.

A WORLDWIDE client reads the bit when it joins. Against a server that has
it, it switches on what the driven `keep` race ran (`WORLDWIDE.md` 8.78):
`rollback_twoclock 4`, `rollback_cleancmds 1`, `rollback_history 12`,
`rollback_keepspec 1`, and the corrections applied (`rollback_drift 1`);
its log says `worldwide: this server runs WORLDWIDE mode`. Against any other
server it switches all of them off and plays the stock netcode. Leaving the
server undoes what the join switched on, and nothing else. The switches
can still be moved by hand after joining, for measuring.

Every WORLDWIDE client adds 5 bytes to its join request (`clientworldwide_pak`,
`d_clisrv.h`); a stock server reads the stock request and ignores them.

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
  netxcmd sent during a speculation cannot be taken back), then four kinds of
  line (`WORLDWIDE.md` 8.60 to 8.66):
  - `rollback_cost` — a pass step by step: restore, network, correction,
    confirmed tics (and how many a pass), save, speculation (and how many
    tics), against a tic's 28571 us. Before `755ea3c0c`, a save made inside
    the speculation under `rollback_keepspec` was counted in both `save` and
    `speculation`;
  - `rollback_frames` — the main loop's iterations with and without a pass,
    their work before the sleep in buckets, the gaps between drawn frames,
    how many frames were drawn and how many iterations ran past a tic (the
    frame after each is skipped). From `e5fb1c61a` (`WORLDWIDE.md` 8.100),
    two more lines, for the frames drawn while the local kart moves (over 2
    units a tic): how the kart's **drawn** position (`R_InterpolateMobjState`
    at `rendertimefrac`) and the **view**'s stepped from the frame before,
    against the kart's speed and the time between the two frames -- even,
    short (under half), long (over one and a half) or backwards -- split by
    whether the frame carried a pass. Counted with prediction on or off, so a
    race without it is the control. Since 2026-10-01 (`WORLDWIDE.md` 8.121),
    two lines more: **the other karts** as drawn, the same classes for every
    kart but this machine's, one line for the bots and one for the people.
    A kart is followed by its slot, so a load that gives it a new body is
    still measured;
  - `rollback_hits` — of the passes that confirmed tics the speculation had
    run, how many had every input right, the first wrong tic, whose input
    was wrong (this machine, bots, people) and in which ticcmd fields. ⚠ It
    compares only the tics confirmed at the next pass, often tics already
    received when they were speculated: it can read near 100% for nothing
    (8.71);
  - `rollback_tic` — a speculated tic's time split into player thinks, the
    thinker lists, ACS and Lua, with the Lua mobj hooks and
    `P_CheckPosition` calls a tic;
  - `rollback_save` (from `ba1e43523`, `WORLDWIDE.md` 8.81) — a local
    save step by step, the mean time and size of each
    step over every local save since the reset: players, world, polyobjects,
    each thinker list, waypoints, ACS, Lua and the rest; the steps under 5 us
    and 1 KB are summed in one line. Then, for each of the four level pools
    every thinker and sector node is allocated from, its block size, blocks
    in use and chunks; and from `df8ed24e9` (8.82), what a raw copy of the
    pools weighs and takes, measured at the report -- the raw snapshot's
    save half, against the save above.
- Setting it (any value) resets all of these, so a race can print one report
  a window.
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
**Client side, two-clock mode. Off by default** (`0`); WORLDWIDE mode sets
`12` at the join. With a depth above 0,
the speculation replays **your own inputs still in flight** -- sent, but not
yet applied by the server -- one per tic, in the order you made them, instead
of repeating your newest input over every speculated tic (`WORLDWIDE.md` 8.39).
It finds which input the server applied on the newest tic it has sent by the
leveltime stamp every ticcmd carries, and replays everything you sent after
it. The speculation then reaches the tic your newest input will land on --
one tic at least, never above `maxdepth` (capped at 34). Since `da5922575`
(`WORLDWIDE.md` 8.106), `rollback_twoclock` is no longer its floor: with a
round trip shorter than it, the floor made the speculation guess this
machine's own input and rebuild at every change (8.105). Without the history,
the depth is `rollback_twoclock`.

That tic moves with the network's jitter, so the command does not chase it:
it holds the drawn tic's **lead over the clock** at the largest one asked for
in the last second, raises it at once, and lowers it by one tic a second at
most. The picture then advances one tic per tic instead of jumping with every
jitter (`WORLDWIDE.md` 8.40, 8.41).

- Meant to change only what is **drawn**, never the confirmed world. In its
  first run the confirmed world parted inside the window where it was on,
  not explained (`WORLDWIDE.md` 8.46); not seen again since, with it on
  throughout every driven race from 8.50 (0.000 units, 8.94 to 8.99).
- About doubles the speculation's cost at 171 ms: measured 1.6 to 1.9 times,
  at 8 tics deep on average (8.46). With `rollback_keepspec`, only a rebuild
  pays it.
- Needs `rollback_cleancmds` on (the default); with it off, it does nothing
  and says so.
- No argument: the state; how often the drawn world moved against the
  clock (on or off -- an off window is the control); the share of passes that
  found the applied input; the inputs in flight on average (the round trip,
  in tics); the average depth, with how often the cap cut it short; and how
  many times the lead was raised and lowered.
- From `4adeea840` (`WORLDWIDE.md` 8.86, 8.87), also: how many samples
  `NetUpdate` made, how many after more than one real tic (and the tics that
  got no sample of their own -- each leaves its tic to the sample before at
  the server), how many carry the same stamp as the one before, and on how
  many passes the anchor matched more than one sample.
- Setting it resets those counts, so the same race can be read off then on.
- Measured from 0 to 428 ms of lag, driven (8.107): the depth follows the
  round trip up to 285 ms, and nothing is guessed. At 428 ms, a round trip
  of 17 tics, the cap of 12 cuts every pass, and the drawn world is about
  5 tics behind the newest input. For such a round trip, a higher value.
- Suggested value: `12`.

### `rollback_histreal [0|1]`
**Client side, with `rollback_history`** (`WORLDWIDE.md` 8.89). On by default:
the replay gives each of this machine's samples in flight as many tics as real
tics passed before the next one was made -- as the server files them, a tic
with no sample of its own going to the sample before -- less the tics already
received for the applied one (R1). `0`: one tic a sample, as before, for a
control (`playtest.sh keepnor1`). No argument: the state, and how many passes
R1 laid out differently from one sample a tic; `rollback_history`'s report
prints the same count.

### `rollback_keepspec [0|1]`
**Client side, two-clock mode. Off by default.** Track A (`WORLDWIDE.md`
8.73 to 8.76): instead of putting the confirmed world back and rebuilding the
speculation every pass, the speculation **stays standing** when the tics the
server confirms are the ones it ran -- same inputs for every player -- and
becomes the confirmed world as it is. Only the tics past the head are then
run. A pass that keeps costs about one tic and one save instead of a restore
and the whole speculation again.

- It rebuilds, as before, when an input differed, when a tic carries a
  netxcmd, when a tic raised a message that had to be refused or hit a
  gamedata guard, when the server confirmed past the head, when a gamestate
  was loaded or the level was starting, and when a correction is due that
  **moves** a kart. A correction whose karts all match what the speculation
  had at that tic changes nothing and is consumed without a rebuild (from
  `69e65f0ac`, 8.75).
- Needs `rollback_twoclock`, and to guess this machine's own input right,
  `rollback_history` and `rollback_cleancmds`; without them it says so.
- No argument: how many passes left the speculation standing, how many kept
  it (and how many of those through a correction that changed nothing), and
  the count of each reason to rebuild. From `a61ccadd8`, for the rebuilds
  for a wrong input (`WORLDWIDE.md` 8.79): how far past the frontier the
  first wrong tic was; whose input was wrong on it, and in which fields; for
  this machine's, whether the speculation had run it from a tic already
  received, from the history, or as the newest input repeated past it; and
  how many of the tics thrown away a rebuild from the first wrong tic would
  have kept. From `4adeea840` (8.86): where the sample the server applied on
  that tic sits in the history against the one replayed -- newer (a sample
  lost at the server) or older (a tic it filled by repeating).
- From `072542eb2` (8.92, 8.93), this machine's own inputs are compared with
  `TICCMD_RECEIVED`, which the kart's steering reads, and R1 clears the flag
  on the tics it gives a sample past its first, as the server does. Other
  players' are compared without it: a guess about them is unreceived on
  purpose. The field lists (here and in `rollback_hits`) name the flag as
  `received`.
- Setting it resets the counts.
- **What is drawn and heard.** From `020b1d653` (`WORLDWIDE.md` 8.102,
  8.103), the picture is interpolated between two kept passes as between
  two tics; before, it held still between passes and jumped a tic at each.
  A tic's sounds and chat lines come the first time this machine runs that
  tic, never again on a rebuild (8.73). From `af4104553` (8.108, 8.109),
  that rule holds inside a tic only, so the menus and the title card are
  heard.
- From `f13231534` (8.109, 8.110), in two-clock mode, each chat line a tic
  writes prints a `rollback_chat:` line, without a command: the tic and its
  leveltime, speculated or confirmed, the gamestate, the horizon, the
  frontier, the standing speculation's head, and how many lines were held
  back so far.
- From `04db0cf71` (8.111, 8.112), the report ends with a `rollback_refs:`
  line. It gives the loads of the network archive so far, how often the
  collision pointers (`g_tm.thing`, `floorthing`, `hitthing`) held an
  object at one and a live object sat at its address after, and how many
  kart bodies' reference counts went below zero.
  - In a `PARANOIA` build (the CI's dev build), a body going below zero
    prints, without a command:
    - its ledger since this machine first saw it, by the file and line
      that took or let go of a reference;
    - where it was first seen and removed;
    - what still points at it.
  - A load that leaves a collision pointer on a freed object, whose address
    holds a live one after, prints a line too.
  - 12 of each at most.
- Measured on Skyscraper Leaps with `rollback_history 12`: 99 to 100% of
  passes kept, a pass of 1.9 to 2.4 ms (each save counted once), as many
  frames as with no speculation, drift 0.000 (8.76, 8.77). Driven on
  Opulence (8.78): 65 to 69% kept, 54 to 65 frames a second against 12
  before, each rebuild a hitch of about 60 ms, the camera stuttering. With
  R1 (8.92), driven on Opulence: 99.4 to 100% kept, a pass of 7.3 to 8.6 ms,
  121 to 129 frames a second all race long; with raw snapshots
  (`rollback_rawsnap 1`, 8.95), 5.6 to 6.4 ms, 133 to 137. In WORLDWIDE
  mode on Skyscraper Leaps, driven: 2.0 to 2.5 ms a pass, 0 to 1 rebuild in
  1000 tics (8.99). Off by default, and on in WORLDWIDE mode; not yet judged
  on a real network or against a remote human.
- From `6209f1786`, the report ends with `rollback_keepearly`'s counts.
- From `b1c0c7444` (`WORLDWIDE.md` 8.101), where in a level
  the passes fall. Every pass is filed under the level's phase at the
  frontier it starts from -- `join` (this machine's player not in the game,
  or spectating), `intro` (before `introtime`), `POSITION` (before
  `starttime`), `race`, `finished` (exiting) -- and the phase only moves
  forward within a level. A line `rollback_phases: <map> -- <phase> from
  leveltime N, tic T` when the phase changes; a level's table, one line a
  phase (`rollback_phases: <map>, ended, <phase> (leveltime A to B)`:
  passes, kept, rebuilt for this machine's input, for another's, for a
  correction, otherwise), when the next level or a restart begins; the
  current level's (`so far`) at the end of this report. Printed only while
  a speculation runs. Setting `rollback_keepspec` resets them.

### `rollback_keepearly [0|1]`
**Client side, with `rollback_keepspec` and `rollback_history`. Off by
default** since `771bec680` (`WORLDWIDE.md` 8.99, 8.100). On: a tic the
standing speculation ran on this machine's input is checked again as soon as
that input is known -- the pass after `NetUpdate` makes the sample -- instead
of when the server confirms the tic, a round trip later. If the input R1
gives it differs from what the tic ran, that tic's saved start is put back
and the speculation runs again from there to the head, instead of the whole
depth at the frontier (8.98). No argument: the state, how many standing
speculations were run again early, and the tics they ran. Setting it resets
the counts. Measured (8.99): in a driven WORLDWIDE race there was nothing
for it to remove (the control rebuilt 0 or 1 time in 1000 tics), and before
the race it fired on most passes to no effect. Harness: `playtest.sh
wwwindows` runs with the default, `wwwindows_noearly` with `0`.

### `rollback_nullspec [0|1]`
Saves and restores the frontier on **every pass** without speculating
anything. Isolates a single question: does the plain round trip through the
archive, on its own, inside the real game loop, suffice to make the server
react — with none of the speculation's own noise.

### `rollback_botsashuman [0|1]`
**Client side: the bots guessed as remote people are**, a stand-in for a
second human (`WORLDWIDE.md` 8.120, ROADMAP item 2(a)). The speculation
guesses a remote person by repeating their last input, but computes a bot's
from this machine's world (`K_BuildBotTiccmd`), so a race of bots shows
none of the rebuilds a person brings. With `1`, each bot is guessed like a
person: its last input, repeated. Only what the speculation guesses
changes; the server still sends every bot's real input, and a human who
finished the race and drives on bot movement is still computed. Off by
default. `rollback_drift`'s grid line says when it is on. The `wwbots`
scenario of `playtest.sh` turns it on (run it with `join`).

### `rollback_lag [tics]`
**Testing only.** Delays every packet received from a peer by this many tics.
A local loopback has no latency at all, so without this command a client never
runs short of confirmed tics and never has anything to predict — which is
what let the rollback loop stay on without ever triggering.

- Warns if packets were lost (queue full): an artificial delay then becomes
  artificial packet loss, and the two would look alike in the results without
  this warning.
- A delay only: no jitter and no loss, so it cannot stand in for a real
  network (`ROADMAP.md`, *Next, in order*, item 3). The WORLDWIDE scenarios
  set it first, before the join, since 8.106; `playtest.sh <scenario>
  lag=<tics>` swaps it in any scenario.

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
**Never measured** (`ROADMAP.md`, Phase D). Since 8.94 no correction moves
a kart in a driven race, so it has had nothing to smooth.

### `rollback_correct [tics] [suppress]`
**Server side.** Asks the server to send every client a light state correction
every `tics` tics. `0` turns it off (stock behaviour: only the full resend
corrects) -- unless the server runs `worldwide On`, which then sends one every
4 tics in place of the resend; the bare command says which is in force.

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
- Applying is not neutral: putting a kart back relinks it at the head of its
  blockmap and sector chains, which the server never does, and the order of
  those chains is the order of a collision. That alone made Opulence's
  confirmed world drift (`WORLDWIDE.md` 8.76). From `69e65f0ac` a kart
  already exactly where the server has it is left alone (8.75); the report
  then says how many karts were put back, how many were refused because the
  destination was blocked, and how many were already where the server had
  them.
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

- **Setting up a WORLDWIDE race, the product way** (`WORLDWIDE.md` 8.80, not
  yet run): `worldwide On` on the server before anybody joins, nothing on the
  clients -- they switch themselves on at join. The `worldwide` scenario of
  `playtest.sh` does exactly that; `vanillajoin` checks the refusal.
- **Setting up a WORLDWIDE race by hand**, for measuring (as the `correct`
  scenarios of `playtest.sh` do, `WORLDWIDE.md` 8.4):
  - server: `rollback_correct 4`;
  - client: `rollback_twoclock 4` and `rollback_drift 1` (it is that `1` that
    applies the corrections; without it, the client only measures them).
    `rollback_cleancmds` is already on by default. Optionally
    `rollback_history 12`, to draw your inputs still in flight, and with it
    `rollback_keepspec 1`, to keep the speculation when the server confirms
    it (neither is settled yet);
  - to simulate 171 ms of latency locally: `rollback_lag 6` on the client.
- **Measuring the switches**: an off/on/off race cannot measure a switch that
  changes the confirmed world, since an on window inherits what the off window
  put out of step (`WORLDWIDE.md` 8.38) -- give each setting its own race. It
  is fine for a switch that only changes what is drawn, like
  `rollback_history`.

  The control is run with `rollback_correct 4 0` on the server (the full
  resend stays active). The measuring scenarios keep setting everything by
  hand: their server does not run `worldwide`, so a client joining it
  switches everything off at join, and the scenario's own lines, run after
  the join, set what it measures.
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
