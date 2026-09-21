# Ring Racers Worldwide -- the design, and what the code does today

Written 2026-09-10, from Gibax's design statement, checked line by line against
this branch and against stock Ring Racers. Everything below is either read from
source (with a `file:line`) or measured (labelled as such). Where the statement
and the code disagree, the code is quoted.

Companion documents: `README.md` is the entry point (working rules, decisions,
environment), `ROADMAP.md` is what is left, `COMMANDS.md` is the command
reference, `ROLLBACK.md` is the closed journal from before the pivot, and
`AUDIT_20260909.md` is the comparison with SRB2 NetPlus and Odamex.

## Current state (2026-09-21) -- read this first

This block is the only part of this file that is rewritten to stay current.
Everything after it is a dated journal: when a later section overturns an
earlier one, the earlier one gets a ⚠ pointing forward, and is not rewritten.

**Architecture.** Client-side prediction with server reconciliation -- not GGPO
rollback. Two clocks: `gametic` runs only the tics the server has confirmed, in
unmodified lockstep; the speculation runs `rollback_twoclock N` tics above it
from a snapshot and is rebuilt every pass. A light correction channel
(`PT_STATECORRECTION`, server to client every `rollback_correct N` tics) puts
each kart back where the server has it, in place of the stock full-state resend.
Every piece is behind a switch that is off by default.

**Measured and holding.**

| | |
|---|---|
| input lag, client's seat | gone -- "ça répond tout de suite", five races, and again after the pivot |
| snapshot determinism | 12/12 replays byte-identical; 0/522 leak checks (8.15); 0/330 resim checks (8.9) |
| full-state resends | 7 to 9 a race without the channel, **0** with it (8.6, 8.8, 8.16) |
| residual drift | mean 0.25 to 0.86 units, worst 36 to 59 -- a kart is about 40 wide |
| cost of a pass | 4.9 ms at 2 karts, 8.3 at 8, **9.5 at 9 with a driver** -- 33% of a 28.6 ms tic |
| listen-server host's input delay | 170-200 ms, now **0** (8.27) |

**Open, in priority order.**

1. **Vanilla compatibility** against the policy below. The savegame misread of
   8.28 is fixed in code (8.29, not yet verified). Still missing: the refusal of
   vanilla clients, the automatic mode switch, and a release-config build --
   CI builds are `DEVELOP` and cannot see public servers at all (8.30).
2. **The drift's cause** (Phase A). Excluded: the archive, the restore,
   determinism, the synchronised RNG, the damage path, the confirmed clock
   running on a guess, late resends. The live lead is the gap between 8.19 and
   8.20 (8.28, point 2).
3. **Cost at sixteen karts** late in a race (Phase B) -- the gate for the alpha.
4. **The relabel histogram's `+2` cluster** (8.27).
5. **Never run under prediction:** a person playing on the host; a full race;
   Battle, Grand Prix, Encore; anything longer than a scripted race.

**Compatibility policy, decided by Alex on 2026-09-21: the server decides.** A
server in WORLDWIDE mode runs client-side prediction and accepts WORLDWIDE
clients only. A vanilla server runs the stock delay-based netcode, and a
WORLDWIDE client that joins it behaves exactly as a vanilla client. This
supersedes 8.4 and 8.14 wherever they say stock compatibility is "given up" or
"abandoned".

⚠ **The test harness is not in this repository.** `playtest.sh` and the `*.cfg`
scenarios every measurement below relies on live in the game folder of the
original machine (8.21). **No launch without Alex's explicit go-ahead, each
time.**

**Which sections below still hold.**

| section | status |
|---|---|
| §0, §2, §3, §4 | valid as analysis |
| §1 | the table is as of 2026-09-10: the server broadcast is now built and measured (8.4-8.6); the client-local delay knob is still missing |
| §5 | superseded the same day by the light correction channel (marked inline) |
| §6 | the "bots only" reading is overturned by 8.1 |
| §7 | superseded by `ROADMAP.md`, rewritten on 2026-09-21 |
| 8.4 | the packet is now 56 bytes a kart: 38 applied, 18 diagnostic (8.7) |
| 8.8 | its mechanism ("the confirmed clock runs a guessed tic") is refuted in 8.9 and 8.17 |
| 8.14 | "vanilla compatibility already abandoned" is superseded by the policy; see 8.28 |
| 8.18 | its candidate (the server guessing a remote client's input) is refuted in 8.20 |
| 8.19 | "this is the mechanism" is withdrawn by 8.20, and 8.20 has a gap (8.28) |
| 8.23 | its reading of the client exemption is retracted in 8.25 (marked inline) |
| 8.25 | its fix was a no-op, explained in 8.26 and replaced in 8.26-8.27 |

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

> ⚠ **Status as of 2026-09-10.** Since then the server broadcast has been
> built *and measured* (8.4-8.6), and the "correct the client" row is covered
> by the correction channel. The client-local delay knob is still missing.

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

> ⚠ **Overturned by 8.1.** A driven race showed the human diverging too:
> "bots" was never the category. The exclusions below still stand.

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

> ⚠ **Superseded by `ROADMAP.md`**, rewritten on 2026-09-21, which folds in
> everything below and the later revisions.

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

> ⚠ **Two things have moved since.** The packet now carries nine diagnostic
> fields on top of the kinematics (8.7): **56 bytes a kart**, 896 for a grid,
> of which only the first 38 are applied. And the "stock-server compatibility
> is given up" wording below is superseded by the server-decides policy (see
> *Current state* and 8.28).

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

> ⚠ **Refuted in 8.9 and again in 8.17**: in two-clock mode the confirmed
> clock never runs a guessed tic (`rollback_loop`: 6280 predicted tics = 1570
> passes x 4, all speculative). Kept as the reasoning that was tested.

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

> ⚠ **Superseded by the compatibility policy of 2026-09-21** (the server
> decides). Written unconditionally, these four fields make this build misread
> a vanilla server's savegame, and a vanilla client misread ours. See 8.28,
> point 1: they belong in local snapshots only.

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

### 8.16 First netplay race after the roulette fix: drift survives, and the native consistency system independently confirms it

`playtest.sh correct`, driven, exe `758de7969535` (carries the roulette-archive
fix from 8.14). One session header/end on the client; the server log ends
cleanly at `*Guest left the game` with no `end of logstream` line, which is
consistent across every server log this branch has produced -- that string has
never once appeared server-side in this project's logs, client-only, benign.

**The fix did not close the network drift, and it was never expected to.**
`rollback_leak` targets one specific mechanism -- honest state a restore fails
to carry -- and that stopped reproducing offline (0/522). Netplay has at least
one more mechanism: the six-tic phase offset documented in 8.9 (the client's
own input lands on the confirmed clock before the server has applied it,
because prediction runs it ahead of the round trip). This race is consistent
with that still being live:

```
mean 0.862 units, worst 59.167 on p1, 40+ spikes (print cap hit)
```

Higher than the pre-fix races (0.33-0.53 mean) rather than lower -- read as
race-to-race variance (crowding, item luck) rather than a regression; nothing
in the fix touches kinematics.

**Independent corroboration, from a system this branch did not write.** Ring
Racers' own `Consistancy()` check fired eight times against the human player
(displayed as "player 9", its 1-indexed convention for slot 8) at tics 2381,
2556, 2731, 2906, 3081, 3256, 3431, 3606 -- roughly every 175 tics, the
periodic check interval, each one landing because the last one was never
repaired (`rollback_correct` keeps `K_RollbackCorrectSuppress()` on, so no
resend ever fires). **The stock desync detector agrees with every instrument
this branch has built**: the human-driven kart accumulates a divergence that
nothing currently corrects.

**And the damage channel caught its first genuine mismatch, not just a timing
offset.** Baseline adopted 3 events on both sides (hash `c95eb0a8`, agreed).
After that:

```
client:  tic 3316  p2 hit by t417 type 16
         tic 3324  p1 hit by t418 type 16
server:  tic 3206  p4 hit by t417 type 16
```

Client total 5, server total 4 -- not merely the same hit landing on a
different tic (8.9's six-tic case): **different victims entirely** (p2/p1 on
the client, p4 on the server), for an inflictor type that does match (417) at
a tic 110 apart. The two worlds resolved genuinely different collisions, not
the same collision read at different times. This is a stronger signal than
8.9's finding and the clearest evidence yet that position drift eventually
changes *what happens*, not only *where things are drawn*.

**Where this leaves the hunt:** the archive-completeness mechanism
(`rollback_leak`'s target) is closed for now. What remains is consistent with
the phase-offset mechanism already named in 8.9, still unconfirmed by a direct
instrument -- `rollback_loop`/`rollback_twoclock`'s counters answer "does the
confirmed clock run on a guess" (no), not "how far out of phase are the two
machines' clocks for the SAME wall-clock instant". That instrument does not
exist yet.

### 8.17 The phase-offset hypothesis does not survive a closer read -- and the real gap was never having checked input agreement at all

Before building anything: re-read `K_RollbackNoteArrival`, the instrument
8.9's "six tics apart" finding was about to be explained by. It answers a
narrower question than it looks like it does -- **does a LATE RESEND of an
already-run tic disagree with what ran** -- and it is gated on `tic < gametic`
at the moment a `PT_SERVERTICS` packet is processed. By construction, the
confirmed clock can only ever reach tic N after the FIRST delivery of tic N's
data has already been processed (that delivery is what lets `gametic` advance
past N in the first place), so this branch is reachable only on a *second*,
later delivery of a tic already consumed -- a resend. On a clean local link,
resends of already-applied tics essentially do not happen, which is exactly
why `g_arrivals` has read 0 in every race this session: not "measured and
found nothing," but "the event this counts has not occurred."

**And the "confirmed clock runs ahead on a guess" mechanism 8.9 leaned on is
provably false**, independently confirmed three times now (`rollback_loop`:
0 predicted confirmed tics, `K_RollbackPredictInputs` never firing there;
the `netticbuffer` reserve comment at `d_clisrv.c:7383` explaining exactly why
not). In two-clock mode the confirmed clock is unmodified vanilla lockstep --
no local prediction, no early advancement, nothing this branch adds touches
it. So a phase-offset-of-a-predicted-tic cannot explain the six-tic gap.

**Which leaves a real, previously-unnoticed gap**: nothing in this codebase
checks input agreement on the *first* delivery, for *any* player, ever. Only
the rare-resend edge case was ever checked, and it never fires. If two
confirmed clocks run the same inputs from the same state with deterministic
tics (three separate mechanisms now proven for this branch: 0/522 leak
checks, 0/330 resim checks, 0 arrivals contradicted) and still diverge, the
one thing left unverified was whether the inputs were *actually* identical on
the tic they were first used -- not assumed identical because nothing
contradicted them later.

**Built the general case**, mirroring the damage tally's proven shape:

- `K_RollbackNoteInput(tic, slot, cmd)` folds one player's ticcmd into a
  running FNV-1a hash, called once per in-game player from the *one* place
  both client and server run a confirmed tic for real -- immediately before
  `G_Ticker` consumes `netcmds[]`, in the shared `TryRunTics` loop at
  `d_clisrv.c:7363`. Not gated on `K_RollbackReplaying()`: this loop never
  runs during a speculation or a resim/leak check, so that guard would never
  fire there, and an untested guard is a claim the code does not back.
- The hash folds `forwardmove`, `turning`, `angle`, `throwdir`, `aiming`,
  `buttons`, and the three `bot.*` confirm fields -- the gameplay-relevant
  bytes. Left out on purpose: `latency` (a local annotation about sample age,
  not an input) and the `TICCMD_RECEIVED`/`TICCMD_BOT` bits of `flags`,
  which are set differently on the two machines by construction and are not
  a disagreement about what was pressed.
- `inputs`/`inputhash` ride the same `statecorrection_pak` the damage tally
  already uses, adopted once at the same baseline boundary, for the same
  reason: both machines sample at the tic the server names, so after one
  adoption the pair is an equality test, not an offset.
- `rollback_inputlog`, mirroring `rollback_damagelog`: one line per
  confirmed tic per player, both machines, diffable directly.
- The drift summary now prints both tallies side by side, so a single
  `rollback_drift` shows whether inputs, damage, or both are where a race's
  divergence is.

Not yet run: this needs the two-instance harness, and per the standing rule,
that only launches when asked. `d_clisrv.c` and `k_rollback.c` both pass the
local per-file syntax check.

### 8.18 First netplay reading from the input tally: hashes part while counts still agree

`playtest.sh correct dedicated` -- **`dedicated` drops only the SERVER's host
player**; the CLIENT is always the one a person plays, and this race was
driven (corrected after an initial misreading that called it idle -- it was
not). The native `Consistancy()` fired four times against player 9 (1-indexed
"player 10"), same shape as every earlier driven race -- expected, not new,
now that the race is correctly understood as driven.

**What the new tally actually said, unaffected by that mix-up:**

```
baseline at tic 940 -- adopting the server's 7557 ticcmds (hash 1cf3f95e)
check 1: here 13253 (hash 0e574b18), server 13253 (hash 9c5fab90)
check 2: here 17213 (hash b3c612bf), server 17189 (hash 6be8e536)
check 3: here 21205 (hash 534cf62c), server 21189 (hash d41a955b)
check 4: here 25205 (hash c4ee18cd), server 25189 (hash 26a5b57f)
```

**Check 1 is the clean reading: identical counts (13253 = 13253), different
hash.** Not a missing or extra input -- the same number of ticcmds folded on
both sides, and the content differs somewhere among them, within roughly 700
tics of the baseline. Checks 2-4 show a stable +16/+24 count gap on top of
that, consistent with ordinary packet-arrival latency in what this print
shows (the server's number is whatever last arrived, always a little behind
the client's live count) rather than a second, separate effect -- the offset
reasoning already established for the damage tally applies here too.

> ⚠ **Refuted in 8.20**: in two-clock mode `runto` stays at `neededtic` on
> both machines, so the server never guesses a remote client's input.

**A candidate mechanism, found by reading the guard that stops the client
from predicting.** `d_clisrv.c`'s `netticbuffer` reserve -- the thing that
holds `gametic` below `neededtic` so the client's confirmed clock never
predicts -- is gated on `client &&`. **Nothing stops the server from doing
the same thing to a remote client's not-yet-arrived input.** If the server's
own confirmed clock ever reaches a tic before that client's packet for it has
landed, `K_RollbackPredictInputs` fires on the server too, and for a
non-bot player that means repeat-last: reusing the previous tic's ticcmd as
a guess for this one. Against a genuinely varying, actively-driven input,
repeat-last disagrees with whatever was actually pressed far more often than
it agrees -- unlike a static, unchanging input, where the guess is right by
construction. This is not yet proven; it is the first mechanism found that
would produce exactly the observed shape (hash parts, counts stay close) on
a driven race specifically.

**`rollback_inputlog`'s cap was too small to catch it.** 400 lines, sized
for the damage tally's few-dozen-events-a-race, covers only the first ~50
tics once every in-game player is folded every confirmed tic -- nowhere near
the ~700 tics where the first split showed up. Raised to 20000 (roughly
2500 tics of an eight-player race), and added to both `playclient_correct.cfg`
and `playserver_correct.cfg`. Not yet re-run -- needs a rebuild and, per the
standing rule, launches only when asked.

### 8.19 The input tally, read tic by tic: two different stories, not one

`rollback_inputlog 1` on both machines, aligned offline by tic and player
(client log ran tics 2237-3800, server 1500-3722, overlap 1486 tics). A
global best-shift search per player, tried first, was misleading and is
recorded here as a mistake rather than quietly dropped: a short, cherry-picked
run of lines looked exactly like the server lagging the client by a constant
7 tics, and the full search across the whole overlap did not confirm it --
`p8`'s best alignment was shift 0, matching 80.3% of tics outright. The
7-tic read does not hold as a constant. What actually held up, read from every
mismatch rather than a handful of convenient ones:

**Bots agree on the decision, almost always.** p2 and p4: 100% agreement,
every tic. p1, p3, p5, p6, p7: 81-98%, and every mismatch found has the exact
same shape:

```
tic 2338 p6: client (50, 0, 353, 0001)   server (50, 0, 354, 0001)
tic 2340 p6: client (50, 0, 111, 0001)   server (50, 0, 110, 0001)
tic 2360 p1: client (50, -800, 5208, .)  server (50, -800, 5152, .)
```

`forwardmove`, `turning` and `buttons` -- the actual control decision -- match
exactly, every time. Only `angle` differs, and by a small amount (1 to 60-odd
units on a field that reaches into the thousands). `ticcmd_t.angle` is
documented in `d_ticcmd.h` as "Predicted angle, use me if you can!" -- a
computed readout of the kart's current facing, not a control the AI presses.
**This is the already-known sub-unit position/orientation drift riding along
in the ticcmd**, not a new or separate cause: the bot's decision is identical
on both machines, and the ticcmd's angle field simply reports whatever
orientation each machine's kart happens to be at, which the SPIKE instrument
has been measuring directly since 8.6.

**The human player is a different story, and it is bursty, not constant.**
p8's 293 mismatched tics (of 1485) do not scatter evenly -- they cluster:

```
runs: [10, 36, 5, 12, 2, 27, 2, 8, 4, 23, 8, 10, 29, 1, 7, 11, 14, 60, 2, 1, 1, 1, 1, ...]
```

A handful of episodes 20-60 tics long early in the race, tapering into
isolated single-tic gaps (the same angle-drift noise bots show) later. Inside
a long episode, the shape is legible:

```
tic 2307 p8: client (50, -800,-21768,1)   server (50, -409,-18240,1)
tic 2308 p8: client (50, -146,-22108,1)   server (50, -800,-18678,1)
tic 2309 p8: client (50,    0,-22279,1)   server (50, -800,-19197,1)
tic 2310 p8: client (50,    0,-22279,1)   server (50, -800,-19714,1)
   ...client holds turn=0/angle=-22279 for six more tics...
tic 2316 p8: client (50,    0,-22279,1)   server (50,    0,-22278,1)  -- server catches up
tic 2328 p8: client (50,  495,-22119,15)  server (50,    0,-22279,1) -- client moves on, server still stuck on the old value
```

The client's own local input, folded into its confirmed clock the instant it
is made, changes at tic 2308-2309 (`turning` drops from -800 to 0). The
server's confirmed clock is still running the OLD value (`turning -800`) six
tics later, ramping `angle` as if the turn were still held, and only catches
up to the client's held value around tic 2316 -- by which point the client
has already moved on again (tic 2328). **The server is not disagreeing about
what was pressed; it is running behind on finding out**, and repeating the
last known input in the meantime.

> ⚠ **The conclusion of this paragraph is withdrawn by 8.20** (the lead it
> rests on is refuted there). The input log above stands as data: it is the
> live lead for Phase A, and 8.28 point 2 explains why 8.20 does not close it.

**Read together with 8.18's `client &&`-gated netticbuffer finding, this is
the mechanism, not a guess about it anymore.** The client never predicts its
own confirmed clock (proven three times over). Nothing stops the SERVER doing
exactly that to a remote client's not-yet-arrived input: when the server's
confirmed clock reaches a tic before that client's packet for it has landed,
it fills the gap the same way `K_RollbackPredictInputs` fills any predicted
tic -- repeat the last known value -- and runs its own confirmed simulation on
the guess. The guess is invisible while the player holds still (a repeated
value is the correct value), which is exactly why an unattended, bot-only
race never surfaces it and why the very first `K_RollbackNoteArrival` reads
came back at zero: that instrument only catches a *resent* tic disagreeing,
and this is not about resends. It only becomes visible when the real,
changing human input finally lands and the server has to catch up -- which is
precisely where every long episode above starts.

**What this changes going forward:** the fix is not in the archive, the
restore, or tic determinism -- all three are proven clean. It is in whether
the server's confirmed clock is allowed to run ahead of a remote client's
input at all. The `client &&` guard on the netticbuffer reserve is the
concrete line to look at next.

### 8.20 The `client &&` lead disproven properly, and where the search went instead

Before instrumenting anything: `runto = neededtic;` at `d_clisrv.c:7211`, with
the comment right above it spelling out the design -- *"A server has nothing
to predict"*. The branch that would extend `runto` (and therefore let
`predicted` ever become true) is `else if (client && gamestate == GS_LEVEL)`,
reached only when `K_RollbackTwoClock() > 0` is FALSE. Every race this branch
has run sets `rollback_twoclock 4`, so that branch is skipped entirely --
**on both machines** -- and `runto` stays at `neededtic` on both. `ticking =
runto > gametic` then means the loop cannot advance `gametic` to or past
`neededtic` at all, so `predicted = (gametic >= neededtic)` can never be
observed true inside it, on the server any more than the client. Three
separate readings now (`rollback_loop`'s own counters, the `client &&`
netticbuffer-reserve comment, and this) all agree: `K_RollbackPredictInputs`
never guesses a remote player's input in two-clock mode. That lead is closed,
not just re-asserted.

**What was actually found instead, reading `PT_CLIENTCMD`'s receive handler
on the server:**

```c
tic_t faketic = maketic;
tic_t timegap = maketic - realstart;

if (timegap < netbuffer->u.clientpak.wantdelay)
    faketic += (netbuffer->u.clientpak.wantdelay - timegap);

netcmds[faketic % BACKUPTICS][netconsole] = ...
```

The server does not file an arriving ticcmd under the tic number the client
itself tagged it with (`realstart`). It re-labels it as `faketic`, computed
from the server's own send schedule (`maketic`) and the gentleman's-delay
`wantdelay` the client requested -- vanilla machinery, present before this
branch and orthogonal to it. If that relabelling is not perfectly steady
packet to packet -- plausible from ordinary jitter, even on a local link --
the identical real bytes can land under different tic numbers on the client's
own confirmed clock (which uses its own numbering directly) and the server's
confirmed clock (relabelled on arrival). The input tally's hash folds the raw
tic number into every fold, so a relabelled sample reads as differing
content -- which is exactly the shape 8.19 found, in bursts, on the one
player who is a genuine network node.

**And it explains something 8.19 left unstated: only the human player showed
long bursts; every bot showed only the sub-unit angle noise.** Bots are never
sent over the wire at all -- both machines compute `K_BuildBotTiccmd` locally
-- so `PT_CLIENTCMD` and its relabelling never touch them. If relabelling is
the mechanism, it should apply to genuine remote nodes only, which is exactly
what 8.19 already showed without this being known at the time.

**Built `K_RollbackNoteRelabel`**, hooked right where `faketic` is finalised:
records `faketic - realstart` into a histogram (mirroring
`rollback_detect`'s existing offset histogram, same span, same style), plus
count/min/max/mean. `rollback_relabel`, server side only, prints it. A single
repeated value would mean the two clocks simply count from different zeroes
-- harmless. A spread means the label itself moves from packet to packet,
which is the discriminator this was built to measure. Added to
`playserver_correct.cfg`'s end-of-race report. Not yet run.

> ⚠ **A gap in this reasoning, found on 2026-09-21 (8.28, point 2).**
> Relabelling decides which tic the server files an input under, but the
> client's confirmed clock runs the server's filing, not its own. It may
> explain the histogram of 8.22-8.27; on its own it does not explain two
> confirmed worlds running different inputs on the same tic.

### 8.21 A harness bug: the server's closing report was scheduled to never run

`rollback_relabel` printed nothing after the race -- not "no packets", nothing
at all. `srvlog_correct.txt` ends mid-race, at `*Guest left the game`, with
no `rollback_damagelog`/`rollback_relabel` line anywhere.

The cause was arithmetic, not code: `playclient_correct.cfg`'s own scripted
waits total 3170 centiseconds (31.7 s); `playserver_correct.cfg`'s total
10400 (104.0 s), with the closing report gated behind a single `wait 8900`
placed right after setup. `playtest.sh` runs the client synchronously and
`kill $SRV`s the server the instant the client's script reaches its own
`quit` -- so the server's 89-second closing wait was never going to elapse
before the client's ~32-second schedule finished and took the server down
with it. **This is not new to today**: `rollback_damagelog`'s own final print
sat behind the exact same unreachable wait and has, by this arithmetic, never
actually printed in any race this session -- every damage-tally number this
branch has reported came from the periodic checkpoints inside the race
(`rollback_drift`'s own damage/input lines, which sample the server's latest
packet), never from the server's own closing tally. Worth knowing, and worth
being plain about: it does not appear to have produced a wrong reading
anywhere, since every actual conclusion drawn from the damage tally used
those periodic in-race lines already.

**Fixed in the harness, not the game**: `playserver_correct.cfg` now prints
`rollback_relabel` and `rollback_damagelog` at four checkpoints (every 2000
centiseconds, `wait 2000` repeated with a smaller final step to keep the same
10400 total ceiling) instead of once behind a wait that never completes. At
least one checkpoint now lands before the client's schedule ends regardless
of exactly how long a given race runs. `playserver_correct.cfg` is not
tracked by git (it lives in the local game install, not the source repo), so
this fix has no commit of its own -- recorded here instead, per the doc
cadence rule, since the harness is as much a part of this project's memory as
the source.

### 8.22 The relabel histogram, read: a bimodal split that names its own cause in the code's own comment

Fixed-schedule server checkpoints (8.21) finally caught `rollback_relabel`'s
output:

```
6441 packets, faketic - realstart ranged 0 to 8, mean 4.12
  +0   36    +1  493    +2 3263    +3    3    +4    1
  +5    2    +6  286    +7 1037    +8 1320
```

Not a spread around one mean -- **two separate clusters**: 3263 (50.6%) sit
at exactly `+2`, and 2643 (41.0%) spread across `+6` to `+8`. The algebra
behind `faketic = maketic + max(0, wantdelay - timegap)` collapses cleanly:
whenever a packet arrives inside its delay budget (`timegap < wantdelay`),
`faketic - realstart` reduces to exactly `wantdelay`, a constant; once a
packet arrives *after* its budget is used up, it reduces to `timegap`, the
raw elapsed delay, which is only as steady as the network jitter behind it.
**The tight spike at +2 is `wantdelay = 2` -- vanilla `cv_mindelay`'s
default.** The spread at +6-8 is raw transit time under `rollback_lag 6`.

`d_clisrv.c` already names this exact failure mode in its own comments, in
detail, from an earlier measurement on this branch: `K_RollbackPays()` exists
specifically to zero `target_lag` (and so `wantdelay`) whenever rollback is
"paying" for latency instead of the gentleman's delay -- *"a client with a
mindelay is asking the server to hold that client's own input for that many
tics -- and then cannot predict it, because the server applies it to a tic
the client never spent it on."* The client-side branch that sets
`target_lag = K_RollbackPays() ? 0 : cv_mindelay.value` has no floor-clamp
and no smoothing -- by the code as read, it should hold at a flat 0 for the
whole two-clock race. It plainly is not doing that: half our packets carry
`wantdelay = 2`.

**Instrumented rather than guessed further**: `rollback_lagcheck`, an
edge-triggered print on the client firing only when `target_lag` *changes*,
showing `K_RollbackPredictAhead()`, `K_RollbackTwoClock()` and `gamestate` at
that moment -- the three inputs `K_RollbackPays()` combines. If `target_lag`
is genuinely locked at 0 for two-clock mode's whole duration, this prints
once, at connect, and the +2 spike has a different, still-unknown source. If
it moves, this names exactly when and against which of the two exemptions
failing. Not yet run.

### 8.23 `rollback_lagcheck` read: `target_lag` never moves on the client, and the instrument was watching one of two senders

The 8.22 instrument ran, on a fresh `playtest.sh correct` race against a
verified binary (`5cb11ff0e`, CI artifact re-downloaded first -- the copy
sitting in the game folder predated the commit and did not contain
`rollback_lagcheck` at all, which is the binary-verification rule paying for
itself again). The client's whole race produced **exactly one line**:

```
rollback_lagcheck: target_lag -> 0 (predictahead 0, twoclock 0, gamestate 1)
```

One line, at connect, and never again. That is 8.22's first branch, stated
before the measurement and now met: **`target_lag` is genuinely locked at 0
on the client for two-clock mode's entire duration.** It does not move, so it
cannot be what makes half the packets carry `wantdelay = 2`. The client-side
mindelay exemption is working exactly as its comment claims. That lead is
closed.

> ⚠ **That second-to-last sentence is wrong, and 8.25 retracts it.** The same
> line prints `predictahead 0, twoclock 0`, so the exemption was *not* firing
> when it printed: the branch taken was `target_lag = cv_mindelay.value`, and
> it still came out 0. What is measured is that the client's `target_lag` is 0
> for the whole race. *Why* is not measured, and this instrument cannot tell
> the two paths apart, because both of them end at 0.

And the relabel histogram reproduced almost to the packet, which is worth
noting on its own -- this project's numbers have been n=1 too often:

```
6453 packets, faketic - realstart ranged 0 to 8, mean 4.15
  +0 36   +1 472   +2 3273   +3 2   +4 1   +5 1   +6 257   +7 1067   +8 1344
```

against 8.22's 6441 / +2 3263 / mean 4.12. Same bimodal split, same
proportions. **Zero `Game state reloaded` in both logs**, consistent with the
correction channel's own measurements.

**So where does `wantdelay = 2` come from, if not from the client?** Reading
the send path rather than guessing: `netbuffer->u.clientpak.wantdelay` is not
assigned `target_lag` directly. It is assigned `lagDelay`
(`d_clisrv.c:6788`), a local in `CL_SendClientCmd` computed from `target_lag`
behind `if (target_lag > 0)` -- and `CL_SendClientCmd()` is called from **two
places**: `if (client)` and, five lines earlier, `if (server)`
(`d_clisrv.c:8035`). A listen server sends a clientpak for its own node 0
player. So the `wantdelay` values arriving at the server's `PT_CLIENTCMD`
handler -- the population the relabel histogram counts -- come from **two
senders**, and the two compute `target_lag` in **two different branches** of
`UpdatePingTable`: the remote client in the `else` branch (instrumented,
locked at 0), the host in the `if (server)` branch (never instrumented). A
bimodal histogram with two senders in it is no longer a puzzle; it is a
question about which sender owns which mode, and the answer has simply never
been measured.

⚠ `K_RollbackPays()`'s own comment argues the two branches are symmetric --
*"on that side this is really asking about the host's own view, same as it is
on a plain client"*. That is a design claim, not a reading. This project has
already been burned twice by an invariant written next to code that did not
honour it (the keeper called mid-tic while its comment promised end-of-tic;
the `local` flag's own promise about renderflags). **Do not close this on the
strength of the comment.**

**Built, not yet run:** the same edge-triggered print, now in the server
branch too, tagged `[server]` against the client's `[client]`, and reporting
the three inputs that branch actually uses (`fastest`, `rollbackpays`,
`twoclock`). The statics are hoisted to function scope, which is safe because
a process takes one branch or the other, never both. One race reads it: if
the host's `target_lag` sits at 2 while the client's sits at 0, the
histogram's two clusters are named and the question becomes whether the host
should be paying a gentleman's delay to itself at all.

### 8.24 The two branches are not symmetric: on a listen server the host pays a delay the client does not

The `[server]` print ran, same scenario, build `214111b46` (CI green, both
markers verified present in the binary before the race). It is not one line:

```
rollback_lagcheck: [server] target_lag -> 2 (fastest 0, rollbackpays 0, twoclock 0, gamestate 13)
rollback_lagcheck: [server] target_lag -> 6 (fastest 6, rollbackpays 0, twoclock 0, gamestate 1)
rollback_lagcheck: [server] target_lag -> 7 (fastest 7, rollbackpays 0, twoclock 0, gamestate 1)
... 6 <-> 7, thirty-two more times, the whole race ...
rollback_lagcheck: [server] target_lag -> 2 (fastest 0, rollbackpays 0, twoclock 0, gamestate 1)
```

against the client's single `[client] target_lag -> 0`, unchanged from 8.23.

**`rollbackpays 0` and `twoclock 0`, on the server, for the entire race.**
Both read zero at every single print, at `gamestate 1` (GS_LEVEL), with the
race plainly running. So the exemption that zeroes the gentleman's delay is
**not** applying on the host side, and `target_lag = fastest` -- the raw ping
figure, 6 to 7 tics under `rollback_lag 6` -- stands instead.

**The mechanism, and it is one line.** `d_clisrv.h:676`:

```c
#define client (!server)
```

`K_RollbackTwoClock()` opens with `if (g_twoclock <= 0 || client == false ||
gamestate != GS_LEVEL) return 0;`. On a listen server `server` is true, so
`client` is false, so this returns 0 regardless of `g_twoclock`. That makes
`K_RollbackPays()` false, and `UpdatePingTable`'s server branch then takes
`target_lag = fastest` with the `cv_mindelay` floor underneath it.

⚠ **`K_RollbackPays()`'s own comment reasons about exactly this and gets it
half right**: *"K_RollbackTwoClock() already returns 0 off a dedicated server
(client == false there), so this is safe to call from either side."* True for
a dedicated server, where node 0 has no human on it and the delay it computes
is nobody's input lag. **But it is the same `client == false` on a listen
server**, where node 0 is a person holding a controller. The comment checked
the harmless case and generalised to the harmful one. Third time on this
branch that an invariant asserted next to the code has not held; the rule
stands and it is cheap -- measure the claim, do not read it.

**What this means in play, and it is not an instrumentation detail:** the
host of a listen server is charged 6 to 7 tics of gentleman's delay on their
own input -- 170 to 200 ms, the exact cost this project exists to remove --
while the remote client, correctly exempted, is charged none. Worldwide has
been measured from the client's seat every time. **Nobody has ever asked the
host whether it felt responsive**, and by this reading it should not have.

**What is still open, and stated as open.** This does *not* yet close the
relabel histogram's `+2` cluster. Worked through: an offset of exactly
`wantdelay` is what the receiver produces whenever a packet arrives inside its
budget, and `timegap` otherwise -- so the client's `wantdelay = 0` explains
the `+6..+8` spread (raw transit) cleanly, but a host sending 6 or 7 should
land on a constant `+6`/`+7`, not on `+2` 51% of the time.
`MAXGENTLEMENDELAY` is `TICRATE`, so it is not a cap, and the
`reference_lag`/`spike_time` smoothing should only hold the low value for
`GENTLEMANSMOOTHING` tics. **So one of the three -- who sends, what smoothing
does, or what the receiver computes -- is not doing what reading it says.**

**The instrument that closes it is the obvious one and was skipped once
already**: log the value at the point it goes on the wire
(`netbuffer->u.clientpak.wantdelay = lagDelay`, `d_clisrv.c:6788`), per
sender, rather than a variable two functions upstream of it. Watching
`target_lag` instead of `lagDelay` is what made 8.22 read half the population
and call it all of it.

### 8.25 Exempting the host -- and retracting what 8.23 said about the client

**First, the retraction, because it changes what 8.23 is worth.** That
section read the client's single `target_lag -> 0` as the exemption working.
It is not. The very same line reports `predictahead 0, twoclock 0`, so
`K_RollbackPays()` was false at that call and the branch taken was
`target_lag = cv_mindelay.value` -- which still printed 0, although
`mindelay "2"` sits in *both* machines' configs and `cv_mindelay` is declared
`Player("mindelay", "2")`. The arithmetic is forced: `cv_mindelay.value` read
0 at that call. Why a profile-backed cvar with a default of 2 reads 0 at
connect is **not established and is not worth a guess here**.

What survives is narrower and still useful: **the client's `target_lag` is 0
for the whole race, measured.** Which of the two paths puts it there is
something this instrument structurally cannot say, because both end at 0.
Filed with the project's other instrument-misreadings: an oracle whose two
outcomes are the same value distinguishes nothing.

> ⚠ **This first version of the fix was a no-op** -- the host never sets
> `rollback_twoclock`. Explained in 8.26, replaced by
> `K_RollbackCorrectingHere()`, measured working in 8.27.

**The fix, in `K_RollbackPays()`.** The predicate asked
`K_RollbackTwoClock() > 0`, which is gated on `client` and so answers *"is
speculation running here"*. The delay policy needs *"is this machine running
Worldwide at all"*. Those come apart on exactly one machine -- a listen
server -- and that is the one with a person on node 0. Split into
`K_RollbackTwoClockConfigured()` (reads `g_twoclock` and the gamestate, no
role gate) and left `K_RollbackTwoClock()` alone, because its `client` gate
is right for the loop: an authoritative server must not speculate.

**No client-side change by construction:** on a client `client` is true, so
the old and new expressions are the same term for the same inputs. Only the
host's answer moves.

⚠ **Written before the race, per the rule that a prediction costs nothing and
an unwritten one is worth nothing:**

1. `[server] target_lag -> 0` at `GS_LEVEL`, and staying there -- so the 6↔7
   oscillation of 8.24 disappears from the log.
2. **This doubles as the discriminator for 8.24's open question.** With the
   host's `wantdelay` at 0 its own loopback packets give the receiver
   `offset = timegap`, which on a loopback is 0 or 1. So *if* the host owns
   the `+2` cluster, `+2` should collapse from 3312 toward nothing and
   `+0`/`+1` should swell by roughly that much. If `+2` survives at ~50%, the
   host is not the `+2` sender, the cluster belongs to the remote client, and
   the question reopens somewhere other than where 8.24 pointed.

Either outcome is worth the one race, which is the only reason to run it
before writing anything else.

### 8.26 The fix was a no-op, and the reason is that "Worldwide is on" is not a thing a machine knows

The race ran on `947f1921e` (artifact taken by run id, `headSha` checked).
The result is worth more than a working fix would have been:

```
rollback_lagcheck: [server] target_lag -> 5 (fastest 5, rollbackpays 0, twoclock 0, gamestate 1)
rollback_lagcheck: [server] target_lag -> 7 (fastest 7, rollbackpays 0, twoclock 0, gamestate 1)
... 6 <-> 7 for the whole race, exactly as in 8.24 ...
```

**`rollbackpays` still 0 at every print.** Nothing moved. And the histogram
did not move either -- `+2` at 3335, against 3312 and 3273 in the two races
before it.

⚠ **Which means the prediction written in 8.25 did not get tested, and the
surviving `+2` is not evidence of anything.** The independent variable never
changed: the host's `wantdelay` was 6-7 before the fix and 6-7 after it, so
the experiment that was supposed to discriminate the `+2` cluster's owner
simply did not run. Reading "`+2` survived, therefore the host does not own
it" would have been the whole trap in one step -- a conclusion drawn from a
control that was never varied. **8.24's question stays exactly as open as it
was.**

**Why the fix did nothing.** `K_RollbackTwoClockConfigured()` reads
`g_twoclock`, and `g_twoclock` is **0 on the host for its entire life**:
`rollback_twoclock` is set in `playclient_correct.cfg` and appears in no
server scenario, because two-clock *is* the client-side mechanism -- a server
is authoritative and never speculates. The predicate was asking the host
about a switch only a client ever throws.

**The real finding, and it is structural:** there is no single "this machine
is running Worldwide" state anywhere. Worldwide is **two switches on two
machines** -- `rollback_twoclock` on the client, `rollback_correct` on the
server (`g_correctrate`, commented in the source as *"0 = off (server
side)"*). Nothing ties them together, and nothing on either machine can see
the other's. That is invisible while every measurement is read from the
client's seat, which is how it survived this long.

**Second version:** `K_RollbackCorrectingHere()`, `g_correctrate > 0` at
`GS_LEVEL`, added as a third term. The `predictahead` term is left exactly as
it was, and the two-clock term is still the same expression a client
evaluates, so once again nothing on the client side moves by construction.

⚠ **`g_correctrate` is a proxy and gets called one in the code.** The
question the delay policy actually wants is *are my clients predicting*,
which a server cannot answer today. That is the capability advertising
already on the roadmap; when it exists this predicate should ask it and stop
inferring. The server print now carries `correctrate` alongside
`rollbackpays` so the next reading says which term did the work, rather than
leaving `twoclock 0` sitting there looking like the answer when it is 0 on
that side by construction.

### 8.27 The host is exempted, measured -- and the `+6/+7` cluster was his

Build `4e641407e`, artifact taken by run id with the new instrument string
checked in the binary first. The server print is now **two lines for a whole
race**, against thirty-two:

```
rollback_lagcheck: [server] target_lag -> 2 (fastest 0, rollbackpays 0, twoclock 0, correctrate 0, gamestate 13)
rollback_lagcheck: [server] target_lag -> 0 (fastest 1, rollbackpays 1, twoclock 0, correctrate 4, gamestate 1)
```

`rollbackpays 1`, `correctrate 4`, `twoclock 0` -- the new term is visibly the
one doing the work, which is why it was added to the print. `target_lag`
reaches 0 when the race starts and **never moves again**: the 6↔7 oscillation
is gone. **The host no longer charges itself 170-200 ms on its own input.**
The client print is unchanged at one line, as construction promised.

**And the histogram moved, which is what settles 8.24's question:**

| offset | before (8.26) | after | |
|---|---|---|---|
| `+0` | 36 | **2036** | +2000 |
| `+1` | 475 | 505 | |
| `+2` | 3335 | **2557** | -778 |
| `+6` | 314 | **1** | |
| `+7` | 1232 | **124** | -1421 with +6 |
| `+8` | 997 | 1176 | |
| mean | 4.01 | **2.48** | |

The `+6`/`+7` mass collapsed and `+0` swelled by almost exactly as much.
**So the host owned `+6`/`+7`**, and the algebra says why: his packets
arrived inside their budget, so their offset *was* his `wantdelay`, 6 or 7.
With `wantdelay = 0` the receiver falls through to `timegap`, which on a
loopback is 0. Predicted in 8.25, and this time the independent variable
actually moved, so the reading counts.

⚠ **`+2` is still there -- 2557 of 6403 -- and it is now the open question,
narrowed rather than answered.** It is not (mostly) the host's gentleman's
delay, since exempting him cost it only 778. But it is hard to attribute to
the remote client either: an offset of exactly 2 requires `wantdelay = 2`
*and* `timegap < 2`, and that client runs under `rollback_lag 6`, so its
`timegap` should never be below 6 -- its own traffic is visible at `+7`/`+8`.
**A third possibility is not yet excluded and no guess is recorded here.**
The instrument that settles it is the one 8.24 already named and this section
does not replace: log `lagDelay` where it goes on the wire
(`d_clisrv.c:6788`), tagged by sender.

**Unchanged and worth stating:** zero `Game state reloaded` in both logs,
three races running. The exemption did not destabilise anything.

⚠ **What has still never been measured is the thing the fix is for.** Nobody
has hosted a listen server on this build and said whether it feels
responsive. Every reactivity judgement in this document -- *"ça répond tout
de suite"*, five races, twice -- was made from the client's seat. **This one
needs somebody at the controls, on the host, and it is the first entry in
"What needs somebody at the controls" that the bench cannot fake.**

### 8.28 Audit, 2026-09-21: two gaps found by reading the code against this file

Read, not measured. Nothing was launched.

**1. The roulette fields of 8.14 break the compatibility policy.** `baseDist`,
`firstDist`, `secondDist` and `secondToFirst` are written and read
unconditionally in `P_NetArchivePlayers`/`P_NetUnArchivePlayers`
(`p_saveg.cpp:927-930` and `:1721-1724`). So they are part of the netgame
savegame a server sends to a joining client, not only of local snapshots.
`PACKETVERSION` is unchanged (`d_clisrv.h:39`), so the version checks
(`d_clisrv.c:1681`, `:4547`) still pass between this build and a stock one --
and then the savegame is misread by 16 bytes a player, for every player, from
the roulette onward.

Against the policy decided the same day (the server decides):

- a WORLDWIDE client joining a vanilla server reads 16 bytes a player that are
  not there -- **the case the policy promises works**;
- a vanilla client joining a WORLDWIDE server reads a stream 16 bytes a player
  too long, where the policy wants it refused cleanly;
- a WORLDWIDE build hosting in vanilla mode sends the longer stream to vanilla
  clients.

The smallest change that satisfies all three: write and read the four fields
**only in a local snapshot**, gated like `tilt` and `rollangle`. The rollback
keeps what 8.14-8.15 measured, and the wire goes back to stock grammar in every
mode. What it gives up is a joining client receiving those four values, which a
stock client never received either. The clean refusal of vanilla clients by a
WORLDWIDE server is separate work (`ROADMAP.md`, *Compatibility*). **Done in
code on 2026-09-21, see 8.29.**

`PT_STATECORRECTION` is not a problem: it is appended at the end of the packet
enum (`d_clisrv.h:144`), so no stock packet number moved.

**2. A gap between 8.19 and 8.20.** 8.19's input log shows the client's
confirmed world running a different `turning` than the server's for the human
kart, on the same tic (tic 2309: `0` against `-800`). 8.20 attributes that to
the server relabelling an arriving ticcmd from `realstart` to `faketic`.

But relabelling decides which tic the *server* files an input under, and the
client does not keep its own filing. Every tic the client's confirmed clock runs
was first overwritten with the server's ticcmds for every slot:
`D_Clearticcmd(i)` then `G_ScpyTiccmd` over `numslots` (`d_clisrv.c:6006-6013`),
and `neededtic` only advances through that branch. Both confirmed clocks should
therefore run the server's filing, relabelled or not, and fold the same tic
number into the hash.

So either the two logs are not measuring the same thing, or an input reaches a
confirmed tic on the client by a path this file has not found. Relabelling may
still explain the `+N` histogram; on this reading it does not explain two
confirmed worlds disagreeing. **This is the live lead for Phase A.**

The instrument that settles it records three values per tic for the human
kart: on the client, `netcmds[T][slot]` when the `PT_SERVERTICS` copy writes
it, and again immediately before `G_Ticker` reads it; on the server, the same
slot when tic T is packed for sending. The first of the three to disagree names
the path. It needs a build and one driven race -- a launch, so asked for first.

**Also found while reading, and moved to `ROADMAP.md` (backlog):**
`botvars.diffincrease` archived as a byte (found in §6, still open); the out-of-bounds read
in the bot-overwrite search (`d_clisrv.c:4120`, upstream); and the harness not
being versioned, which blocks every measurement in this file on any machine
other than the original one.

### 8.29 The roulette fields go back to local snapshots only

Code, not measured. Nothing launched.

`baseDist`, `firstDist`, `secondDist` and `secondToFirst` are now written under
`if (localsnapshot)` and read under `if (localrestore)` -- the same pair of flags
that already gates `cmd`, `oldcmd`, the chain order and `floordrop`. Both flags
are set before the players block is archived or unarchived (`P_SaveNetGame`,
`P_LoadNetGame`), and the two player archivers have no other caller.

Checked by reading the whole `p_saveg.cpp` diff against `05cca02c9`: every other
read or write this branch adds is already gated the same way, and `tilt`,
`rollangle` and `livestudioaudience_timer` are *skipped* locally but still
written for the wire. So **the netgame savegame is stock grammar again**; what
remains are the semantic differences listed in `ROLLBACK.md`'s wire-format audit
(values, not layout). No packet layout changed either: `PT_STATECORRECTION` is
appended, and the one removed header field (`SIGNGAMETRAFFIC`) was dead code,
removed upstream (`26b114339`, Kart Krew).

What this does not do: refuse a vanilla client on a WORLDWIDE server, or switch
a client's mode automatically. Both stay in `ROADMAP.md`, *Compatibility*.

**To verify, each a launch to be asked for:** `soak_leak.cfg` at 0 failures as
in 8.15 (the four fields are still in every local snapshot, so the roulette leak
must not come back); then a join in each direction against a stock build of the
same base (see 8.30 for why it must be the same base).

### 8.30 Four facts found while preparing the next proposals

Read, not measured.

1. **A CI build cannot see a public server.** The workflow builds with
   `SRB2_CONFIG_DEV_BUILD=ON`, which defines `DEVELOP`, and under `DEVELOP`
   `VERSION` and `SUBVERSION` stay 0 (`d_main.cpp:1487-1490`). The server
   browser and the join both compare them (`d_clisrv.c:1684-1688`, `:4547`). So
   a WORLDWIDE CI build and a v2.4 release server never see each other --
   which is why 8.28's misread never bit anyone. The branch is also based on
   `v2.4-106-g05cca02c9`, upstream's development line, not on a release tag.
   The policy's "a WORLDWIDE client on a vanilla server" needs a release-config
   build on the release base the public servers run.
2. **8.19 is not an instrument that logs speculation.** `K_RollbackNoteInput`
   is called only in the authoritative loop (`d_clisrv.c:7390`);
   `K_RollbackSpeculate` drives `G_Ticker` directly (`k_rollback.c:4359`).
3. **Nor is the client labelling its packets with a speculated tic.**
   `K_RollbackUnspeculate` runs before `NetUpdate` (`d_clisrv.c:7179-7182`) and
   the label is `lastconfirmedtic` (`:6686-6689`).
4. **The speculation writes into `netcmds` and nothing undoes it.**
   `K_RollbackPredictInputs` writes guesses -- and this machine's live input,
   flagged `TICCMD_RECEIVED` -- into `netcmds[T]` for the speculated tics.
   `K_RollbackUnspeculate` restores the world, but `netcmds` is not in the
   archive. Every tic the confirmed loop runs should first be overwritten by
   the server's copy (`d_clisrv.c:6006-6013`, all `numslots`), so on reading
   this is harmless -- but it is the one piece of speculative state known to
   outlive the speculation, and the offline leak check (8.10) cannot see it,
   because it perturbs `players[].cmd`, not `netcmds`.

And one for Phase B: **`P_RelinkPointers` is quadratic.** Every relinked pointer
calls `P_FindNewPosition`, a linear scan of every mobj for a matching `mobjnum`
(`p_saveg.cpp:5070-5088`). With about two thousand objects and several pointers
each, that fits it being 4.7 ms of an 8.6 ms restore.
