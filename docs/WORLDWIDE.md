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

**Unmeasured, and what to look for.** Whether the drift curve is flat or
diverging decides everything after it: flat means corrections alone make this
shippable and the remaining bug is cosmetic; diverging means a correction every
four tics is fighting a leak and the leak still has to be found. Either way the
number replaces nine years of resync counts with a distance in units.
