# Ring Racers Worldwide -- the design, and what the code does today

Written 2026-09-10, from Gibax's design statement, checked line by line against
this branch and against stock Ring Racers. Everything below is either read from
source (with a `file:line`) or measured (labelled as such). Where the statement
and the code disagree, the code is quoted.

Companion documents: `ROLLBACK.md` is the journal, `AUDIT_20260909.md` is the
comparison with SRB2 NetPlus and Odamex, `ROADMAP.md` is the phase list this file
revises.

## 0. The rename, and what it actually commits to

"Ring Racers Rollback" becomes **Ring Racers Worldwide**, and that is the honest
name: GGPO-style rollback needs peers with no authority between them, and this
game has an authoritative server and a consistency check. What is being built is
client-side prediction with server reconciliation. `AUDIT_20260909.md` reached the
same conclusion from the other end -- the name was wrong before the design was.

The stated objective -- *remove delay-based input lag while leaving the original
gamecode intact* -- is the one the current architecture already satisfies and
should not be traded away. `G_Ticker` is called as-is for both confirmed and
speculated tics (`K_RollbackSpeculate` in `k_rollback.c`), and four separate bug
hunts earlier in this project all came back to a replay that reconstructed a tic
instead of running it. **Any redesign that starts reimplementing the simulation
loses that, and it is not recoverable cheaply.**

## 1. The design statement, claim by claim

| the statement says | the code says | verdict |
|---|---|---|
| stock netcode is delay-based, the client waits a round trip | `TryRunTics` runs `while (neededtic > gametic)`; `neededtic` only advances when `PT_SERVERTICS` arrives | **correct** |
| lag is doubled in felt terms | the input goes out, the server folds it into a tic, the tic comes back; plus `cv_mindelay` adds more on purpose | **correct**, and worse than stated: `cv_mindelay` is on by default and the client was asking the server to delay *its own* input (fixed on this branch) |
| the player should run its own input immediately | `K_RollbackPredictInputs` feeds `D_LocalTiccmd` straight into `netcmds[tic]` for every local player, `for (i = 0; i <= splitscreen; i++)` | **already done, splitscreen included** |
| remote players should dead-reckon by repeating their last input | same function: `*to = netcmds[(tic - 1) % BACKUPTICS][i]` for humans, `K_BuildBotTiccmd` for bots | **already done exactly as described** |
| the game must be made "more deterministic" so state can be exchanged | +1120 lines of `p_saveg.cpp` fixes on this branch; three of them are upstream bugs | **already done, and it was most of the work** |
| correct the client when the server disagrees | this is where it stands open -- see section 2 | **partly** |
| interpolation to smooth the correction | frame interpolation already exists stock (`r_fps.h`, `R_InterpolateMobjState`); correction smoothing was written on this branch and is **unmeasured** | **exists, unproven** |
| the client should be able to add delay to *its own* simulation, GGPO-style | does not exist. `cv_mindelay` sends `wantdelay` to the *server* -- the opposite knob | **missing, and cheap to add** |
| the server should broadcast state every N tics ("Server Time Step") | **built** on 2026-09-10, in the form the arithmetic allows: kinematics per kart, not a state dump -- see sections 5 and 8 | **done, unmeasured** |
| correct only some players, not the whole world | the archive is whole-world and all-or-nothing -- see section 3 | **missing; it is the main cost lever** |

## 2. Audit: what stock Ring Racers can already resynchronise

Asked directly, because the plan depends on the answer. **It can, there is
exactly one mechanism, and it is the largest one possible.**

The full path, stock:

1. Every client sends `consistancy[reporttic]` with its input
   (`CL_SendClientCmd`, `d_clisrv.c:6501`).
2. The server compares it with its own for that tic and, on any difference,
   answers `PT_WILLRESENDGAMESTATE` (`d_clisrv.c:5717`).
3. The client acknowledges with `PT_CANRECEIVEGAMESTATE`, deletes `$$$.sav` and
   enters `cl_redownloadinggamestate` (`PT_WillResendGamestate`, `d_clisrv.c:4799`).
4. The server calls `SV_SendSaveGame(node, true)` (`d_clisrv.c:4854`): a full
   `P_SaveNetGame`, lzf-compressed, sent **as a file transfer**.
5. The client loads it with `P_LoadNetGame` and prints `Game state reloaded`
   (`d_clisrv.c:1560`, `:1578`).

What that means for the design:

- **It corrects everything, never a part.** There is no per-entity, per-player or
  positional correction anywhere in the protocol. The complete list of packet
  types is `d_clisrv.h:70-142`; the only world data the server ever sends is
  `PT_SERVERTICS` (inputs and textcmds) and a whole savegame as a file. Nothing
  in between exists to build on.
- **It is serialised and rate-limited.** `SV_ResendingSavegameToAnyone()` allows
  one at a time and `savegameresendcooldown[node]` throttles it. With sixteen
  clients that is a queue, not a correction.
- **It is a discrete hitch, not a smoothing.** `Downloading $$$.sav`, then
  `Loading savegame length 16382`, then a HUD notice (`hu_stuff.cpp:2105`).
  Measured on this branch: a load costs about **11 ms** on a client that is
  drawing, under 3 ms on a dedicated server.
- **It is a repair, not a netcode.** Stock, it fires when something has gone
  wrong. A predicting client makes it fire on purpose, which is why the earlier
  design produced **seven `Game state reloaded` per two-minute race**, and why
  the two-clock pivot exists.

**So: "can Ring Racers already resync" is yes. "Is it usable as the correction
mechanism for Worldwide" is no.** The correction has to come from the client's
own snapshot -- which is what `k_rollback.c` is -- and the server's full resend
has to go back to being the thing that never happens.

## 3. Audit: what our archive covers, and whether it is too much or too little

`P_SaveNetGame` (`p_saveg.cpp:8054`) writes, in order: net cvars, misc, the end
camera, players, parties, the round queue, the zone vote, the world, polyobjects,
thinkers, specials, colormaps, tube waypoints, waypoints, ACS, Lua, the RNG, and
luabanks. **The `local` flag does not skip a single section** -- a rollback
snapshot and a resend to a joining client carry the same information.

**Too much or too little?** Both, for different jobs.

- **Too little for correctness, in one known way.** 187 globals are declared
  `extern` in `doomstat.h`; **133 of them are never mentioned in `p_saveg.cpp`**.
  Most are tunables fixed at startup (`sneakertime`, `invulntics`, ...), view
  state that is per-client by design (`splitscreen`, `displayplayers`,
  `g_localplayers`), or menu and presentation. But the list also holds things a
  tic can move: `bombflashtimer`, `comebacktime`, `comebackshowninfo`,
  `wantedfrequency`, `wantedreduce`, `musiccountdown`, `g_quakes`. All Battle or
  presentation, which is consistent with the desync being seen in Race -- but
  this audit had never been done before today, and this is the first list of it.
- **Far too much for streaming.** 120 KiB at the start of a race, 318 KiB three
  minutes in. Section 5 does the arithmetic.
- **Right-sized for what it is used for.** Restore **6.8 ms** early and
  **8.6 ms** late in a race, a replayed tic **1.85-3.1 ms**, twelve replays out
  of twelve byte-identical. As a correction oracle it works; it is the
  *granularity* that is wrong, not the contents.

**The one real gap against the design statement is granularity.** The statement
asks whether "just some player locally can be corrected, if smaller corrections
are easier to deal with". Today there is no such thing: `K_LoadGameState`
restores the whole world or nothing. Odamex restores one player and the moving
sectors (`CL_PredictWorld`, `cl_pred.cpp`); Rocket League separates the car from
the ball. **Two shipped implementations say predict less. It is the same lever as
Phase B's cost problem, so it is one change that pays twice.**

## 4. Items, the roulette and randomness

Checked, because the statement flags it as "probably a target of desync".
**The state is all archived. The hazard is real, but it is a prediction hazard,
not a desync hazard.**

Archived:

- The whole roulette, per player, **including the generated item list**:
  `p_saveg.cpp:884-928` writes `active`, `itemList` (cap, len and every entry),
  `preexpdist`, `dist`, `index`, `sound`, `speed`, `tics`, `elapsed`, `eggman`,
  `ringbox`, `autoroulette`, `reserved`; read back at `:1640-1690`.
- **Item cooldowns**, the global one that is easy to miss: `itemCooldowns[]` lives
  in `g_game.c:312` and is archived at `p_saveg.cpp:7492` / `:7885`.
- **The synchronised RNG.** `PR_ITEM_ROULETTE` and `PR_AUTOROULETTE` are both
  below `PRNUMSYNCED` (`m_random.h:66`, `:88`), so they are archived by
  `P_NetArchiveRNG` and hashed by `Consistancy()`.
- Item boxes are mobjs (`MT_RANDOMITEM`) and go through `P_NetArchiveThinkers`
  like everything else.
- `player->cmd` and `player->oldcmd` (`p_saveg.cpp:389`), so a restore puts back
  what the player was holding as well as where they were.

The mechanism, read:

- The FREE PLAY reel is **re-seeded from a constant**:
  `P_SetRandSeed(PR_ITEM_ROULETTE, ITEM_REEL_SEED)`, with
  `ITEM_REEL_SEED 0x22D5FAA8` (`k_roulette.c:70`, `:1348`). Deterministic by
  construction.
- The normal reel is not. `K_FillItemRoulette` draws
  `P_RandomKey(PR_ITEM_ROULETTE, totalSpawnChance)` in a loop that runs **as many
  times as the odds table says** (`k_roulette.c:1821`), and the odds come from
  `roulette->dist` -- the kart's race distance.

That is the sharp edge, stated precisely: **the number of synchronised random
draws a roulette makes depends on the exact position of the kart that opened it.**
A speculated tic that opens an item box walks the shared seed by an amount that
depends on a guess. The archive puts the seed back, so it does not desync -- but
it means a roulette can never be *slightly* wrong. It is either restored exactly,
or it is a different reel.

**What follows is a design rule rather than a bug fix: do not predict the
roulette.** A speculated pick shows the player an item they may not get, and
swapping it a few tics later is worse than a spin that lands a few tics late.
Concretely: let the reel spin visually under speculation and commit the result
only on a confirmed tic. Nothing does this today, and nothing measures it either
-- the soak replays frozen inputs and is structurally blind to anything
edge-triggered like an item pick.

## 5. The "Server Time Step" proposal, priced

The statement proposes the server broadcast game state every 3-4 tics, with the
step configurable up to 35 per second, and clients replaying queued inputs
between broadcasts. That is the Odamex and Quake 3 shape, and it is the right
long-term answer. **As stated, with the snapshot this game has, the arithmetic
does not close.**

A snapshot is 120 KiB early in a race and 318 KiB three minutes in (measured).

| broadcast rate | per client, uncompressed | 16 clients |
|---|---|---|
| 35 Hz | 11 MB/s | 178 MB/s |
| every 4 tics (8.75 Hz) | 2.8 MB/s | 45 MB/s |
| every 35 tics (1 Hz) | 318 KB/s | 5 MB/s |

lzf takes perhaps a third off. It is still two to three orders of magnitude past
a home upstream link, and the game currently sends **inputs**, which is kilobytes
per second.

**So the shape is right and the payload is wrong.** State streaming needs two
subsystems this game does not have:

1. **Per-entity delta encoding** -- send the fields that changed, on the objects
   that changed, against a baseline the client acknowledges. Odamex's
   `p_snapshot.cpp` is exactly this.
2. **Relevance** -- do not send a client the objects it cannot see.

That is a project on the scale of everything done on this branch so far, and it
gives up talking to stock servers. **It should stay a decision point with a named
trigger, not a plan:** if prediction of remote karts looks wrong on screen no
matter what smoothing is applied, then state for remote karts is the answer, and
the table above is what it costs. Decide it with eyes on a screen.

The cheap half of the same idea is already half-built: **the client's own
snapshot ring is a state stream with a one-machine wire.** Every correction it
makes costs no bandwidth at all. Predicting less (section 3) makes it cheaper
still.

⚠ **Superseded the same day.** Gibax gave up stock-server compatibility
explicitly and asked for lighter states, so the middle of that table got built
rather than argued about: **kinematics per kart instead of a state dump.** What
was refused above is a *snapshot* every N tics; what exists now is 38 bytes a
kart. Section 8 has the layout and the numbers. Per-entity deltas and relevance
-- the parts that would let a server stream the *world* rather than the karts --
are still a project and still unbuilt.

## 6. What this reading changes about the desync

The open statement is: *a speculated tic modifies state the archive does not
carry, and that state reaches a bot's simulation.* Evidence: at tic 1908 only
`p2` and `p4` -- both bots -- differ, by 16 and 1052 units in x and y, while both
idle humans are byte-identical.

Excluded **by reading, today**, on top of what measurement had already excluded:

- **The input mailbox is not it.** `netcmds[]` is not archived, and the
  speculation writes guesses into it for tics `gametic .. gametic+ahead-1`. But a
  tic `T` can only be run authoritatively once `neededtic > T`, and `neededtic`
  advances only through the branch that calls `D_Clearticcmd(i)` and copies the
  server's ticcmds for every tic up to `realend` (`d_clisrv.c:5960-5975`).
  **Every tic the real loop runs was overwritten with the server's inputs first.**
  This was the strongest remaining candidate and it is dead.
- `player->cmd` and `oldcmd` are archived (`p_saveg.cpp:389`).
- `botvars` is archived in full -- all 18 fields, and the struct has 18
  (`d_player.h:401-429` against `p_saveg.cpp:863-881`).
- The roulette, item cooldowns and the synchronised RNG are archived (section 4).
- `K_BuildBotTiccmd` writes only into the `ticcmd_t` it is handed and into
  `botvars`; `k_bot.cpp` has no file-scope mutable state.
- **Bot inputs really are authoritative.** `SV_Maketic` builds them into
  `netcmds[maketic]` *before* `maketic++` and before the send
  (`d_clisrv.c:6899`), so a client's guessed bot input is always replaced by the
  server's. The client computing bot inputs during a speculation cannot be the
  divergence.
- **The TID hash is not it either**, and it looked like it would be:
  `TID_Hash[]` is file-scope in `p_mobj.c:15867`, is not archived, and
  `P_InitTIDHash()` is called from exactly one place -- `p_setup.cpp:8793`, map
  load -- so a restore never clears it. It is safe only because the purge at the
  top of `P_NetUnArchiveThinkers` goes through `P_RemoveSavegameMobj`, which
  calls `P_RemoveThingTID` on every mobj it frees. **Correct by one line in
  another file**, worth knowing about before anyone makes the purge cheaper.

**The file-scope enumeration, done.** 314 non-const file-scope statics in the
gameplay translation units (`p_*`, `k_*`, `g_*`); 311 of them are never mentioned
in `p_saveg.cpp`. Sorted by hand, almost all fall into four harmless groups:
HUD patches (161 of them, all in `k_hud.cpp`), definition tables loaded from
lumps (`k_terrain.c`, `p_spec.c` animations, `k_roulette.c` odds tables),
per-call scratch that is written before it is read inside a single function
(`p_map.c`'s `tmxmove`, `bombdamage`, `slidemo`; `k_collide.cpp`'s `grenade`;
`p_maputl.c`'s intercepts), and map-load constants (`k_waypoint.cpp`'s
`finishline` and `circuitlength`, `k_race.c`'s beam points, `k_rank.cpp`'s
capsule counts).

**Nothing in that list survives a tic and feeds a kart's motion.** Which means
the remaining space is smaller than section 6 assumed, and the next paragraph
matters more than another instrument.

⚠ **The evidence may have been read wrong from the start.** "Only `p2` and `p4`
differ, and both are bots" was taken to mean *something specific to bots*. The
two humans in that race were **parked**. A kart at rest hides a small state
difference; a kart at 200 units a tic turns it into a position gap within a
second. So the reading that fits the same data is: **a general small divergence,
visible only on objects that are moving.** This project has already had one
number read the wrong way round -- an undriven race that looked exactly like a
fix -- and the shape is the same: something that was not moving was mistaken for
a control.

If that reading is right, the discriminator is cheap and does not need a build:
**run the bot bench with a person driving one kart, and see whether the driven
human diverges too.** If it does, "bots" was never the category and the search
returns to what a speculated `G_Ticker` touches that `P_SaveNetGame` does not --
with `G_Ticker`'s own work, not `P_Ticker`'s, as the first place to look, since
that is the part the old design never exercised and the pivot made mandatory.

⚠ One latent archive bug found while counting, unrelated to this desync but real:
`botvars.diffincrease` is `int16_t` (`d_player.h:406`) and is written with
`WRITEUINT8` (`p_saveg.cpp:867`). It survives a round trip only while it fits in
a byte. It is a between-rounds Grand Prix value, so it is zero during a race --
which is why nothing has caught it.

**Where that leaves the hunt.** What remains is globals and file-scope state in
gameplay code that a tic can move and the archive does not carry. The
`doomstat.h` audit in section 3 is the first half of that list. The memory
comparison (`K_NameMobjField`, `P_NamePlayerField`) cannot see any of it, because
it walks `mobj_t` and `player_t` -- **that is the blind spot, and it is why five
instruments have all come back clean.** So the second half was enumerated today
rather than instrumented again.

## 7. Phases to alpha, revised for Worldwide

`ROADMAP.md` holds the phases with their exit criteria. This statement changes
three of them and adds one.

- **Phase A (close the desync) keeps its place and its exit criteria** -- zero
  resyncs over five unattended bot races and two driven -- but its *next step*
  changes, per section 6. The enumeration it was going to do is done and came
  back empty, so the next move is the free discriminator: **one bot race with a
  person driving**, to find out whether "bots" was ever the right category. That
  costs a race, not a build.
- **Phase B (fit at sixteen karts) gains a second reason to exist.** "Predict
  less" was a cost lever; the design statement asks for partial correction as a
  feature. One change, two payoffs.
- **Phase C gains an item policy**, from section 4: do not predict the roulette
  result, and prove it with a deliberately driven item test rather than the soak.
- **New, small, not previously listed: a client-local delay knob.** The statement
  asks for it, GGPO has it, and it is the honest answer for a player on a bad line
  who would rather have a stable picture than the last 60 ms. Local only -- it
  must never become `wantdelay` to the server, which is the bug already fixed on
  this branch.

And one thing the pricing removes from the plan: **the wire change stays
unspent.** State streaming is costed in section 5; the trigger for reopening it is
Phase D reporting that remote karts look wrong on screen no matter the smoothing.

---

## 8. The driven bot race, and the light correction channel

Two things happened on 2026-09-10 after the sections above were written.

### 8.1 The discriminator answered, and it closed a category

`playtest.sh botdesync`, unchanged from the unattended run so the two are
comparable, with a person driving. Nine resyncs. The blame lines from both ends,
diffed per kart on each refused tic:

- **The driven human diverges on eight of the nine refusals**, by up to 330
  units. So **"bots" was never the category.**
- **The parked host is byte-identical on all nine**, every kart, every tic.

That is the reading section 6 predicted: *a general divergence, visible only on
what is moving.* The earlier conclusion was an artefact of two stationary karts,
and the same shape as the undriven race that once looked like a fix.

### 8.2 Three things that fall out of the same nine lines

**The refusals are cooldown-limited, not periodic.** Tics 1897, 2086, 2276,
2466, 2654, 2843, 3042, 3231, 3421 -- spacings of 189, 190, 190, 188, 189, 199,
189, 190. `savegameresendcooldown` is `I_GetTime() + 5 * TICRATE`, which is 175
tics plus a round trip. **So the client is diverging continuously and nine is a
floor, not a count.** Every resync figure in this journal is a measurement of
the cooldown as much as of the bug.

**The causal order is now fixed, and the RNG is downstream.** On the first
refusal the differences are **sub-unit** -- 0.007, 0.013 and 0.034 units -- and
`rngsum` is *identical*. Only later does `rngsum` diverge. Given section 4, that
is exactly the expected chain: positions drift, a roulette's draw count depends
on the kart's distance, so the shared seed follows the positions apart. **The
RNG was never a cause and can be struck off for good.**

**Cost, incidentally: 9.5 ms a pass at nine karts with somebody driving** --
33% of a tic, already past Phase B's 30% line at nine of sixteen.

### 8.3 The archive gap, enumerated at field level

`mobj_t` has 122 fields and **17 are never named in `p_saveg.cpp`**; `player_t`
has 352 and **6 are never named**. And the oracle that has said "byte-identical"
twelve times out of twelve is **structurally blind to every one of them**: it
compares archives, and these fields are not in an archive. That is why five
instruments came back clean.

Sorted, they are interpolation origins (`old_z`, `old_x2`, `old_scale`, ...),
rebuilt links (`touching_sectorlist`, `tid_next`), camera (`bob`,
`deltaviewheight`, `cameraOffset`, `fovadd`) and `karthud` -- whose 199 gameplay
references turn out to be sound and camera on inspection. Two are real defects
even so:

- **`old_z` is never restored.** The load does `mobj->x = mobj->old_x = ...` for
  x, y, angle, pitch and roll (`p_saveg.cpp:5205`) and nothing at all for
  `old_z`. `K_PuntHazard` reads `z - old_z` as a motion vector
  (`k_collide.cpp:1402`) -- bounded, because it takes the max with momentum, so
  it is not the drift, but it is wrong.
- **`K_HandleLapIncrement` reads `old_x`/`old_y` as simulation** for the
  false-start penalty (`p_spec.c:1981`), and after a restore those hold the
  current position rather than the previous one.

Neither explains a continuous sub-unit drift on every moving kart. **The hunt is
still open, and the next instrument is in 8.4 rather than in another field
comparison.**

### 8.4 The light correction channel, as built

`PT_STATECORRECTION`, server to client, unreliable, sent every N tics:

| | |
|---|---|
| per kart | 38 bytes: x/y/z, momx/momy/momz, angle, hitlag, rings, itemtype, itemamount |
| full grid | **608 bytes**, against **318 KiB** for a snapshot -- a factor of 500 |
| at one every four tics | under **6 KB/s** a client, where snapshots would be 2.8 MB/s |
| applied | on the *confirmed* world, right after `GetPackets()` in `TryRunTics`: the speculation is undone, the packet is read, the authoritative loop has not run |
| moved with | `P_MoveOrigin`, which keeps the interpolation origin, so a kart slides to where the server says instead of appearing there |

**And it is the best instrument this branch has had for the drift.** Every
correction measures the gap between one client's confirmed world and the
server's, on a named tic, for every kart, *continuously* -- where the checksum
could only ever say "these differ", once per cooldown.

So measurement and correction are separate knobs, and measurement is the
default:

- `rollback_correct N 0` on the server -- **the control.** Corrections are sent
  and measured; the server still resends the full state on a mismatch, so the
  resync count stays comparable with everything measured before today.
- `rollback_correct N` -- **the change.** Corrections stand in for the resend.
  This is the line where stock-server compatibility is given up, and the point
  of the exercise: a predicting client stutters *because* a stock server resends
  here.
- `rollback_drift` on the client -- prints mean and worst error in fractions of
  a unit. `rollback_drift 1` also applies them.

Scenarios: `playtest.sh drift` (control) and `playtest.sh correct` (change),
both six bots on `RR_SkyscraperLeaps` so they can run driven or unattended.

### 8.5 First numbers off the channel

**The control** -- `playtest.sh drift`, six bots, nobody driving, full-state
resends still enabled:

```
295 corrections, 295 measured on the tic they name, 0 stepped over, 2655 samples
    mean 0.157 units, worst 13.368 on p8
420 corrections, 420 measured, 0 stepped over, 3780 samples
    mean 0.110 units, worst 13.368 on p8
544 corrections, 544 measured, 0 stepped over, 4896 samples
    mean 0.085 units, worst 13.368 on p8
```

Three things, in order of what they are worth.

**The sample rate is perfect: 544 of 544 measured on the tic they name, none
stepped over.** That was the risk in holding a correction until the confirmed
clock reaches its tic -- a loop that runs two tics in one pass could step over
it. It never does. So this is **4896 kart samples** rather than a count of
resyncs.

**The drift is tiny: 0.085 units mean, on a kart forty units wide.** And the
blame lines from the driven race put the first divergence at 0.007 to 0.034
units. **Two instruments agree on the order of magnitude** -- the first time in
this project that two measurements have corroborated each other rather than
contradicting.

⚠ **And it is not yet evidence of anything.** The control keeps the resends by
design, four of them fired, and every one resets the divergence to zero. So this
measures *drift between resyncs*, and a falling mean may be the resyncs doing
their job. The run that answers it is the one with them suppressed.

**What to look for there.** Flat means corrections alone make this shippable and
the residual bug is cosmetic. Diverging means a correction every four tics is
fighting a leak, and the leak still has to be found. Either way the number
replaces a resync count with a distance in units.

### 8.6 The change, measured: the stutter becomes a quarter of a unit

`playtest.sh correct` -- same six bots, nobody driving, resends **suppressed**
and corrections **applied**:

```
resyncs: 0          full-state resends suppressed by the server: 9
125 corrections, 125 measured on their tic, 1125 samples
    mean 0.082 units, worst 4.290 on p1,  1125 karts put back, 0 refused
250 corrections, 2250 samples
    mean 0.242 units, worst 36.257 on p6, 2250 karts put back, 0 refused
375 corrections, 3375 samples
    mean 0.252 units, worst 36.257 on p6, 3375 karts put back, 0 refused
```

**Zero `Game state reloaded` for a whole race**, against four in the control and
nine in the driven race. The server still disagreed with the client nine times
-- the checksum compares exact positions, and a quarter of a unit fails it every
time -- and nine times it sent 600 bytes instead of 318 KiB.

**The drift does not run away.** 0.082, then 0.242, then 0.252: it rises once the
resyncs stop resetting it, then flattens. The worst single sample spiked to 36
units and never went past it. A kart is forty units wide, so the steady state is
a quarter of a unit invisible and the worst case is under one kart length, once.
**Every correction landed: 3375 karts put back, none refused for a blocked
destination.**

**What this is, stated precisely.** The desync is *not* fixed -- client and
server still diverge, continuously, by about 0.25 units per four tics. What has
changed is the consequence: **a 318 KiB file transfer and a visible hitch became
a sub-unit position error.** That is a palliative, and a very effective one.

⚠ **What it does not do is satisfy Phase A.** Phase A asks for zero resyncs over
five unattended races and two driven, and it means *no divergence*, not
*divergence absorbed*. This gives zero on one unattended race by suppressing the
resend. So Phase A changes purpose rather than closing: it stops being what
blocks the alpha -- the channel unblocks that -- and becomes the thing that
lowers the correction rate and the residual. **The leak is still unfound.**

⚠ **And n = 1, unattended.** The race that produced nine resyncs and 330-unit
gaps was *driven*. This one was not. The driven repeat is the first thing to do.

⚠ **One crash worth keeping written down**, because the shape recurs: the first
run that *applied* a correction died on `P_MapStart: g_tm.thing set!` within
seconds. `P_MoveOrigin` goes through `P_CheckPosition`, which parks the thing it
is testing in `g_tm.thing` and leaves it for the caller to clear; the ticker
brackets its own work with `P_MapStart`/`P_MapEnd`, and anything that moves a
mobj from outside the ticker has to bracket itself. **Any future code that
corrects the world outside `P_Ticker` needs the same bracket.**

### 8.7 The state probe answered, and the collision probe did not

One race at 171 ms, nine karts, exe verified, logs cleared beforehand. The
packet carried nine extra state fields per kart and a running collision tally,
and applied none of them: the question was *which* state differs.

**The field tally across forty spikes:**

```
37 speed      13 flash      9 hitlag      7 tumble      2 item      1 nothing
```

`speed` is derived from `momx`/`momy` and recomputed every tic, so its 37 hits
restate the momentum difference already known. What remains is one group with
one author: **`flashing`, `tumbleBounces` and `hitlag` are all written by the
damage path.** And the values say which way round it is:

```
tic 2952 p6   flash 0/64   tumble 0/2   hitlag 0/20
tic 2988 p6   flash 0/64   tumble 0/3
tic 3016 p6   flash 0/60
tic 3032 p6   flash 0/44   hitlag 6/0   item 0/7
tic 3040 p6   flash 0/37   hitlag 0/6
```

The server counts a 64-tic flash down for ninety tics. **The client never
started it.** So the two machines disagree about *damage events* -- who got hit
and when -- and it goes both ways: p8 read `hitlag 7/0` at tic 2728 and `0/6`
eight tics later.

⚠ **This retires option (a).** Carrying `flashing` and `tumbleBounces` over the
wire would make the kart blink and tumble on the client *without the damage
having happened* -- no rings lost, no item lost, no speed penalty. It would
desynchronise the meaning of the race in order to tidy the position, and bury
the cause. Measuring before applying was worth the race.

⚠ **And the collision half of that instrument was built wrong.** It sat in
`PIT_CheckThing`, which fires on every pair of objects that come *near* each
other: it read 27 million per race, and its value depends on who is near whom --
that is, on the divergence it was meant to date. The offset moved +837k, +820k,
then −757k, −747k, and that was the instrument, not the game. **Circular, and
therefore mute.** It also cost a call per pair in a loop whose budget is already
9.5 ms of a 28.6 ms tic. Retired, not kept alongside.

**What replaces it: a damage-event tally.** `P_DamageMobj` became a wrapper
around the original body, and it notes the **outcome** -- only when the function
returns true. Attempts are refused differently on the two machines all day long
(an invincible kart here, a punt there) without either being wrong, so counting
attempts would have read non-zero innocently, exactly like the tally it
replaces. A few dozen events per race instead of 27 million.

Two things make it readable:

- **The counts are an equality test, not an offset.** The packet names the tic
  the server was about to run, and the client holds it until its own clock
  reaches that tic, so both sample at the same boundary. The client adopts the
  server's count and hash once, on the first correction that lands on its own
  tic -- it missed the events from before it joined -- and from there the two
  fold the same events in the same order. Equal means agreement.
- **`rollback_damagelog 1`, run on both machines,** prints one line per event:
  tic, victim, inflictor, damage type, running hash. Confirmed tics are lockstep
  so the tic numbers match, and the two logs diff directly. The first line where
  the hashes part is the first hit the two machines judged differently.

That is the upstream question the position error has been a symptom of since the
start, asked at the one place where a disagreement cannot be innocent.

### 8.8 The damage path is innocent: the divergence is in *when*, not *whether*

One race, 171 ms, nine karts, exe `1d2ca682fed9` on both instances, one session
header and one end-of-logstream in each log.

**0 resyncs, 8 resends suppressed. 3366 karts put back, 0 refused. Mean 0.338
units, worst 56.025 on p7, 40 spikes.**

**The damage tally answered, and it cleared the damage path.** Two events in the
whole race, and the counters were *equal at every check* -- `here 1 (hash
7e02595a), server 1 (hash 7e02595a)`, then `2 / 2 (debbc4bd)` on both. Where the
two machines judged a hit, they judged it identically: same victim, same
inflictor, same damage type, same hash.

⚠ **Then the two logs were diffed, and this came out:**

```
client:  rollback_damage: tic 2861 #2 p7 hit by t430 type 3 -- hash debbc4bd
server:  rollback_damage: tic 2867 #2 p7 hit by t430 type 3 -- hash debbc4bd
```

**The same hit, six tics apart.** Confirmed tics are lockstep, so those two tic
numbers describe the same instant of the same race. `rollback_lag` is 6.

The spikes around it tell the same story from the state side:

```
tic 2864 p7 off by 40.355 -- damage 2/1 -- flash 82/0  tumble 1/0  hitlag 12/0
tic 2868 p7 off by 56.025 -- damage 2/2 -- tumble 2/1             hitlag 0/14
```

At 2864 the client is already three tics into the flash and the server has not
started it; at 2868 the server starts while the client is a bounce further on.
**Nobody is wrong about the hit. They are out of step.** So the earlier reading
of this branch -- "the server holds a damage state the client never took" -- was
half right and pointed the wrong way: it is the same state, offset in time, and
whichever machine is ahead depends on which side of the offset the sample lands.

**And the shape of the error says the same thing.** p8 -- `*Guest has joined the
game (player 8)`, the client's own kart -- owns 30 of the 40 spikes, with
momentum differing by 10 to 20% while the position differs by four to ten units:

```
tic 2444 p8 off by 4.700 -- mom here (437710,-455959,0) server (533567,-483705,0)
```

A large velocity difference with a small position difference is not a spatial
error. **It is a phase difference: the same trajectory, sampled at different
times.** Sub-unit drift cannot move a collision by six tics -- at eight units a
tic that is fifty units of travel -- but a phase offset of the lag does it
exactly.

**The mechanism, read out of the code rather than guessed:**

`d_clisrv.c:7290` decides a tic is *predicted* when `gametic >= neededtic` --
the client has caught up with what the server has told it -- and then **runs
that tic anyway, advancing `gametic`, the confirmed clock**, on inputs filled in
by `K_RollbackPredictInputs`. For the local player that fill is not a guess: it
writes what you are holding *now* and stamps it `TICCMD_RECEIVED`. The server
receives that same input a trip time later and spends it on a **later tic**. So
the client's confirmed world applies your input six tics before the server's
does.

That is sound as long as the contradiction is repaired, and the repair exists:
`K_RollbackPending` re-runs a confirmed tic the network has contradicted. But it
opens with

```c
if (g_havecorrection == false || g_loopahead <= 0)
    return false;
```

and `Command_RollbackTwoClock_f` sets **`g_loopahead = 0`** whenever the
two-clock mode is turned on. **In the mode every measurement on this branch has
been taken in, a confirmed tic that ran on a guess is never re-run.** A
permanent divergence, continuously fed, on exactly the kart whose input is
guessed -- which is exactly what 30 of 40 spikes on p8 look like.

⚠ **Not yet proven, and the missing proof is one line.** `g_predicted` and
`g_furthestahead` count this path, and they are printed by `rollback_loop`,
which was not in the scenario. Added now. The next race says whether the
confirmed clock ever ran a guessed tic, and how far ahead it got.

**Three conditions localise the cause, and none of them needs a build:**

| Scenario | Saves | Restores | Speculates | Predicts confirmed tics |
|---|---|---|---|---|
| `rollback_twoclock 0` | no | no | no | (old loop, rollback armed) |
| `rollback_twoclock 4` + `rollback_nullspec 1` | yes | yes | no | yes, unrepaired |
| `rollback_twoclock 4` | yes | yes | yes | yes, unrepaired |

If the drift survives with nothing speculated, the archive is lossy. If it
survives with nothing saved either, it is upstream of everything this branch
added. If it only appears in the third row, the speculation leaks. **The
instruments are in place for all three.**

### 8.9 What four races and a soak closed, and the one test nobody had written

The night's measurements, in the order they eliminated things. The grid was the
same every time: `p0` the host, `p1`-`p7` bots, **`p8` the local player, with a
person driving it** -- which is why p8 carries 22 to 30 of every 40 spikes.

| Test | Result | What it excludes |
|---|---|---|
| `correct` (speculation on) | mean 0.33-0.53 u, 37-40 spikes, 0 resyncs | -- |
| `nospec` (save + restore, nothing speculated) | **0.000 u over 3357 kart samples, 0 spikes** | the archive, the save/restore cycle |
| offline soak, 4-tic resim, same map | **0 failures in 330 checks** | the determinism of the tics, and the restore against a simulation |
| `rollback_loop` | 6280 predicted tics = 1570 passes x 4, exactly | the confirmed clock ever running on a guess |
| `rollback_detect` | **0 inputs arrived for tics already run, 0 contradicted** | the inputs, entirely |

**And the channel finally caught a disagreement outright.** The server resolved
*no* damage at all in that race; the client resolved one:

```
client:   rollback_damage: tic 3299 #2 p5 hit by t423 type 17 -- hash 06b6dfac
server:   (nothing, tally still 1)
```

The spike on the following tic shows the phantom hit in full -- `flash 64/0
tumble 2/0 hitlag 13/0`, a fresh 64-tic flash and two tumble bounces the server
knows nothing about.

⚠ **But the drift came first, by three hundred tics.** p5 was already 22.851
units out at tic 3000, and 4 to 10 units out repeatedly from 3244. So the
phantom hit is a *consequence*: the two worlds had p5 and the object in
different places and one of them connected. This is not a collision bug, and
the earlier reading that put the damage path in the dock is now closed twice
over -- the counts and hashes agree everywhere the two machines both resolve a
hit, and `nospec` lands them on the same tic.

**Also worth keeping: p5 is a bot.** The divergence is not confined to the kart
with a person on it.

**And a measurement bug of my own, found by reading a print I did not expect.**

```c
2807:  static uint32_t g_corrections;   // corrections received         <- the channel
3369:  static uint32_t g_corrections;   // rollbacks the loop performed <- the old loop
```

Two file-scope `static uint32_t g_corrections;` in one translation unit are a
tentative definition of **one object**: legal C, no warning. The channel had
been incrementing the old loop's counter since it was written, and each
command's reset cleared the other's count. Nothing was measured wrong, because
the old loop performs no rollbacks while `g_loopahead` is 0 and two-clock mode
forces that -- but `rollback_loop` printed the channel's 391 as its own.
Renamed to `g_statecorrections`.

### 8.10 The blind spot, and `rollback_leak`

Same inputs, same starting state, deterministic tics -- all three now proven
separately -- and the confirmed world still drifts half a unit every four tics
with the speculation on. One of the three has to be false, so look at what the
soak *cannot* see.

**`K_ResimCheck` runs both of its passes on the same inputs.** It freezes
`players[i].cmd` and replays it, which is what makes its two passes comparable.
The netcode does something else: it speculates on **predicted** inputs, restores,
and then runs the confirmed tic on the **real** ones. Anything that lives
outside the archive, the players and the mobjs -- a static, a cache, a global
the tic writes and later reads -- would be left holding a value computed from
inputs that never happened. And two passes with identical inputs recompute such
a value identically and agree. **330 clean checks beside half a unit of drift is
exactly that shape.**

So `rollback_leak [tics]`, three runs of one tic on the real inputs, all from
one saved world:

```
B    a pristine reference, taken before anything else has run
A1   after a pass of N tics on the REAL inputs, restored
A2   after a pass of N tics on PERTURBED inputs, restored
```

- `A1 != B` -- any extra pass pollutes, whatever it simulated.
- `A1 == B` and `A2 != B` -- it takes a *wrong* pass. **The netplay case
  exactly**, reproduced on one machine, in one tic, with no network.

The perturbation is a **neutral** input, not an invented extreme, because a
neutral input is what a client really predicts for somebody who was doing
nothing. And when every real input is already neutral -- a parked grid -- the
check refuses rather than passing: the wrong pass would be the right one, and a
check that cannot fail is worse than no check.

`rollback_soak <interval> <tics> 1` runs it as a soak, because the phantom hit
took 1800 tics to appear and one check at an arbitrary moment proves little.
`soak_leak.cfg` is that soak on the netplay map, ~250 checks, one instance, no
network. **An iteration goes from five minutes to two seconds.**

### 8.11 rollback_leak's first catch: an honest pass already leaks, and it is not one field

`soak_leak.cfg` on `RR_SkyscraperLeaps`, 8 racers, `rollback_soak 20 4 1`: **261
checks, 1 failure**, at leveltime 1500. One machine, no network, two seconds a
check.

⚠ **The failure is the HONEST-pass case, not the mispredicted one.** A pass of
4 tics on the *real* inputs, restored, followed by the same real tic that a
pristine snapshot would have run directly -- already disagrees with running
that tic straight from the snapshot. This is a more fundamental leak than the
"wrong inputs" hypothesis `rollback_leak` was built to test, and it is exactly
what `K_ResimCheck`'s own design cannot see: that check compares two N-tic
passes to each other, never a fresh 1-tic run against a "ran ahead, restored,
ran again" one.

**And it is not narrow.** `K_CompareMobjs` came back clean -- 1359 objects,
0 differed, every mobjnum matched on both sides. So whatever leaked is not in
any archived per-object field (which rules out `old_z`, a known gap this
project has flagged before: it is real -- 0 hits in `p_saveg.cpp` -- but the
mobj comparison proves it is not what fired here). The leak is in `player_t`,
and it is **not one field**: five different players (1 through 5) carried
differing bytes at offsets 110, 284, 292, 296, 332, 672, 680 and 1016 -- all
inside the gap between `tilt` (84) and `timeshitprev` (1129), which the report
had no names for. Five players changing at once from a single mispredicted
pass is the shape of something *shared* -- an RNG draw count, a tic-global
timer -- more than of five independent per-player bugs.

**Extended the offsets line** (`K_ComparePlayers`) to name the candidates living
in that gap by `offsetof`: `speed`, `lastspeed`, `exiting`, `cmomx`, `cmomy`,
`rmomx`, `rmomy`, `totalring`, `realtime`, `laptime`, `laps`, `latestlap`,
`timeshit`, `deadtimer`. `exiting` is on that list on purpose: it is the second
half of `K_PlayerUsesBotMovement` (`bot` OR `exiting`), so a player finishing
mid-check would switch prediction mechanism precisely where this leak lives.
Nothing else changed -- next failure names the field instead of needing a hex
dump triangulated by hand.

Rare (1/261, about 0.4%) but decisive: **the netplay drift is not a networking
artifact.** The same class of leak reproduces on one machine, from a single
honest extra pass, with no round trip involved.

### 8.12 Second soak: 2/261, and one offset recurs across independent runs

Re-ran `soak_leak.cfg` after 8.11's naming pass. **261 checks, 2 failures**
(leveltime 1480 and 1520) -- roughly the same rate as the first run (1/261),
consistent with something that needs a particular moment to fire rather than a
fixed schedule.

**Both failures are the honest-pass case again**, and both still landed past
the names just added: offset 1013 (player 7, before `speed` at 1044), offsets
672 and 680 (player 4), and -- new -- 1828/1832 on players 1, 3 and 4, past
`roundconditions` entirely.

⚠ **Offset 672/680 on player 4 is not new.** The very first failure (8.11) put
differing bytes at those exact same two offsets, on the same player index,
in a different race and a different tic. Two independent honest-pass failures
landing on the identical byte pair is either a field with unusually bad luck or
a field the leak genuinely targets -- and it is now named: the
seasaw/turbine/cloud/tulip timer group between `karthud` and `speed`.

Closed every remaining gap in the same pass rather than chase it one field at a
time again: `seasaw`, `seasawcooldown`, `seasawdist`, `seasawangle` and its two
companions, `seasawdir`, the turbine and cloud/tulip timer groups, `lives`,
`xtralife`, and everything declared after `roundconditions` to the end of the
struct (`powerup`, `icecube`, `tally`, `darkness_start`, `darkness_end`). The
whole struct is named now; the next failure should land inside a printed range
without exception.

### 8.13 A confound in the harness itself, found before trusting its three catches

Before chasing `cloud`/`turbineheight` further: `rollback_leak`'s B (the
reference) ran its one tic **directly on the live world**, with no
`K_LoadGameState` call at all. A1 and A2's comparable tic always runs **after**
a restore (undoing the detour). So the comparison was quietly "never rebuilt"
against "rebuilt", on top of the "no detour" against "a detour" question the
check exists to ask -- two variables where there should be one.

`K_ResimCheck`'s own first-vs-second pass carries a similar shape (first is
live, second is restored) and reads 0/330 clean, so "restored once" alone is
not obviously the whole story. But A1/A2's comparable tic here runs after being
rebuilt from the archive a second time (once for the detour, once again to
return from it), which `K_ResimCheck` never does and B never matched. Whatever
that second-order difference is worth, it had no business being present on one
side of the comparison and absent from the other.

**Fixed: B now goes through one `K_LoadGameState` too**, immediately after the
save and before its own tic -- matching the one restore every other branch's
comparable tic already gets. What the check isolates is now exactly one
variable: whether an extra pass **in between** two otherwise-identical restores
leaves anything behind. The three failures logged in 8.11-8.12 were taken
*before* this fix and cannot yet be trusted as the netcode's fault rather than
the harness's -- they are consistent with either. Re-running the soak on the
corrected build is the next thing this session does.

### 8.14 With the confound fixed: one failure survives, and it names two real bugs

Re-ran `soak_leak.cfg` on the corrected harness (8.13). **261 checks, 1
failure** -- down from 2-3, but not zero. This one is real: both sides of the
comparison now get exactly one restore before their comparable tic, so nothing
about "live vs rebuilt" explains it.

**The primary byte (offset 1010, player 2) sits inside `turbineheight`** --
already named, already traced (8.11-8.12): computed each tic from archived
player and target-mobj positions alone, no hidden C-side global in
`wpzturbine.c`. Still open.

**And offset 672 -- the one that has now recurred on four separate soak runs,
on four different players, surviving the confound fix -- finally has a name.**
The offset table in 8.11-8.12 named everything from `seasaw` (972) onward but
never read the ~230 bytes between `karthud` and `seasaw`, because a
`itemroulette_t itemRoulette;` member sits in there and nothing in that range
had been extracted by name. A standalone probe (same stub, real `offsetof`,
verified against the three offsets the game itself had already printed --
`cmd`=8, `karthud`=228, `tilt`=84, all exact) placed it precisely:
`itemRoulette.itemList` at 656-679, `itemRoulette.playing` at 680. **672 is
`itemRoulette.itemList.cap`** -- the allocated capacity of the roulette's item
buffer, a heap-allocation bookkeeping value. Plausibly benign (an allocator
choosing a different capacity for the same content is not a gameplay
difference) rather than the leak itself.

**But reading `p_saveg.cpp` to name it turned up something worse, right next
to fields that already are archived.** `itemroulette_t` declares six
tic-order-relevant fields: `preexpdist`, `dist`, `baseDist`, `firstDist`,
`secondDist`, `secondToFirst`. Only the first two were ever written or read.
The other four -- confirmed live in `k_roulette.c`, not dead code -- are what
the roulette itself uses to decide **how fast it spins** (`baseDist`, against
`ENDDIST`/`ROULETTE_SPEED_DIST`) and **whether to force an SPB into the
result** (`secondToFirst >= SPBFORCEDIST`). Exactly the kind of value the
project's very first audit already named as the reason a roulette result must
never be predicted -- and it turns out the roulette's own internal accounting
was never protected by a restore at all. Any `K_LoadGameState` mid-roulette,
not only this check's synthetic one, left these four holding whatever the
live world last computed instead of what the snapshot actually had.

**Fixed**: `baseDist`, `firstDist`, `secondDist`, `secondToFirst` now write and
read in `P_NetArchivePlayers`/`P_NetUnArchivePlayers`, in the same order,
immediately after `dist`. Additive and symmetric -- vanilla wire compatibility
was already abandoned for this branch, and both ends of every test run the
same CI build, so there is no version-skew risk to weigh.

Two separate things remain open after this: whether `turbineheight`'s
divergence is a second real leak or the same family of gap under a different
name, and whether fixing the roulette archive gap alone drops the leak-check's
failure rate toward zero -- the next soak, on this build, answers the second
one directly.

### 8.15 Confirmed: two clean soaks after the fix, 0/522

Two more `soak_leak.cfg` runs on the roulette-archive fix (8.14), back to back:
**0 failures in 261, then 0 failures in another 261.** Against 1/261 on the
already-confound-corrected harness just before the fix, and 2-3/261 before
that on the unfixed harness. `baseDist`/`firstDist`/`secondDist`/
`secondToFirst` were the leak `rollback_leak` was built to find.

**`itemRoulette.itemList.cap` (offset 672) did not reappear either**, across
522 more checks -- consistent with 8.14's reading that it was allocator
bookkeeping riding along with the real bug rather than a second leak of its
own, though 522 checks is not a proof of never.

Phase A -- zero divergence over unattended runs -- has not been re-measured
over the network since this fix (that needs the two-instance harness and a
person driving, per the roadmap). What this closes is narrower and still
real: **the specific mechanism `rollback_leak` was written to isolate --
honest state a restore fails to carry -- is no longer reproducing on this
soak**, on the map and kart count the netplay races have been run on.
