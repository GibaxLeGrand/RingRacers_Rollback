# Ring Racers Worldwide -- the design, and what the code does today

Written 2026-09-10, from Gibax's design statement, checked line by line against
this branch and against stock Ring Racers. Everything below is either read from
source (with a `file:line`) or measured (labelled as such). Where the statement
and the code disagree, the code is quoted.

Companion documents: `ROADMAP.md` is what is left and `COMMANDS.md` is the
command reference. These three files are kept **identical** in the public code
repository (`docs/`) and in the private notes repository
(`RingRacers_Worldwide-notes`, `docs/`): edit one, copy it to the other. The
rest lives in the private notes only -- `README.md`, the entry point (working
rules, decisions, environment); `ROLLBACK.md`, the closed journal from before
the pivot; and `AUDIT_20260909.md`, the comparison with SRB2 NetPlus and Odamex.

## Current state (2026-09-30, evening) -- read this first

This block is the only part of this file that is rewritten to stay current.
Everything after it is a dated journal: when a later section overturns an
earlier one, the earlier one gets a ⚠ pointing forward, and is not rewritten.

**Architecture.** Client-side prediction with server reconciliation -- not GGPO
rollback. Two clocks: `gametic` runs only the tics the server has confirmed, in
unmodified lockstep; the speculation runs above it from a snapshot and is
rebuilt every pass -- or, with `rollback_keepspec` (8.73), kept as it stands
when the server confirms the inputs it ran, and extended by a tic.
`rollback_history` (8.39, 8.41) replays the inputs still in flight instead of
repeating the newest, and R1 (`rollback_histreal`, 8.89) gives each sample the
tics the server gives it; with the history the speculation is as deep as those
inputs reach, one tic at least -- `rollback_twoclock` is no longer its floor
(8.106). A light correction channel (`PT_STATECORRECTION`, server to client
every `rollback_correct N` tics) puts back each kart that is not already where
the server has it (8.75, 8.77), in place of the stock full-state resend. B2's
`rollback_rawsnap` (8.88) saves and restores the level's four object pools as
raw memory instead of through the network archive. What is drawn is
interpolated between kept passes as between tics (8.102). A tic's sounds and
chat lines are heard the first time this machine runs it, never again on a
rebuild (8.73), and the rule holds inside a tic only, so the menus are heard
(8.108, 8.109).

**Switches.** One server switch, `worldwide On` (8.80), advertises the mode,
sends light corrections in place of resends and refuses clients that do not
declare themselves WORLDWIDE; a client that joins such a server switches on
`rollback_twoclock 4`, `rollback_history 12`, `rollback_keepspec` and applied
corrections, and puts its settings back when it leaves (8.80; the join and a
race run end to end in 8.97, the leave never checked).
Outside that mode every piece is off by default except `rollback_cleancmds`
(8.36) and `rollback_histreal` (8.89). Off by default and waiting:
`rollback_rawsnap` (open item 7), `rollback_keepearly` (8.99, 8.100),
`rollback_smooth` (never measured).

**Builds.** Installed on the measuring machine: `093a79aeb` (sha256
`ce744500…`, CI run 36833865387), with the private notes' `titre/worldwide.pk3`. It
carries:
- the fix for the join's `MT_PLAYER` alerts (`0412e7760`, 8.112),
  **measured at the join** (8.118): 0 bodies below zero against 2 for
  `04db0cf` in the same session; the case in the middle of a race not
  reached;
- the `WORLDWIDE Mode` menu entry, saved, and its state on the host screen
  (`89aba69fb`, seen in the game in 8.113);
- the title's second pass: flash at 0.6 s, and space behind the globe,
  over the level the stock title runs behind its art (8.113).

The previous measuring build is kept as `.bak_04db0cf`, and `89aba69fb` and
`2209b7130` as `.bak_89aba69` and `.bak_2209b71`. Gibax's own release-config build of `04db0cf` sits beside
them as `ringracers_release_rollback_netcode.exe`. Beside them too, the 2.4
branch's builds: `ringracers_azerty-2.4.exe` (`0a9877dd1`) and
`ringracers_worldwide-2.4.exe` (`68f5eb582`, sha256 `2c53237e…`, the one
that starts in a stock 2.4 folder, 8.117). `rollback-netcode` itself is at
`1fcef131b` (`azerty` merged, CI run 36901738576 green), not installed.

**Measured and holding.**

| | |
|---|---|
| input lag, client's seat | gone -- "ça répond tout de suite", seven races, both cleancmds races included (8.36); none felt at any latency from 0 to 428 ms: "parfait" (8.107) |
| the picture | the kart and the view drawn in even steps, as without prediction, since kept passes are interpolated (8.102, 8.103): "largement plus fluide". A few more long steps than without prediction: 430 to 760 a window, against about 263 (8.103, 8.107) |
| latency, 0 to 428 ms | the depth follows the round trip up to 285 ms, and nothing is guessed: this machine's wrong inputs 0 in 11 of 12 race windows, 1 in the other. At 428 ms the history's cap of 12 cuts every pass, and the drawn world is about 5 tics behind the newest input (8.107) |
| snapshot determinism | 12/12 replays byte-identical; 0/330 resim checks (8.9); network-restore leak soaks 0 or 1 failure in about 300 checks on Skyscraper Leaps, Opulence, Coastal Temple and Carnival Night, the one failure being `itemList.cap`, known and harmless (8.34, 8.59, 8.62, 8.94) |
| confirmed-tic inputs | with `rollback_cleancmds`, 0 to 0.1% of confirmed tics run an input the server did not, against 64-85% (local kart) and 23-80% (bots) without it (8.35, 8.37) |
| full-state resends | 7 to 9 a race without the channel, **0** with it (8.6, 8.8, 8.16), none in a WORLDWIDE race (8.97) |
| residual drift | **0.000 units in every driven race since 8.94** -- Opulence with `keep` and `keepraw`, Skyscraper Leaps in WORLDWIDE mode: no kart state field off and no kart put back, 6696 to 10305 kart samples a race until 8.99 and about 6000 in the three windows of each race since (8.94, 8.95, 8.97, 8.99, 8.107, 8.109, 8.110). First read on Skyscraper Leaps with a same-session control that does diverge (8.44, 8.45) |
| this machine's wrong inputs, R1 | **0 in three windows on Opulence**, 99.4 to 100% of passes kept, against 29 to 125 a window and a loop in the control without R1 (8.92); 0 to 2 a window in the WORLDWIDE races since (8.107, 8.110) |
| rebuilds, WORLDWIDE mode | in a race, 0 to 2 a window of 1000 tics, driven, Skyscraper Leaps (8.99, 8.107, 8.109, 8.110); at the race map's start, 1 to 9 in its first 800 tics, against 265 with the depth floor (8.105, 8.107) |
| cost of a kept pass, driven | Opulence: **6.9 to 7.6 ms, 127 to 131 frames a second** with network snapshots (8.94); **5.6 to 6.4 ms, 133 to 137** with raw ones (8.95). Skyscraper Leaps in WORLDWIDE mode: 1.9 to 2.5 ms, about 144 frames a second, **the same at every latency from 0 to 428 ms** (8.99, 8.107). A rebuilt pass on Opulence was 25 to 35 ms (8.62-8.77) |
| a save / a restore, Opulence | network archive: 2.5 to 2.9 ms / about 6.5 ms; **raw: 1.1 ms / 1.9 ms** (8.95) |
| raw snapshots, exactness | Opulence soaks 0 of 278 and 0 of 379 failures; the archive identical after 1668 of 1668 and 1128 of 1137 restores (8.94) |
| WORLDWIDE mode | the declared client joins and switches on, an undeclared one is refused, a race runs to its end at 0.000 with no full-state resend, and a predicting client records no replay (8.97) |
| sounds, WORLDWIDE mode | a tic's sounds heard once, the first time this machine runs it (8.73); the menus and the title card heard again since the rule holds inside a tic only (8.109) |
| listen-server host's input delay | 170-200 ms, now **0** (8.27) |
| restore, relink step | 4.7 ms before the index, **under 0.1 ms** after (8.34) |

**Open, in priority order** -- the audit of 2026-09-30 of what an alpha still
needs; `ROADMAP.md`, *Next, in order*, has the order of work and the proposed
scope.

1. **The `MT_PLAYER` alerts at the join: explained (8.112).**
   - On the client, 1 to 18 kart bodies a race went to -1 (8.97, 8.110):
     the joiner's old body, a bot removed to make room, and now and then
     one in a race.
   - The load of the network archive claims each body for `players[].mo`,
     then its caller's `P_AddThinker` sets the count back to 0.
   - Every body a rebuild or the join's load brings back is one reference
     short, which can end in a use after free.
   - Fix pushed as `0412e7760`, installed, **not yet measured**: a client
     nobody drives never joins the race, so the bench cannot reach the path
     (8.113). It needs a driven pair with a control in the same session, or
     a console command that joins (8.113).
2. **A second human on two machines**: never run, the largest unknown. A
   remote human is guessed by repeating their last input, which may multiply
   rebuilds and make their kart shake. A cheap stand-in, not written: a
   switch that guesses the bots as if they were remote humans. Then a person
   driving on the host.
3. **A real network**: `rollback_lag` only delays, with no jitter and no
   loss. R1 is exact only in that case; R2 (samples filed by sequence number)
   is the remedy for a real one, and the harness needs jitter first.
4. **Vanilla compatibility**, against the policy below: a release base -- CI
   builds a release-config exe, but the branch still sits on upstream's
   development line (8.30). **Ported onto v2.4 and built (8.114, 8.115):
   the branch `worldwide-2.4`, not yet run;** a WORLDWIDE client on a real vanilla server,
   which needs that base; a stock client refused by a WORLDWIDE server (so
   far `rollback_vanillajoin` stands in for one); the leave. The savegame
   misread of 8.28 is fixed in code (8.29), never checked against a stock
   build.
5. **Sixteen karts** late in a race, Phase B's gate: measured up to nine. A
   speculated tic on Opulence is 3.4 to 4 ms, 83% of it the map's
   decorations (8.66, 8.69) -- the tic itself is now most of a kept pass
   (8.95).
6. **The history's cap** past about 340 ms of round trip (8.107): raise it
   (up to 34) or set it from the round trip; a rebuild then goes deeper.
7. **B2, before `rollback_rawsnap` goes on by default**: the players-block
   difference after 9 of 1137 raw restores on Opulence (8.90, 8.94), not
   explained; the double claim, one kart rebuilt at 67 references against 66
   live (8.94); `floorspriteslope`, which after a raw restore may point at a
   freed plane (only Lua creates one, 8.93); the level interpolators, which a
   raw restore does not rebuild (8.88, drawing only).
8. **R1's two gaps** (⚠ under 8.89): the depth is taken from the samples in
   flight, not from the tics R1 gives them, so the newest input may reach the
   drawn world a tic or two late; and the instrument counts some replayed
   tics as guessed. The measuring machine's local `wip/histgaps` is a
   reference for both.
9. **Never run under prediction**: Battle, Grand Prix, Encore; items used on
   purpose; a driven race on any map but Skyscraper Leaps and Opulence
   (soaks ran on seven maps, 8.51-8.62, 8.94).
10. **Small, seen, not blocking**: "`*Guest entered the game.`" printed 1 to 5
    times a join on the client, 17 at 15 tics -- each copy in a new world
    where the join, driven by this machine's input, landed later, each
    rebuild moving it (8.109, 8.112). The drawn kart's extra long steps (above). `rollback_keepearly`,
    off, to remove or keep. A predicting client records no replay (8.96).
11. **Older, left open**: the Garden Top ride (Carnival Night resim, 2 of 347,
    8.56); Coastal Temple's 6 resim failures, not analysed (8.57);
    `chainorder_block` (8.59); an object removed in a speculated tic stops its
    sounds (8.73); the cameras put back by every restore, a lead for the
    camera's stutter on rebuilds, not checked (8.78); the one divergence in
    2000 history tics (8.46), not seen since (8.50); the network load not
    counting a delayed executor's caller (8.88); the relabel histogram's `+2`
    cluster, probably harmless (8.27, 8.44).

**Compatibility policy, decided by Gibax on 2026-09-21: the server decides.** A
server in WORLDWIDE mode runs client-side prediction and accepts WORLDWIDE
clients only. A vanilla server runs the stock delay-based netcode, and a
WORLDWIDE client that joins it behaves exactly as a vanilla client. This
supersedes 8.4 and 8.14 wherever they say stock compatibility is "given up" or
"abandoned".

**The test harness is versioned in the private notes repository** (`harnais/`,
since 2026-09-21), never in the public one: it carries local paths. The
scripts run the scenarios from there, take the game folder from an environment
variable, print whether the installed exe is the code repository's HEAD, and
keep every run's logs under a name dated and tagged with the exe's sha. **No
launch without Gibax's explicit go-ahead, each time.**

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
| 8.14 | "vanilla compatibility already abandoned" is superseded by the policy; see 8.28. Its roulette-leak fix is itself incomplete: `playing`/`exiting` were missed (8.34) |
| 8.18 | its candidate (the server guessing a remote client's input) is refuted in 8.20 |
| 8.19 | "this is the mechanism" is withdrawn by 8.20, and 8.20 has a gap (8.28) |
| 8.23 | its reading of the client exemption is retracted in 8.25 (marked inline) |
| 8.25 | its fix was a no-op, explained in 8.26 and replaced in 8.26-8.27 |
| 8.28 point 2 | the gap it describes is explained by 8.31 |
| 8.30 point 4 | "harmless on reading" is wrong: the speculation starts on a received tic (8.31) |
| 8.31 | its mechanism is seen at full scale in the 2026-09-20 logs, and on the bots as well, which its counters do not count (8.33) |
| 8.33 | its inputs prediction is confirmed at race scale, twice. Its drift prediction cannot be judged by the off/on/off protocol it wrote (8.38). Its feel risk did not materialise (8.36) |
| 8.39 | its adaptive depth made the drawn tic follow the server's filing jitter (8.40); replaced by a held lead over the clock (8.41), so its depth figures no longer apply |
| 8.32 | its prediction fails on its label -- `RR_TESTRUN` counts as a race -- and the cluster falls before every measurement window, on the counts (8.44) |
| 8.38 | its `rngsum` clause could not be read: no blame line printed with the correction channel on (8.42). Readable from a build with 8.43's once-a-second lines. Its prediction holds on every clause (8.44), and its inheritance is seen directly in the same-session control (8.45) |
| 8.42 | its proposed server line in the suppressed branch was left out of 8.43 |
| 8.40 point 1 | its map figures hold on the measuring machine's install (8.42) |
| 8.40 point 2 | the proposed "held depth" was built as a held *lead over the clock* instead (8.41) |
| 8.39, 8.41 | "it only changes what is drawn" and "drift unchanged between windows" fail in the first run: the confirmed world parted inside the history window (8.46). They hold in the second (8.50). Its display-side predictions mostly hold; its depth and cost figures do not |
| 8.40 point 3 | `rngsum` is not in the logs to be read (8.42) |
| 8.55 | its pairs are named in 8.57: the trick-panel timing visual, placed from the camera. Its candidate (the bubble waves) was wrong |
| 8.57 | its three fixes are pushed, with a fourth (8.58). Its polyobject mechanism is wrong (8.59): the ownership fix changed nothing on Coastal Temple |
| 8.35, 8.37 | their input results stand. Their drift readings -- "no effect" and "a large one" -- are both confounded: an off window's divergence carries into the on window (8.38) |
| 8.80 | "not yet run": run in 8.96, where both scenarios crashed at the waiting map's restart, and end to end after two fixes (8.97) |
| 8.89 | two gaps are noted under it (⚠): the depth and the instrument still count samples, not the tics R1 gives them. Its predictions are measured in 8.92 |
| 8.90 | its three differences are read in 8.91, fixed in 8.93 and measured in 8.94: the slope planes and the sign are gone, the kart's reference mostly (a double claim left), the players-block difference stays |
| 8.97 | its reading of the stutter -- rebuilds on guessed tics -- is wrong: those rebuilds were before the race, not in it (8.99) |
| 8.98 | its predictions fail (8.99): there was nothing in the race for the early rerun to remove; `rollback_keepearly` is off by default since 8.100 |
| 8.99 | its figures hold; its leads for the stutter -- the frame pacing, an uneven head, the camera, the corrections -- are superseded by 8.102: no interpolation at all while a speculation was kept |
| 8.101 | its remedy, stopping prediction in the intro, had nothing to act on in the bench's Free Play races, which have no intro and no POSITION (8.104); the start's rebuilds came from predicting on a loopback under the depth floor (8.105) |
| 8.104 | the stray keyboard explains 8.103's other-human rebuilds; this machine's own rebuilds at the start came from the loopback and the floor (8.105) |
| 8.106 | "about 3 tics" late at 15 tics of lag is about 5: the round trip there is 17 (8.107) |
| 8.108 | the 17 copies were the 15-tic race's, the sweep's others wrote 1 to 4; the fix brought the sounds back, not the join line down to one (8.109) |
| 8.109 | its hypothesis for the extra join lines is not yet tested: the next race wrote the line once (8.110) |

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
4. ⚠ *The conclusion of this point ("harmless") is wrong -- see 8.31, found the
   same day.* **The speculation writes into `netcmds` and nothing undoes it.**
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

### 8.31 The speculation overwrites the local input of tics already received

Read, not measured. **The best candidate yet for the drift**, and it closes the
gap of 8.28 point 2.

8.30 point 4 said the speculation's writes into `netcmds` are harmless because
every confirmed tic is first overwritten by the server's copy. That holds only
if the speculation starts at `neededtic`. **It does not**:

1. The netticbuffer reserve at the end of `TryRunTics`' loop breaks out when
   `neededtic <= gametic + cv_netticbuffer.value`, gated on
   `K_RollbackPredictAhead() == 0` (`d_clisrv.c`, "Leave a certain amount of
   tics present in the net buffer"). Two-clock mode sets `g_loopahead` to 0, so
   **the reserve is active under two-clock**: whenever a pass has two or more
   tics to run, the confirmed loop stops one short (`netticbuffer` defaults
   to 1), with that tic already received.
2. `K_RollbackSpeculate` then starts from that frontier, and calls
   `K_RollbackPredictInputs` on the received tic. Remote players and bots are
   skipped when their slot carries `TICCMD_RECEIVED`, but **the local player's
   slot is overwritten unconditionally** with `D_LocalTiccmd` -- what the
   player holds *now*.
3. `K_RollbackUnspeculate` restores the world, not `netcmds`. The packet for
   that tic has already been processed and will not be copied again. So the
   next pass runs that tic **as confirmed** on the client's current input,
   while the server ran the input it had assigned to it.

Every observation fits:

- 8.19's input log: the client's confirmed tic "folded its own input the instant
  it is made"; the server caught up tics later.
- The local human's kart owns most spikes (8.8: 30 of 40 on p8). Other karts
  drift only through contact.
- Nobody driving means the current input equals the assigned one: no drift
  (8.1 before correction, the undriven control races).
- `nospec` never calls `K_RollbackPredictInputs`: 0.000 units (8.9).
- The offline leak check perturbs `players[].cmd`, never `netcmds`, and runs no
  reserve: it cannot see this (0/522).
- 8.17's "the confirmed clock never runs on a guess" stays true -- it runs on
  a real input, the wrong one for that tic.

**Built, off by default:** `rollback_cleancmds 1` makes `K_RollbackPredictInputs`
return untouched on any tic below `neededtic` (exposed as `D_NeededTic()`), so
received tics stay exactly as the server sent them. Counted either way: how many
local inputs were (or would have been) written over a received tic, and how many
of them differed from the server's -- the second count is the size of the
effect, and it should be 0 when nobody drives.
⚠ *The counts cover the local players only. The same write hits every bot on
that tic, which the switch also stops but the counts do not see -- see 8.33.*

**Prediction written before any run:** with `rollback_cleancmds 0` in a driven
race, the changed count is non-zero and grows with steering; with it on, mean
drift falls well below the 0.25-0.86 units measured so far, spikes on the local
kart mostly disappear, and 8.19's input hashes agree. If drift does not move
with the count non-zero, this reading is wrong.

Another fix would be to stand the reserve down in two-clock mode as well, so the
speculation always starts at `neededtic`. Not done: it changes the confirmed
loop's pacing, which is a second variable.

### 8.32 A hypothesis for the `+2` cluster, and the split that tests it

Read, not measured.

`K_RollbackCorrectingHere()` and `K_RollbackTwoClockConfigured()` both require
`gamestate == GS_LEVEL`. Outside a race the host's delay exemption is off, so
its `target_lag` is the `cv_mindelay` floor, 2 -- and 8.27's own first print
shows exactly that: `target_lag -> 2 (... gamestate 13)` before the race. The
host's clientpak goes through its own loopback, where `timegap` is 0 or 1, so
the receiver files it at `wantdelay`: **exactly +2**. The host runs the whole
session -- waiting in the menu for the client, the race, whatever follows --
so it contributes far more packets outside a race than the client does. The 778
that the exemption removed from `+2` would be the start of the race, before the
host's ping was measured.

If that is right, the cluster is harmless: no race is being delayed.

**Built:** `rollback_relabel` now also prints the histogram split four ways --
host or remote, in a race or not (`node == servernode`,
`gamestate == GS_LEVEL`). Prediction: `+2` sits almost entirely in "host,
outside a race". If it sits in "remote, in a race", this reading is wrong.

> ⚠ **2026-09-23 (8.44): it sits in "host, in a race" and "remote, in a
> race"**, because the waiting map `RR_TESTRUN` is a level. On the counts, it
> is the tics before the scenarios' settings, not a race being delayed.

### 8.33 The 2026-09-20 logs already show 8.31, at full scale and on the bots too

Read from logs already on disk, and from the code. No new run.

The harness went into the private notes repository on 2026-09-21, and its
first new piece is a report that compares, **tic by tic**, the input each
player ran on a confirmed tic on the client against the one the server ran on
the same tic (`rollback_input:` lines, both logs). The running hash in the
`rollback_drift` report cannot do this: the 2026-09-20 race printed equal counts
and different hashes from the very first report, because the two machines
start folding at different points, and once a running hash parts it never
agrees again. Tried first on the 2026-09-20 driven `correct` race (binary
`4e64140`, before `rollback_cleancmds` existed), tics 2192-3722, and the
alignment scanned from -12 to +12 tics per player:

| player | same tic | best shift |
|---|---|---|
| p8, the client's own kart | 279 of 1530 agree | **+7: 1432 of 1523 (94%)** |
| p1-p7, bots | 5263 of 10711 agree (49%; 31% to 73% per bot) | 0 -- no shift does better |
| p0, the idle host | all | uninformative: a constant input matches at any shift |

The server's log stopped at its 20000-line cap at tic 3722, so the last 33 tics
of the client's window had nothing to compare against (304 lines); they are
left out, not counted as agreeing.

**The client's own kart.** On 94% of its confirmed tics it ran, at tic T, the
input the server filed at T+7 -- `rollback_lag 6` plus the reserve's one tic.
8.31 described the speculation writing the current input over *a* received tic;
this says it is **nearly every** confirmed tic, which is what 8.31's mechanism
gives when each pass runs one confirmed tic and stops one short: every
confirmed tic was the first speculated tic of the pass before. The confirmed
world plays the local kart's whole input stream seven tics early, and the
correction channel pulls it back every four tics. Worst drift sits on p8 in
every report of that race (6.9, 28.1, 40.9 units).

**The bots -- the second face of the same mechanism.** A bot's ticcmd is built
by `K_BuildBotTiccmd`, which clears the command and sets `TICCMD_BOT` only
(`k_bot.cpp:2083`, `:2087`); `TICCMD_RECEIVED` is never set, on the server or
anywhere else. `G_MoveTiccmd` copies the flags verbatim (`g_game.c:1026`), so a
bot's input arrives at the client without it. `K_RollbackPredictInputs` skips
a slot only when it carries `TICCMD_RECEIVED`, so on the received tic the
speculation starts on, it **recomputes every bot's input from the client's own
world** and writes it over the server's. The next pass runs that tic as
confirmed on it. That is the half of the bots' ticcmds that disagree, at shift
0 -- not a relabel, a recomputation. Why a recomputed input differs at all is
read, not measured: the server builds a bot's input in `SV_Maketic`, which
`NetUpdate` calls once per elapsed real tic in a loop (`d_clisrv.c:8116`), all
from the world as it stands at that moment; the client rebuilds it from its
world after T-1 (in which, in a driven race, the local kart also runs seven
tics ahead). The first difference needs no driver, which would fit the 10/09 undriven bot race (`botdesync`, 2-3 resyncs
with nobody at the wheel).

`rollback_cleancmds 1` covers both faces: it returns before either loop on any
tic below `neededtic`. But **its counters see only the first**:
`K_RollbackCountReceivedWrites` (`k_rollback.c:5146`) counts local players, not
bots. The per-tic comparison is the instrument for the bots.

**The race scenario, rebuilt for this** (private notes, `harnais/`): three
windows of 1000 tics, `rollback_cleancmds` **off, on, off**. The third window
is the control for the race phase -- the 2026-09-20 race's mean drift went
0.152, 0.269, 0.334 over its three reports, so a drop between two windows of
one race proves nothing on its own. At each boundary the client prints its
reports, flips the switch, then resets the drift, cost and input-log counters
(`rollback_drift 1`, `rollback_twoclock 4`, `rollback_inputlog 1`), so each
window reads alone. The server re-arms its input log at every report, so the
per-tic log covers the whole race.

**Prediction, written before the run** (binary `7b8d605`):

- **Off windows:** at the same tic, most of p8's inputs and about half of the
  bots' disagree with the server, as on 2026-09-20; p8 agrees at +7.
  `rollback_cleancmds` reports a non-zero "different from the server" count
  that grows with steering.
- **On window:** at the same tic, p8's and the bots' inputs agree with the
  server's on every tic, bar a tic or two at the switch. Mean drift well below
  the off windows', and p8 no longer owns the worst sample.
- **Cost:** unchanged by the switch; each pass does the same work.
- **Feel -- the risk, not a prediction of success.** With the switch on, the
  confirmed world plays the local input when the server does, about seven tics
  after it was made, and the speculation covers only four tics above the
  confirmed frontier, with the *current* input repeated. So a turn should start
  on screen at once, then its rate should lag by about 0.2 s. The off windows
  felt immediate partly *because* of the defect: every sample was applied the
  tic it was made, on the confirmed world. If the on window feels worse, the
  answer is not to switch it back off: it is to speculate at least lag + 1
  tics, replaying the local input **history** tic by tic rather than the latest
  sample repeated (Quake 3 replays every command the server has not
  acknowledged). Not built; to be decided after the race.
- If the inputs agree in the on window and drift does not move, 8.31 is right
  about the inputs and wrong about the drift.

**Also prepared for the leak soak:** `soak_leak.cfg` now stops the soak, then
runs one `rollback_test` late in the race, whose restore profile prices the
relink index (8.30). Prediction: 0 failures, as in 8.15, and the
`relink pointers` step under 0.5 ms against 4.7 ms before. It may not print at
all: `K_PrintLoadProfile` hides steps under 100 us.

### 8.34 `soak_leak.cfg` broke its own prediction: two more roulette fields were never archived

Measured (binary `7b8d605`, unattended, `soak.sh leak`), against a prediction
of 0 failures written down in 8.33. **Result: 261 checks, 2 failures** -- at
leveltime 1400 and 1520, both "a pass on the REAL inputs already changed the
tic that followed it" (`rollback_leak`'s honest-pass check, not the
misprediction one). The prediction was wrong; this section is why.

Both failures name the same two fields, once each: a byte at 672 into
`player_t` going `00`->`20`/`00`->`ff`, and one at 680 going `00`->`08`/`00`->
`20`. The memory-comparison walker (`K_ComparePlayers`) that printed them
stops short of the roulette, unlike the archive-stream walker
(`P_NamePlayerField`), so it names them only by struct offset. Read against
the `.pdb` with `cdb -c "dt player_t"` (`itemRoulette` starts at `+0x288`,
`itemroulette_t` from `d_player.h`): **672 is `itemRoulette.itemList.cap`,
680 is `itemRoulette.playing`.**

`itemList.cap` is deliberately not restored to the snapshot's value when the
block is already large enough (`p_saveg.cpp`, the comment above the read code:
"both passes of a check share this allocation... unaffected"). That holds for
`rollback_test`'s round trip on one allocation. `rollback_leak` runs the *same*
tic three times over three restores of the *same* player array, so a genuine
allocation growth during the honest pass's detour is exactly what changes
`cap` between the reference and the honest-pass snapshot -- not a leak, the
comment's own reasoning working as designed, just not anticipated for this
checker. Left alone.

**`itemRoulette.playing` is a real leak.** It is never written or read by
`P_NetArchivePlayers`/`P_NetUnArchivePlayers` at all -- not gated by
`localsnapshot`, just absent, alongside `exiting`. Read by
`K_GetItemRouletteDistance` (item-odds distance, `k_kart.c:15065`, `:15457`,
`:17810`, `:17812`) and mutated every tic the roulette spins
(`k_roulette.c:771`, `:796`). Exactly the class of bug 8.14/8.15 fixed for
`baseDist`/`firstDist`/`secondDist`/`secondToFirst`: "neither archived until
now, so a restore mid-roulette left them holding whatever the world last
computed rather than what this snapshot actually had" -- except these two were
simply missed when that fix was written, not decided against.

**Fixed** (`p_saveg.cpp`, not yet measured): `playing` and `exiting` now
written and read alongside `preexpdist`/`dist`, gated by
`localsnapshot`/`localrestore` like `baseDist` and its siblings -- the netgame
savegame keeps stock grammar (8.28), and `k_rollback.c`'s ring already saves
local (`P_SaveNetGame(&save, true, true)`), so the fix reaches the checker
that found the gap.

**The same run also confirms 8.30's relink index**, unasked: the restore
profile it printed at the end (`misc` 160us, `thinkers purge` 322, `thinkers`
549, `colormaps` 237, `waypoints` 173, `chain order` 647) has no
`relink pointers` line at all -- `K_PrintLoadProfile` hides steps under 100us,
so the step that cost 4.7 ms before the index now costs under a tenth of a
millisecond. Better than the under-0.5ms prediction in `ROADMAP.md`.

**Re-run the same evening, binary `3400299` (HEAD, sha verified): 1 failure in
260 checks, not the predicted 0.** But the prediction's own escape clause is
exactly what happened: the one surviving failure names byte 672 alone --
`itemList.cap` -- with **no byte 680**. `playing` never came back. The fix
closed the leak it was written for; `cap`'s occasional, expected drift is what
is left, matching the reasoning above rather than contradicting it.

The relink index (8.30) holds too: the restore profile from the same run again
has no `relink pointers` line.

### 8.35 The driven cleancmds race: the inputs prediction lands, the drift prediction does not

> ⚠ **The drift half is confounded (8.38).** The on window inherits whatever the
> off window before it put out of step, and the correction channel repairs only
> kart kinematics. The inputs half stands.

Measured (binary `bee33d9`, sha verified, `playtest.sh correct`, driven by
Gibax). Predictions were written in 8.33 before the run.

**The inputs.** `cleancmds_report.py` compared what the client ran against
what the server ran, tic by tic, per window:

| window | switch | local: compared / differ | others: compared / differ | mean drift | worst |
|---|---|---|---|---|---|
| 0 | off | 994 / 638 (64%) | 7952 / 1823 (23%) | 0.268 | 32.5 (p8) |
| 1 | **on** | 1000 / **1** (0.1%) | 8000 / **7** (0.09%) | 0.474 | 29.9 (p8) |
| 2 | off | 1070 / 869 (81%) | 8560 / 6831 (80%) | 0.683 | 97.1 (p3) |

**Exactly the predicted shape.** In both off windows, most of the local
kart's confirmed tics and most of the field's ran on the wrong input; in the
on window, essentially none did -- 1 tic out of 1000 for the local player, 7
out of 8000 for everyone else (both plausibly the one or two tics either side
of the switch itself, not the mechanism). 8.31/8.33's read of the code is
confirmed at race scale, not just in an old log: the fix does what it was
written to do.

**The drift did not fall.** It rose in every window, on then off then on
again: 0.268 -> 0.474 -> 0.683. 8.33 wrote this exact outcome down as the
falsifying case: *"If the inputs agree in the on window and drift does not
move, 8.31 is right about the inputs and wrong about the drift."* That is what
happened.

**Read against the only other race with a mid-race breakdown** (2026-09-20,
no switch at all, three reports over one race): 0.152 -> 0.269 -> 0.334, ratios
1.77 and 1.24. This race: ratios 1.77 and 1.44. The first ratio matches to
three figures. The simplest reading is that drift grows with **how far into
the race it is**, not with which tics ran the wrong input -- the on window
does not interrupt the trend it sits in the middle of. One race each, so this
is a shape match, not a proof; the next race that reads `rollback_drift`
without cutting it into windows would settle whether the growth is really
race-position and not, for instance, grid disorder that a longer scripted
race would also produce with the switch on throughout.

**8.31's mechanism is real and now closed, and it was not the drift's
source.** `rollback_cleancmds 1` stays a fix -- the client runs the tics the
server actually sent, which the design statement (section 1) asks for on its
own -- but Phase A's open question is exactly where it was: the next
instrument is the one 8.33/`ROADMAP.md` already named for this branch, memory
hashed just before a speculation and just after the restore, narrowed to an
address with the `.pdb`.

**Not yet asked: how the on window felt to drive.** Only Gibax can answer
that, and 8.33 flagged it as the real risk of turning the fix on (speculating
only 4 tics above a confirmed frontier that now runs 7 tics behind the local
input).

### 8.36 The on window's feel, asked and explained

Gibax's answer, same race as 8.35: **"Ça répondait tout de suite dans les
trois fenêtres"** -- no felt difference between the switch off and on. 8.33's
risk (speculating only 4 tics above a frontier that now runs 7 tics behind the
local input) did not show up.

**Why, read in the code, not guessed:** `rollback_cleancmds` only changes
`K_RollbackPredictInputs` for `tic < D_NeededTic()` -- tics the server has
already sent. For every tic **at or above** `neededtic`, the genuinely
speculative ones, the function always writes the local player's current input
(`D_LocalTiccmd`), switch or not (`k_rollback.c`, the early return is gated on
`g_cleancmds &&` the tic-below-`neededtic` check, nothing else touches the
loop above it). And what the player sees is that speculation, not the
confirmed world: `NetUpdate` rebuilds it from the confirmed frontier "every
pass, unconditionally... so what the player sees and acts in is ahead of what
the server has confirmed" (`d_clisrv.c:7474-7479`, the comment's own words).

So the felt immediacy was never wired through the tics `rollback_cleancmds`
touches. The switch fixes what the **confirmed** clock does with the local
player's already-sent input -- a bookkeeping question the correction channel
and the consistency check care about -- not what the player sees each frame,
which comes from the speculation on top and was never broken this way. That
also reads consistently with 8.35: a fix confined to a layer the eye never
sampled was never going to move a drift measured from confirmed-world state
corrections either.

8.33's risk is closed: **nothing to trade off.** `rollback_cleancmds` is free
to default on.

### 8.37 A second cleancmds race contradicts the first, on drift, not on inputs

> ⚠ **Both drift readings are confounded, this one and 8.35's (8.38)**: an off
> window's divergence carries into the on window. The test that can settle it
> is a race with the switch on throughout. The inputs result stands.

Measured (binary `89e5d30`, sha verified, same scenario as 8.35, a second
driven `playtest.sh correct` race, run right after flipping the default in
8.36 -- the three windows still set the switch explicitly, so the flip did not
change what this race tested).

**The inputs result repeats, cleaner than before.** On window: **0 of 1000**
local mismatches, **0 of 8000** others -- not 1 and 7 as in 8.35, actually
zero. Off windows: 85%/34% and 81%/74% mismatched. 8.31's mechanism and
`rollback_cleancmds`'s fix are confirmed a second time, at race scale, with no
residue at the boundary this time.

**The drift result does not repeat.** 8.35's race: 0.268 -> 0.474 -> 0.683,
the on window landing almost exactly on the straight line between the two off
windows (0.476 predicted by that line, 0.474 measured -- no effect beyond
time). **This race: 0.442 -> 0.133 -> 0.539** -- the on window **0.358 units
below** that same straight-line prediction (0.491). A large effect, in the
direction 8.33 originally predicted, where the first race found none.

| | window 0 (off) | window 1 (on) | window 2 (off) | on vs. off-off line |
|---|---|---|---|---|
| 8.35 | 0.268 | 0.474 | 0.683 | -0.002 (no effect) |
| 8.37 | 0.442 | 0.133 | 0.539 | -0.358 (large effect) |

**Not resolved, and said so rather than picked.** Two races, two shapes: one
says the switch does nothing to drift beyond the race-position trend, the
other says it cuts drift hard. Nothing about the second race's setup differs
from the first in a way that should matter -- same scenario, same map, same
grid, only the driver and the run. This is exactly the kind of disagreement a
sample of two exists to surface, not settle: `rollback_drift`'s mean is a
coarse, race-shaped, single-driver-dependent number, and Phase A's actual
cause needs the instrument that does not depend on it. **The memory-hash
instrument (state just before a speculation, just after a restore, narrowed
with the `.pdb`) stays next**, and now has a better reason to be next: an A/B
race, even repeated, is not resolving this on its own.

`rollback_cleancmds` stays on by default regardless (8.36's reasoning was the
feel, never the drift).

### 8.38 Audit, 2026-09-22: the off/on/off race cannot measure the drift, and what can

Read, not measured. Nothing launched.

**The correction channel repairs only kart kinematics.** Each
`PT_STATECORRECTION` puts position, momentum, angle, hitlag, rings and item back
(38 bytes, 8.4). The 18 bytes of kart state beside them are measured and never
applied (8.7), and nothing else in the world is carried at all -- the
synchronised RNG, thrown items, hazards, item boxes. With `rollback_correct N`
the full resend is suppressed, so nothing repairs those either.

**So an off window's divergence outlives it.** In the off windows of 8.35 and
8.37, 64-85% of the local kart's confirmed tics and up to 80% of the bots' ran
an input the server did not. By 8.2 the synchronised RNG follows positions
apart, and items thrown, boxes taken and hits landed differ with them. The on
window then runs identical inputs on deterministic tics (0 of 1000 and 0 of 8000
mismatched, 8.37) -- from a world already different in every way the channel
does not correct. Its drift measures what the off window left behind, not what
the switch does, and how much was left depends on the race: what was thrown,
who hit whom. That is enough for one race to show no effect and the other a
large one, without either being wrong about the switch. The third window
controls the race-position trend, not this inheritance.

**What does measure it: a race with the switch on from start to finish.** The
only other configuration in which the confirmed world never runs a wrong input
is `nospec`, and it read 0.000 units over 3357 kart samples (8.9). If 8.31's
mechanism was the drift's source, a race that never runs it should read like
`nospec`. Built as `playtest.sh correct_on` in the private notes' harness: the
same race and server, three windows kept for reading by race position,
`rollback_cleancmds 1` throughout, `rollback_history 0`.

**Prediction, written before the run:** mean drift under 0.05 units in every
window, no worst sample above a few units, the blame lines' `rngsum` identical
on both machines at every refusal, and every input identical tic by tic. If the
drift reads like 8.35/8.37 instead, there is a second leak, and the memory-hash
instrument (`ROADMAP.md`, step 5) is next.

**Checkable without a launch**, on the measuring machine: the second cleancmds
race's `rollback_blame` lines carry `rngsum` for both machines at every
refusal. If they part in window 0 and never agree again, the inheritance is
seen directly. The first race's logs are gone -- the second overwrote them --
so the harness now also keeps a copy of every run's logs named by date and exe
sha.

> ⚠ **2026-09-23 (8.42): there are no such lines.** With the correction
> channel on, a refused checksum prints the "resend suppressed" notice and
> stops before the blame line, on both machines. The `rngsum` clause of the
> prediction above cannot be read as the harness stands; the count of
> refusals can, and `nospec`'s is 0.

**Also found:**

- The relabel split (8.32) was read in the first cleancmds race and recorded
  nowhere; its logs are the overwritten ones. To read again in the next race.
- The leak soak can no longer read 0: `itemList.cap` fails about 0.4% of checks,
  for a reason 8.34 shows to be harmless. A regression net that always shows a
  failure teaches its reader to ignore failures. Excluding `cap` from the
  comparison is a small change, not made yet.
- The display, not the drift: the speculation repeats the newest input over
  tics on which the server will apply older inputs still in flight. See 8.39.

### 8.39 `rollback_history`: the speculation replays the inputs still in flight

Built, off by default, not run.

**The gap.** At 171 ms the server applies each input this machine sends about
seven tics after it was made (8.33). Those seven are in flight: sent, not yet in
any tic the server has sent back, so the confirmed world has not run them. The
speculation drew the world 4 tics above the confirmed frontier by repeating the
*newest* input on each of them. So the drawn kart:

- stops short of where the server will put it: seven tics of inputs happen on
  the server's timeline before the newest one applies, and the speculation shows
  four;
- and draws the recent past wrong: a turn released three tics ago is drawn as
  already over, while the server will still apply those turning inputs. The
  confirmed world catches up a round trip later and the kart turns a little
  more -- a small, late correction on every release.

It does not show as lag -- the newest input is always on screen at once, which
is why every race "répondait tout de suite" -- but it is what 8.33's feel risk
described, and it shows on quick flicks and releases.

**The fix is Quake 3's:** replay every command not yet acknowledged, in order,
from the last acknowledged state. The pieces were already there:

- `localcmds[p][0..34]` (`d_clisrv.c`) keeps this machine's last 35 inputs,
  newest first, one built and sent per pass.
- `G_BuildTiccmd` stamps each with the leveltime it was built at
  (`cmd->latency`, `g_build_ticcmd.cpp:171`), and the server copies ticcmds
  verbatim into `netcmds` and back. So the newest tic the server has sent,
  `neededtic - 1`, names the input it applied there, by its stamp.
- `K_RollbackMapHistory` finds that input in the local history -- stamp,
  forward, turn and buttons; not the angle, which `D_ResetTiccmdAngle` rewrites
  across the history. Every input younger than it is in flight. Newest match
  wins, so a run of identical inputs is undercounted, never overcounted.
- `K_RollbackPredictInputs` gives the local slot of speculated tic
  `neededtic + j` the j-th input in flight, oldest first, and the newest once
  they run out. The speculation goes as deep as that needs -- from the frontier
  to `neededtic`, plus one tic per input in flight -- never below
  `rollback_twoclock`, never above `rollback_history N`.

No wire change: stock servers already send the stamp back. It needs
`rollback_cleancmds` (on by default): without it, the tic the applied input is
read from may hold this machine's own overwrite.

**What it does not touch:** the confirmed world. It only changes what is
drawn, so the drift and the correction channel should not move.

> ⚠ **2026-09-23 (8.46): in its first run, the confirmed world parted inside
> the history window.** Not yet explained.

**What it costs:** the speculation grows from 4 tics to about 8 at 171 ms, so a
pass roughly doubles its replay cost -- an estimated 14 to 16 ms at nine karts,
against 9.5 measured at depth 4. It works against Phase B, and it scales with
latency. That is why it is a switch.

**Remote karts:** unchanged in kind, still predicted by repeating their last
input -- but now over twice as many tics, so a remote kart that changes what it
is doing is mispredicted further ahead. The next pass redraws it, as today;
whether that reads as jitter is for eyes to say.

**Prediction, written before the run** (`playtest.sh history`:
`rollback_cleancmds` on throughout, `rollback_history` 0, then 12, then 0):

- `rollback_history` finds the applied input on over 95% of passes, with about
  7 inputs in flight on average and a speculation about 8 tics deep, never cut
  short at 12.
- The on window's cost per pass is 1.6 to 2 times the off windows'.
- Drift does not change between windows beyond the race-position trend.
- The driver: in the on window the kart goes where the hands say on quick
  flicks and releases, with no small late turn afterwards. If it feels worse
  -- remote karts jumping, the frame rate dropping -- that is the finding.

⚠ **2026-09-23 (8.40):** the depth as built is not steady. It makes the drawn
tic follow the delay the server files this machine's inputs with, which
jitters, so the on window probably judders. Read 8.40 before running this.

### 8.40 Audit, 2026-09-23: one map, and a judder in `rollback_history`

Nothing run. Read: what landed since 8.39 (only the CI's docs filter, green),
the map files of a Ring Racers install, and `rollback_history` again. Public
repository clean of personal information; the three shared documents
identical.

**1. Every driven race ran on one of the plainest maps in the game.**
`harnais/maps.py` reads the game's `.pk3` files, without launching anything,
and says what each race map contains. Read on a 2025 install; to be read again
on the measuring machine, whose version may differ:

- `RR_SkyscraperLeaps`: 973 map things (the mean over the 152 race maps is
  1561), **no water FOF, no polyobject, no linedef executor, no ACS**. It is
  one of 30 race maps with none of the four.
- Of the 152: 85 have water FOFs, 57 run ACS, 36 have linedef executors, 6
  have polyobjects.

So everything Phase A lists as "excluded by measurement" was excluded on that
map: polyobjects (`P_ArchivePolyObjects`), sectors moved by executors, and ACS
threads (`ACS_Archive`) have never been through a speculation during a driven
race, and a kart in water has never been predicted. The soaks also ran on
Northern District (7 water FOFs, 861 bytes of ACS), Green Hills and Sonic
Speedway, which covers some of it without a driver.

**What to run, per map, each launch asked for:** `soak.sh leak map=<lump>` then
`soak.sh ww map=<lump>` first (unattended, one instance, and they name the
field that breaks), then `playtest.sh correct_on map=<lump>` driven, with
`playtest.sh nospec map=<lump>` as that map's control. Shortlist, one family
each:

| map | laps | why |
|---|---|---|
| `RR_NorthernDistrict` | 4 | already soaked; water 7, ACS 861 -- the gentle first step |
| `RR_CarnivalNight` | 3 | water 37, executors 24, ACS 845 |
| `RR_Labyrinth` | 2 | water 80, the most of any race map |
| `RR_CoastalTemple` | 3 | polyobjects 6, ACS 2537 |
| `RR_DeathEgg` | 3 | polyobjects 4, executors 15, ACS 3132, 2629 things: everything at once |
| `RR_Opulence` | 3 | 3538 things, 3.6 times Skyscraper Leaps: the snapshot, so Phase B |

A driven race on these is also Phase C ahead of Phase B: the resends and the
drift can be read, but a judgement of feel is suspect wherever a pass
overruns the tic.

**Prediction, written before any of it runs:** the leak soak fails only on
`itemList.cap`, and the resim check stays at 0, on all six maps; `correct_on`
reads like `nospec` on each. A failure named in the polyobject or specials
block by `P_LocateSnapshotBlock` -- or in "waypoints", under which it reports
ACS and Lua, since they write no marker of their own -- is the second leak
Phase A has been looking for.

**2. `rollback_history`'s depth makes the drawn world judder.** As built
(8.39), a pass speculates `(neededtic - frontier) + A` tics, `A` being the age
of the input the server applied at `neededtic - 1`. The drawn tic is therefore
`neededtic + A`, which works out to the local tic plus the delay the server
filed that input with. With the switch off, the drawn tic is `frontier + 4`,
and the frontier advances one tic per tic because the netticbuffer reserve
absorbs arrival jitter. With it on, the drawn tic carries the server's filing
delay, and for a client that pays for its own latency that delay is the raw
transit time: `faketic - realstart` spread over `+6` to `+8` under
`rollback_lag 6` (8.22). Each time it changes, the drawn world moves by that
many tics in one frame: two tics' travel at once, or a kart that holds or
steps back. A pass whose match fails falls back to `rollback_twoclock`, a jump
of about four tics.

**Prediction:** as built, the on window of `playtest.sh history` shows visible
hitches, often enough that the driver notices them before any gain on flicks.

**Proposed fix, not coded** (it touches `src/`). ⚠ Built the same day as a
held *lead over the clock*, not a held depth (8.41):

- **Hold the depth**: speculate to the high-water mark of the needed depth over
  the last second, lowering it by at most one tic a second. The drawn tic then
  advances one per tic except when the mark moves. The tics past the last
  input in flight repeat the newest one, as today.
- On a failed match, keep the previous depth instead of falling back to
  `rollback_twoclock`.
- **Measure the judder** instead of leaving it to the eye: count the passes
  where the drawn tic minus the local tic changed, and print it in the
  `rollback_history` report.

**3. Still owed, no launch needed:** `rngsum` in the second cleancmds race's
blame logs (8.38); the relabel split, written down this time (8.32);
`itemList.cap` excluded from the leak comparison, so a clean soak reads 0.

### 8.41 `rollback_history` holds the drawn tic's lead over the clock

Built on 2026-09-23 from 8.40 point 2, **not run**. Code:
`src/k_rollback.c` (`K_RollbackSpeculate`, `Command_RollbackHistory_f`).

**Why the lead, not the depth.** 8.40 proposed holding the *depth*. But the
drawn tic is the frontier plus the depth, so a held depth still passes the
frontier's own unevenness to the screen: a pass whose confirmed loop ran no tic,
or two, moves the drawn world by a tic. What should stay put is the drawn tic
against real time (`I_GetTime()`). So that is what is held, and the depth is
whatever reaches it from the frontier on each pass.

**How.** On each pass with the switch on:

- The lead asked for is the tic the newest input in flight lands on
  (`neededtic` plus the number in flight), minus `I_GetTime()`.
- The held lead rises to it at once when a pass asks for more. When a whole
  second passes in which no pass asked for the held lead, it comes down by one
  tic. A pass that finds no match asks for nothing, and the lead stands.
- The depth is the held lead plus `I_GetTime()` minus the frontier, never
  below `rollback_twoclock` nor above `rollback_history N`.
- A gap of more than a second between passes (a map change, a pause) starts
  the hold again.

The inputs in flight are still replayed from `neededtic`, oldest first. The
extra tics a held lead adds past the newest one repeat it, as the speculation
always did.

**Measured instead of eyeballed:** every pass, with the switch on or off,
compares the drawn tic minus `I_GetTime()` with the previous pass's. The
`rollback_history` report now prints how many passes it changed on, and by how
many tics in all, plus how many times the lead was raised and lowered. Setting
the switch resets the counts. The `history` scenario already prints the report
at each window's end. `correct_on` now does too, reset each window, for a
baseline with the switch off throughout. `cleancmds_report.py` reads the new
lines.

**Prediction, written before the run** (`playtest.sh history`). It replaces
8.39's depth figures and 8.40's judder prediction, which were about the first
build:

- off windows: the drawn world moves against the clock on under 5% of
  passes;
- on window: within 2 points of the off windows, with the lead raised and
  lowered fewer than 20 times each;
- the applied input found on over 95% of passes, about 7 inputs in flight, a
  speculation 9 to 10 tics deep on average (the held lead sits at the top of
  the jitter, a tic or two above 8.39's 8), never cut short at 12;
- cost per pass 1.8 to 2.3 times the off windows';
- drift unchanged between windows beyond the race-position trend;
- the driver: no hitch in the on window that the off windows do not have, and
  quick flicks and releases drawn where the hands put them.

> ⚠ **Run on 2026-09-23 (8.46):** the display-side figures mostly hold, the
> depth (8.03) and the cap (4 passes cut) do not, and the drift clause fails:
> the confirmed world parted inside the on window.

### 8.42 The measuring machine, before any launch: the maps agree, and the blame lines were never printed

Read on the measuring machine, 2026-09-23. Nothing launched. The dev artifact
of `2b58e1d53` is installed in the game folder and in the second instance's
home, the old build kept as `.bak_89e5d30`; both copies carry
`rollback-netcode 2b58e1d`, and `2b58e1d53` is the code repository's `HEAD`.

**1. The maps are the ones 8.40 read.** `harnais/maps.py` on this machine's
install (the v2.4 assets): 152 race maps, 85 with water FOFs, 57 with ACS, 36
with linedef executors, 6 with polyobjects, 30 with none of the four, 1561
things on average. Every row of 8.40's shortlist, and Skyscraper Leaps, reads
the same. The shortlist stands as written.

**2. There is no `rngsum` to read in the second cleancmds race.** Both of its
logs (8.37, binary `89e5d30`) switch `rollback_blame` on -- the confirmation
line is there, once each -- and neither holds a single blame line: 0
`SERVER`, 0 `CLIENT`. Read in the code, it cannot be otherwise
(`d_clisrv.c`, the consistency check in the client-packet handler):

- the server prints its `rollback_blame: SERVER` line after it has decided to
  resend. With `rollback_correct` on, `K_RollbackCorrectSuppress()` prints the
  "resend suppressed" notice and `break`s first;
- the client prints its `CLIENT` lines when `PT_WILLRESENDGAMESTATE` arrives,
  and a suppressed resend never sends it.

So no race with the correction channel on has ever printed a blame line. The
readings this file took from them (8.1, 8.5) come from races that still
resent. **8.38's prediction for `correct_on` -- "`rngsum` identical on both
machines at every refusal" -- cannot be read**: its server runs
`rollback_correct 4`. ROADMAP step 5's first item has nothing to read.

**3. What those logs do show: a refusal count, and it never stops.** The server
log carries one "consistency mismatch ... resend suppressed" line per refused
checksum, throttled like the resend it replaces, to one per five seconds.
`cleancmds_report.py` now counts them per window:

| window | switch | tics | wrong inputs, local / others | mean drift | refusals |
|---|---|---|---|---|---|
| before 0 | (speculation off) | join to 2203 | -- | -- | 0 |
| 0 | off | 2204-3197 | 840/994 / 2729/7952 | 0.442 | 6, from tic 2243 |
| 1 | **on** | 3198-4197 | **0**/1000 / **0**/8000 | 0.133 | **6** |
| 2 | off | 4198-5267 | 865/1070 / 6362/8560 | 0.539 | 6, last at 5219 |

All 18 are for the client's kart (player 9 in the server's numbering, `p8`).
They fall 175 tics apart, once 176: each time the five-second throttle ran
out, the very next checksum from the client disagreed. The first came 39 tics
into window 0 -- twoclock was 0 until that window opened, and nothing was
refused before it -- and from there no sample ever agreed again, the on window
included, while that window ran not one wrong input.

**The control, from the same folder:** the `nospec` race of 8.9 (binary
`cbd2011`, same server scenario, `rollback_correct 4`, blame on) has **0**
refusals, over the race that read 0.000 units on 3357 kart samples.

**Reading.** This is what 8.38 said an off window would leave behind: the
confirmed worlds part in the first off window and do not come back together,
whatever the next window's inputs. But it does not say *what* differs. The
checksum sums each player's `x`, `y`, `itemtype` and the synchronised RNG
seeds, and a position a fraction of a unit off is enough to change it -- the
on window's drift, 0.133 units mean, is that already. RNG and objects are
neither shown nor ruled out.

**What changes for `correct_on`.** The refusal count is readable and has a
control, so the prediction gains a clause, written before the run: if 8.31's
mechanism was the drift's whole source, `correct_on`'s confirmed world agrees
with the server's as `nospec`'s did -- **0 refusals** in every window, beside
a mean drift under 0.05 units. Any refusal means something still differs; the
report dates the first one, but not what it is.

**Proposed, not coded** (it touches `src/`): make `rollback_blame` print
without a resend. Both machines print the blame line of every 35th confirmed
tic (`gametic % TICRATE == 0`, at the one place `Consistancy_Describe` is
called), and the server also prints its own line in the suppressed branch.
The two logs then hold lines for the same tics once a second, refusal or not,
and the first tic at which `rngsum` parts can be told apart from the first
tic at which a position does. About 200 bytes a second a machine.

> ⚠ **Built the same day (8.43)**, without the server's line in the suppressed
> branch: the client has no line for the same tic to set beside it. About 300
> bytes a second, not 200, at nine karts.

### 8.43 `rollback_blame` prints once a second, refused or not

Built on 2026-09-23 from 8.42's proposal, **not run**. Code: `src/d_clisrv.c`,
`Consistancy_Describe`.

**What it prints.** With `rollback_blame` on, both machines print
`rollback_blame: SAMPLE tic N: p0(x,y,iT) ... rngsum=S` for every confirmed
tic N that is a multiple of 35, in a level: the line the ring already kept,
made of exactly the fields `Consistancy()` hashes. It is printed where the tic
loop records it, the ring's only writer, so a speculated tic never prints. One
line a second, about 300 bytes at nine karts. The `SERVER` and `CLIENT` prints
on the way to a resend are unchanged.

**Left out of 8.42's proposal:** the server's line for the tic it refuses, in
the suppressed branch. Nothing tells the client which tic was refused, so it
has no line for the same tic, and a line alone compares with nothing -- the
seed sum changes every tic.

**How it is read.** `cleancmds_report.py` sets the two logs' SAMPLE lines side
by side, per window, before the first window, and over the whole log: how many
sampled tics differ in `rngsum`, in a player's position, in a player's item,
and the first tic of each. Tested on made-up logs, and on the second cleancmds
race's, which have none and say so.

Syntax checked with MSYS2 `gcc -fsyntax-only -Wall -Wextra` on `d_clisrv.c`,
with stub headers for opus and renamenoise: no diagnostic on the changed
lines. Not compiled -- CI is the only build.

**What it can tell, in `correct_on`:**

- 0 refusals and SAMPLE lines identical on every sampled tic: 8.38's
  prediction holds, Phase A closes.
- 0 refusals but SAMPLE lines that differ: the instrument is wrong, since the
  lines carry exactly what the checksum hashes. To be fixed before anything is
  read from it.
- Refusals: the first sampled tic at which `rngsum` parts, against the first
  at which a position does. Seeds first: something the channel does not carry
  parts first (8.38's list: RNG, objects, kart state beyond kinematics).
  Positions first with the seeds still equal: the kart's own simulation parts,
  and the next instrument is the memory hash.

The prediction for `correct_on` is unchanged (8.38, plus 8.42's 0 refusals),
with its `rngsum` clause now readable: **`rngsum` and every position equal on
every sampled tic, in every window.** `history` changes only what is drawn,
so its SAMPLE lines should read the same way.

### 8.44 `correct_on`: every clause holds -- the confirmed world never parts

Measured: binary `a1df8bb85` (sha checked by the harness), `playtest.sh
correct_on`, driven by Gibax, `RR_SkyscraperLeaps`, nine karts, `rollback_lag
6`. The predictions were written in 8.38, 8.42 and 8.43, before the run. Logs
kept as `*_correct_on_20260923-132623_a1df8bb.txt`.

| window | tics | kart samples | mean / worst drift | refusals | blame samples differing | inputs differing, local / others | cost of a pass |
|---|---|---|---|---|---|---|---|
| 0 | 2140-3133 | 2241 | **0.000 / 0.000** | **0** | **0** of 28 | 0 of 994 / 0 of 7952 | 7.1 ms |
| 1 | 3134-4133 | 2250 | **0.000 / 0.000** | **0** | **0** of 29 | 0 of 1000 / 0 of 8000 | 8.6 ms |
| 2 | 4134-5203 | 2250 | **0.000 / 0.000** | **0** | **0** of 30 | 0 of 1070 / 0 of 8560 | 9.5 ms |

Also: no `rollback_drift: SPIKE` line (the second cleancmds race had 79), no
reload, damage events 8 on both machines with the same hash. The speculation
ran all along: 1000 passes and 4000 speculated tics a window.

**Every clause holds, and harder than written.** Under 0.05 units was
predicted; 0.000 was measured on 6741 kart samples -- the reading of `nospec`
(8.9: 0.000 on 3357). The 87 blame samples are not agreeing on empty lines:
each carries nine players, and the seed sum is different at every sample, and
identical on both machines.

**Reading.** 8.31's mechanism -- the speculation writing the local input over
tics the server had already sent -- was the whole of the confirmed world's
divergence on this map. With it gone from the first tic, the client's
confirmed world is the server's on everything the checksum hashes, at every
sample, for three thousand tics, with the speculation running on top. The
drift of 8.5 to 8.37 was that mechanism and what its off windows left behind
(8.38).

**Limits, said now.** One race, one map, and that map has no water, no
polyobject, no executor and no ACS (8.40). And **no control in this session
yet**: `nospec` ran on 2026-09-14 and the cleancmds races on 2026-09-21, on
other builds. On this build, only the `correct` race's off windows can show
that the same instruments still see a divergence when there is one.

**Prediction for that control**, written before it runs (`playtest.sh
correct` on `a1df8bb85`, windows off / on / off):

- refusals from early in window 0 to the end of the race, the on window
  included, as in 8.42;
- in the off windows, most of the local kart's confirmed inputs wrong (64-85%
  in 8.35 and 8.37), and a mean drift above 0.1 units;
- blame samples that differ from window 0 on and never all agree again. The
  first difference is a position, with the seed sum parting at the same
  sample or a later one: by 8.2 the synchronised RNG follows positions. A seed
  sum that parts while every position still agrees would mean RNG drawn by
  something other than the karts' motion.

**Also read from this race:**

- **The drawn world against the clock** (8.41's baseline, `rollback_history`
  off): it moved on 6 of 999 passes in window 0, and on 0 and 0 in windows 1
  and 2. 8.41 predicted under 5% for off windows.
- **The relabel split, written down this time (8.32).** The server printed it
  once, at tic 3500; the later prints fell after the harness closed the server,
  when the client quit. 6456 packets. Host, in a race: `+0` ×2034, `+2` ×1464.
  Remote, in a race: `+0` ×2, `+1` ×579, `+2` ×1019, `+3` to `+6` ×7, `+7` ×149,
  `+8` ×1202. Nothing was labelled "outside a race".
  8.32's prediction -- `+2` almost entirely "host, outside a race" -- **fails
  on its label**: the waiting map before the race, `RR_TESTRUN`, is a level,
  so `gamestate == GS_LEVEL` calls it a race. The counts fit a plainer cause,
  on arithmetic only:
  - the host's 1464 `+2` are the tics before the server scenario sets
    `rollback_correct` (the exemption of 8.27 depends on it), about 1500;
  - the client's `+1`/`+2`, about 1600, are its tics from joining (about
    tic 540) to the `rollback_lag 6` its scenario sets at tic 2140;
  - its `+7`/`+8`, about 1350, are the tics from 2140 to 3500.

  All of the `+2` comes before the first measurement window; within the
  windows it is harmless. A split by "before or after the scenario's settings"
  would prove it, where `GS_LEVEL` cannot.

**Harness defect, found reading this race and fixed.** `cleancmds_report.py`
kept the last report of each kind in a window. `correct_on` and `history`
reset their counters before echoing the next window's marker, so in windows 0
and 1 the reset's all-zero print won: "0 corrections received" and "0 us a
pass". The report now skips an all-zero print when the window already has a
report. It also counted the local player among the others in window 0, since
the grid line comes at that window's end; it now reads the grid line first.
The table above is the fixed report's, checked against the raw lines.

### 8.45 The same-session control: the instruments see the divergence, and positions part before the seeds

Measured: same build as 8.44 (`a1df8bb85`, sha checked by the harness), same
session, `playtest.sh correct` -- `rollback_cleancmds` off / on / off --
driven by Gibax, `RR_SkyscraperLeaps`, nine karts, `rollback_lag 6`.
Prediction written in 8.44 before the run. Logs kept as
`*_correct_20260923-133445_a1df8bb.txt`; the second cleancmds race's logs,
which this run would have overwritten, were first saved as
`*_correct_20260921-235500_89e5d30.txt`.

| window | switch | local inputs wrong | others wrong | mean / worst drift | refusals | blame samples differing: seeds / a position |
|---|---|---|---|---|---|---|
| 0 | off | 766 of 994 (77%) | 2552 of 7952 | 0.297 / 42.2 | 6, from tic 2203 | 26 / 27 of 28 |
| 1 | **on** | 1 of 1000 | 4 of 8000 | 0.116 / 13.5 | **6** | **29 / 29 of 29** |
| 2 | off | 899 of 1070 (84%) | 6286 of 8560 | 0.603 / 57.4 | 6, last at 5181 | 31 / 31 of 31 |

The five wrong inputs of the on window all sit on its first tic, 3185, the
switch itself -- as in 8.35.

**Every clause of the prediction holds.**

- Refusals from 12 tics into window 0 to the end of the race, the on window
  included: 18, six a window, one per five-second throttle.
- Off windows: 77% and 84% of the local kart's confirmed inputs wrong, and a
  mean drift of 0.297 and 0.603 units.
- Blame samples differ from window 0 on and never all agree again: over the
  race, the seeds differ on 86 of 88 samples and a position on 87.

So on this build, in this session, the same instruments that read 0 in 8.44
see a divergence as soon as there is one. **8.44's zeros are not blind
instruments.**

**What parts first, sample by sample, from the first shared sample:**

| tic | seed sum | positions that differ |
|---|---|---|
| 2205 | equal | `p8` (the driven kart), by 0.002 units |
| 2240 | equal | `p2` (a bot), by 0.001 units |
| 2275 | **differs** | none -- all nine equal |
| 2310 | differs | `p2`, under 0.001 |
| 2345 | differs | `p2`, and `p8` by 0.24 |
| 2380 | differs | `p2`, `p3`, `p6`, and `p8` by 1.54 |

The driven kart parts first, by a few thousandths of a unit, three tics after
the first wrong input (tic 2202, `p8`, turn 800 against 696). A bot follows. The
seed sum parts two samples later, at a sample where the correction channel had
just put every kart back. It never agrees again: the channel repairs positions
and never the seeds. This is 8.38's inheritance, seen directly. In the on
window every input is right and every sample still differs, in the seeds and
in the positions, for all of its 29 samples.

**Phase A's question is answered on this map.** The drift was the speculation
overwriting the local input of tics already received (8.31). That put the
confirmed kart a fraction of a unit off, then the bots through contact and
the synchronised RNG, which nothing repairs. With `rollback_cleancmds` on
from the first tic, none of it happens (8.44). Still owed: the other maps
(8.40), and any mode other than a scripted race.

**The relabel split again**, as a check on 8.44's reading. Host, in a race:
`+0` ×2034, `+2` ×1464, identical to `correct_on` -- the same server scenario,
the same tics before `rollback_correct`. Remote, in a race: `+1`/`+2` ×1598,
`+7`/`+8` ×1300, the rest ×8. It is the same shape, and it fits the same
arithmetic.

**How they felt, asked after both:** Gibax, "pas vraiment senti une grosse
diff sur les sessions" -- no real difference, between the races or between
`correct`'s windows. It is what 8.36 explains: what is drawn comes from the
speculation, which `rollback_cleancmds` does not touch.

### 8.46 `history`: the confirmed world parts inside the history window

Measured: same build and session as 8.44-8.45 (`a1df8bb85`, sha checked by the
harness), `playtest.sh history` -- `rollback_cleancmds` on throughout,
`rollback_history` 0 / 12 / 0 -- driven by Gibax, `RR_SkyscraperLeaps`, nine
karts, `rollback_lag 6`. Prediction written in 8.41 (and 8.39 for what it
does not touch), before the run. Logs kept as
`*_history_20260923-134133_a1df8bb.txt`.

| window | history | kart samples | mean / worst drift | refusals | blame samples differing: seeds / a position | cost of a pass | drawn world moved |
|---|---|---|---|---|---|---|---|
| 0 | off | 2241 | 0.000 / 0.000 | 0 | 0 / 0 of 28 | 6.1 ms | 6 of 999 |
| 1 | **12** | 2223 (1 stepped over) | 0.017 / **22.5** (`p6`, tic 4104) | **1**, tic 4101 | 2 / 1 of 29, from tic 4130 | 11.6 ms | 5 of 999 |
| 2 | off | 2250 | 0.005 / 0.19 | **6** | **30 / 28 of 30** | 7.5 ms | 0 of 999 |

Confirmed inputs identical on every tic of every window, local and bots (0 of
27 576). No reload.

**What the history window did as designed** (8.41's prediction):

- The applied input was found on 1000 of 1000 passes (predicted over 95%),
  with 7.07 inputs in flight on average (predicted about 7).
- The drawn world moved against the clock on 5 of 999 passes, against 6 and 0
  in the off windows (predicted within 2 points).
- The held lead was raised 0 times and lowered 0 times (predicted under 20
  each). It never moved at all after its first pass, which this report cannot
  tell from a lead set high once and held.

**What it did not:**

- The depth averaged 8.03 tics, not the 9 to 10 predicted, and the cap of 12
  cut 4 passes short, where "never" was predicted.
- The cost was 1.92 times window 0's and 1.55 times window 2's; 1.8 to 2.3 was
  predicted.
- **The confirmed world parted.** 8.39 said `rollback_history` "only changes
  what is drawn, so the drift and the correction channel should not move", and
  8.41 predicted the drift unchanged between windows. The blame samples agree
  through tic 4095. The server refuses the checksum of tic 4101. At tic 4104,
  the correction finds two bots out of place, `p6` by 22.5 units and `p7` by
  15.2, with the diagnostic fields reading `bumped 0/3` for both (client /
  server: the server's two bots have just bumped, the client's have not) and
  `flash 15/0` for `p7` (flashing on the client only). The damage logs agree:
  the one event near it, `p3` hit at tic 4101, is on both machines with the
  same hash. From tic 4130 the seed sum differs, and window 2, with the switch
  off again, inherits it: every sample and a refusal every five seconds, as in
  8.45's on window.

This is also the first correction ever stepped over (8.5 recorded "it never
does"): a pass whose loop ran two tics. The code drops such a correction
without applying it (`K_RollbackApplyServerState`), so on reading it cannot
move the confirmed world. Noted, not blamed.

**Reading, and its limits.** The confirmed inputs were the server's on every
tic, so the difference came through state, not input. The state the checksum
and the blame samples see -- positions, items, seeds -- was equal until the
sample before, so it started in something they do not see (`flashing` or
`justbumped` among them), and surfaced in a bump one machine had and the
other did not. The history window's speculation ran twice as deep as every
window that has held (8039 speculated tics against 4000). A leak that only a
deeper speculation reaches fits that. So would a rare leak that 4000 tics at
depth 4 happened not to hit: this is one event in one window. **Not a
mechanism, and no fix is proposed from it.**

**What tells them apart, without guessing:**

1. `soak.sh leak12` (new, `harnais/soak_leak12.cfg`): the leak soak with a
   12-tic speculation instead of 4, unattended, one instance. **Prediction:**
   it fails beyond `itemList.cap`, and `P_LocateSnapshotBlock` names a player
   field -- a leak through the archive that only a deeper speculation
   reaches. If it reads like `soak_leak`'s 1/260, the leak is outside the
   archive (the memory-hash instrument, Phase A) or not about depth.
2. Then a driven race with plain speculation at 12 tics (`rollback_twoclock
   12`, history off) against `history`: depth, or the replay of inputs in
   flight.

**Until then, `rollback_history` stays off by default.** Asked of the driver
whether the history window felt closer to the hands: "ça allait, j'ai
l'impression que ça allait mieux" -- better, by impression, with no hitch
reported. One race, one driver, told which window was which beforehand: a
lead worth following, not a result.

### 8.47 The leak soak at 12 tics: no leak through the archive

Measured: same build (`a1df8bb85`), `soak.sh leak12` (new,
`harnais/soak_leak12.cfg`), unattended, one instance, `RR_SkyscraperLeaps`,
eight karts: `rollback_soak 20 12 1`, a leak check every 20 tics on a 12-tic
speculation instead of `soak_leak`'s 4. Prediction written in 8.46. Log kept
as `soaklog_leak12_20260923-135203_a1df8bb.txt`.

**264 checks, 2 failures, both `itemList.cap`.** Each is one byte in the
players block of the archive, `0x00` became `0x20`, and the struct walker puts
it at 672 bytes into `player_t` for the same player (`p7`, then `p4`):
`itemRoulette.itemList.cap`, the known and harmless case of 8.34. No other
archived byte differs. 0 of 1347 and 0 of 1310 objects differ. The
non-archived bytes the walker also lists (240, 292/296, 1828/1832 into
`player_t`) are the ones every leak soak has printed since 8.11-8.12: HUD
counters (`karthud`) and `roundconditions`.

**The prediction was wrong.** No field of the archive leaks through a 12-tic
speculation that does not leak through a 4-tic one: 2 of 264 against 1 of
260, both `cap`. So 8.46's divergence did not come through the archive. It
came from state outside it, or from something a solo soak does not do (a
netgame, a second machine, the correction channel), or it was not about depth.

**Harness defect, found in this run and fixed.** The race check that
`map_override.sh` added on 2026-09-23 looked for `Map is now "<map>`, which
only a netgame prints. It called this soak's map "never loaded" and made the
script exit 1. It said the same of every earlier soak log, in a dry run. A
local session is now recognised by the rollback command's own opening line
(`rollback_soak: RR_SKYSCRAPERLEAPS, 8 racers ...`), and a later map by a
second "Speeding off to level...". Checked on the three soak logs, the two
history logs, and a wrong map name.

**Next: depth, or history's replay?** `playtest.sh depth12` (new,
`playclient_depth12.cfg`, same server as `history`): the history race's
layout -- `rollback_cleancmds` on throughout, three windows of 1000 tics --
with the middle window at plain `rollback_twoclock 12` and `rollback_history`
0. It can run without a driver: it was two bots that parted in 8.46, and bots
are what a deep speculation mispredicts.

**Prediction, written before it runs:**

- the middle window parts: a refusal, a drift spike, blame samples that
  differ from some tic on, while the first window reads 0 as in 8.44. Plain
  12 is deeper than history's 8 on average, and mispredicts the bots as far;
- its passes cost about three times window 0's (12 tics against 4), near 20
  ms at nine karts, and more of them step over a correction;
- if it holds instead, the divergence belongs to history's replay of the
  inputs in flight -- or to an event too rare for one window. One event in
  1000 tics (8.46) is not a rate, so **a clean window would not clear depth**;
  a second `history` race would be the check.

### 8.48 `depth12`: plain speculation at 12 tics holds, without a driver

Measured: same build (`a1df8bb85`), `playtest.sh depth12`, **no driver** (the
local kart sat at the start), `RR_SkyscraperLeaps`, nine karts, `rollback_lag
6`. Prediction written in 8.47. Logs kept as
`*_depth12_20260923-231351_a1df8bb.txt`.

| window | speculation | kart samples | mean / worst drift | refusals | blame samples differing | cost of a pass |
|---|---|---|---|---|---|---|
| 0 | 4 tics | 2241 | 0.000 / 0.000 | 0 | 0 of 29 | 7.5 ms |
| 1 | **12 tics** | 2241 (2 stepped over) | 0.000 / 0.000 | 0 | 0 of 28 | **17.9 ms** |
| 2 | 4 tics | 2250 | 0.000 / 0.000 | 0 | 0 of 31 | 9.4 ms |

Confirmed inputs identical on every tic, no spike, no reload.

**The prediction was wrong again.** The 12-tic window did not part. Its cost,
17.9 ms a pass, 2.4 times window 0's, is near the "about three times, near 20
ms" written. The two corrections it stepped over did nothing, as the code
says: that clears 8.46's stepped-over correction too.

**What it does and does not say.** Plain speculation at 12 tics, for 1000
tics, with the bots racing and nobody driving, leaves the confirmed world
untouched. It does not separate depth from history's replay, because it
changed a second thing: **no driver**. In 8.46 a person was driving, and the
two karts that parted were bots bumping each other, near or not near the
driven kart -- the log does not say. With nobody at the wheel, the local
kart's inputs never change, so the speculation never mispredicts it, and a
bot never meets it somewhere it will not be. So the question stands:

- history's replay of the inputs in flight;
- deep speculation *with a driver* (plain 12 was never driven);
- or a rare event: one in 1000 history tics, none in 1000 plain-12 tics nor in
  about 7000 tics at 4, driven or not, since 8.44.

Read in the code while the race ran, and ruled out by reading only: every
`botvars` field is archived (`p_saveg.cpp`), so the bot prediction
`K_BuildBotTiccmd` runs during the speculation cannot leave bot state behind
through a missing field; and bots' confirmed inputs come from the server
(`d_clisrv.c`, `SV_Maketic`), so none is recomputed on the client.

**Next, one driven race, each asked for:** `history` again, driven, as the
reproducibility check. If it parts again, `depth12` driven separates depth
from the replay. If it holds, the event is rare, and it needs a longer on
window rather than a second guess.

**Scenario defect, noted:** `playclient_depth12.cfg` never sets
`rollback_history`, so its drawn-world count is not reset between windows and
reads cumulatively (6, 11, 12 over 999, 1999, 2999 passes: 6, 5 and 1 a
window). Harmless here, and fixed in the scenario.

### 8.49 `rollback_drift` names a differing state before the kart is out of place

Built on 2026-09-24, before the second `history` race, **not run**. Code:
`src/k_rollback.c`, `K_RollbackApplyServerState` and the `rollback_drift`
report.

**The gap it closes.** Every correction already compared each kart's state
with the server's -- spinout, flashing, `justbumped`, hitlag, offroad, speed,
item and a few more (8.7's diagnostic bytes) -- but printed it only beside a
sample 4 units or more out of place. In 8.46 that first happened at tic 4104,
with `p7` already 15 units out and flashing on the client only: since when,
and after what, the log could not say.

**What it does now.** The state comparison runs on every kart sample. A sample
below the spike threshold whose state differs prints
`rollback_drift: STATE tic N pK off by X -- differs: ...`, the first 40 after
each reset of the report. The report adds `M of N kart samples had a state
field differing, the first at tic T`, printed at 0 too. The SPIKE line is
unchanged. `cleancmds_report.py` shows the count and the first six STATE or
SPIKE lines per window. Tested on made-up logs, and on the history race's,
where it shows its two SPIKE lines.

Syntax checked with MSYS2 `gcc -fsyntax-only -Wall -Wextra` on
`k_rollback.c`, with no diagnostic in the file. A copy with an error injected
into the changed function failed, so the check does compile it. Not compiled
-- CI is the only build.

**Prediction, written before the second `history` race:** in the windows that
held until now (every `correct_on` and `depth12` window, and `history`'s
window 0), 0 samples with a state field differing -- the instrument is quiet
on a world that agrees. If the history window parts again, the first STATE
line comes before the first refusal, and names a field on the karts that
part: `flash` or `bumped`, as in 8.46.

### 8.50 A second `history` race holds: 8.46 does not reproduce

Measured on 2026-09-28: binary `88d8a878f` (sha checked by the harness; it
carries 8.49's STATE lines), `playtest.sh history`, driven by Gibax,
`RR_SkyscraperLeaps`, nine karts, `rollback_lag 6`. Predictions written in
8.46 and 8.49. Logs kept as `*_history_20260928-130110_88d8a87.txt`.

| window | history | kart samples | drift | samples with a state field differing | refusals | blame samples differing | cost of a pass |
|---|---|---|---|---|---|---|---|
| 0 | off | 2241 | 0.000 / 0.000 | **0** | 0 | 0 of 28 | 7.7 ms |
| 1 | **12** | 2250 | 0.000 / 0.000 | **0** | 0 | 0 of 29 | 13.5 ms |
| 2 | off | 2250 | 0.000 / 0.000 | **0** | 0 | 0 of 30 | 8.4 ms |

Confirmed inputs identical on every tic, no reload. The history window found
the applied input on every pass, with 7.08 inputs in flight, a speculation
8.00 tics deep, **cut short by the cap on 0 passes** (4 in 8.46), and the
drawn world moved against the clock on 0 of 999 passes. The held lead again
was raised 0 times and lowered 0: in two races it has never moved after its
first pass. That is within 8.41's "fewer than 20", and it is also what a lead
set once at the top of the jitter would do. Not yet read in the code.

**8.49's prediction for a world that agrees holds:** the new STATE comparison
ran on 6741 kart samples and found none differing, in any window. The
instrument is quiet when it should be.

**8.46 did not reproduce.** The divergence -- a refusal, two bots 15 and 22
units out, one bump on the server only -- came once in 1000 history tics, and
not in these 1000. Counting since 8.44, with `rollback_cleancmds` on
throughout and outside an off window's inheritance, the confirmed world has
held for about 9 200 tics without history -- `correct_on` 3064, `depth12`
3068 (1000 of them at 12 tics, without a driver), the history races' off
windows 994 and 2064 -- and parted once in 2000 tics with it. One event cannot tell a
leak tied to history from a rare one that history happened to be running
for. It also cannot say that nothing is there.

**What that decides.** 8.48 wrote this branch down: "the event is rare, and it
needs a longer on window rather than a second guess". A driven race with
history on throughout -- three windows of 1000 tics, like `correct_on` --
triples the history tics a race gives. The STATE lines will name the first
differing field if it comes. Until then `rollback_history` stays off by
default. The other maps, the host's seat and Phase A's strict half do not
depend on it and go ahead.

**Relabel split**, for the record: host `+0` ×2034, `+2` ×1464, identical to
the three races of 2026-09-23; remote `+1`/`+2` ×1594, `+7`/`+8` ×1348, the
rest ×10. The same shape, the same reading (8.44).

**How it felt, asked after the race.** Gibax: the history window "a l'air
d'être mieux", with "un peu de latence, genre à-coups, je saurais pas dire" --
and, new, **frame drops**, "comme si le jeu galère à afficher les images".
Not measured: no log records the frame rate, and the drawn-world count
above (0 jumps in the history window) is about which tic is drawn, not how
often a frame is. Read against what is measured, it is what the pass costs
predict. A pass runs once a tic, all at once, inside one frame: 7.7 to 8.4
ms with history off, 13.5 ms with it on. That is 27 to 29% of a second's
CPU with history off, 47% with it on, on the thread that also renders, with
the server's instance running on the same machine. A frame that carries a
13.5 ms pass has about 3 ms left of a 60 Hz frame's 16.7 to draw it. This
is Phase B's problem -- the cost of a pass -- felt as frames rather than read
as milliseconds. Whether the drops came only in the history window is not
known.

### 8.51 The first other map: Northern District's leak soak, and an ACS reference count that goes negative

Measured on 2026-09-28: binary `88d8a878f`, `soak.sh leak
map=RR_NorthernDistrict`, unattended, one instance, eight karts,
`rollback_soak 20 4 1`. Prediction written in 8.40. Log kept as
`soaklog_leak_RR_NorthernDistrict_20260928-131434_88d8a87.txt`. The first
launch never started: see the harness defect below.

**The prediction holds.** 258 checks, 1 failure: `itemList.cap` (one byte of
the players block, `00`->`20`, 672 bytes into `player_t` for `p4`), the known
and harmless case of 8.34. 0 of 1728 objects differ. `rollback_test`'s round
trip after the soak is byte-identical over the whole 184 086-byte snapshot --
1.7 times Skyscraper Leaps' 106 909 -- with save 0.9 ms and load 2.7 ms,
against 0.5 and 1.7 there.

**New, and not a leak failure: six `PARANOIA/P_SetTarget ... MT_RING ...
references=-1, references go negative!`**, all from `src/acs/thread.hpp:119`.
No Skyscraper Leaps log has one; Northern District runs ACS (861 bytes, 8.40),
Skyscraper Leaps none. So it comes from ACS, which no speculation had
exercised before.

Read in the code, **not proven**:

- Line 119 is `ThreadInfo::operator=`: it sets `thread_era = thinker_era`,
  then `P_SetTarget(&mo, info.mo)`, which releases the old `mo` -- one
  reference down on whatever it points at. `Thread::start` and
  `Thread::stop` both assign (`src/acs/thread.cpp`).
- `thinker_era` exists so that a `mo` from before the thinkers were rebuilt
  is never touched: the destructor (`thread.hpp:110`) and the environment
  (`environment.cpp:376`) only release it `if (thread_era == thinker_era)`.
  The assignment does not check.
- **Every restore rebuilds the thinkers**: `P_LoadNetGame` calls
  `P_InitThinkers`, which bumps `thinker_era` (`p_tick.c:299`). Stock Ring
  Racers restores on a join or a resync; a rollback client restores on every
  pass, and the soak every 20 tics.
- So a thread whose `mo` dates from before a restore, assigned after it,
  takes one reference off an object that was freed -- or off a new object
  allocated at the same address. Here that was a ring: -1.

A count that goes negative can keep an object from ever being freed. A count
that reaches 0 early frees an object that is still referenced. The second is
a crash, or a world that differs on one machine only -- on a client, whose
confirmed world is restored every pass. It is not seen in this soak's leak
checks, which compare archives, and the reference count is not archived.

**Proposed, not coded** (it touches `src/`, and `acs/` is upstream code): the
destructor's guard in the assignment too -- forget, rather than release, a
`mo` from an older era. The prediction for that change: the next leak soak on
an ACS map prints no `references go negative`.

**Harness defect, found at the first launch and fixed.** `rrww_map_exists`
(`map_override.sh`, 2026-09-23) grepped each `.pk3` as text. A 300 MB archive
with almost no newline is one "line" of hundreds of MB: `grep` aborted on
every file, and the check refused a map that `maps.py` lists, before
launching anything. It had only been tried on fake files. It now reads each
zip's member names with Python, as `maps.py` does. A dry run on the real
`.pk3` files found the six maps of the shortlist and Skyscraper Leaps,
refused a made-up name, and accepted a `/d/` path.

**Instrument limit, not a defect:** `rollback_test` reports "42 appeared from
nowhere" here and 0 on Skyscraper Leaps. `K_CopyMobjs` copies at most 2048
objects before the restore (`k_rollback.c`), and Northern District has 2090:
2090 - 2048 = 42 with no copy to compare against. Opulence (3538 things)
will show more. The message should say "not copied".

### 8.52 Northern District's resim soak holds, and the ACS guard is built

**Measured:** binary `88d8a878f`, `soak.sh ww map=RR_NorthernDistrict`,
unattended, one instance, eight karts, `rollback_soak 15 4`. Log kept as
`soaklog_ww_RR_NorthernDistrict_20260928-132403_88d8a87.txt`. **347 resim
checks, 0 failures**; `rollback_detect` reports nothing; and **no `references
go negative`** -- the leak soak's six (8.51) come from its restores, not from
ACS running on its own. 8.40's prediction for Northern District holds on both
soaks. The script exited 1 on this clean run: its last line greps the
`rollback_test` profile, which the `ww` scenario does not print. Fixed in
`soak.sh`.

**Built on 2026-09-28, not run:** the guard of 8.51, in `src/acs/` (upstream
code). Reading the load path while writing it showed that the assignment
alone would not do:

- `ThreadInfo::operator=` (`thread.hpp`): a `mo` from an older era is
  forgotten, as the destructor does, instead of released.
- `Thread::loadState` (`thread.cpp`) takes a reference on the thread's new `mo`
  without stamping the era. With only the first change, that reference would
  then be forgotten too, never given back, and its object never freed. So the
  load now forgets a `mo` from an older era, stamps `thread_era =
  thinker_era`, then reads the new one. That also fixes a second gap: a
  thread saved with no `mo` came back with the stale one it held.

Syntax checked with MSYS2 `g++ -fsyntax-only -Wall -Wextra -std=c++17` on
`thread.cpp`, which includes `thread.hpp`: no diagnostic. A copy with an
error injected into each changed function failed on both, so both are
compiled. Not compiled -- CI is the only build.

**Prediction, written before it runs:** `soak.sh leak map=RR_NorthernDistrict`
on the build with the guard prints **no `references go negative`**, and its
leak checks still fail only on `itemList.cap`. On Skyscraper Leaps, which has
no ACS, nothing changes.

### 8.53 The ACS guard, measured: no reference count goes negative

Measured on 2026-09-28: binary `9652ccc7c` (sha checked by the harness), the
same `soak.sh leak map=RR_NorthernDistrict` as 8.51, unattended. Prediction
written in 8.52. Log kept as
`soaklog_leak_RR_NorthernDistrict_20260928-133715_9652ccc.txt`.

| build | checks | failures | `references go negative` | any PARANOIA |
|---|---|---|---|---|
| `88d8a878f` (8.51) | 258 | 1, `itemList.cap` | **6** | 6 |
| `9652ccc7c`, the guard | 260 | 1, `itemList.cap` | **0** | 0 |

**The prediction holds.** 0 of 1636 objects differ, and the round trip after
the soak is byte-identical. One soak each side, so this is 6 against 0 in
about 260 checks, not a rate. But the warning named the one line the guard
changes, and it is gone.

**What it does not show.** A reference the guard forgets instead of giving back
would keep an object from being freed: a slow leak of memory, not of state,
which a leak soak does not see. The load path was changed so that it takes
its references in the current era (8.52), and nothing in this run points to
such a leak. It is still not measured.

**What it changes for the other maps.** Coastal Temple (2537 bytes of ACS)
and Death Egg (3132) run far more ACS than Northern District's 861. They
now go through their soaks on a build where an ACS thread cannot take a
reference off freed memory at a restore.

### 8.54 Carnival Night: a spray can re-derived from gamedata at every load, and a player's reference count

Measured on 2026-09-28: binary `9652ccc7c`, `soak.sh leak
map=RR_CarnivalNight`, unattended, the first of a series of ten soaks
Gibax approved to stop at the first problem. It stopped there. Log kept as
`soaklog_leak_RR_CarnivalNight_20260928-134215_9652ccc.txt`. 263 checks, 2
failures, and the round trip after the soak identical.

**Failure 1 (leveltime 1760): `itemList.cap`**, the known case -- a players
block byte `00`->`20`, 1012 bytes into the record. The struct walker's twelve
shown runs cut off the 672 line this time, so the series' checker now
accepts the archive-side signature as well.

**Failure 2 (leveltime 2040): a spray can, and the cause is in the load
path.** An honest pass ("a pass on the REAL inputs already changed the tic
that followed it"): object 1238, `MT_SPRAYCAN`, gains `MD2_RENDERFLAGS` in
`diff2` (`08008008` -> `08018008`), 4 more bytes, the one object of 1924 that
differs. Read in the code and the log:

- `P_NetUnArchiveThinkers` calls `P_SprayCanInit` on every spray can it loads
  (`p_saveg.cpp`). `P_SprayCanInit` sets the can's `renderflags` -- 50%
  translucent if this machine has already grabbed it -- from **`gamedata`**:
  the unlock progress of this install, saved to disk, and neither archived
  nor restored.
- Grabbing a can (`P_TouchSpecialThing`, party players only, never a bot)
  updates `gamedata` and saves it. This log has three "Gamedata saved"
  lines; every other soak log has two, at the start and the end. The third
  comes just before this failure.
- So the local kart grabbed the can during the soak. The next restore
  re-derived the can's look from the new `gamedata`: translucent. The
  reference had been archived before: opaque. That is the difference.

What it means for a race is larger than the soak. `renderflags` is drawn, not
simulated, and not in the checksum. But **grabbing a can is a persistent side
effect -- an unlock saved to disk -- run inside whatever tic grabs it**. On a
client, a speculated tic that grabs a can the confirmed world never reaches
unlocks it anyway, and keeps it. The same goes for anything else that writes
`gamedata` from a tic; which other paths do has not been read yet.

**New PARANOIA, not explained:** twice, between the 90th and 100th checks
(leveltime about 2480 to 2680),
`P_SetTarget: ... MT_PLAYER P_RemoveThinkerDelayed references=-1` from
`p_mobj.c:10827` -- `P_MobjThinker` dropping a target that was removed, here
a player's kart object, whose count was already 0. Not ACS (the guard of
8.52 is in this build). Seen on this map only so far.

**Proposed, not coded:**

- On a rollback restore (a local snapshot), keep the archived `renderflags`
  of a spray can instead of re-deriving them from `gamedata`.
- Keep speculated tics from writing `gamedata`: an unlock belongs to the
  confirmed timeline. Which paths write it during a tic is to be read first.

**The series goes on** with the four other maps (Labyrinth, Coastal Temple,
Death Egg, Opulence), with both cases above as known -- reported, not a stop
-- at Gibax's choice. Carnival Night's resim soak (`ww`) waits for them.

**Also read, for the fix:** `gamedata` is written from inside a tic in
several places, each followed by a deferred save -- emblems
(`p_inter.c:794`), spray cans (`p_inter.c:833-896`), prison-egg pickups
(`p_inter.c:966-978`), an unlock in `p_mobj.c:7302`, a condition check in
`p_mobj.c:12138`. The spray can is one case of a family: anything a tic
unlocks.

### 8.55 Labyrinth: fifteen honest-pass failures, all on pairs of `MT_THOK`

Measured on 2026-09-28: binary `9652ccc7c`, `soak.sh leak map=RR_Labyrinth`,
unattended, the first soak of the resumed series; it stopped there. Log kept
as `soaklog_leak_RR_Labyrinth_20260928-134917_9652ccc.txt`.

**267 checks, 15 failures -- 5.6%, against 1 in 260 on Skyscraper Leaps and
Northern District.** No PARANOIA; the round trip after the soak is identical;
no player differs in the archive. All fifteen are the same kind:

- an honest pass ("a pass on the REAL inputs already changed the tic that
  followed it"): running extra tics at all, no misprediction involved;
- in the thinkers block, **two objects each time (three in four cases), all
  `MT_THOK`**, with the same masks -- position, type, momentum, tics, sprite,
  frame, eflags, and `MD2_RENDERFLAGS`;
- the record keeps its size; 8 bytes differ in 4 runs, the low halves of two
  32-bit fields right after the type, the high halves equal -- small
  differences in values like an angle or a momentum, not objects appearing
  or vanishing.

**Not identified. A candidate, by reading only:** `P_MobjRegularThink` spawns
exactly two `MT_THOK` a tic as "wave effects" (`S_BUBBLESHIELDWAVE1`) behind
an object moving near the floor, each with an angle perpendicular to its
parent's and a `P_Thrust` along it (`p_mobj.c`, the case around line 10150).
Pairs of `MT_THOK` fit it. What makes them differ after an extra pass is not
known: the parent's own record does not differ. `MT_THOK` is spawned by at
least twenty sites, so the candidate is a lead, not a finding. The leak
report names the changed objects by type only; naming their state would
settle which effect it is.

**What it means.** `MT_THOK` is a visual effect: if nothing in the simulation
reads these, they are harmless to the race and only make the leak soak noisy
on this map -- 15 failures a soak hide anything else. That "nothing reads
them" has not been checked.

**Where the series stands.** Labyrinth's resim soak, Coastal Temple, Death Egg
and Opulence were not run. Three open items from these soaks, none explained
to the end: the spray can and `gamedata` (8.54, mechanism read), the player's
reference count (8.54), and these `MT_THOK` pairs.

### 8.56 The resim soaks of Labyrinth and Carnival Night: a player on a Garden Top

Measured on 2026-09-28: binary `9652ccc7c`, `soak.sh ww` on each map,
unattended, the first two soaks of the series resumed after 8.55 with the
`MT_THOK` pairs as known. Logs kept as
`soaklog_ww_RR_Labyrinth_20260928-140410_9652ccc.txt` and
`soaklog_ww_RR_CarnivalNight_20260928-140706_9652ccc.txt`.

- **Labyrinth: 312 resim checks, 0 failures.** The `MT_THOK` pairs of its leak
  soak (8.55) do not show in a resim: they need an extra pass, which a resim
  check does not make. The game used 1.24 GB during this soak, against 387 MB
  during Northern District's leak soak; one reading each, not compared on the
  same scenario.
- **Carnival Night: 347 resim checks, 2 failures**, at leveltime 4650 and
  4680. Both the same: after a restore, the replayed tics differ from the
  first pass on **one player's kart (`MT_PLAYER`, the same object both times)
  and one `MT_GARDENTOP`** -- the kart's `spritexoffset` (and
  `old_spritexoffset`), the top's `rollangle`, and `shadowcolor` on a few
  smoke objects. Every damage event and every collision pair agrees
  between the passes, and the two restored passes agree with each other:
  "the replay is repeatable and it is the restore that loses something the
  simulation uses". So a player was riding a Garden Top, and the restore does
  not bring back something its ride reads.

Read, not proven: the top's tilt (`gardentop.c`, `tilt`) steps `rollangle`
from its own value and the rider's `steering`, and the vibration sets
`spritexoffset` from `topinfirst` and `leveltime`. The restore's own report
lists, besides the fields every soak shows, bytes of `player_t` that are not
put back -- at 44, 106-111 and 252 -- which the struct walker cannot name.
Named afterwards from the `.pdb` (`cdb`, `dt player_t`, no launch): 44 is
`viewz`, 106-111 are `old_drawangle` and `old_drawangle2`, 252 is
`karthud[6]` -- the camera, drawing interpolation and the HUD, none of them
simulation. And what the tilt reads -- `steering`, `topinfirst`,
`topdriftheld`, `topAccel` -- is archived (`p_saveg.cpp`). So the obvious
candidates are ruled out, and what the restore loses for a Garden Top rider
is not found. The fields that differ are drawn, not hashed; whether the
ride's physics reads any of them has not been checked.

**The series stopped here, and partly by my error.** I committed the
state-naming instrument to `src/` locally while it ran; the harness then saw
an exe that is not the code repository's `HEAD` and stopped with a warning
before its checker ran. The Carnival Night resim failures would have
stopped it anyway. The commit was moved to a local branch
(`wip/leak-identity`), `HEAD` is back on the pushed branch, and the soak
itself ran on the right exe. Not run yet: Coastal Temple, Death Egg,
Opulence.

### 8.57 The state-naming instrument, and Coastal Temple: the polyobject leak

**The instrument, pushed and measured.** Commit `854bcf5e9` (Gibax's go-ahead,
CI green, installed, sha checked): each per-object capture keeps the living
object's state, sprite, frame and target, and a changed object prints them.
On its first run -- Labyrinth's leak soak again (262 checks, 5 failures: 4
`MT_THOK`, 1 `itemList.cap`; log
`soaklog_leak_RR_Labyrinth_20260928-142058_854bcf5.txt`) -- it named every
changed `MT_THOK`: **`S_THOK`, sprite `TRCK`, frame 0 or 1, no target.** Only
`K_trickPanelTimingVisual` (`k_kart.c`) spawns that sprite: the two half
circles of the trick-panel timing visual, frames 0 and 1 -- the pairs. It
places them with `R_PointToAnglePlayer`, which measures from **the local
camera** (`camera[i].x/y`, else `viewx/viewy`): drawing state, never archived,
different on every machine. So they move with the camera across a restore.
**8.55 is explained: a view-dependent effect built into the game, drawn only,
not hashed.** No fix is proposed. The candidate 8.55 named (the bubble waves)
was wrong: those set a state other than their spawn state, and these masks
had no `MD_STATE`.

**Coastal Temple's leak soak: 261 checks, 10 failures, 9 of them in the
polyobjects block** (the tenth `itemList.cap`; log
`soaklog_leak_RR_CoastalTemple_20260928-142356_854bcf5.txt`). All nine after
an honest pass, no object record differing, and the archive **24 bytes longer
every time** (142 544 -> 142 568, for one). The polyobject record shows its
`diff` byte gaining `PD_TRANS` and a translucency of 3 or 4 where the
reference had none.

> ⚠ Wrong for these failures (8.59): ownership does not keep a fade out.
> The restore left the translucency changed, because the polyobject archive
> only carries values that differ from spawn and a reload does not reset them.

**The mechanism, read in the code and matched to the bytes:**

- A load nulls every `po->thinker` (`P_UnArchivePolyObj`): "the thinkers
  themselves will fight over who gets the field when they first start to
  run".
- The polyobject actions refuse to start on a polyobject that has a thinker
  -- "Don't crowd out another thinker", `if (po->isBad || po->thinker)
  return` (`p_polyobj.c`).
- Between a restore and the first run of the restored thinker, the
  polyobject looks idle, so a trigger in that window **starts a second
  action** the unbroken run had refused.
- A polyobject fade thinker archives as exactly **24 bytes** (type, number,
  source, destination, three flags, duration, timer), and it changes the
  translucency. That is the growth and the `PD_TRANS`.

A fade can also switch the polyobject's collision (`docollision`), and a
second move or rotate would move it. So this is not only drawn: **on a map with
polyobjects, a client that restores every pass can start polyobject actions
the server never started.** The second leak Phase A was looking for, on the
maps that have them (8.40 predicted it would be named in this block).

**Coastal Temple's resim soak: 347 checks, 6 failures** (log
`soaklog_ww_RR_CoastalTemple_20260928-142947_854bcf5.txt`). The first names
three `MT_WAYPOINT` objects whose record changes by one in a counter field
(`0x03` -> `0x02`); the struct walker also lists `po_movecount` on rings --
"NOT savegame", compared with an ever-growing counter, harmless on reading --
and shadow fields. Not analysed further yet.

> ⚠ Pushed on 2026-09-28 as `12c2fa755`, `f185713cc` and `74df18f95`,
> with a fourth fix, the dynamic slope's plane (8.58).

**Built, not pushed, not run** (Gibax asked for the fixes after the series):

- **Polyobject ownership** (`p_saveg.cpp`): a local snapshot records, for each
  of the eight polyobject thinker types, whether it owns its polyobject, and
  a local restore gives the ownership back as it loads the thinker, before
  anything runs. Network gamestates keep the stock format.
- **Unlocks and `gamedata` on off-timeline tics** (`p_inter.c`, `p_mobj.c`,
  `k_rollback.c`): an emblem, a spray can, a prison-egg pickup, the Mystic
  Melody record and the challenge-destructible condition check write nothing
  while `K_RollbackReplaying()` -- a speculation, a correction replay, and
  now the soak's check passes (`K_RunFrozenTics`).
- **`roundconditions`** (`p_saveg.cpp`): only `unlocktriggers` is archived;
  the rest -- what happened this round, for the challenges -- goes into
  local snapshots whole, so a speculated tic cannot count towards an unlock.

**Predictions for that build, written before it runs:** Coastal Temple's leak
soak shows no polyobjects-block failure; Carnival Night's leak soak shows no
spray-can failure even when the can is grabbed; and the struct walker's
lines at 1828/1832 -- inside `roundconditions` -- stop appearing.

### 8.58 The end of the series: Death Egg holds, and Opulence reads a dynamic slope the restore never put back

**Death Egg, on `854bcf5e9`.** Leak soak: 274 checks, 2 failures, both
`itemList.cap` (log `soaklog_leak_RR_DeathEgg_20260928-143404_854bcf5.txt`).
Resim soak: 357 checks, **0 failures**
(`soaklog_ww_RR_DeathEgg_20260928-143704_854bcf5.txt`).

**Opulence, on `854bcf5e9`: 37 failures in each soak, on the karts.**

- Leak soak: 284 checks, 37 failures
  (`soaklog_leak_RR_Opulence_20260928-144016_854bcf5.txt`). One is
  `itemList.cap`. The other 36 all follow an **honest pass** -- the same
  inputs, four extra tics, a restore -- and every object shown in full is a
  kart or one of its effects: `MT_PLAYER` 20 times, `MT_SMOOTHLANDING` 20,
  `MT_WAVEDASH` 20, `MT_FASTLINE` 19, `MT_DRAFTDUST` 4. Every bot is named
  in turn, and states and masks stay the same: the values move, not the
  shape. When the first difference falls in the players block, it is 996 to
  1001 bytes into a bot's record, "past the fields this can name" (the one
  at 1006 is the `itemList.cap`).
  Walking `P_NetArchivePlayers` byte by byte on the dump places it in
  `botvars.recentDeflection` and `botvars.lastAngle`, which
  `K_UpdateBotGameplayVars` derives from the kart's momentum angle
  (`k_bot.cpp`). The kart moved differently.
- Resim soak: 375 checks, 37 failures
  (`soaklog_ww_RR_Opulence_20260928-144324_854bcf5.txt`), every one "the two
  restored passes agree with each other": the restore loses something. The
  players block names kart `speed` and `angleturn`; the thinkers block names
  `MT_FASTLINE` and `MT_SNEAKERTRAIL`. Both passes examine **the same
  collision pairs in the same order** and agree on every hit event.
- Both soaks fail in the same two stretches: leveltime 1620 to 2300 and 4240
  to 4660 (leak), 1710 to 2160 and 4245 to 4545 (resim). The two stretches
  are 2620 tics apart -- a lap. **One part of the track.**
- The first resim failure is different: at leveltime 705, the very first
  check, 11 decorations (splash flowers, a tumble gem) differ by one byte.
  Decoded on the dump, that byte is `chainorder_block`, the object's place
  in its blockmap chain (1 on the live side, 0 on the restored). It does not
  come back in the 374 checks that follow. Not analysed.

**Why the per-object comparison named nothing.** On Opulence the karts carry
mobjnums above 3100, because 3538 map things are numbered first, and
`K_CopyMobjs` stops at 2048 objects: "1791 appeared from nowhere" on every
check. The resim soak's field comparison never looked at a kart. That is an
instrument limit (8.40 called Opulence "3.6 times Skyscraper Leaps").

**What Opulence has that the other maps do not** (read from its `TEXTMAP` and
`scripts.pk3`): 41 slopes flagged dynamic (special 700, `TMSL_DYNAMIC`), 37
continuous plane movers (special 53, tags 650 to 697), 1127 slope anchors,
and Lua-driven tumble gems and coins. No polyobject.

**The mechanism, read in the code:**

- A dynamic slope's plane (`pslope_t`: `o`, `normal`, `d`, `zdelta`,
  `zangle`, `xydirection`) is recomputed from its control sectors by its
  thinker, `T_DynamicSlopeLine` or `T_DynamicSlopeVert` (`p_slopes.c`), in
  `THINK_DYNSLOPE` -- the first list `P_RunThinkers` runs.
- **`P_Ticker` runs `P_PlayerThink` before `P_RunThinkers`** (`p_tick.c`),
  and `P_3dMovement` reads the plane under the kart to direct its thrust:
  `zdelta`, `xydirection`, and `P_QuantizeMomentumToSlope` on the normal
  (`p_user.c:2145-2166`). Effects spawned in that step take
  `P_GetMobjZMovement`, which reads the plane too.
- The archive carries the slope thinkers, with the slope's id, but **not the
  plane**. A load leaves the plane as the last tics run left it.
- So the first tic after a restore steers the karts on the plane of **the
  tics before the restore**: four tics ahead after the leak check's honest
  pass, the first pass's end in the resim check. Everywhere else the plane
  is the one its thinker computed in the previous tic. On a floor that moves,
  those differ. Hence one stretch of the lap, every kart, speed and angle,
  and the effects spawned from them.
- The `MT_FASTLINE`-only failures fit: the fast line's first archived field,
  `z`, is the one that moves, and its `momz` is three quarters of the kart's
  `P_GetMobjZMovement`. `MT_SMOOTHLANDING` is the slope-landing effect.

**This is not only the instruments.** Every pass of the netcode restores the
confirmed snapshot after a speculation of `rollback_twoclock` tics, then runs
the next confirmed tic. On a dynamic slope over a moving floor, that tic
reads a plane `rollback_twoclock` tics ahead, and the server never restores.
**On such a map, the confirmed world can part from the server's** -- the
second leak Phase A was looking for, on the maps that have them.

**Fixed:** `8749842d6` (Gibax's go-ahead, pushed 2026-09-28). Each dynamic
slope thinker writes its plane into a local snapshot, and a local restore
puts it back as it loads the thinker, before anything runs. Network
gamestates keep the stock format. Syntax checked, with an injected error as
the counter-test. With it, 8.57's three fixes, split one subject a commit:
`12c2fa755` (`gamedata` on off-timeline tics), `f185713cc` (polyobject
ownership), `74df18f95` (`roundconditions`).

**Left as it is:** a network load -- a joining client, a full-state resend --
still keeps whatever plane the client had. Stock does the same: a joiner
reads the map's plane until the thinker's first run. A recompute after such
a load would be one tic off while a floor moves; not done.

**Predictions for `8749842d6`, written before it runs:**

- Opulence, leak soak: no honest-pass failure on a kart or its effects;
  `itemList.cap` may remain.
- Opulence, resim soak: none of the 36 kart failures. The first-check
  `chainorder_block` failure has another cause and may remain.
- 8.57's still stand (⚠ the first fails, 8.59): no polyobjects-block failure on Coastal Temple, no
  spray-can failure on Carnival Night, no struct-walker line at 1828/1832
  -- on Opulence's leak soak too, where they appear today.

### 8.59 The fixes measured: Opulence holds, and Coastal Temple's polyobject leak was not ownership

Measured on 2026-09-28, binary `8749842d6` (Gibax's go-ahead for the install
and the four soaks; sha256 `17b8358e...`), unattended, one after the other.
Predictions in 8.57 and 8.58.

| soak | before | on `8749842d6` | log |
|---|---|---|---|
| Opulence, leak | 37 of 284 | **0 of 292** | `soaklog_leak_RR_Opulence_20260928-151947_8749842.txt` |
| Opulence, resim | 37 of 375 | **1 of 386** | `soaklog_ww_RR_Opulence_20260928-152300_8749842.txt` |
| Coastal Temple, leak | 10 of 261 | **9 of 261** | `soaklog_leak_RR_CoastalTemple_20260928-152559_8749842.txt` |
| Carnival Night, leak | 3 kinds | **1 of 261**, `itemList.cap` | `soaklog_leak_RR_CarnivalNight_20260928-152857_8749842.txt` |

- **8.58's predictions hold.** Opulence's 36 kart failures are gone from both
  soaks. The one resim failure left is the first check's, at leveltime 705,
  byte for byte the one 8.58 set aside: `chainorder_block` of splash flowers
  and a tumble gem.
- **The struct walker's lines at 1828/1832 are gone** wherever a failure
  prints the walker: 0 on Coastal Temple (21 before), 0 on Carnival Night (6
  before). 8.57's `roundconditions` prediction holds.
- **The spray can:** no failure, but nothing shows the can was grabbed during
  this soak. Not tested, then.
- **Coastal Temple: 8.57's prediction fails.** Nine failures, the same
  signature: honest pass, polyobjects block first, archive 24 bytes longer,
  the polyobject's `diff` gaining `PD_TRANS` with a translucency of 3. The
  ownership fix (`f185713cc`) changed nothing here. Also two `MT_PLAYER`
  reference-count lines (8.54's, known).

**The real mechanism, read in the code:**

- `EV_DoPolyObjFade` never refuses to start: it removes the running fade only
  if `po->thinker` is that fade, and it **does nothing when the translucency
  already is its destination** (`p_polyobj.c`). So ownership was never what
  kept a second fade out; 8.57 read the code of the move and rotate actions
  and applied it to the fade.
- `P_UnArchivePolyObj` writes `flags` and `translucency` **only when the diff
  bit is set** -- the values differ from spawn. That is right for a load into
  a freshly loaded level. A reload does not reload the level:
  `P_NetUnArchiveMisc` puts sectors, lines and sides back to their spawn
  values first, **but not the polyobjects**.
- So when the snapshot's translucency was the spawn value and the extra pass
  ran a fade, the restore left the faded value. On the next tic the fade
  trigger found the translucency not at its destination and started a fade the
  reference never had: one more thinker (24 bytes), and `PD_TRANS` set.

**Fixed, not pushed: `8142e07c4`.** When the bit is absent, the load puts the
spawn value back. No format change; right for every load, local or not.

**Prediction for it:** Coastal Temple's leak soak shows no polyobjects-block
failure. The ownership byte of `f185713cc` stays: it restores a true piece of
state, and the move and rotate actions do refuse on it -- but no failure has
been traced to it.


### 8.60 What a pass sends, rebuilds and costs, against a tic -- and where the stutter comes from

Gibax's question (2026-09-28): what is sent and what is re-simulated, by weight
and by milliseconds against a tic, to explain the latency and the stutter felt
in 8.50 -- then ways to make it cheaper. Read from the code and from logs
already kept; nothing launched for it. Every figure is labelled measured or
estimated.

**The rhythm (read).** `D_SRB2Loop` calls `TryRunTics` only when a real tic
has elapsed (`renderisnewtic`, `d_main.cpp`), so there is **one pass per tic,
35 a second, not one per frame**. A pass is, in order: undo the speculation
(`K_RollbackUnspeculate`, a full `K_LoadGameState`), `NetUpdate` and
`GetPackets`, apply a correction, run the confirmed tics (usually one), save
the new frontier (`K_SaveGameState`), run `rollback_twoclock` speculated tics
through `G_Ticker`, then draw. **All of it inside one frame.** Stock runs one
tic a tic.

**What a pass costs (measured, Skyscraper Leaps, 9 karts, driven, 8.50's
race):**

| | per pass | of a 28.6 ms tic |
|---|---|---|
| undo the speculation (restore) | 2.6 - 2.9 ms | 9 - 10% |
| 4 speculated tics | 5.1 - 5.5 ms, so **1.3 - 1.4 ms a tic** | 18 - 19% |
| **pass as reported** | **7.7 - 8.4 ms** | **27 - 29%** |
| same, `rollback_history` on (about 8 tics) | 13.5 ms | 47% |
| same, 12 tics (8.48) | 17.9 ms | 63% |

Not in those figures: the save of the frontier (0.5 to 0.9 ms there), the
confirmed tic itself (about one more tic of simulation, which stock pays too),
the network.

**The snapshot, by map (measured: `rollback_test` at the end of each leak
soak, one sample each, late in the soak):**

| map | snapshot | save | load |
|---|---|---|---|
| Northern District | 127 - 184 KB | 0.9 ms | 2.5 - 2.7 ms |
| Coastal Temple | 156 KB | 1.1 ms | 2.9 ms |
| Carnival Night | 181 KB | 1.2 ms | 3.2 ms |
| Death Egg | 226 KB | 1.7 ms | 5.2 ms |
| Labyrinth | 186 - 289 KB | 1.4 ms | 5.1 - 6.2 ms |
| **Opulence** | **281 - 294 KB** | **2.4 - 3.6 ms** | **6.5 ms** |

Where a load's time goes (measured: `rollback_test`'s restore profile, same
samples):

| step | Northern District | Coastal Temple | Death Egg | Opulence |
|---|---|---|---|---|
| free every thinker ("thinkers purge") | 0.47 ms | 0.71 ms | 1.04 ms | 1.75 ms |
| re-create them ("thinkers") | 0.83 ms | 1.07 ms | 1.19 ms | 2.13 ms |
| waypoints | 0.27 ms | 0.11 ms | **1.46 ms** | **0.95 ms** |
| chain order | 0.31 ms | 0.36 ms | 0.51 ms | 0.90 ms |
| misc, relink, the rest | 0.5 ms | 0.5 ms | 0.8 ms | 0.7 ms |

Freeing and re-creating every object is 60% of the load on Opulence. The
waypoint step is a plain inefficiency, read in the code: each waypoint finds
its object with `P_FindNewPosition`, which walks the whole mobj list when the
relink index is not built -- and it is built only by `P_RelinkPointers`, the
step *after* the waypoints (`P_LoadNetGame`). About 150 waypoints times 3700
objects on Opulence.

The load grows with the snapshot, about 2.3 ms per 100 KB. **Estimated** for
Opulence at depth 4: 6.5 + 3 + 5 tics of at least 1.3 ms = **15 ms or more a
pass, over half a tic.** The per-tic cost there is not measured.

**What that does to frames (reasoned from the above; frame times are not
logged).** At 60 Hz a frame has 16.7 ms. The frame that carries the pass keeps
what the pass leaves: about 8 ms on Skyscraper Leaps, 3 ms with history on,
nothing on Opulence -- **that frame is late, 35 times a second**, a regular
hitch that reads as dropped frames. At 144 Hz (6.9 ms) every pass frame is late
on every map. On the measuring machine the server's instance shares the CPU,
which a real client does not.

**What crosses the wire (read from the packet structs; per client, 9 karts,
`rollback_correct 4`):**

| | size | rate | weight |
|---|---|---|---|
| `PT_CLIENTCMD`, client to server | 5 + 16-byte ticcmd + 8 header = 29 B | 35/s | about 1 KB/s |
| `PT_SERVERTICS`, server to client | 3 + 16 B a player + 8 = 155 B, at least | 35/s | about 5.4 KB/s |
| `PT_STATECORRECTION` | 26 + 56 B a kart = 530 B | 1 every 4 tics | about 4.6 KB/s |
| full-state resend (stock) | 107 - 318 KiB, then an 11 ms load | 0 a race with the channel, 7 - 9 without | -- |

Plus 28 bytes of UDP/IP a packet. **About 11 KB/s down and 2 KB/s up: the wire
is not where the stutter comes from.** The heavy thing is the snapshot, and it
never leaves the machine: 107 to 294 KB written and read back 35 times a
second, 4 to 10 MB/s of serialisation.

**Latency as the player sees it (the test setup: `rollback_lag 6` adds 171 ms
to everything the client receives; `rollback_twoclock 4` speculates 114 ms).**

- **Your own kart:** your input enters the next speculation and is drawn on the
  next tic -- no input delay (8.36).
- **The confirmed world** trails the server by the delay, 6 tics or more, plus
  the server's filing. The drawn world is 4 tics further on, so at `lag 6`
  still 2 or more behind the server's present. `rollback_history` exists to
  close that gap (8.39).
- **The other karts** are drawn where the speculation puts them: repeating a
  person's last known input, recomputing a bot from this machine's world. When
  the real input differs, the next pass redraws them from the corrected past,
  and **the kart moves by up to 4 tics of error at once: the visible "à-coup"**,
  larger the deeper the speculation.
- **Sounds wait for the confirmed tic** (read: `S_StartSoundAtVolume` returns
  while `K_RollbackReplaying()`, which a speculation counts as, `s_sound.c`).
  Every sound, your own included, plays when the authoritative loop reaches
  its tic: behind the picture by the whole lead -- delay plus depth, about 10
  tics or 290 ms in the test setup.

**Why the pass cannot simply be spread over two frames (read).** The game
draws the one world in memory. Between the restore and the end of the
speculation that world is in the past, and a frame drawn then would show
everything jump back. So the pass has to finish before the next frame, whole.
Spreading it means changing *what* is done each tic, not *when*.

**Ways to make it cheaper, in the order proposed -- none built, none measured,
every figure an estimate from the measurements above:**

1. **Measure first** (a small instrument): every frame's duration, with `nospec`
   as the control -- the one figure the stutter needs and nobody has; the cost
   of each step of a pass, the save and the confirmed tic included; and how
   often the speculation was right (per pass: did every newly confirmed tic
   carry the inputs the speculation used). The per-map cost needs no code:
   `rollback_maxdepth 4`, `rollback_test`, `rollback_delay` at the end of the
   resim soak, added to `soak_ww.cfg` on 2026-09-28.
2. **The waypoint relink through the index** (small, read): build the relink
   index before the waypoints are restored, not after. About 1 ms a restore
   on Opulence, 1.5 on Death Egg, for a few lines.
3. **Keep the speculation when it was right** (the large one; GGPO's own
   shape). The speculation stays standing from one tic to the next. Each tic
   runs one more tic at its front and saves that state into a ring of
   snapshots: about 2 ms on Skyscraper Leaps, 4 to 5 on Opulence, instead of
   10 and 15 or more. When the server confirms a tic, its inputs are compared
   with those the speculation used: equal, nothing to do -- the speculated tic
   *is* the confirmed one; different, restore the snapshot before the first
   wrong tic and re-run from there only. The steady cost becomes small and
   even; a burst comes only with a misprediction, and scales with it.
   **Sounds** could then play on a tic's first run and be kept from replaying
   on a re-run, instead of waiting for the confirmation. What it needs:
   - a speculated tic has to do exactly what the confirmed one does. Today some
     guards change what a replayed tic does (the spray can is not grabbed,
     `12c2fa755`): such side effects have to be queued until confirmation
     instead of skipped;
   - the local input is built from the kart's angle, which is why the
     speculation is undone before `NetUpdate` today (`d_clisrv.c`); it will
     need the confirmed angle some other way;
   - the consistency checksum computed when the tic runs, and kept with it;
   - a correction that changes nothing (drift at 0.000) must not force a
     rebuild;
   - the hit rate, measured first. Bots are recomputed from the same world, so
     they should hold; a remote person on an analogue stick changes `turning`
     most tics, so repeat-last will often miss -- many short rollbacks.
   Behind a switch, off by default, validated by the soaks -- which test
   exactly what it relies on, that a restore and a replay give the same world.
4. **A cheaper restore**, since mispredictions will still need one. Freeing
   and re-creating every object is 60% of Opulence's load (1.75 + 2.13 ms):
   keep objects removed during a speculation aside instead of freeing them, so
   a restore finds every object at its old address, copies its fields back,
   relinks it only if it moved, and removes those born in the speculation --
   the 2199 rings and the still decorations are then not touched at all. Then
   a local snapshot in raw memory for objects and players (the whole struct,
   about 2.2 MB on Opulence, copied in about 0.3 ms) rather than the network
   format field by field (3.6 ms to write 294 KB there); the network format
   stays for Lua, ACS and the wire. That also makes track 3's ring of
   snapshots nearly free.
5. **Smaller ones:** a depth taken from the measured delay (each tic of depth
   is about 1.3 ms); the speculated tic profiled (the game already times its
   thinker lists, player thinks, bot commands and ACS -- `ps_thlist_times`,
   `ps_playerthink_time`, `ps_botticcmd_time`, `ps_acs_time`); a corrected
   kart drawn with an offset that decays over a few frames instead of a jump;
   and on the wire, which is not latency: the correction without its 18
   diagnostic bytes a kart (-32%), at a lower rate now that drift reads 0.000,
   and `PT_SERVERTICS` packed in WORLDWIDE mode -- about 10 KB/s down to 3.

**Estimated:** Opulence from 15 ms or more every tic to about 4 or 5 in the
steady state, bursts only on a misprediction, and smaller ones after track 4.

### 8.61 Steps 0 and 1 of 8.60, pushed, and what their first runs should show

Gibax, 2026-09-28: "fais dans l'ordre que tu proposes", then chose the order
of the rest after the pros and cons of each track: **B1 (restore in place),
then A (keep the speculation when it was right), then B2 (raw snapshot)**,
after the measurement. Pushed with his go-ahead:

- `8142e07c4` -- polyobject flags and translucency back to spawn values on a
  reload (8.59; first committed as `a59fa6203`, reordered before its push).
- `865f79d95` -- **the instrument (step 0).** `rollback_twoclock`'s report
  adds `rollback_cost` (per pass: restore, network, correction, confirmed
  tics, save, speculation), `rollback_frames` (each main-loop iteration's work
  before its sleep, with and without a pass, in buckets; the gap between two
  drawn frames; iterations that ran past a tic, after which the game skips a
  frame) and `rollback_hits` (of the passes that confirmed guessed tics, how
  many had every input right, the first wrong tic, whose inputs were wrong).
  No behaviour change.
- `242f394f2` -- **the waypoints restored through the relink index (step
  1).** Same lookups, same order.

**Predictions, written before the runs Gibax asked for** (Coastal Temple's
leak soak, Opulence's leak and resim soaks, then `correct_on` and `off` on
Opulence without a driver):

- Coastal Temple, leak: **no polyobjects-block failure**.
- Opulence, leak: 0 failures again; resim: at most the first check's
  `chainorder_block`. The restore profile's `waypoints` step falls from about
  0.95 ms to **under 0.1 ms**, the load by about as much.
- `correct_on` on Opulence: `rollback_cost` **12 ms a pass or more**
  (estimated 15 in 8.60), of which restore about 5.5, save 2.5 to 3.5,
  speculation 5 or more. Iterations with a pass mostly above 16.7 ms, some
  past a tic -- so skipped frames, which `off` should not show.
- `rollback_hits`, no driver: **90% of passes or more with every input
  right** -- the idle kart's input does not change and bots are recomputed
  from the same world. A driven race is what will say how a person fares.

### 8.62 The first measurements: on Opulence a pass takes the whole tic, 12 to 17 frames a second

Measured on 2026-09-28, binary `242f394f2` (sha256 `0a9b68ae...`), Gibax's
go-ahead for the install, the three soaks and the race without a driver.
Predictions in 8.61. Logs `soaklog_leak_RR_CoastalTemple_20260928-184530_242f394.txt`,
`soaklog_leak_RR_Opulence_20260928-184843_242f394.txt`, `soaklog_ww_RR_Opulence_*_242f394.txt`,
`playlog_correct_on_RR_Opulence_20260928-185445_242f394.txt`,
`playlog_frames_off_RR_Opulence_20260928-185726_242f394.txt`.

**The soaks.**

- Coastal Temple, leak: **1 of 261, `itemList.cap`** -- the nine
  polyobject failures are gone. 8.61's prediction holds; 8.59's mechanism
  is the one.
- Opulence, leak: 2 of 286, both `itemList.cap`. 8.61 said 0: known and
  harmless, but not what was written. Resim: 1 of 377, the first check's
  `chainorder_block` at leveltime 705, as set aside.
- **The waypoint step leaves the restore profile**, which prints only steps
  of 100 us or more: from 0.95 ms to under 0.1, as predicted. The load
  went from 6.5 to 6.1 ms only: the purge, one sample each, read 1.75 then
  2.2 ms.

**The race: `correct_on` on Opulence, no driver, three windows of 1000
tics** (Gibax, watching it: "injouable", 15 frames a second at most).

| per pass | window 0 | window 1 | window 2 |
|---|---|---|---|
| restore | 6.05 ms | 6.49 | 6.78 |
| network, correction | 0.13 | 0.13 | 0.13 |
| confirmed tics (1.05 - 1.13 a pass) | 6.54 | 6.37 | 7.43 |
| save of the frontier | 2.24 | 2.28 | 2.32 |
| 4 speculated tics | 13.61 | 12.80 | 14.20 |
| **total, of a 28.6 ms tic** | **28.6 (100%)** | **28.1 (98%)** | **30.9 (108%)** |
| frames drawn in 1000 tics | 502 | 452 | 346 |
| gaps over 50 ms between two frames | 314 | 368 | 316 |
| iterations past a tic, next frame skipped | 519 | 563 | 663 |

- **A simulated tic costs 3.2 to 3.5 ms on Opulence**, 2.5 times Skyscraper
  Leaps' 1.3, and a pass runs five of them. With the restore and the saves,
  a pass is the whole tic; drawing a frame on top of it (about 2.4 ms, see
  the control) pushes most iterations past a tic, and the game then skips
  the next frame on purpose (`frameskip`, `d_main.cpp`). **12 to 17 frames a
  second** -- Gibax's 15.
- **The confirmed step was 6.4 to 7.4 ms for one tic because the same state
  was saved twice**: `rollback_twoclock` turns `rollback_keep` on, so
  `K_RollbackTicker` saved every confirmed tic, and `K_RollbackSpeculate`
  then saved the frontier -- the same tic, the same slot. Two-clock mode
  restores only the frontier. Found here, fixed as `a09cc3bbe`.
- **8.61's prediction fails twice.** The pass: "12 ms or more", measured 28
  to 31. The hit rate: "90% or more", measured **48 to 53%** (473 of 980,
  525 of 995, 513 of 991 passes with every input right), the first wrong tic
  almost always the frontier's own (355 to 455 a window at tic 0, 51 to 123 at
  tic 1, none later). Wrong inputs: **this machine's, about 500 a window,
  with nobody at the wheel**, and bots', about 1550. Not explained. The
  candidates, unread: the local input's `latency` stamp, which changes every
  tic whatever the stick does, and the server building a bot's input from
  another tic's world than the client does. As measured, track A would
  rebuild from the frontier on half the passes.

**The control: `frames_off`, the same race with no speculation.** 4048 to
4077 frames in 1000 tics -- **about 142 a second**. An iteration that runs a
tic costs 6.4 to 6.6 ms, one that only draws 2.3 to 2.4; 0 or 1 skipped
frame a window. The two Opulence loads the harness warned about came before
window 0: the windows are Opulence's.

**So on Opulence the speculation takes the game from about 142 frames a
second to 12 to 17.** Written, not pushed: `a09cc3bbe` (the double save, 2.3
ms) and `7a8c2707f` (the interpolation list emptied before the purge, part
of up to 2.2 ms). Together about 25 ms a pass: still most of the tic. The
largest item is now **the tics themselves: five a pass at 3.3 ms**. What
would cut it: a depth of 2 on such maps (-6.6 ms), finding what makes an
Opulence tic cost 3.3 ms (3700 objects; 401 gems and coins run a Lua
`MobjThinker` every tic), and track A once the hit rate is understood.

### 8.63 The two quick wins, pushed, and what the next race should show

Pushed on 2026-09-28 with Gibax's go-ahead ("pousse, installe et lance la
course de mesure"): `a09cc3bbe` (no second save of the confirmed tic in
two-clock mode) and `7a8c2707f` (the interpolation list emptied before the
purge). Both were first committed under other shas, reordered below the docs
commit before their push.

**Prediction for `correct_on` on Opulence, no driver, written before it
runs:** the confirmed step falls by about 2.3 ms (to about 4.1 to 5.1), the
restore by 0.5 to 1.5 ms; a pass of **24 to 27 ms**, still most of a tic;
fewer skipped frames but still hundreds a window, **20 to 30 frames a
second**. The hit rate does not move (48 to 53%): nothing here touches the
inputs.

### 8.64 The quick wins measured: 23 to 33 frames a second on Opulence, and a hit rate that moves with the race

Measured on 2026-09-28, binary `7a8c2707f` (sha256 `d6bd1304...`), Gibax's
go-ahead, `correct_on` on Opulence without a driver
(`playlog_correct_on_RR_Opulence_20260928-190902_7a8c270.txt`). Prediction in
8.63; before in 8.62.

| per pass | window 0 | window 1 | window 2 | 8.62, same windows |
|---|---|---|---|---|
| restore | 5.22 ms | 5.55 | 5.94 | 6.05 - 6.78 |
| confirmed tics (1.00 - 1.05 a pass) | 3.87 | 3.85 | 4.41 | 6.37 - 7.43 |
| save of the frontier | 2.48 | 2.52 | 2.58 | 2.24 - 2.32 |
| 4 speculated tics | 13.52 | 13.42 | 14.73 | 12.80 - 14.20 |
| **total** | **25.2 (88%)** | **25.5 (89%)** | **27.8 (97%)** | 28.1 - 30.9 |
| frames drawn in 1000 tics | **945** | **844** | **648** | 346 - 502 |
| iterations past a tic, next frame skipped | 265 | 309 | 436 | 519 - 663 |
| passes with every input right | 799 of 992 | 364 of 998 | 310 of 994 | 473 - 525 of about 990 |

- **The prediction holds on the costs**: the confirmed step lost 2.7 to 3.0
  ms (the double save), the restore 0.8 ms (the interpolation list); a pass
  of 25 to 28 ms, for 24 to 27 written. Frames: **33, 30 and 23 a second**,
  about twice 8.62's, for 20 to 30 written -- window 0 slightly above.
  Skipped frames halved, still hundreds.
- **It fails on the hit rate**, written as not moving: 80% of passes right
  in window 0, then 36% and 31%. Same build, same map, no driver. It moves
  with the race, not with the code; why is not known yet.
- **Still most of a tic.** Four speculated tics are 13.4 to 14.7 ms, more
  than half of it; each worsens as the race goes on (3.4 to 3.7 ms a tic).
  The profile of a speculated tic and the fields of the wrong guesses are
  built next, not yet pushed.

### 8.65 The profile of a speculated tic, pushed, and what it should show

Pushed on 2026-09-28 with Gibax's go-ahead: `0baa0e7df`, `rollback_tic` (a
speculated tic's time by part, summed from the game's own m_perfstats
figures) and `rollback_hits`' wrong fields by who. **Predictions for the same
race on Opulence, written before it runs:**

- Costs as in 8.64, within 1 ms a pass: the instrument adds a few reads a tic.
- A speculated tic of 3.3 to 3.7 ms, **more than half of it in the objects'
  thinker list**; player thinks 0.5 to 1 ms; **400 Lua mobj hook calls a tic
  or more** (401 gems and coins with a `MobjThinker` hook).
- This machine's wrong inputs differ in **`latency`**, the stamp that moves
  every tic whatever the stick does. The bots' in **`turning`** and the
  `bot` fields, from an input the server built off another tic's world.

### 8.66 The profile of a speculated tic: 83% in the objects' thinker list, and bots wrong on one field

Measured on 2026-09-28, binary `0baa0e7df` (sha256 `5aa87901...`), the same
race as 8.64 (`playlog_correct_on_RR_Opulence_20260928-191845_0baa0e7.txt`).
Predictions in 8.65.

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| pass | 27.2 ms | 28.8 | 31.8 |
| frames drawn in 1000 tics | 602 | 422 | 299 |
| **a speculated tic** | **3.55 ms** | **3.64** | **4.09** |
| ... objects' thinker list | 2.90 | 3.08 | 3.41 |
| ... player thinks | 0.38 | 0.27 | 0.39 |
| ... the rest of G_Ticker | 0.24 | 0.25 | 0.25 |
| ... slopes, main list, ACS, Lua ThinkFrame | 0.03 | 0.03 | 0.04 |
| Lua mobj hook calls a tic | 427 | 445 | 435 |
| `P_CheckPosition` calls a tic | 1583 | 1829 | 1900 |
| passes with every input right | 430 of 992 | 345 of 994 | 613 of 986 |
| this machine's wrong inputs: `latency` / `angle` / `turning` / `buttons` | 565 / 451 / 305 / 91 | 657 / 518 / 350 / 120 | 402 / 295 / 182 / 55 |
| bots' wrong inputs: `angle` / `turning` | 821 / 16 | 1189 / 23 | 769 / 55 |

- **The run itself moved**: the same code as 8.64 plus a few reads a tic, and
  a pass 2 to 4 ms dearer, 10 to 21 frames a second against 23 to 33. 8.65
  said "within 1 ms": wrong. Race-to-race spread is larger than the effects
  being measured; one race per build is not enough to rank builds.
- **A speculated tic is 83 to 84% the objects' thinker list** (predicted:
  more than half), 427 to 445 Lua mobj hooks a tic (predicted: 400 or
  more), player thinks 0.27 to 0.39 ms (predicted 0.5 to 1: wrong). Which
  objects is the next question: 2199 rings, 401 Lua gems and coins, the
  karts, the effects -- and 1600 to 1900 `P_CheckPosition` calls a tic.
- **This machine's wrong inputs all differ in `latency`**, as predicted -- but
  also in `turning` and `buttons`, which an idle kart should not change. Either
  someone was at the wheel, or the idle input is not constant; to settle.
- **The bots' wrong inputs differ in `angle`, almost only** (predicted:
  `turning` and the `bot` fields: wrong). For a bot `angle` carries its
  prediction error, `|destangle - moveangle|` (`k_bot.cpp`, `K_HandleBotTrack`),
  which the game turns into friction (`p_user.c`, `k_kart.c`) -- a real input.
  A value that moves every tic while the steering it comes with rarely
  changes reads like the server building it from a world one tic older than
  the client's guess. A candidate, not read yet in the server's loop.

### 8.67 Why the guesses fail on Opulence: a driver, a stamp, and a confirmed world that drifts on speed

Read on 2026-09-28, from 8.66's log and the code; nothing launched.

- **Gibax was driving** during 8.66's race, and during 8.62's and 8.64's
  ("oui je jouais"). That explains this machine's wrong `turning` and
  `buttons`: a person's input changes and repeat-last misses it -- and part of
  the race-to-race spread in frames. Those races are driven, not unattended
  as written in 8.62 to 8.66.
- **This machine's `latency` is wrong on every wrong input**, as 8.65 said:
  the speculation fills the local slot with the newest input, whose stamp is
  not the one the server applied, a round trip older. `rollback_history`
  replays the inputs in flight with their own stamps (8.39); measuring the
  hit rate with it on is how to tell.
- **The server builds a bot's input exactly as the client guesses it**, read
  in the code: `NetUpdate` runs only at the head of `TryRunTics`, and the
  server's `SV_Maketic` makes tic `maketic` from the world after `maketic - 1`
  and runs it in the same pass (`neededtic = maketic`); `target_lag` delays
  inputs, not the server's world. **8.66's candidate -- a world one tic older
  -- is refuted.**
- **What the bots see differently is the world itself.** The same race's
  drift report: mean 0.002 units, worst 1.42 then 0.15, **287 to 315 of about
  2200 kart samples with a state field differing, and that field is `speed`
  every time** (120 STATE lines, all `speed`), from tic 2248 -- Opulence's
  dynamic-slope stretch (8.58). About 2200 karts put back a window by the
  correction channel. A bot's `angle` input is its prediction error, computed
  from its position and heading: a world a hair off gives another value while
  the steering, quantised, rarely moves. **On Opulence the confirmed world
  does not reach 8.44's 0.000**; the correction channel hides it.
- **A candidate, not tested**: the order objects sit in their blockmap chains.
  A client restores every pass and relinks in the archived order; the server
  never restores. 8.58's first-check resim failure is exactly that order
  (`chainorder_block`) differing between a live world and a restored one --
  which the leak soak, restoring on both sides, cannot see.

**For track A**: its hit rate on bots is bounded by this drift, and on this
machine by the stamp. Neither is a reason A cannot work; both come first.

### 8.68 `rollback_objprofile`, pushed, and what it should show

Pushed on 2026-09-28 with Gibax's go-ahead: `5a417494f`. Run with
`playtest.sh objprofile map=RR_Opulence` (correct_on, the objects' list timed
by type, printed each window). **Predictions, written before it runs:** the
list's timed total a little above 8.66's 2.9 to 3.4 ms a speculated tic (the
timing costs); **`MT_RING` first by count**, about 2200 a tic, and among the
first three by time, **about 1 ms a tic**; the Lua gems and coins
(`MT_TUMBLEGEM`, `MT_OPULENCECOIN`, 401 together) **0.5 ms a tic or more**,
their hook being the cost; the eight `MT_PLAYER` dearest each, **0.3 to 0.6
ms a tic** together.

### 8.69 What an Opulence tic spends its time on: the map's decorations, not the karts

Measured on 2026-09-28, binary `5a417494f` (sha256 `c27af9d6...`),
`playtest.sh objprofile map=RR_Opulence`
(`playlog_objprofile_RR_Opulence_20260928-193348_5a41749.txt`). Prediction in
8.68. Figures a tic, over about 5000 tics a window (confirmed and speculated).

| type | objects | window 0 | window 1 | window 2 | each |
|---|---|---|---|---|---|
| `TUMBLEGEM` (Lua hook) | 219 | 347 us | 370 | 345 | 1.6 us |
| `OPULENCECOIN` (Lua hook) | 182 | 271 | 268 | 267 | 1.5 |
| `OPULENCECHAIN` (mace chains) | 304 | 428 | 325 | 134 | 0.4 - 1.4 |
| `OPULENCEBRAZIER` | 55 | 116 | 114 | 99 | 2 |
| `MT_SIGNSPARKLE` | 72 - 135 | 132 | 74 | 97 | 1 |
| `MT_FLINGRING` | 26 - 58 | 85 | 171 | 145 | 3 |
| `MT_FASTLINE` | 15 - 25 | 68 | 103 | 138 | 5 |
| `MT_CUSTOMMACEPOINT` | 9 | 96 | 84 | 54 | 6 - 11 |
| `MT_PLAYER` | 9 | 52 | 71 | 78 | 6 - 9 |
| `MT_RING` | 2138 - 2174 | 22 | 40 | 62 | 0.01 - 0.02 |
| timed, every type | | 1961 | 2141 | 1955 | |

- **The karts are cheap** (52 to 78 us a tic for nine) and **the rings are
  nearly free** (0.01 us each: they return early). 8.68 said rings about 1 ms
  and karts 0.3 to 0.6: both wrong.
- **The map's decorations are the cost**: the Lua gems and coins 0.61 to 0.64
  ms a tic (predicted 0.5 or more: holds), the swinging maces -- chains,
  maces, mace points -- 0.2 to 0.55, the braziers 0.1, the sign's sparkles
  0.1. **No single type is most of it**; the largest, the gems, is under a
  fifth of a tic.
- The timed total is 2.0 to 2.1 ms against the list's 2.8 to 3.0 (8.66, and
  `rollback_tic` in this race): **0.8 ms of the list is not inside an
  object's thinker** -- removals (`P_RemoveThinkerDelayed`) and the walk
  itself. Not placed yet. 8.68 said "a little above": wrong.
- This race: 26 to 27 ms a pass, 28, 21 and 22 frames a second; 351, 644
  and 242 passes of about 995 with every input right.

**What it says for the cost:** on this map a tic stays about 3.4 ms, spread
over decorations that behave the same in every tic; making one type cheaper
takes a tenth of it at best. The number of tics a pass runs -- five -- is the
lever: a shallower speculation now, track A later, which needs the drift
(8.67) and the latency stamp settled first.

### 8.70 `depth2`: the same race at half the speculation

Gibax's go-ahead on 2026-09-28. Harness mode `depth2`: `correct_on` with
`rollback_twoclock 2`. Run on the installed `5a417494f`
(`rollback_objprofile` off). **Prediction, written before it runs:** a pass
of **18 to 21 ms** (the 8.69 race's 26 to 27, minus two speculated tics of
about 3.4 ms), speculation 6.5 to 7.5 ms; **35 to 45 frames a second** on
Opulence; skipped frames down to tens a window, not hundreds. The hit rate
rises a little: fewer guessed tics a pass, the same first one.

### 8.71 `depth2` on Opulence: 50 to 71 frames a second, for one tic of the local input ahead instead of three

Measured on 2026-09-28, binary `5a417494f`, `playtest.sh depth2
map=RR_Opulence` (`playlog_depth2_RR_Opulence_20260928-194532_5a41749.txt`).
Prediction in 8.70.

| | window 0 | window 1 | window 2 | depth 4 (8.69) |
|---|---|---|---|---|
| pass | 18.6 ms (65%) | 20.6 (72%) | 21.9 (77%) | 26.4 - 27.5 |
| ... speculation, 2 tics | 6.8 | 7.3 | 8.2 | 13.9 - 14.4 |
| frames drawn in 1000 tics | **2030** | **1631** | **1437** | 598 - 791 |
| frames a second | **71** | **57** | **50** | 21 - 28 |
| iterations past a tic, next frame skipped | 14 | 82 | 109 | 352 - 455 |
| drift: mean / worst / samples with a state field off | 0.004 / 0.99 / 749 of 2241 | 0.003 / 0.24 / 817 of 2250 | 0.196 / 5.04 / 1335 of 2250 | |

- **The pass holds its prediction** (18 to 21 written, 18.6 to 21.9
  measured); **the frames beat it**, 50 to 71 a second for 35 to 45 written,
  and skipped frames fall to tens, as written.
- **The hit rate says nothing at this depth.** Windows 1 and 2 read 1000 of
  1000 passes right. `rollback_cleancmds` explains it: exactly one local
  input a pass "would have been written over an already-received tic" --
  **the first of the two speculated tics is a tic the server has already
  sent**, and it is the one the next pass confirms. The check compares only
  confirmed tics, so it compares a received tic with itself. The same is
  likely true at depth 4, where its "first wrong tic at 0" then needs
  another reading; to settle before track A leans on it.
- **What depth 2 costs the player, then:** of two speculated tics, one
  already carries the server's (old) input, so the newest local input steers
  the drawn kart for **one tic instead of three**. The input is still drawn
  at once, but moves the kart a third as far ahead. Gibax is the only
  instrument for whether that feels worse.
- The drift grows in window 2 (mean 0.196, worst 5.0 units, 59% of samples
  with `speed` off). The confirmed world should not depend on the depth;
  one race, not explained.

### 8.72 Driven, depth 2 then depth 4 on Opulence: the feel

Gibax's go-ahead on 2026-09-28: `depth2` then `correct_on` on Opulence, Gibax
driving both, binary `5a417494f`. **Prediction, written before they run:**
frames as unattended -- about 50 to 70 a second at depth 2, 20 to 33 at
depth 4; **depth 2 feels smoother and its steering a little less ahead**,
and Gibax prefers it on this map if the steering does not float. The drift
stays on `speed` in the dynamic-slope stretch either way.

### 8.73 The driven races, the sound's stutter, and track A built

**Driven, 2026-09-28, binary `5a417494f`, Gibax at the wheel** (8.72's
prediction): `depth2` then `correct_on` on Opulence
(`playlog_depth2_RR_Opulence_20260928-195035_5a41749.txt`,
`playlog_correct_on_RR_Opulence_20260928-195321_5a41749.txt`).

| | depth 2 | depth 4 |
|---|---|---|
| pass | 18.9 / 19.2 / 21.3 ms | 25.4 / 27.5 / 30.0 |
| frames a second | **69 / 65 / 54** | 32 / 21 / 14 |
| skipped frames a window | 29 / 37 / 89 | 269 / 463 / 609 |
| worst drift | 1.8 / **126** / 10.7 units | 23 / 34 / 48 |

Gibax: "Depth2 était bien plus fluide sans aucun doute. Assez fluide en
moyenne", and the sound stutters. The frames hold 8.72's prediction. **New:
driven, the confirmed world drifts far more** -- worst 23 to 126 units, three
kart widths once, against under 5 unattended -- which the channel then snaps
back: the speed drift of 8.67 under a driver.

**The sound's stutter, found in the code.** A local restore leaves sounds
playing on purpose (`P_RemoveSavegameMobj`), but every channel kept the
address of the object it came from, and the restore frees every object and
loads it again, most elsewhere. `S_UpdateSounds` then set each sound's volume
and panning from whatever that memory now held, every pass; the local
player's engine stopped being recognised as its own (`c->origin !=
listenmobj`). **Fixed, not pushed: `ab1c24e15`** -- each channel notes its
object's mobj number and type before the purge and is pointed at the object
carrying them after the load, through the relink index, or stopped. A second
cause, not fixed: an object removed in a speculated tic stops its sounds
(`P_RemoveMobj`), and the restore that brings it back does not restart them.

**Track A, built on Gibax's word** ("tu veux pas juste faire A au pire ?
... au pire on revient en arrière"), **not pushed: `b24e0a2f2`,
`rollback_keepspec`**, off by default -- off, every pass runs as before
(checked line by line). On, the speculation is left standing: the netcode
gets the frontier's clock (`gametic`, and the `leveltime` the local input is
stamped with) while the world stays at the head; once the server's tics are
in, the tics the loop would run are checked against the inputs the
speculation ran. Kept: their checksums (noted when the speculation ran them)
go to `consistancy[]`, the frontier moves, and the pass runs only the tics
beyond the head. Otherwise: back to the frontier, and the usual pass. Never
kept: a tic with a netxcmd filed, one that raised a refused message, one where
a gamedata guard changed what it did (`K_RollbackOffTimeline`), or a pass
with a correction due. Sounds now play on a tic's first run
(`K_RollbackSoundsSilenced`). It needs `rollback_history` and
`rollback_cleancmds`: without them this machine's own input is guessed wrong
every pass. Harness mode `keep` = `history_on` with it on.

**What it costs, and the prediction, written before it runs.** A kept pass
is one tic and one save; a rebuilt one now saves every speculated tic, and
with `rollback_history` that is 7 to 12 of them. So:

- **Skyscraper Leaps, `keep`, unattended** (correctness first, the map with
  no drift): no refusal, drift 0.000 as in 8.44; **60% of passes kept or
  more**; the rebuilds mostly "a correction was due" (one pass in four with
  `rollback_correct 4`); a pass of about 2 ms kept and 15 to 20 rebuilt,
  bimodal.
- **Opulence, `keep`**: fewer passes kept (its drift makes the bots' inputs
  differ), rebuilds of 40 ms or more; **no better than depth 2 on
  average**, and hitchier. Opulence needs its drift found before A pays.

### 8.74 Track A measured: correct and twice as cheap on Skyscraper Leaps, unusable on Opulence

Measured on 2026-09-28, binary `b24e0a2f2` (sha256 `3942628d...`), Gibax's
go-ahead, harness mode `keep` (`history_on` with `rollback_keepspec 1`).
Predictions in 8.73.

**Skyscraper Leaps, unattended** (`playlog_keep_20260928-210840_b24e0a2.txt`):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| passes kept, of 999 | **744** | **749** | **747** |
| rebuilt: a correction was due | 249 | 250 | 249 |
| rebuilt: an input differed | 6 | 0 | 3 |
| pass | 7.3 ms | 7.4 | 7.6 |
| ... speculation (2.8 tics a pass on average) | 4.4 | 4.4 | 4.5 |
| ... saves | 1.5 | 1.6 | 1.6 |
| frames drawn in 1000 tics | 3680 | 3681 | 3660 |
| drift: mean / worst / samples with a state off | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 |

- **Correct**: 0.000 units and not one state field off in 6714 samples,
  exactly 8.44's reading -- keeping the speculation's run as the confirmed
  one parts nothing from the server.
- **75% of passes kept** (predicted 60% or more). The rebuilds are the
  corrections, one pass in four exactly (`rollback_correct 4`), and a handful
  of wrong inputs.
- **A pass of 7.3 to 7.6 ms with `rollback_history` 12**, against 13.5 ms for
  the same history without it (8.50): half. About 128 frames a second.
  ⚠ 8.77: under `rollback_keepspec`, `rollback_cost` counts twice every save
  made inside the speculation; counted once, a pass is about 6 ms.

**Opulence, driven** (`playlog_keep_RR_Opulence_20260928-211114_b24e0a2.txt`),
window 0 only -- the session ended in window 1:

- **77 of 999 passes kept**; rebuilt 511 times for a wrong input and 411 for
  a due correction.
- **A pass of 83.5 ms, three tics**: saves 23.9 ms, speculation 42.1 ms (6.2
  tics a pass), confirmed tics 7.3 (2.1 a pass). 344 frames in 1000 passes,
  717 iterations past a tic, gaps up to 329 ms.
  ⚠ 8.77: the same double count -- the saves of the tics after the
  frontier sit inside the speculation's 42.1 ms too; counted once, about
  60 ms.
- Why: a rebuild now saves every speculated tic (2.5 ms each on Opulence),
  with history 12 that is six a pass; the bots' inputs differ (8.67's
  drift: 71 STATE lines, all `speed`), so most passes rebuild; a pass longer
  than a tic confirms two tics the next time, so a correction falls due more
  often (41%, not 25%), and it feeds on itself.
- 8.73 said "no better than depth 2 on average, and hitchier": far worse
  than that.

**So:** A works, and on a map where client and server agree it halves the
cost of the history window. On Opulence it needs, in this order: the drift
found (the bots' inputs), a due correction that changes nothing not to force
a rebuild (on Skyscraper Leaps that alone would take it from 75% toward all
passes kept), and cheaper saves (B2) before rebuilding stops hurting.

### 8.75 A correction that changes nothing: no rebuild, and no kart put back

Written 2026-09-28, not pushed: `69e65f0ac`, on Gibax's go-ahead for 8.74's
first point.

- **A kart already exactly where a correction has it is left alone.**
  Putting it back was not neutral: `P_MoveOrigin` re-runs `P_CheckPosition`
  and relinks the kart at the head of its blockmap and sector chains. The
  server never moves a kart. So every correction -- every kart, every four
  tics, about 2200 a window -- reordered the client's chains and not the
  server's, and the order objects are met in a collision is the order of
  those chains. **A candidate for Opulence's drift** (8.67), where karts touch
  gems and coins in crowds and order can matter; the leak and resim soaks
  cannot see it, since neither applies corrections.
- **With `rollback_keepspec`, a correction due at the frontier no longer
  rebuilds when it changes nothing.** Each speculated tic notes its karts in a
  correction's terms; the due correction is compared with the frontier's
  note, and if every kart matches it is measured against the note (the same
  drift figures) and consumed.
- Harness mode `measure`: `correct_on` with `rollback_drift 0`, corrections
  measured and never applied.

**Predictions, written before they run:**

- `keep` on Skyscraper Leaps, unattended: **97% of passes kept or more**, the
  corrections kept through (drift 0.000 there); a pass of 3 ms or less on
  average; every kart "already where the server had it".
- `measure` on Opulence, unattended: if the put-back is the drift's source,
  **0.000 with nothing applied**. If it drifts anyway, the source is
  elsewhere, and applying only hid it. I expect the second: 8.67 saw the
  first `speed` difference at tic 2248, into the dynamic-slope stretch, not
  at the first correction.

### 8.76 Opulence's drift was the corrections themselves; track A keeps every pass on Skyscraper Leaps

Measured on 2026-09-28, binary `69e65f0ac` (sha256 `45d59222...`), Gibax's
go-ahead, unattended. Predictions in 8.75.

**`keep` on Skyscraper Leaps** (`playlog_keep_20260928-212641_69e65f0.txt`):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| passes kept, of 999 | **992** | **999** | **999** |
| ... through a correction that changed nothing | 248 | 246 | 249 |
| pass | **2.5 ms** | **2.7** | **3.2** |
| frames drawn in 1000 tics | 4100 | 4097 | 4103 |
| drift | 0.000 | 0.000 | 0.000 |

The prediction holds (97% or more; 3 ms or less, window 2 just above):
**with `rollback_history` 12, the speculation now costs about what one tic
does**, and the game draws as many frames as it does with no speculation at
all (8.62's control: about 4050 to 4077). The harness warned that the race
ended inside the session; the windows are whole (1000 passes each).

⚠ 8.77: the pass figures count each save twice (`rollback_cost` under
`rollback_keepspec`); counted once they are **1.9, 2.0 and 2.4 ms**.

**`measure` on Opulence -- corrections measured, never applied**
(`playlog_measure_RR_Opulence_20260928-213001_69e65f0.txt`):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| kart samples | 2096 | 1936 | 1968 |
| drift: mean / worst | **0.000 / 0.000** | **0.000 / 0.000** | **0.000 / 0.000** |
| samples with a state field off | **0** | **0** | **0** |

**8.75's prediction fails, and the failure is the finding: with nothing
applied, Opulence's confirmed world matches the server's exactly, in every
field, for three windows.** Every drift seen on Opulence since 8.62 -- the
`speed` of 8.67, the 126-unit spike of 8.73, and the bots' wrong inputs
behind A's rebuilds (8.74) -- came from applying the corrections: putting
back karts the server never moves, which relinks them in their chains
(8.75). With `69e65f0ac` an identical kart is no longer put back, so a
client that does not diverge never puts one back at all.

The same run was slow -- restore 6.9 to 8.5 ms, a save 5.1 to 6.3 (2.5
before), 1.25 to 1.47 confirmed tics a pass, 9 to 11 frames a second -- with
nothing in this build to explain a save twice as dear. Noted, not explained;
the machine may have been busy.

**What follows:** `correct_on` on Opulence on this build should read 0.000
with no kart put back; and `keep` on Opulence, its bots now guessed right,
should keep most passes.

### 8.77 Opulence with corrections applied again, then track A there

Launched on 2026-09-28 on Gibax's go-ahead ("lance la suite"), binary
`69e65f0ac` (sha256 `45d59222...`), unattended, one after the other.
`correct_on` is also the same-session control for `keep`'s timings: the same
race with no history and nothing kept.

**Predictions, written before they run:**

- `correct_on` on Opulence: **drift 0.000 in all three windows, and every
  kart sample "already where the server had it"** -- no kart put back once.
  If a window drifts, something other than the put-back parts the two worlds
  as soon as a correction is applied, and 8.76's conclusion is only half
  right. **Passes with every input right: 90% or more in every window**,
  against 8.64's 80%, 36% and 31% falling with the race -- if the bots were
  wrong because the confirmed world drifted, they no longer are. The pass
  stays near 8.64's 25 to 28 ms: this build changes nothing it pays for.
- `keep` on Opulence: **90% of passes kept or more**, the rebuilds a handful
  of wrong inputs (nobody drives) and no due correction among them; drift
  0.000. A kept pass runs one tic and saves it, so about 4 ms of tic
  (8.66) plus 2.5 of save: **a pass of 6 to 9 ms**, against 83.5 ms in 8.74
  and 25 or so in `correct_on`, and **more than 2500 frames in 1000 tics**.
  If the machine is as slow as in 8.76's `measure`, both races are, and the
  ratio between them is what counts.

**`correct_on` on Opulence, corrections applied**
(`playlog_correct_on_RR_Opulence_20260928-230010_69e65f0.txt`):

| | window 0 | window 1 | window 2 | 8.64, same windows |
|---|---|---|---|---|
| kart samples | 2295 | 2160 | 2259 | |
| drift: mean / worst | **0.000 / 0.000** | **0.000 / 0.000** | **0.000 / 0.000** | 0.002 / 1.42 (8.67) |
| samples with a state field off | **0** | **0** | **0** | 287 - 315 (8.67) |
| karts put back / already where the server had them | **0 / 2295** | **0 / 2160** | **0 / 2259** | about 2200 put back |
| passes with every input right | 547 of 986 | 538 of 988 | 554 of 998 | 799, 364, 310 |
| wrong inputs: this machine / bots | 475 / 8 | 489 / 22 | 525 / 5 | 8.66: 565 - 657 / 769 - 1189 |
| pass | 33.0 ms | 32.8 | 35.2 | 25.2 - 27.8 |
| ... restore / save / 4 speculated tics | 6.4 / 4.7 / 16.4 | 7.1 / 4.6 / 15.7 | 7.7 / 4.4 / 17.0 | 5.2 - 5.9 / 2.5 - 2.6 / 13.4 - 14.7 |
| a speculated tic | 4.0 ms | 3.8 | 4.2 | 3.4 - 3.7 |
| frames drawn in 1000 tics | 304 | 302 | 262 | 648 - 945 |

- **The first prediction holds whole: 0.000, not one state field off, and
  not one kart put back in 6714 samples** -- every correction found every
  kart where the server had it. With the put-back gone, applying the
  corrections is the same as measuring them (8.76's `measure` read the same).
  Opulence's confirmed world now matches the server's, as Skyscraper Leaps'
  has since 8.44.
- **The bots are guessed right**: 5 to 22 wrong bot inputs a window, against
  769 to 1189 in 8.66 -- the drift was what made them wrong (8.67's reading
  confirmed).
- **The hit-rate prediction fails -- 55%, not 90%** -- on a cause already
  written in 8.67 and forgotten here: with `rollback_history` off the local
  slot repeats the newest input, whose `latency` stamp is not the one the
  server applied. 475 to 525 wrong inputs a window are this machine's, and
  in windows 1 and 2 they differ in `latency` and nothing else (window 0 also
  has a few `turning`, `angle`, `buttons`: nobody drives, so probably the
  start of the race). This race cannot read better: `keep` runs with the
  history, which replays the stamps.
- **The pass costs 33 to 35 ms, not 25 to 28**: every step is dearer than in
  8.64 on the same map -- a speculated tic by 10 to 15%, the restore by 25%,
  **the save by 80%** (2.5 ms to 4.4 - 4.7). Nothing between `7a8c2707f`
  and `69e65f0ac` adds work to a save with `rollback_keepspec` off (read in
  `K_RollbackSpeculate`: one `K_SaveGameState` a pass, as before). 8.76's
  `measure` had the same slow save (5.1 to 6.3 ms). At launch the processor
  sat at 26% with nothing of the game running, the League of Legends client
  open. **The likeliest cause is the machine, not the build -- not
  measured**: the same-session control that would settle it is the previous
  build (`b24e0a2f2`) on the same race.

**`keep` on Opulence** (`playlog_keep_RR_Opulence_20260928-230251_69e65f0.txt`)
-- ⚠ **with this machine's player a spectator**: the client "has joined the
game" as the server changed map, and never "entered the game" (the only such
log among the 20 kept; `correct_on` just before entered). The race is
therefore the bots and the host, with nothing local to guess:

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| passes kept, of 999 | **995** | **999** | **999** |
| ... through a correction that changed nothing | 245 | 250 | 250 |
| rebuilt: an input differed | 4 | 0 | 0 |
| pass as printed | 14.1 ms | 13.9 | 14.1 |
| **pass, each save counted once** (below) | **9.2** | **9.0** | **9.2** |
| ... a save / the tic run | 4.9 / 3.9 | 4.9 / 3.7 | 4.9 / 4.1 |
| frames drawn in 1000 tics | **3308** | **3369** | **3334** |
| iterations past a tic | 8 | 1 | 1 |
| drift: mean / worst / samples with a state off | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 |
| `rollback_history`: inputs in flight / depth | 0.00 / 4.18 | 0.00 / 4.00 | 0.00 / 4.00 |

- **`rollback_cost` counts a save twice under `rollback_keepspec`.** A save
  made by `K_RunSpeculatedTic` (every speculated tic's start, and the head's
  in `K_KeepExtend`) is added to `g_saveus`, and runs inside the timer of
  `g_specus` as well (read in `K_KeepExtend` and `K_RollbackSpeculate`); the
  printed total adds both. Speculation minus save is 4.0 ms here, one tic.
  Without `rollback_keepspec` no save runs inside that timer, so
  `correct_on`'s figures stand. The true pass is the printed one less the
  saves, **9.0 to 9.2 ms**.
- **Kept: 99.6 to 100%**, the corrections consumed without a rebuild, drift
  0.000 -- as predicted. **But it proves less than it reads**: with no local
  player, the input this machine guesses worst cannot miss, and
  `rollback_history` found no input in flight, so the speculation ran 4 tics
  deep instead of about 8. It does show the bots are guessed right tic after
  tic on Opulence now, and what a pass costs when nothing is rebuilt.
- **Pass: 9.0 to 9.2 ms, for 6 to 9 written** -- at the top, because the save
  is 4.9 ms and not 8.66's 2.5, the same slowness as `correct_on`. **Frames:
  3308 to 3369 in 1000 tics, 116 to 118 a second, against 9 to 11 in
  `correct_on` in the same session**: eleven times as many, and 1 to 8
  iterations past a tic against about 700.

**So:** on Opulence, with its confirmed world now exact, keeping the
speculation turns a pass of 33 to 35 ms into one of 9 -- one tic and one
save -- and the frame rate from 10 to 117. What is not measured yet: the same
race with the local player in it, then driven. A rebuild there costs the
whole depth again, about 8 tics of 4 ms plus their saves.

**Next, each on Gibax's go-ahead:** count a save once in `rollback_cost`
(a `src/` change); `keep` on Opulence again with the player in the race (the
harness now warns when it is not: `558c8a7`); then driven; and, for the slow
save, a race with the machine otherwise idle.

### 8.78 `keep` on Opulence again, with the local player in the race

Pushed on 2026-09-28 on Gibax's go-ahead ("oui, pousse 755ea3c0c et lance
keep sur Opulence"): `755ea3c0c`, `rollback_cost` counts a save made inside
the speculation once (8.77). Then the same `keep` race on Opulence,
unattended, with the harness's new check that this machine's player entered
the game.

**Predictions, written before it runs:**

- The client's player enters; `rollback_history` finds about 7 inputs in
  flight and speculates about 8 tics deep, as on Skyscraper Leaps (8.76).
- **97% of passes kept or more**, the rebuilds a handful of wrong inputs;
  drift 0.000. The history replays this machine's inputs with their own
  stamps, and the bots are guessed right (8.77).
- `rollback_cost`, now counting each save once: the speculation about one
  tic (4 ms), the save beside it; **a pass of 8 to 10 ms** if the save is
  still 4.9 ms, 6 to 7 if it is back at 2.5. **More than 3000 frames in 1000
  tics.**
- A rebuild, when one comes, costs about 8 tics and their saves, some 70
  ms: a handful of them shows as a handful of iterations past a tic.

**Measured on 2026-09-28**, binary `755ea3c0c` (sha256 `11c60b35...`),
processor at 5% before launch, the League of Legends client closed
(`playlog_keep_RR_Opulence_20260928-235407_755ea3c.txt`). The harness's new
line: "this machine's player entered the game". ⚠ **Written as unattended,
it was driven**: Gibax played it ("j'ai joué, c'est pour ça"). So this is the
driven `keep` race on Opulence, to compare with 8.74's, not with 8.77's.

| | window 0 | window 1 | window 2 | 8.74, driven, `b24e0a2f2` |
|---|---|---|---|---|
| passes kept, of 999 | **694** | **654** | **694** | 77 |
| ... through a correction that changed nothing | 234 | 249 | 236 | -- |
| rebuilt: an input differed | 305 | 345 | 305 | 511 |
| rebuilt: a correction was due | 0 | 0 | 0 | 411 |
| pass (each save counted once) | **29.5 ms** | **33.7** | **33.9** | about 60 (8.77) |
| ... saves / speculation (tics a pass) | 12.6 / 11.8 (3.14) | 13.7 / 13.5 (3.32) | 13.5 / 13.9 (3.19) | |
| iterations past 50 ms | 305 | 347 | 306 | |
| frames drawn in 1000 tics | **1852** | **1556** | **1599** | 344 |
| drift: mean / worst / samples with a state off | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 | 0.000 / 0.000 / 0 | |
| inputs in flight / depth | 5.75 / 7.15 | 5.64 / 7.01 | 5.50 / 7.37 | |

- **Held**: the player entered; drift 0.000 with a driver, in every field;
  no rebuild for a due correction.
- **Failed, 97% kept: 65 to 69%**, and with it the pass (8 to 10 ms written)
  and the frames (more than 3000). The prediction assumed nobody drives.
- **What the rebuilds are**: 305, 345, 305 rebuilds against 305, 347, 306
  iterations past 50 ms -- every rebuild is a hitch. A rebuild re-runs the
  whole speculation, 7 to 8 tics at 3.7 to 4.3 ms, and saves every one of
  their starts, about 60 ms. The bots are guessed right (8.77: 4 rebuilds in
  3000 passes with no local player), so these are this machine's own
  inputs, the driver's. Not measured: `rollback_hits` does not run under
  `rollback_keepspec`, so nothing says who or which tic.
- **A reading, not tested**: the speculation runs 1.4 to 1.9 tics past the
  newest local input (depth 7.0 to 7.4 against 5.5 to 5.75 in flight: the
  lead is held at its largest over the last second, 8.41). Those head tics
  repeat the newest input. A driver changes it, the kept tics were run with
  the old one, and nothing checks them against the input actually made
  until the server confirms them, about six tics later -- when the whole
  speculation is rebuilt from the frontier. Both halves have a remedy
  already half built: every speculated tic's start is saved, so a rebuild
  could start from the first wrong tic instead of the frontier; and the
  local input for a guessed tic is known one pass later, not six.
- **Against 8.74's driven race** (same map, driven, `b24e0a2f2`): nine times
  as many passes kept, a pass about half as dear, **five times the frames:
  54 to 65 a second against 12**.
- **The slow save is not the machine**: with the processor idle, a save
  costs about 4.0 to 4.1 ms (12.6 ms of saves over about 3130 saves in
  window 0; 13.7 over about 3320 in window 1). Every Opulence race up to
  `5a417494f` saved in 2.2 to 2.8 ms; every one from `69e65f0ac` in 4 to
  6.3. Between them, `ab1c24e15`, `b24e0a2f2` and `69e65f0ac`, and none
  touches the save path (`K_WriteSnapshot`, `P_SaveNetGame`: read). Not
  explained. The installed backups of `5a417494f` and `b24e0a2f2` can
  bisect it without a build.
- **Gibax's feel**: "la cam qui déconait un peu, genre qui laggait, le jeu
  bon ça va, toujours pas du 60 fps+ mais mieux que le diapo d'avant". A
  lead for the camera, not checked: every snapshot carries the cameras
  (`slot->cameras`, `K_WriteSnapshot`) and a restore puts them back, so each
  rebuild may move the camera back to where it stood at the frontier.

### 8.79 Who is wrong when a kept speculation is rebuilt: the instrument

Written on 2026-09-29 on Gibax's go-ahead ("oui, écris l'instrument"):
`80d530d82`, on the local branch `wip/keep-miss`, **not pushed**. Syntax
checked, and an error injected at each of the ten changed places is reported
by the compiler.

`rollback_hits` does not run while the speculation is kept. For every
rebuild for a wrong input, `rollback_keepspec`'s report now says: how far past
the frontier the first wrong tic was; whose input was wrong on it (this
machine, bots, people) and in which fields; for this machine's input, where
the speculation took it from -- a tic already received, an input replayed
from `rollback_history`, or the newest input repeated past it; and how many
of the tics the rebuild threw away came before the first wrong tic, which a
rebuild from that tic would keep. The field counting is shared with
`rollback_hits`, whose lines read as before.

**A doubt about 8.78's reading, found while writing it**: the depth counts
from the frontier, the inputs in flight from the first tic the server has not
sent, and the confirmed loop stops short of that tic by the `netticbuffer`
reserve. On Skyscraper Leaps depth minus inputs in flight is 0.6 tics, so the
speculation hardly ever runs past the newest input there; on Opulence it is
1.4 to 1.9, part of which is the same gap. The guessed tics may be fewer
than 8.78 assumed, and the wrong inputs may be replayed ones -- the history
losing its place after a hitch.

**Predictions, for a driven `keep` race on Opulence with it:**

- The wrong inputs are this machine's, 95% or more; the bots a handful.
- Of this machine's, **the newest input guessed past the history is the
  larger part** (8.78's reading), with the driver's fields (`turning`,
  `buttons`, `forwardmove`) and `latency` with them. If the replayed ones
  are the larger part, the history loses its place, and that is what to fix
  first.
- The first wrong tic mostly 4 or more past the frontier, and a rebuild from
  it would keep **more than half** of the tics thrown away.

⚠ Pushed the same night on Gibax's go-ahead ("pousse dès que c'est bon") as
`a61ccadd8` -- `80d530d82` put on top of the docs -- CI run 36489955086. Not
installed, not run: the session ended there ("on finit là-dessus ce soir").

### 8.80 WORLDWIDE mode: one switch on the server, and the client follows

Written on 2026-09-29 on Gibax's go-ahead ("oui fais la partie 4": step 4 of
the plan to a playable alpha, the compatibility section of `ROADMAP.md`,
steps 2 to 6). `51ba899d6`, on the local branch `wip/worldwide-mode`, **not pushed, not
built**: the machine it was written on has no compiler, so the CI will be its
first compile. Checked by reading only: every new function is declared in
`k_rollback.h` (the build turns an implicit declaration into an error), the
braces balance as before, and the three console commands it touches keep
their behaviour -- their setting code moved into `K_SetTwoClock`,
`K_SetHistory` and `K_SetKeepSpec`, which the mode calls too.

**Why.** Everything this branch does is behind console switches set on two
machines by the harness: `rollback_correct` on the server, `rollback_twoclock`
and its companions on the client. Nobody else can be asked to type those, and
the host's delay exemption had to guess the mode from the correction rate
(8.26). The policy of 2026-09-21 says the server decides.

**What the server does, with `worldwide On`** (a console variable, `Off` by
default, not saved in the config):

- it sends a light correction every 4 tics **in place of** the full-state
  resend -- what `rollback_correct 4` does, and a rate set by hand still wins;
- it sets `SV_WORLDWIDE` (`0x04`), a free bit of `kartvars` in its server
  info (`d_clisrv.h`); a stock client reads that byte through `SV_SPEEDMASK`
  and three other flags only;
- it refuses a join that does not declare itself WORLDWIDE, with a readable
  message, and one that declares another `WORLDWIDE_PROTOCOL` (1 today); its
  own player on a listen server (node 0) is never refused. The server's log
  says `worldwide: refused node N`;
- `K_RollbackPays()` exempts its host from the gentleman's delay through the
  correction rate, as before -- which is now the question it was a proxy for.

The variable can only change with nobody else connected; a change under
connected players is put back with a warning. A client learns the mode when
it joins, and a server that switched under it would stop resending to
clients that need it, or start correcting clients that ignore it. That still
lets a dedicated server's script, or the harness, set it before anybody
joins -- the game's own `CV_NOTINNET` would not have: it refuses any change
in a netgame, before anybody has joined as well.

**How a client declares itself.** A WORLDWIDE client's join request is the
stock `clientconfig_pak` with five bytes after it (`clientworldwide_pak`:
`"RRWW"` and the protocol). Read in `HandleConnect` and the network layer
before writing it: a stock server reads the request as a `clientconfig_pak`
and checks neither its length nor anything past it, and the packet checksum
covers the whole length, so the five bytes cost it nothing. A stock client
sends exactly `sizeof (clientconfig_pak)`, which is how the server tells it
apart. `rollback_vanillajoin 1` makes a WORLDWIDE client join without them,
to test the refusal with one exe.

**What the client does.** When it has the server info of the server it is
joining (`CL_ServerConnectionSearchTicker`), `K_WorldwideJoin` reads the bit:

- set: what the driven `keep` race ran (8.78) -- `rollback_twoclock 4`,
  `rollback_cleancmds 1`, `rollback_history 12`, `rollback_keepspec 1` --
  with the corrections applied rather than only measured;
- not set: all of them off. A stock server gets a stock client.

Leaving (`CL_Reset`, the one place every departure goes through) undoes what
the join switched on, and nothing else: switches set by hand outside a
netgame are someone measuring. A speculation still standing is forgotten
rather than restored, since the world it would restore is being thrown away.

**Two things found while writing it:**

- **The snapshot keeper outlives two-clock.** `rollback_twoclock` switches the
  keeper on and nothing switches it off; with two-clock off, the keeper saves
  the whole world every tic (`K_RollbackTicker`). A player leaving a WORLDWIDE
  server would have paid a save a tic -- 1 to 4 ms on the maps measured --
  in every game after it, alone or hosting. The mode puts the keeper back as
  it found it. The harness has always left it on after a race, which does not
  matter there: the game quits.
- **A saved `worldwide` would contaminate the controls.** Set once on the
  measuring machine, it would put every later test server in the mode, and a
  client joining a `nospec` or `measure` server would then predict at join
  without the scenario saying so. So it is not saved.

**What it does not do yet:**

- A WORLDWIDE client on a *public* stock server still needs the release base
  of 8.30: the branch sits on upstream's development line.
- No menu entry: the host types `worldwide On` (console, command line or
  script). The server browser does not mark WORLDWIDE servers.
- Step 7 of the section (a few bytes appended to the server info, for a
  recommended delay) is not done.
- The mode's client settings are the `keep` race's, hitches included (8.78):
  it packages what exists, it makes nothing cheaper.

**The measuring scenarios are unchanged in intent.** Their server never runs
`worldwide`, so a client joining one switches everything off at join, then
the scenario's own lines -- run after the join -- set what it measures.
Every client log gains one line, `worldwide: this server runs the stock
netcode`.

**Predictions, for the first build that carries it:**

- `playtest.sh vanillajoin` (new, unattended, a few minutes; loopback on
  purpose): the first join is let in, the client's log says `worldwide: this
  server runs WORLDWIDE mode` and its bare `rollback_twoclock` prints 4 tics;
  the second join, undeclared, is refused -- the server's log says
  `worldwide: refused node N -- it did not declare itself WORLDWIDE` -- and
  nothing crashes on either side.
- `playtest.sh worldwide` (new; the client sets no prediction switch, one
  window for the whole race): the server prints `worldwide: on` and
  `rollback_correct: not set -- WORLDWIDE mode sends a light correction every
  4 tics`; the client prints `worldwide: this server runs WORLDWIDE mode`;
  `rollback_lagcheck` reads a target of 0 on both machines; no `Game state
  reloaded`, drift 0.000, and the kept fraction of the `keep` races: 99% or
  more unattended (8.77), 65 to 69% driven (8.78).
- Any old scenario, `keep` for instance: as before, with the one extra log
  line above.

⚠ Pushed the same day on Gibax's go-ahead ("oui pousse") as `51ba899d6`. CI
run 36541593195 green on its three jobs (Linux GCC, Windows clang release and
dev): the mode compiles. Not installed, not run.

### 8.81 Where a save's time goes -- and B2 through the level pools

Gibax, 2026-09-29, on the order of the tracks: "ça serait pas mieux de déjà
faire B2 ? ... on cherche à VRAIMENT gagner de la perf au plus possible",
then "oui pousse et commence B2". The case for B2 now, whatever a race with
remote people shows: in the driven `keep` race on Opulence (8.78) the saves
are 12.6 to 13.7 ms of a 29.5 to 33.9 ms pass, about 40%, at some 4 ms each,
and a rebuild saves every tic it re-runs. The measure of remote people only
says whether B2 is enough, not whether it is needed.

**The instrument, first** (`ba1e43523`, pushed, 8.82).
The load's steps have been timed since 8.34; the save had only its total.
Every local `P_SaveNetGame` now books each step's time and bytes -- netvars
and misc, numbering and the chain stamp, players, parties, world,
polyobjects, the object pointers, each of the six thinker lists, specials and
colormaps, waypoints, ACS, Lua, RNG and luabanks -- and `rollback_twoclock`'s
report prints the mean per save as `rollback_save` lines. About fifteen clock
reads a save.

**Found while writing it: the level pools.** Every thinker -- objects
(`P_SpawnMobj`), moving floors and ceilings, lights, polyobject and slope
thinkers -- and every sector node (`p_map.c`, `P_GetSecnode`) is allocated
from one of four fixed-block pools (`z_zone.cpp`, `srb2::PoolAllocator`):
blocks of `sizeof (mobj_t)` in chunks of 1024, of `sizeof (precipmobj_t)` in
chunks of 32768, of 128 bytes in chunks of 4096, of 64 in chunks of 8192. A
pool's free list is threaded through its free blocks, so its chunks, copied
whole, with its head and count, *are* its state. Copied back:

- an object removed since the snapshot is at its old address again, with its
  old fields, and one born since is gone -- its block is free again;
- every pointer from one pooled thing to another is right as it stands: the
  thinker list links, `target`/`tracer`/`hnext`, the sector and blockmap
  chains *in their order* (8.76's drift was that order), the sector nodes,
  the reference counts between pooled things;
- the next allocations hand out the same blocks in the same order as the
  first run did.

No per-type code for the forty thinker types and the hundreds of lines of
`SaveMobjThinker`: the 8.60 proposal ("keep removed objects aside, copy the
fields of objects and players raw") needed both halves written by hand. The
pools already do the first half.

**What a copy of the pools does not cover, and would need beside it** --
read, to be checked one by one when it is built:

- *pointers into the pools from outside them*: the thinker list heads
  (`thlist`), each sector's `thinglist`, `touching_thinglist`, `floordata`,
  `ceilingdata`, `lightingdata`, `fadecolormapdata` and precipitation list,
  the blockmap heads (`blocklinks`), polyobject fields (their thinker, 8.57),
  the players' pointers (`mo`, `followmobj` and the rest), the global object
  pointers the archive already enumerates (`P_SaveMobjPointers`), the TID
  hash heads, the renderer's interpolators, sound channels (already put back
  by object number, 8.73);
- *pointers out of the pools to memory freed or reallocated since*: an
  object's string arguments (freed with it, `P_DeleteMobjStringArgs`), FOFs
  added at run time, sector light lists, and outside the pools a player's
  roulette list (`itemList`, reallocated -- the known `itemList.cap`);
- *reference counts held from outside the pools*: ACS threads (8.52), and the
  players if they stay in the network format. A raw restore puts back the
  counts the snapshot had; a network loader that then adds its own would
  count twice. This is the part to get exactly right: a count too low frees
  an object still in use;
- *what the pools' free list cannot see*: a pool that grew a chunk after the
  snapshot has blocks the restored list does not reach -- they have to be
  given back, or they leak; a block freed by `LUA_InvalidateUserdata`'s path
  (`PoolAllocator::deallocate`) comes back while Lua forgot its userdata --
  Lua stays in the network format and finds objects by number, so it should
  make a new one;
- *everything else a tic changes*: the misc globals, RNG, world (heights,
  lights), polyobject positions, waypoints, ACS and Lua stay in the network
  format, minus the players and thinker sections the pools replace.

**How it would be checked**: a verify mode. After each raw restore, write the
network archive of the restored world and compare it byte for byte with a
network snapshot of the same tic, as the leak soak does (8.34); a difference
names its block (`P_LocateSnapshotBlock`). The seven maps' leak and resim
soaks in that mode, then the `keep` race.

**What it would cost (estimate, to replace with the instrument's pool
lines)**: a copy of the pools' chunks. The object pool on Opulence, about
3700 objects, is four chunks of 1024 objects -- a few MB, which a memory copy
moves in 0.3 to 1 ms, against about 4 ms of save. A 128-, 64- or
precipitation-sized chunk is far larger than what it holds: 32768 blocks for
the third. Copying whole chunks could cost more than the save it replaces;
limiting the copy to the blocks a chunk has ever handed out (a bump pointer
in `PoolAllocator`) makes it proportional to what the level used.

**B2, in order:**

1. the instrument (`ba1e43523`);
2. `PoolAllocator` snapshot and restore, bounded to the blocks ever handed
   out, and the chunks grown since given back -- no change in behaviour;
3. `rollback_rawsnap 1`: the pools and the pointers into them raw, the rest
   in the network format; `rollback_rawsnap 2`: the same, verified after
   every restore against the network archive;
4. the soaks of the seven maps in verify mode; then `keep` on Opulence, for
   the cost.

**Predictions for the instrument** (any race with `rollback_twoclock`'s
report; Opulence's `keep` for the figures of 8.78):

- a local save about 4 ms and about 290 KB there (8.60, 8.78), the object
  list (`thinkers: objects`) the largest step in time and bytes -- half or
  more; players under 0.5 ms; Lua and ACS together under 1 ms, the least sure
  of these (401 Lua gems and coins, 8.69); the world, waypoints, specials
  and polyobjects small;
- the object pool: about 3700 blocks in use on Opulence, four chunks. The
  block size is `sizeof (mobj_t)`, read in the report;
- if Lua is a millisecond or more, a copy of the pools leaves it in the save,
  and it is the next target.

⚠ Pushed the same day on Gibax's go-ahead ("oui pousse et continue B2") as
`ba1e43523` -- `d20b9a0a9` put on top of the docs -- CI run 36543175861 green
on its three jobs. Not installed, not run.

### 8.82 B2, step 2: the level pools snapshot and restore themselves

Written on 2026-09-29, `df8ed24e9` (pushed, see below; first written as `352d3f204`).
Checked by reading only, as 8.80 was: declarations, balance, the compile is
the CI's.

**`PoolAllocator`** (`core/memory.cpp`) gains `snapshot()`, `can_restore()`
and `restore()`, and `z_zone.cpp` does the four pools at once --
`Z_LevelPoolSnapshot`, `Z_LevelPoolRestore`, the second checking all four
before writing any, so a refused snapshot leaves every pool as it was.

- **Only what a pool has ever handed out is copied.** A new chunk's blocks
  were linked into the free list when the chunk was made, so the list ran
  through every block of every chunk and a copy would have had to take the
  chunks whole -- 32768 blocks for the precipitation-sized pool. They are no
  longer pre-linked: a never-used block comes from a fresh pointer that
  walks the last chunk. Blocks are handed out **in the same order as
  before** -- handed-back ones first, most recent first, then never-used ones
  in address order -- so nothing in play changes; the pool just knows where
  its used blocks end. A snapshot is the size of the most the level has ever
  held at once.
- **A restore refuses what is not its own**: another pool's snapshot (block
  size), other chunks (another level: the chunk addresses are in the
  snapshot and are compared), a length that does not add up.
- **Chunks grown since the snapshot** are taken off the pool and kept as
  spares, not freed: a pool that grows during a speculation and is restored
  every pass would otherwise allocate and free a chunk every pass.
- **Lua.** `deallocate()` has always made Lua forget a block as it is freed
  (`LUA_InvalidateUserdata`). A restore frees nothing block by block, so a
  block it hands back may still be known to Lua as the object that stood
  there after the snapshot. `allocate()` now forgets a block as it hands it
  out as well -- one registry lookup, which finds nothing for a block that
  went through `deallocate()`. What Lua itself holds across a raw restore is
  step 3's business (8.81).

**Nothing restores the pools in play yet.** Measured by a new command,
`rollback_poolcopy [times]`: it copies the pools `times` times (10 by
default), puts the last copy back onto the world it was taken from -- which
changes nothing -- copies again and compares the two byte for byte. And
`rollback_twoclock`'s report ends with a line, `rollback_save: a raw copy of
the level pools -- N KB, T us`, under the save's steps (8.81), measured on
the world as it stands at the report.

**Predictions, on Opulence** (a `keep` race or any race with
`rollback_twoclock`'s report; `rollback_poolcopy` at any point of a level):

- the round trip is **exact**, every time;
- a copy is **3 to 5 MB** -- about 3700 objects at `sizeof (mobj_t)`, which
  the report's pool lines give, plus the sector nodes -- and takes **0.3 to 1
  ms**, against about 4 ms for the network save of the same world (8.78). A
  restore of the same copy about as long;
- on Skyscraper Leaps, a third of that or less;
- if a copy is over 1.5 ms, copying every used block is too much, and the
  next idea is to copy only the blocks written since the last snapshot.

**Next, step 3**: `rollback_rawsnap`, the pools plus what points into them
from outside (8.81's list), with its verify mode. That is where the reference
counts held outside the pools, the players and Lua have to be settled.

⚠ Pushed the same day on Gibax's go-ahead ("oui pousse et fais le
recensement") as `df8ed24e9` -- `352d3f204` put on top of the docs -- on
`rollback-netcode`, and as the new branch `feature-b2`, where B2 goes on
from here and which merges back into `rollback-netcode` when it holds (Gibax:
"pour pouvoir reprendre après avec un merge dans la main branch"). CI run
36544246555.

### 8.83 B2, step 3's census: what points into the pools from outside them

Read only, 2026-09-29, on Gibax's word ("fais le recensement"). What a raw
restore of the level pools (8.82) does not put back by itself.

**How the census was taken.** Today's restore frees every object and makes it
again at a *new* address (`P_NetUnArchiveThinkers`, then `LoadMobjThinker`).
So any pointer into the pools that lasts from one tic to the next is already
rebuilt somewhere in the load -- or it dangles today. The census is what the
load rebuilds, checked against a search of the file-scope pointers to pooled
types. Every pooled pointer the load *relinks* goes through `P_SetTarget`
(`RelinkMobj`, `p_saveg.cpp`), which **counts a reference**: today every
count is rebuilt from zero at every restore.

**A. Inside the pools -- the copy puts them back, nothing to do:** thinker
list links; an object's `target`, `tracer`, `hnext`, `hprev`, `itnext`,
`owner`, `terrainOverlay`, `punt_ref`; the sector and blockmap chains
(`snext`, `sprev`, `bnext`, `bprev`) in their order; every sector node and its
links; the pointers other thinkers hold (an executor's caller); the
reference counts *between* pooled things; the objects' numbers.

**B. Outside, pointing in -- to save beside the pools, raw, small:**

| holder | where | today's load |
|---|---|---|
| thinker list heads, `thlist[]` | `p_tick.c` | `P_InitThinkers`, then each thinker appended |
| per sector: `thinglist`, `touching_thinglist`, `touching_preciplist`, `floordata`, `ceilingdata`, `lightingdata`, `fadecolormapdata` | `r_defs.h:474-543` | cleared, then `P_SetThingPosition` per object and each thinker's loader |
| an FOF's `fadingdata` | `r_defs.h:263` | its fade thinker's loader |
| blockmap heads, `blocklinks[]`, `precipblocklinks[]` | `p_setup.cpp:190` | `P_SetThingPosition` |
| a polyobject's `thinker` | `p_polyobj.h:107` | the polyobject thinker loaders (8.57) |
| TID chains, `TID_Hash[]` | `p_mobj.c:15889` | `P_AddThingTID` per object |
| `waypointcap`, `trackercap`, `overlaycap` | `p_mobj.c:61-67` | `P_SetTarget` in `LoadMobjThinker` (the first two); `overlaycap` to check |
| `svg_battleUfoSpawners`, `svg_checkpoints`, `svg_rocks` | `p_link.cpp` | `P_LoadMobjPointers`, relinked by number |
| `g_endcam.panMobj` | end camera | relinked by number |
| `skyboxviewpnts[]`, `skyboxcenterpnts[]` | `p_spec.c:63` | `P_InitSkyboxPoint` per skybox object |
| `tubewaypoints[][]` | `p_setup.cpp:218` | relinked by number, `P_SetTarget` |
| race waypoints' `mobj` | `k_waypoint` | relinked by number, `P_SetTarget` |
| **the players**: about twenty pointers -- `mo`, `followmobj`, `follower`, `awayview.mobj`, `skybox.*`, `ringShooter`, `hoverhyudoro`, `flickyAttacker`, `stumbleIndicator`, `wavedashIndicator`, `trickIndicator`, `whip`, `hand`, `flybot`, `ballhogreticule`, `stoneShoe`, `toxomisterCloud`, `powerup.*` | `players[]` | relinked by number in `P_RelinkPointers`, `P_SetTarget` |
| module statics: `beamPoints[2]` (`k_race.c:46`), `minimapGear` (`objects/ancient-gear.c:40`) | | **not seen in the load**: set again by their own code each tic, or already dangling after today's restores -- to check |

**C. Outside, pointing in -- derived or short-lived, rebuilt or harmless:**
the renderer's object interpolators (`R_AddMobjInterpolator`, rebuilt for
every object today) and level interpolators (per thinker); sound channels
(put back by object number since 8.73 -- with stable addresses, only a
channel on an object born after the snapshot needs stopping); Lua's registry
(keyed by address: `allocate()` forgets a reused block since 8.82, Lua itself
stays in the network format and finds objects by number); and pointers that
only live inside one call -- `currentthinker`, `sector_list`,
`precipsector_list`, `tm.*`, `r_viewmobj`, and the file-scope temporaries of
iterator callbacks (`grenade`, `lightningSource`, `attractmo`, `stand`,
`slidemo`, `bombsource`, `bombspot`, `minus`, `barrel`, `sourceofmurder`,
`referencepuyo`, `bestpuyo`, `promptmo`). The cameras are already saved
beside each snapshot.

**D. Out of the pools, to memory that can change under them:**

- **an object's string arguments** (`thing_stringargs`) are freed when the
  object is removed (`P_DeleteMobjStringArgs`, `p_mobj.c`): an object removed
  after the snapshot comes back pointing at freed strings. Remedy: keep them
  while a snapshot may need them, or put them in the snapshot;
- everything else read is level data made at load and kept to its end --
  `player` (the `players[]` array), `subsector`, `state`, `info`,
  `spawnpoint`, `standingslope`, `floorrover`/`ceilingrover` (FOFs are only
  made by `P_SpawnSpecials`, at load), sectors, lines, polyobjects.

**E. Reference counts -- the part to get exactly right.** A raw restore puts
back each count as the snapshot had it, which includes the references held
from *outside* the pools at that moment (B's players, caps, links, waypoints;
ACS threads, 8.52). Whatever is then reloaded through `P_SetTarget` counts
again. A count too high only keeps a removed object's block from being freed
(a leak); **too low frees an object still in use**. Two ways, to choose in
step 3:

1. **Everything that counts is restored raw**: the players (`players[]`
   copied whole, with the one pointer out that moves -- the roulette's
   `itemList`, reallocated -- handled apart), the caps, links and waypoints'
   pointers. Only ACS stays in the network format, and its reload must not
   count the references its threads already had.
2. **Recount**: after the restore and the network sections, set every count
   to zero and walk every holder once -- the enumeration `P_RelinkPointers`
   already has, plus ACS -- adding one per pointer. Slower (every object,
   every restore), but nothing to keep in step with.

Either way, the verify mode should **check the counts** as well as the
bytes: the network archive does not store them, so the byte comparison of
8.81 cannot see a wrong one. A full walk that recounts and compares, in
verify mode only, can.

**F. What stays in the network format**: netvars and misc (`leveltime`, the
globals), RNG and luabanks, the world (heights, lights, flags), polyobject
positions, specials, colormaps, waypoints' state, ACS, Lua, the end camera,
parties, round queue and vote -- a local save without its thinker section,
and without the players if they go raw. The instrument's lines (8.81) say
what that leaves to write.

**What the census changes in the plan:** nothing in the order, two things in
the scope. The raw part is the pools **plus** about twenty small arrays and
heads (B), a copy of `players[]` if E1 is chosen, and the string arguments.
And the verify mode needs a reference-count check beside the byte
comparison. Two module statics (`beamPoints`, `minimapGear`) are to read
before relying on them.

### 8.84 Driven `keep` on Opulence with the instruments: the history loses its place, and a raw copy of the pools is 0.4 ms

Measured on 2026-09-29, binary `df8ed24e9` (sha256 `385b0a29...`, CI run
36544246555), both copies checked against the artefact, the previous exe
kept as `.bak_755ea3c`. Gibax's go-ahead ("oui, installe et lance keep sur
Opulence, je pilote"), **driven by Gibax**. Processor at 27% before the
launch, League of Legends client closed. The player entered the game
(`playlog_keep_RR_Opulence_20260929-124631_df8ed24.txt`). Predictions in
8.79, 8.81, 8.82.

**Kept speculation and its rebuilds**

| | window 0 | window 1 | window 2 | 8.78 (driven) |
|---|---|---|---|---|
| passes kept, of 999 | **965** | **830** | **720** | 654 - 694 |
| rebuilt: an input differed | 34 | 168 | 279 | 305 - 345 |
| first wrong tic, from the frontier: 0 / 1 / 2 / 3 / 4+ | 24 / 7 / 3 / 0 / 0 | 131 / 28 / 9 / 0 / 0 | 256 / 16 / 6 / 1 / 0 | |
| wrong inputs on it: this machine / bots | 32 / 4 | 166 / 9 | 279 / 0 | |
| ... this machine's: received / **replayed** / guessed | 0 / **32** / 0 | 0 / **165** / 1 | 0 / **279** / 0 | |
| ... differing in `latency` / `angle` / `turning` / `buttons` | 32 / 28 / 4 / 4 | 166 / 122 / 48 / 16 | 279 / 181 / 95 / 22 | |
| tics thrown away / kept by a rebuild from the first wrong tic | 273 / 13 | 1329 / 46 | 2004 / 31 | |
| pass | **9.7 ms** | 20.6 | 29.3 | 29.5 - 33.9 |
| frames drawn in 1000 tics | **3458** | 2595 | 1786 | 1556 - 1852 |
| iterations past a tic | 35 | 172 | 282 | |
| inputs in flight / depth | 7.09 / 7.98 | 6.31 / 8.02 | 5.96 / 7.33 | |
| a speculated tic | 3.96 ms | 4.26 | 4.75 | |

- **8.79's prediction fails, and its doubt was right: every wrong local
  input was replayed from the history, none guessed past it** (one in 477).
  Every one differs in `latency` -- **the replay handed the tic a different
  sample from the one the server applied** -- and most in `angle`, which
  `D_ResetTiccmdAngle` rewrites across the history (the reason
  `K_RollbackMapHistory` does not match on it). So 8.78's reading was wrong:
  the speculation does not run past the newest input; the history's
  alignment with the server's tics slips.
- **The first wrong tic is the frontier's own** (24 of 34, 131 of 168, 256
  of 279): the oldest tic in flight, the one just confirmed. A rebuild from
  the first wrong tic would keep 1.5 to 5% of what is thrown away -- **not
  the remedy**. The remedy is where the history loses its place.
- **It feeds on itself**: 3% of passes rebuilt in window 0, 17%, then 28%;
  the pass 9.7 ms, 20.6, 29.3; iterations past a tic 35, 172, 282. A late
  frame is the likely way a sample and a tic come apart (a tic run with no
  new sample, or two samples in one), and each rebuild makes a late frame.
  Not measured.
- **Window 0 is what A gives when the history holds: 97% kept, a pass of
  9.7 ms, 3458 frames in 1000 tics -- 121 a second, driven, on Opulence.**
- The bots: 13 wrong inputs in 3000 passes.
- Drift 0.000 but once: tic 3348, this machine's kart 0.036 units out,
  `speed` 729370 against 729410, put back once. One sample in 6696; not
  explained.

**Where a save's time goes** (8.81; per local save, 1260 to 3001 saves a
window):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| a local save | 2938 us, 275 KB | 2865 us, 288 KB | 2896 us, 296 KB |
| thinkers: objects | 1938 us, 231 KB | 1961 us, 243 KB | 1975 us, 251 KB |
| world | 464 us, 4 KB | 452 us, 4 KB | 453 us, 4 KB |
| numbering / chain stamp | 283 us | 219 us | 201 us |
| Lua / ACS | 147 / 18 us | 146 / 18 us | 151 / 18 us |
| players | 4 us, 11 KB | 4 us, 11 KB | 4 us, 11 KB |
| specials, slopes, main list, waypoints, misc | 78 us | 57 us | 86 us |

- **Holds**: the objects are two thirds of the time and 84% of the bytes;
  players far under 0.5 ms; Lua and ACS together 0.17 ms, far under 1 ms --
  so Lua is **not** the next target.
- **Fails**: a save is **2.9 ms, not about 4**, and the world is not small:
  **0.45 ms for 4 KB**, a sixth of the save; the numbering and chain stamp
  another 0.2 to 0.3 ms. The world is the second target after the objects.
- The 4 ms of 8.78 was an estimate from counting saves; measured directly a
  save is 2.9 ms against 2.2 to 2.8 up to `5a417494f` (8.78). The
  "regression" is smaller than 8.78 said, if it is one at all.

**The level pools and their raw copy** (8.82):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| objects: 776-byte blocks in use / chunks | 3982 / 5 | 4231 / 5 | 4050 / 5 |
| 128-byte / 64-byte blocks in use | 501 / 8783 | 501 / 8851 | 501 / 8806 |
| **a raw copy of the pools** | **4091 KB, 385 us** | **4148 KB, 375 us** | **4233 KB, 460 us** |

`rollback_poolcopy` at the end: 4233 KB, a copy 369 us (mean of 10), putting
it back 192 us, **the round trip exact**.

- **Holds**: exact; 4.1 to 4.2 MB, in 3 to 5; 0.37 to 0.46 ms, in 0.3 to 1.
  The block size is 776 bytes, and five chunks, not four (3982 to 4231
  objects, not about 3700).
- **Against the save it replaces: 0.4 ms against 2.9**, and the restore half
  that. Well under 8.82's 1.5 ms line: **step 3 of B2 is worth building**
  (the resume note of 2026-09-29: `rollback_rawsnap` on `feature-b2`, after
  choosing E1 or E2 for the reference counts, 8.83).

**So, in order:** find where the history and the server's tics part (reading
first: how the client builds and labels its samples when a frame is late,
and how the server fills a tic that has none); then B2's step 3, whose raw
copy is measured at a seventh of the save; the world's 0.45 ms after that.
- **Gibax's feel**: "au début très fluide, aucun souci ; vers la fin par
  contre ça laggait un peu plus" -- the windows' figures, felt.

### 8.85 Where the history loses its place: read in the code

Read on 2026-09-29 on Gibax's go-ahead ("oui, commence la lecture pour
l'historique"); nothing launched. `rollback_history`'s replay
(`K_RollbackLocalCmdFor`) assumes that the server applies this machine's
samples **one per tic, in order**, starting from the one it applied last. What
the code does instead:

1. **One sample per `NetUpdate`, whatever the time elapsed.** `NetUpdate` is
   called once per `TryRunTics`, returns when no real tic has passed, and
   otherwise builds exactly one sample (`Local_Maketic`, `CreateNewLocalCMD`)
   -- also when two to five tics have passed (`realtics`, capped at 5 on a
   client). `CL_SendClientCmd` sends that one sample, with no tic number of
   its own.
2. **The server files a sample by when it arrives, not by its rank**
   (`HandlePacketFromPlayer`, `PT_CLIENTCMD`): at `faketic = maketic` (plus
   the wanted delay, zero here), or one tic later if that slot already holds
   a received sample -- once; a third sample in the same slot overwrites the
   second.
3. **A tic with no sample repeats the previous one** (`SV_Maketic`): same
   input, same stamp, `TICCMD_RECEIVED` cleared.
4. **`rollback_lag` delays on reception only** (`d_net.cpp`): in the harness
   the client's samples reach the server over the loopback the moment they
   are sent. The server's placement follows the client's sending pace
   exactly.

**The chain this gives, not measured:** a client frame longer than a tic
makes one sample for two tics; the server finds a tic with nothing and
repeats the previous sample; every sample still in flight then lands one tic
later than the replay assumed; the kept speculation's replayed tics hold the
neighbouring sample -- a different `latency` stamp every time, a different
`angle` and `turning` while steering, exactly 8.84's fields -- and are
rebuilt at the frontier. A rebuild costs about two tics, so the next frame is
late too. 8.84: 35, 172 and 282 iterations past a tic, against 34, 168 and
279 rebuilds.

The other way round: when the server's own frame is late, two or three
client samples reach one `GetPackets`; the first goes to `maketic`, the
second to the tic after, a third overwrites it -- a sample lost, and the
replay one tic early. Here the server is the same machine, on the same map,
with a window and a host player.

**A second candidate, on the client: the anchor.** `K_RollbackMapHistory`
finds the sample the server applied last by its stamp, forward move, turning
and buttons, newest match first. The stamp is `leveltime & 0xFF` when the
sample is built, and in two-clock mode that is the **frontier's** leveltime
(`K_RollbackKeepArm` hands it over; the restore gives the same without
`rollback_keepspec`). The frontier moves by the tics confirmed in the pass
before: 1.03 to 1.30 a pass in 8.84, so some passes confirm two and others
none. **After a pass that confirmed none, two consecutive samples carry the
same stamp**; with the same buttons and turning -- a keyboard held full
left, or straight ahead -- they are the same to the anchor, the newer is
taken, and the inputs in flight are undercounted by one: the same slip.

Why it barely showed on Skyscraper Leaps (99 to 100% kept, 8.76): a pass
there is 2 ms, so frames are almost never late and passes rarely confirm
two tics or none.

**To tell the candidates apart, an instrument** (client and server,
`rollback_keepspec`'s report and the relabel report):
- client: samples made after more than one real tic (`realtics` 2 or more);
  anchors that matched more than one sample in the history;
- server: per player, tics filled by a repeat, and samples overwritten.
If the rebuilds follow the late samples and the repeats, it is the first
chain; if they follow the ambiguous anchors, the second.

**Remedies, by reach:**
- **R1, client only**: the replay follows the server's rule -- a sample
  made after `k` real tics is preceded, in the replay, by `k - 1` repeats of
  the one before it, as `SV_Maketic` will do; and the anchor, when
  ambiguous, is placed by the count of samples since, not by the newest
  match. Exact while the client-to-server path has no jitter (the harness);
  approximate on a real network, and blind to the server's own late frames.
- **R2, WORLDWIDE mode, server and client**: every sample carries its own
  sequence number, and the server files a WORLDWIDE client's samples by it,
  at a fixed offset from the first -- the input buffer of ordinary
  client-side prediction. A tic still repeats when a sample is really late,
  and only then. Exact on any network but for true lateness; vanilla
  clients untouched, since the mode (8.80) decides who is WORLDWIDE. A
  change to the packet and to the server's filing.
- R3, client: after a late frame, send one sample per elapsed tic -- the
  stock "+1 once" rule absorbs two, not three. Half a remedy.

**Recommended:** the instrument first (it decides which chain), then R2 --
the only one exact on a real network, and the WORLDWIDE switch now exists to
carry it -- with R1 as a stopgap if R2 takes long.

### 8.86 An instrument for 8.85's two chains

Written on 2026-09-29 on Gibax's go-ahead ("oui, écris l'instrument"):
`f719868f8`, on the local branch `wip/filing`, **not pushed**. Syntax
checked on `k_rollback.c`, `d_clisrv.c` and a C++ file that includes the
header, with no warning; an error injected at each of the fifteen changed
places is reported by the compiler.

- **Client**, in `rollback_history`'s report (reset with it): samples made
  after more than one real tic, and the tics that got no sample of their
  own; samples with the same stamp as the one before; passes whose anchor
  matched more than one sample.
- **Server**, in `rollback_relabel`'s report, per player (never reset, like
  its histogram): samples filed a tic late because the slot was taken,
  filed over one already there, and tics that got none and repeated the one
  before.
- **`rollback_keepspec`**, for this machine's wrong inputs: where the sample
  the server applied sits in the history against the one replayed -- newer
  (the replay fell behind: a sample lost at the server) or older (it ran
  ahead: a repeated tic, or an anchor that took a newer twin).
- The anchor's matching moved into a helper; what it matches on is
  unchanged.

**Predictions, for a driven `keep` race on Opulence:**

- **The replay runs ahead**: 80% or more of this machine's wrong inputs have
  the server's sample **older by one**; newer ones a handful.
- **The first chain**: the client's late samples about as many as the
  iterations past a tic, and the server's repeated tics for this machine's
  player within 10% of the client's tics without a sample of their own;
  samples filed late or over another, a handful -- the server's frames are
  rarely late.
- **Not the second**: ambiguous anchors on fewer than 10% of passes, samples
  with a shared stamp a few percent.
- If instead the misses follow the ambiguous anchors, the anchor is the
  thing to fix first (count the samples since the last anchor rather than
  take the newest match), before R2.

⚠ Pushed on Gibax's go-ahead ("oui, pousse, installe et lance keep sur
Opulence, je pilote") as `4adeea840` -- `f719868f8` put on top of the docs --
CI run 36559025884.

### 8.87 The replay runs one sample ahead after every late frame -- 8.85's first chain, on the client side

Measured on 2026-09-29, binary `4adeea840` (sha256 `b8b03cf4...`, CI run
36559025884), both copies checked, the previous exe kept as `.bak_df8ed24`.
Gibax's go-ahead ("oui, pousse, installe et lance keep sur Opulence, je
pilote"), **driven by Gibax**; processor at 5%, League of Legends client
closed; the player entered the game
(`playlog_keep_RR_Opulence_20260929-130740_4adeea8.txt`). Predictions in 8.86.

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| passes kept, of 999 | **987** | **998** | 842 |
| rebuilt: an input differed | 12 | 1 | 155 |
| this machine's wrong inputs / bots' | 9 / 8 | 1 / 0 | 155 / 0 |
| **server's sample against the replayed: older by one** | **9 of 9** | **1 of 1** | **155 of 155** |
| **samples made after more than one real tic** (tics with none) | **9** (9) | **2** (2) | **157** (160) |
| iterations past a tic | 13 | 3 | 159 |
| samples with the stamp of the one before | 6 | 12 | 26 |
| passes whose anchor matched more than one sample | 5 | 6 | 64 |
| pass | **7.2 ms** | **6.7** | 18.4 |
| frames drawn in 1000 tics | **3765** | **3769** | 2778 |
| a local save / a raw copy of the pools | 2573 / 368 us | 2676 / 430 us | 2508 / 346 us |
| drift | 0.000 | 0.000 | 0.000 (worst 0.001) |

- **Holds: the replay runs ahead, every time** -- 165 of 165 of this
  machine's wrong inputs, always by exactly one sample (80% or more
  written). None newer, none the same to the anchor.
- **Holds: the first chain, on the client's side.** The wrong inputs follow
  the samples made after more than one real tic almost one for one -- 9 and
  9, 1 and 2, 155 and 157 -- and those follow the iterations past a tic.
- **Holds: not the anchor.** Ambiguous anchors on 0.5, 0.6 and 6.4% of
  passes, shared stamps 0.6 to 2.6% of samples, and neither follows the
  misses (window 2: 64 ambiguous anchors, 155 misses).
- **Cannot be checked: the server's side.** It printed `rollback_relabel`
  once, about 2100 tics into the race -- the scenario's next report comes
  after the harness has killed it -- and the count starts on the waiting
  map. Up to there: this machine's player 2901 samples filed, **678 a tic
  late because the slot was taken**, 0 over another, **39 tics repeated**;
  the host 3498, 2000 and 2. So the server takes up timing more often by
  its "one tic later" rule than by repeating, and 8.86's "repeats within
  10% of the late samples" cannot be set against window 2. For the replay
  it makes no difference: either way, a sample made after `k` real tics
  leaves `k - 1` tics to the one before it, which is what the misses show.
- **The best driven race yet**: windows 0 and 1 kept 98.8 and 99.9% of
  passes, a pass of 7.2 and 6.7 ms, 132 frames a second, driven, on
  Opulence. Window 2 fell into the loop again (157 late samples), and still
  kept 84% at 97 frames a second. What started it is not measured: the
  first late frame comes before any rebuild.

**So: R1 is what the numbers ask for.** Record how many real tics each
sample stands for, beside the history, and let the replay give the sample
before a late one `k - 1` extra tics -- anchored on the first confirmed tic
that holds the anchor's sample. Client only, no protocol change, and every
one of the 165 misses above is of the kind it removes. R2 (samples filed by
sequence, WORLDWIDE mode) stays the remedy for a real network, where
arrivals jitter on their own.

The harness's server scenario prints `rollback_relabel` every 2000 tics and
is killed before its last print; it needs a print inside every client
window.

### 8.88 B2, step 3: raw snapshots, with every reference count rebuilt (E2)

Written on 2026-09-29 on Gibax's choice of E2 (8.83: "E2"), `371ca7419`,
branch `feature-b2`, **not pushed, never compiled** (no compiler on the
machine it was written on). Behind a new switch, off by default.

**`rollback_rawsnap 0|1|2`** -- 0: network snapshots, as before. 1: raw
snapshots. 2: raw snapshots **verified**: beside each one the full archive,
and after each restore the archive of the restored world compared with it
byte for byte, and every rebuilt reference count compared with the one the
copy brought back. The tests and soaks compare archives, which mode 1 does not
write: they refuse to start in mode 1, and a running soak waits.

**A raw snapshot is three parts**, in one buffer per slot (about 5 MB on
Opulence, grown as needed, so about 100 MB for the 20-slot ring):

1. the archive **without what the pools hold** (`P_SaveNetGameRaw`): of the
   thinker section only the global object links; no chain stamp -- the chains
   come back raw; objects never archived are numbered 0, so a restored one
   can never answer for a synced object in the relink index;
2. the **level pools** (`Z_LevelPoolSnapshot`, 8.82);
3. the **heads** that point into the pools from outside (`P_SaveRawHeads`,
   8.83's list B): thinker list heads; per sector its object chain, its
   sector-node and precipitation-node lists and its four thinker slots; each
   FOF's fade thinker; the blockmap and precipitation blockmap heads; each
   polyobject's owner; the TID chains; `waypointcap`, `trackercap`,
   `overlaycap`; the skybox points.

**A raw restore**, every part checked before anything is written (the heads
against this level's layout, the pools against their chunks):

- sounds noted by object number, and the living thinkers listed, while the
  old objects are still in memory; then the pools, then the heads;
- Lua forgets every thinker the restore took away -- the objects born after
  the snapshot: a network load's purge makes it forget every object, and a
  raw copy frees nothing block by block;
- the ACS era is advanced, as `P_InitThinkers` does in a network load: ACS
  threads forget the objects they held without giving their references back;
- **every reference count set to zero**; the network sections are loaded --
  players, world, polyobjects (their owner is no longer nulled: it came back
  with the heads), specials, waypoints, ACS, Lua -- and the loaders count the
  references they relink: players, race and tube waypoints, the global
  links, the end camera, ACS threads;
- players reclaim their bodies (`players[].mo`), as `LoadMobjThinker` does;
- **`P_CountRawReferences`** counts the rest: every object's eight counted
  pointers, *removed objects included* -- `P_RemoveMobj` gives back all of
  them but `terrainOverlay`, which the live count therefore still holds; the
  caller of every delayed linedef executor; the caps and the skybox points;
- the renderer's interpolation list is rebuilt from the objects there are.

**Found on the way -- a latent defect of today's restore:** a delayed linedef
executor counts its caller (`p_spec.c`, `P_SetTarget`), but the network load
puts the caller back without counting it (`P_NetUnArchiveThinkers`,
`restoreNum`). After any restore the caller's count is one too low, so an
object held only by a pending executor could be freed under it. The raw
restore counts it; verify mode will say whether the live counts agree.

**Kept, not fixed:** level interpolators (sector planes a thinker moves) are
not rebuilt, so a thinker born after the snapshot leaves a stale one and one
brought back has none -- visual only, a plane moving without smoothing;
precipitation is rolled back with the pools; the string arguments of
removed objects are no longer freed while raw snapshots may bring them back
(a few bytes an object, freed with the level); `beamPoints` and
`minimapGear` (8.83) are still to read.

**Harness:** `soak.sh leakraw` and `soak.sh wwraw` -- the leak and resim soaks
with `rollback_rawsnap 2`, and its report at the end; `playtest.sh keepraw`
-- the `keep` race with `rollback_rawsnap 1`, for the cost.

**Predictions, on the first build that carries it** (each launch asked for):

- **It compiles** -- or the CI names the lines to fix; the code is large and
  was never compiled.
- **`soak.sh leakraw`, then `wwraw`, on Skyscraper Leaps, then Opulence:**
  every restore verified; **no archive difference** but the ones the stock
  soaks already show (`itemList.cap`, in the players block, which stays in the
  network format); **no reference count rebuilt differently** -- or, if some
  are, each line names an object type and its live and rebuilt counts, which
  is a holder the recount misses (or counts twice). The soak's own failures
  as in the stock soaks (8.59, 8.62).
- **`playtest.sh keepraw map=RR_Opulence`, driven:** a raw save **1.2 to 1.6
  ms** (the pools 0.4, the world 0.45, Lua and ACS 0.17, numbering and the
  rest) against 2.9 (8.84); a raw restore **1.5 to 2.5 ms** against about 6.5
  (8.60); so a kept pass about 1.5 ms cheaper, and a rebuild -- a restore and
  a save per re-run tic -- 15 to 20 ms cheaper. The fraction kept unchanged
  (the history's loop, 8.87, is not this).
- If the verify mode finds nothing on the seven maps' soaks, mode 1 can be
  trusted in races; if it finds counts off, the missing holder goes into
  `P_CountRawReferences` before anything else.

⚠ Pushed on Gibax's word ("pousse aussi feature-b2, et vois pour la fusionner
si ça change pas trop") -- `feature-b2` pushed, then merged into
`rollback-netcode` as `49daf1196` (a merge commit, so `371ca7419` stands):
with `rollback_rawsnap 0` the snapshot paths are the ones before -- the
default write is unchanged, the rest applies in raw mode only, and the relink
index's treating 0 as blank changes nothing a network load does. CI run
36588490220: the first compile of this code.

### 8.89 R1: the replay gives each sample the tics the server gives it

Written on 2026-09-29 on Gibax's go-ahead ("écris-le"), `7a455f6fe` on
`rollback-netcode`, **not pushed**. What 8.87 asked for.

**The fault (8.85, 8.87):** `rollback_history` replayed the samples in flight
one per tic. `NetUpdate` makes one sample a call however many real tics went
by; the server gives a tic that got no sample of its own to the sample
before -- a repeat (`SV_Maketic`) or its "one tic later" filing. After every
late frame the replay ran one sample ahead: 165 of 165 of this machine's
wrong inputs in 8.87, and the loop -- a rebuild makes the next frame late.

**R1, client only:**

- every sample's real tics (`realtics`, capped at 5 by `NetUpdate`) are kept
  in a ring beside the local history, filled by `K_RollbackNoteSample` for
  every sample, in a level or not, so the ring ages with the history;
- the anchor also counts how many received tics, ending at the last one,
  still hold the applied sample -- tics the server already gave it by
  repeating it;
- the replay then gives each sample as many tics as real tics passed before
  the next one was made, less the ones already received for the applied
  sample; the newest owns every tic past them (`K_HistoryAgeForTic`).

Exact while the path to the server does not jitter (the harness: the
client's samples reach the server over the loopback when sent, 8.85);
approximate on a real network, where R2 (samples filed by sequence, WORLDWIDE
mode) stays the remedy. Blind to the server's own late frames, which 8.87's
server side shows are rare against the client's (39 repeats against 678
shifted filings).

**`rollback_histreal 0|1`**, on by default -- it only changes what
`rollback_history` replays, itself off by default and on in WORLDWIDE mode.
0 gives the old layout back, for a control. `rollback_history`'s report adds
how many passes R1 laid out differently from one sample a tic.

**Harness:** `playtest.sh keepnor1` -- `keep` with `rollback_histreal 0`,
the same-session control.

**Predictions** (on a build with R1; `keep map=RR_Opulence` driven, then
`keepnor1` in the same session):

- **`keep`**: this machine's wrong inputs **from about 165 to 10 or fewer**
  over the three windows, with the late samples about as many as before --
  R1 does not make frames less late, it stops a late frame from costing a
  rebuild; **no window falls into the loop**: 97% or more of passes kept in
  every window, a pass around 7 ms, 120 frames a second or more, drift
  0.000. The history's report shows R1 laying out about as many passes
  differently as there were late samples.
- **`keepnor1`**: 8.87 again -- this machine's wrong inputs following the
  late samples one for one, older by one, and the loop in at least one
  window.
- If `keep` still misses after late samples, the misses' direction says
  where: older by one again -- the held count is short (a repeat not seen as
  one); newer -- R1 gives a sample a tic the server did not.

⚠ Pushed the same day on Gibax's go-ahead ("pousse"), `7a455f6fe` with its
docs `decbe360a`, CI run 36589172001. The merge of B2 step 3 before it
(`49daf1196`, CI run 36588490220) is green on its three jobs: the raw
snapshot code compiles with GCC and clang.

⚠ Read on the measuring machine before any race, 2026-09-29: two places R1
leaves on one sample a tic. (1) `K_SpeculationDepth` still takes the lead
from the samples in flight (`g_histunacked`), not from the tics R1 gives
them: after a late sample the speculation may stop short of the tic the
newest sample lands on, so the newest input reaches the drawn world a tic
or two later -- a matter of feel, not of what is kept. (2)
`K_LocalInputSource` still calls a tic replayed only within the first
`unacked` tics past `g_histbase`: a tic R1 fills from the history past those
is counted **guessed**. So in the `keep` race a miss read as "guessed past
it" may be a replayed one; its "sample against the one replayed" line says
which. The same machine had written R1 separately (local branch
`wip/histgaps`, never pushed), with the lead taken from what the samples
cover and the source read the same way; it stays a reference, not a
candidate.

### 8.90 B2 step 3's raw snapshots, soaked: exact enough on Skyscraper Leaps, not on Opulence

Measured on 2026-09-29, binary `decbe360a` (sha256 `9e403fef...`, CI run
36589172001; R1 on the merge of B2 step 3), both copies checked, the
previous exe kept as `.bak_4adeea8`. Gibax's go-ahead ("réessaie donc, et
lance"), unattended; processor at 40% at launch. Predictions in 8.88. Logs
`soaklog_{leakraw,wwraw}[_RR_Opulence]_20260929-22*_decbe36.txt`.

| `rollback_rawsnap 2` | leakraw, Skyscraper | wwraw, Skyscraper | leakraw, Opulence | wwraw, Opulence |
|---|---|---|---|---|
| soak failures | **0 of 260** | **0 of 347** | **19 of 280** | **23 of 377** |
| same soak, network restore | 1 of 260 (8.34) | -- | 0 of 292 (8.59) | 1 of 386 (8.59) |
| restores verified | 1560 | 1041 | 1680 | 1131 |
| ... the archive differing after | 252 | 141 | **1400** | **1131** |
| ... first difference | players block | players block | **thinkers block, byte 14942** | **thinkers block, byte 14942** |
| ... reference counts rebuilt differently | 12 | 6 | 6 | 3 |
| raw save / raw restore (verified) | 278 / 670 us | 241 / 698 us | 842 / 1628 us | 905 / 1832 us |
| pools / heads / archive per save | 1512 / 44 / 14 KB | 1539 / 44 / 14 KB | 4112 / 157 / 16 KB | 4074 / 157 / 16 KB |

- **Fails, three ways** (8.88 said: no archive difference but `itemList.cap`,
  no count rebuilt differently):
  1. **Opulence: the raw restore does not give back the world at the start
     of the thinkers block** -- byte 14942, on 1400 of 1680 and 1131 of 1131
     restores, from the first check (tic 701). The block's first list is the
     dynamic slopes' (8.58: 41 of them on Opulence). And the soaks fail again
     where the network restore held: the leak soak's failures are kart
     fields ("8 players examined -- 7 runs differ as fields"), 8.58's shape,
     when a slope's plane was left from the tics before a restore. **A
     reading, not checked: the raw path does not restore the slope planes
     that `LoadSlopePlane` restores in the network path** (8749842d6), or
     puts them back before the thinkers that recompute them. On Skyscraper
     Leaps, which has no dynamic slope, nothing of the kind.
  2. **Reference counts**: `MT_PLAYER` always one higher live than rebuilt
     (for example live 15, rebuilt 14; 8 counts off at the first check, as
     many as there are karts) -- **a holder of a kart
     the recount misses**; `MT_SIGN_PIECE` live 0, rebuilt 1 or 2 -- **a
     pointer the recount counts that never took a reference**. The report
     prints the first few; 3 to 12 restores in each soak.
  3. **Skyscraper Leaps: the players block differs after 13 to 16% of raw
     restores** (bytes 4125 to 9917), with no soak failure. The players stay
     in the network format, so this is the network section reading back a
     world the raw part put back differently -- not identified; `itemList.cap`
     is one candidate, at a rate far above the stock soaks' (1 check in 260).
- **Holds, the cost**: a raw save 0.24 to 0.28 ms on Skyscraper Leaps and
  **0.84 to 0.91 ms on Opulence** (4.1 MB of pools), against a local save of
  2.5 to 2.9 ms there (8.84, 8.87); a verified restore 1.6 to 1.8 ms on
  Opulence, the check included. The save is below 8.88's 1.2 to 1.6 ms.

**So:** not yet safe on a map with dynamic slopes -- **`keepraw` waits**.
Next, reading the code, no launch: where the raw path leaves the slope planes;
which holder of a kart `P_CountRawReferences` misses; what a sign piece's
counted pointer is. The R1 races (`keep`, `keepnor1`) do not use raw
snapshots and can go ahead.

### 8.91 The three differences of 8.90, read in the code

Read on 2026-09-29 while the R1 races ran; nothing changed.

1. **The slope planes.** A dynamic slope's plane (`pslope_t`) is allocated
   with `Z_Calloc(..., PU_LEVEL)` (`p_slopes.c:349`), not from the level
   pools, so the raw copy never puts it back; the network path does, through
   `SaveSlopePlane`/`LoadSlopePlane` in the slope thinkers' archive
   (`8749842d6`, 8.58), which the raw path no longer writes
   (`P_NetArchiveThinkersRaw` writes only the object pointers). So after a
   raw restore every dynamic slope keeps the plane the tics after the
   snapshot left: **8.58's bug, back through the raw path** -- Opulence's
   byte 14942 and its kart-field failures. `mobj_t::floorspriteslope`
   (`p_mobj.c:12081`) is the same kind of allocation, held by a pooled
   object: after a raw restore it can point at a plane freed since.
   **Remedy**: the raw archive keeps the planes of the dynamic slope
   thinkers (`SaveSlopePlane` over `THINK_DYNSLOPE` and `THINK_DYNSLOPEDEMO`
   in list order, read back in the same order), and the floor-sprite slopes
   go the same way or are checked for.
2. **A kart's missing reference.** A player's body is held with a counted
   reference -- `P_SetTarget(&p->mo, mobj)` at spawn (`p_mobj.c:12850`),
   `P_SetTarget(&player->mo, NULL)` when let go (`g_game.c:3271`) -- but
   both loaders give it back by plain assignment: `mobj->player->mo = mobj`
   in the network load (`p_saveg.cpp:5631`), `mo->player->mo = mo` in the
   raw one (8494), and `P_CountRawReferences` does not count it. **One
   reference short per kart after every restore**, as measured (live 15,
   rebuilt 14). The network path has done this since before this branch;
   the count then reaches -1 when the body is let go -- **8.54's unexplained
   "`MT_PLAYER` references=-1" after restores.** **Remedy**: count
   `players[].mo` in both loaders.
3. **The sign's pieces.** Stock code chains them with plain assignments --
   `cur->hnext = P_SpawnMobjFromMobj(sign, ..., MT_SIGN_PIECE)`
   (`p_spec.c:4658` to `4702`) -- so the live game holds no reference for
   them, while the recount, and the network load's relink, counts one for
   each `hnext`. Harmless (a piece freed later than it could be) but a real
   difference. **Remedy**: `P_SetTarget` in those five lines, so the live
   count is what a load rebuilds.

The players block difference on Skyscraper Leaps (8.90, 3) is not explained
by any of these; 2 may be part of it, if a player field reads a count.

### 8.92 R1 measured: no wrong input of this machine's in three windows; the control loops as before

Measured on 2026-09-29, binary `decbe360a` (8.90), same session, **both
driven by Gibax**, one after the other: `keep` (R1 on, the default) then
`keepnor1` (`rollback_histreal 0`, the control). Gibax's go-ahead ("allez").
Processor at 4%. Both players entered the game
(`playlog_keep_RR_Opulence_20260929-223811_decbe36.txt`,
`playlog_keepnor1_RR_Opulence_20260929-224119_decbe36.txt`). Predictions in
8.89.

| | `keep`, R1 on: w0 / w1 / w2 | `keepnor1`, R1 off: w0 / w1 / w2 |
|---|---|---|
| passes kept, of 999 | **993 / 999 / 998** | 961 / 949 / 874 |
| rebuilt: an input differed | 4 / 0 / 0 | 35 / 50 / 125 |
| ... **this machine's wrong inputs** | **0 / 0 / 0** | 29 / 50 / 125 |
| ... server's sample older by one | -- | 29 / 49 / 124 (newer by one: 0 / 1 / 1) |
| bots' wrong inputs | 10 / 0 / 0 | 7 / 0 / 0 |
| rebuilt: a correction was due | 2 / 0 / 1 | 3 / 0 / 0 |
| samples made after more than one real tic | 5 / 1 / 2 | 33 / 51 / 126 |
| passes R1 laid out differently | 22 / 6 / 12 | -- |
| ambiguous anchors | 0 / 0 / 0 | 9 / 37 / 106 |
| pass | **7.3 / 7.8 / 8.6 ms** | 9.2 / 10.9 / 16.8 ms |
| frames drawn in 1000 tics | **3680 / 3592 / 3463** | 3536 / 3385 / 2889 |
| iterations past a tic | 7 / 1 / 3 | 39 / 52 / 128 |

- **Holds: this machine's wrong inputs ~165 to 10 or fewer -- 0 in three
  windows.** And no window in the loop: 99.4, 100 and 99.9% of passes kept,
  **a pass of 7.3 to 8.6 ms, 121 to 129 frames a second, driven, on
  Opulence, all race long.**
- **Holds: the control is 8.87 again** -- this machine's wrong inputs
  following the late samples one for one (29 and 33, 50 and 51, 125 and
  126), the server's sample older by one in 202 of 204, and the loop in
  window 2 (126 late samples, 874 passes kept, 101 frames a second).
- The control's two newer-by-one are probably the server's "one tic later"
  rule (8.87) catching up, not measured; R1 had no miss either way.
- **Late samples 8 in the whole R1 race against 210 in the control**: with
  the misses gone, the rebuilds that made the late frames are gone, and
  the loop cannot start. What starts the few left is not measured.

**A fourth difference, found reading why every driven race has one or two
`STATE` lines on this machine's kart** -- always `speed`, 0.001 to 0.088
units (8.84, 8.87 and both races here; never unattended):
`K_SameInput` compares inputs without `TICCMD_RECEIVED`, and
`P_UpdatePlayerAngle` reads it (`p_user.c:2371`): a tic the server filled by
repeating a sample, the flag cleared, is steered by the "missed a single
tic" rule; the client ran the same sample on that tic with the flag set, by
the turn solver. The two inputs compare equal, the tic is kept, and the
kart's steering differs by a hair -- which the correction channel then puts
back (a kart put back, 8.84). R1 makes it likelier, since it now replays the
repeated sample on exactly the tic the server repeats it on. **Remedy**: the
keep decision compares the flag too, and R1 clears it on the tics it gives a
sample past its first, as `SV_Maketic` does.

### 8.93 Four fixes written: the flag, the slope planes, the kart's reference, the sign

Written on 2026-09-29 (Gibax: "lance tout", then "allez"), on the local
branch `wip/fixes-0929`, **not pushed**. One commit each; syntax checked on
every changed file with no new warning, and an error injected at each changed
place is reported by the compiler.

- **`82b1a4f94`, the flag (8.92).** R1 says whether a tic is a sample's first;
  on the tics after, the speculation clears `TICCMD_RECEIVED` on this
  machine's input, as `SV_Maketic` does. The keep decision compares the flag
  for this machine's own players (not for others: a guess about another
  player is unreceived on purpose). The instruments count a `received` field.
- **`8985fd990`, the slope planes (8.91, 1).** The raw archive writes every
  dynamic slope thinker's plane (`SaveSlopePlane`) in list order and the raw
  load reads them back in the same order. `floorspriteslope` is not covered:
  only Lua creates one, and after a raw restore it may point at a freed
  plane -- open.
- **`3d853424b`, the kart's reference (8.91, 2).** Both loads count the
  reference a player holds on its body, where the object claims its player.
  This changes the network load too, which every restore and every stock
  resync uses.
- **`ae7f84961`, the sign (8.91, 3).** Its pieces are chained with
  `P_SetTarget`. Also stock code; before, removing a piece let go of `hnext`
  and `hprev` references it had never taken.

**Predictions, on a build with the four:**

- **Soaks, raw, Opulence** (`leakraw`, `wwraw`): failures back to the network
  restore's (0 to 1 in about 300); no archive difference at the start of the
  thinkers block; **no `MT_PLAYER` and no `MT_SIGN_PIECE` count rebuilt
  differently**. The players-block difference of Skyscraper Leaps (8.90, 3)
  may stay: none of the four explains it.
- **Soaks, network** (`leak`, `ww`): no "`MT_PLAYER` references go negative"
  warning.
- **`keep` on Opulence, driven**: **no `STATE` line on this machine's kart**
  in three windows (1 or 2 a window since 8.84); passes kept as in 8.92
  (99% or more); `received` in no wrong input.

⚠ Pushed on Gibax's go-ahead ("oui, pousse, installe et lance tout"), one by
one: `072542eb2`, `492118879`, `e2da72742`, `cc6ca1c0e` (the four above,
put on top of the docs); the build to install is `cc6ca1c0e`'s, CI run
36629162827.

> ⚠ 8.112: the claim of `players[].mo` in `LoadMobjThinker` never held in a
> load of the network archive: the caller's `P_AddThinker` set the count back
> to 0 right after. Every body a network load brought back stayed one short.

### 8.94 The four fixes measured: no kart off in a driven race, and raw snapshots exact on Opulence

Measured on 2026-09-29, binary `cc6ca1c0e` (sha256 `95e5ca79...`, CI run
36629162827, green on its three jobs), both copies checked, the previous exe
kept as `.bak_decbe36`. Gibax's go-ahead ("oui, pousse, installe et lance
tout"). Processor at 17%. Predictions in 8.93.

**`keep` on Opulence, driven by Gibax**
(`playlog_keep_RR_Opulence_20260929-225701_cc6ca1c.txt`; the player entered):

| | window 0 | window 1 | window 2 |
|---|---|---|---|
| passes kept, of 999 | **993** | **999** | **999** |
| rebuilt: an input differed | 6 (this machine 3, bots 8 inputs) | 0 | 0 |
| **kart samples with a state field off / karts put back** | **0 / 0** | **0 / 0** | **0 / 0** |
| drift, worst | 0.000 | 0.000 | 0.000 |
| pass | 7.1 ms | 6.9 | 7.6 |
| frames drawn in 1000 tics | 3753 | 3702 | 3637 |
| longest gap between frames | 131 ms | **22 ms** | **21 ms** |
| late samples / passes R1 laid out differently | 5 / 15 | 1 / 6 | 1 / 6 |

- **Holds: no `STATE` line on this machine's kart, in three windows** -- the
  first driven race without one since 8.84 (1 or 2 a window before). No kart
  put back all race. `received` in no wrong input.
- **Holds: 99% or more kept** -- 993, 999, 999; **127 to 131 frames a
  second**, and in windows 1 and 2 never more than 22 ms between two frames:
  not one hitch.
- This machine's 3 wrong inputs, all in window 0: two older by one (a late
  sample R1 missed), one the same to the anchor, differing in `angle` -- the
  field `D_ResetTiccmdAngle` rewrites across the history.
- **The server's side, now reported every 500 tics**: over the last 500,
  this machine's player had 500 samples filed, **129 of them a tic late
  because the slot was taken, none repeated**; 35 repeats in the whole race.
  So the server's "one tic later" rule runs a quarter of the time, and the
  replay is not misled by it while it holds steady -- only a change of it
  is. None over another.
- No `PARANOIA` line in either log.

**Soaks** (unattended, after the race):

| | leakraw, Opulence | wwraw, Opulence | leak, Carnival Night |
|---|---|---|---|
| soak failures | **0 of 278** (8.90: 19 of 280) | **0 of 379** (8.90: 23 of 377) | 2 of 261 |
| restores with the archive differing | **0 of 1668** (8.90: 1400) | **9 of 1137** (8.90: 1131) | -- |
| ... where | -- | players block, bytes 2832 and 9292 | -- |
| reference counts rebuilt differently | 6 restores: one kart, live 66, rebuilt 67 | **0** | -- |
| `PARANOIA` lines | 0 | 0 | **0** (8.54: 2 on `MT_PLAYER`) |
| raw save / verified restore | 776 / 1560 us | 857 / 1717 us | -- |

- **Holds: the slope planes** -- no archive difference at the thinkers block
  any more, and both Opulence soaks at 0 failures, better than the network
  restore's 0 and 1 (8.59).
- **Holds: the sign** -- no `MT_SIGN_PIECE` line. **Holds, mostly: the
  kart's reference** -- no kart one short any more, and no `MT_PLAYER`
  warning on Carnival Night, where 8.54 had two. But in one check of the
  leak soak (tic 3901, five restores) one kart's rebuilt count was **one
  over** live (67 against 66): probably a second object claiming the same
  player in one load, counting one reference the live game does not hold.
  `e2da72742` noted the case and left it; it may be what shows. Harmless (an object kept until
  the level ends); the remedy is to count only the claim that stays.
- **Stays, as predicted: the players-block difference** (8.90, 3), 9 of 1137
  restores on Opulence; not explained. Carnival Night's 2 failures are one
  byte each in the players block, 0x00 to 0x20: the known `itemList.cap`
  (8.54).
- **The cost**: a raw save 0.78 to 0.86 ms on Opulence, a verified restore
  1.6 to 1.7 ms with its check.

**So:** B2 is exact enough on Opulence to measure its cost in a race --
**`keepraw` can run**. Still open: the double claim, the players-block
difference, `floorspriteslope`.

### 8.95 `keepraw` driven on Opulence: the save to 1.1 ms, the pass 1.3 ms cheaper, still exact

Measured on 2026-09-29, binary `cc6ca1c0e` (8.94), **driven by Gibax**
("oui, lance keepraw, je pilote"); processor at 5%; the player entered
(`playlog_keepraw_RR_Opulence_20260929-231149_cc6ca1c.txt`). `keep` with
`rollback_rawsnap 1`: raw snapshots, not verified. Predictions in 8.88; the
same build's `keep` race (8.94) is the control, driven too, 12 minutes before.

| | `keepraw`: w0 / w1 / w2 | `keep` (8.94): w0 / w1 / w2 |
|---|---|---|
| passes kept, of 999 | 990 / 999 / 999 | 993 / 999 / 999 |
| kart samples with a state field off | **0 / 0 / 0** | 0 / 0 / 0 |
| pass | **5.8 / 5.6 / 6.4 ms** | 7.1 / 6.9 / 7.6 ms |
| ... saves a pass | **1.20 / 1.16 / 1.19 ms** | 2.84 / 2.89 / 2.89 ms |
| a save | **1.12 to 1.14 ms** (the network part 0.75 to 0.82, 26 KB; the pools 0.35 to 0.42, 4.2 MB) | 2.5 to 2.9 ms (8.84, 8.87) |
| a restore (rebuilds: 9, 1, 1) | **1.9 ms** | about 6.5 ms (8.60) |
| frames drawn in 1000 tics | **3901 / 3891 / 3794** | 3753 / 3702 / 3637 |
| longest gap between frames | 145 / **19** / 39 ms | 131 / 22 / 21 ms |

- **Holds: the save** -- 1.12 to 1.14 ms, just under 8.88's 1.2 to 1.6: the
  pools 0.35 to 0.42 ms, and what stays in the network format (players,
  world, specials, Lua, ACS, the slope planes) 0.75 to 0.82 ms for 26 KB.
- **Holds: the restore** -- 1.9 ms, inside 1.5 to 2.5, against about 6.5
  for the network one: a rebuild's restore is a third of what it was.
- **The pass is 1.3 ms cheaper** (5.6 to 6.4 against 6.9 to 7.6), all of it
  the save; **133 to 137 frames a second, driven, on Opulence**.
- **Exact in a race**: no kart state off in 6696 samples, no kart put back,
  no `PARANOIA` line; this machine's wrong inputs 4 in window 0 (three older
  by one, one newer by one), none after.
- What a kept pass costs now: a tic (about 4.2 ms) and a save (1.1): the
  tic is most of it. The next lever is the tic itself -- Opulence's
  decorations, 83% of it (8.66, 8.69) -- or making the save rarer.

**So B2 does what it was for**, on the map that needed it. `rollback_rawsnap`
stays off by default until the players-block difference (8.90, 8.94) and the
double claim are understood.

### 8.96 WORLDWIDE mode's first runs: both crash at the waiting map's restart

Run on 2026-09-29, binary `cc6ca1c0e`, Gibax's go-ahead ("oui, lance
vanillajoin puis worldwide"), unattended. Predictions in 8.80. Logs
`playlog_{vanillajoin,worldwide}_20260929-2314*_cc6ca1c.txt`.

- **Holds, as far as it got**: the server prints `worldwide: on`; the client,
  on joining, prints `worldwide: this server runs WORLDWIDE mode --
  predicting, rollback_twoclock 4, rollback_history 12, rollback_keepspec
  on, corrections applied`, and `rollback_lagcheck` reads a target of 0.
- **Both clients crashed at the same place, before either test reached its
  point** (the refusal; the race): right after the player entered the game,
  when the server restarted the waiting map (`RR_TESTRUN`), with
  `EXCEPTION_ACCESS_VIOLATION` in `G_WriteDemoExtraData` (`g_demo.cpp:550`),
  called by `P_Ticker` from a speculated tic (`K_RunSpeculatedTic` <
  `K_RollbackSpeculate` < `TryRunTics`). Just before, `PARANOIA` lines:
  two `MT_PLAYER` at `references=-1`, and a spring (`MT_BLUESPRING`, then
  `MT_REDSPRING` in the second run) at `references=-1` from
  `g_game.c:3271`.
- **What is new**: every scenario so far switched prediction on mid-race,
  after the level had started. WORLDWIDE mode switches it on at the join, so
  for the first time a speculation stands while a player enters and a level
  restarts.

**Read in the code, not tested:**

1. **A player's body pointer is left dangling by a restore.** A restore purges
   every object and loads the snapshot's; `players[i].mo` is set again only
   for a body the snapshot holds (`LoadMobjThinker`, and the raw path's
   claim). A player whose body is not in the snapshot -- one who entered the
   game after it, as this client did -- keeps pointing at the purged body,
   whose memory the next objects reuse. At the restart `G_DoReborn`
   (`g_game.c:3261` to `3271`) does `P_RemoveMobj(player->mo)` and
   `P_SetTarget(&player->mo, NULL)` on it: **it removes whatever object now
   lives there -- a spring -- and takes a reference it never had.** From
   there the world is corrupt. **Remedy**: clear `players[].mo` before a load
   brings the objects back, in both paths -- every body the purge freed is
   gone, and the loads claim the ones that exist.
2. **Speculated tics write the replay.** `P_Ticker` writes the demo's extra
   data and every player's input on every tic it runs (`p_tick.c:783` to
   `789`) when a replay is being recorded, and nothing in the prediction
   code stops it: every speculated tic has been appended to the client's
   replay since the pivot, the restores never rewinding it, and here one
   wrote into the replay across the level's restart. Whether the crash is
   this write itself or the corruption of 1 reaching it is not settled.
   **Remedy**: no replay writes from a speculated tic; with
   `rollback_keepspec` a kept tic is written when it is kept -- or a
   predicting client does not record a replay at all (a choice for Gibax).

WORLDWIDE mode cannot be tested past the join until 1 is fixed; the old
scenarios, which switch prediction on mid-race, are not affected by 1 and
carry 2 as they always have.

⚠ Both fixed the same night on Gibax's go-ahead ("oui, option A, pousse et
relance les deux"): `d847ce681` clears `players[].mo` before a load's
objects come back (network and raw); `ab126a1a7`, Gibax's option A, stops a
predicting client from writing or beginning a replay
(`K_RollbackPredicting`). Pushed one by one; the build is `ab126a1a7`'s,
CI run 36632765321. **Predictions for the rerun of both scenarios**: no
crash and no `PARANOIA` line on either machine; `vanillajoin`: the second,
undeclared join refused, the server's log saying `worldwide: refused node`;
`worldwide`: the race runs to its end with prediction on from the join,
drift 0.000, 99% of passes kept or more if nobody drives; the client's log
records no replay, the server's does.

### 8.97 WORLDWIDE mode runs end to end; a slight stutter, and where it comes from

Rerun on 2026-09-29, binary `ab126a1a7` (sha256 `57f48de7...`, CI run
36632765321, green on its three jobs; the previous exe kept as
`.bak_cc6ca1c`). **Both driven by Gibax** ("j'ai piloté", "les 2");
processor at 12%. Predictions under 8.96. Logs
`playlog_{vanillajoin,worldwide}_20260929-23*_ab126a1.txt`.

**Holds, all of it:**
- **No crash**, on either machine, in either scenario.
- `vanillajoin`: the declared join let in and switched to WORLDWIDE mode;
  the undeclared one **refused** -- the server's log: `worldwide: refused
  node 1 -- it did not declare itself WORLDWIDE`.
- `worldwide`: prediction on from the join (`rollback_twoclock 4`,
  `rollback_history 12`, `rollback_keepspec on`, corrections applied), target
  lag 0; the server `rollback_correct: not set -- WORLDWIDE mode sends a light
  correction every 4 tics`; **the race ran to its end: drift 0.000, 0 of
  10305 kart samples with a state field off, 0 karts put back**, no full-state
  resend; **no replay recorded by the client** (option A).

**Left at the join**: 1 and 3 `PARANOIA` lines on the client, all at the
moment the player enters the waiting map with prediction on --
`*Guest entered the game.` printed twice, then an `MT_PLAYER` at -1 from
`P_MobjThinker` (`p_mobj.c:10831`): the entry ran twice around a restore, and
the first body was still some object's target. None during the race, none
on the server. Not explained further.

**The stutter Gibax felt** ("ça stutter très légèrement ... ça rejoue un tic
en arrière ?"). One window for the whole session -- waiting map included --
so these are totals over 4600 passes:

- **488 rebuilds for a wrong input (11% of passes)**: this machine's 473, of
  which **469 on a tic guessed past the history**, the server's sample
  **newer by one**. The speculation ran on a tic before the sample for it was
  made -- 6.67 tics deep for 5.09 in flight -- repeated the newest, and a
  driver's next sample differed. Opulence's `keep` races (8.92, 8.94) had
  none of these: there it ran 0.7 tic past the history, here 1.6.
- **Each rebuild makes a longer frame**: 526 iterations with a pass took
  8.3 to 16.7 ms, 13 more over 16.7, against about 7 ms for the rest, at 141
  frames a second -- **a slightly longer frame about four times a second**.
- **The drawn world moved against the clock 15 times** (22 tics in all):
  the lead raised twice, lowered five times -- the "one tic back" Gibax
  describes, but rare.
- So the likelier stutter is the rebuilds on guessed tics; the jumps are
  the rarer one. Not separated by window, and the waiting map is in the
  count.

**Remedies, not written:** (a) when a new sample is made, compare it with
what the standing speculation guessed on the tic it will land on, and if
they differ rebuild from that tic only -- its start is saved -- a tic or two
instead of the whole depth, a round trip earlier; (b) keep the speculation
from running past the tic the newest sample lands on, trading guessed tics
for more jumps of the drawn world. And a `worldwide` scenario with windows,
to measure the race apart from the waiting map.

### 8.98 `rollback_keepearly`: a guessed tic run again as soon as the input for it is made

Written on 2026-09-29 on Gibax's go-ahead ("oui, écris (a) et le scénario en
fenêtres"): `5165cdd99`, on the local branch `wip/keepearly`, **not
pushed**. Syntax checked with no warning; an error injected at each of the
eight changed places is reported by the compiler.

- On a kept pass, once `NetUpdate`'s new sample is in the history
  (`K_SpeculationDepth` maps it), `K_KeepEarlyCheck` gives every standing
  tic past what the server has sent its input again, as R1 would
  (`K_RollbackLocalCmdFor`, the `TICCMD_RECEIVED` rule of 8.93 included),
  and compares it with what the tic ran (`K_KeepSameInput`). At the first
  that differs it puts that tic's saved start back, and the extension runs
  again from there: the tics from it to the head, instead of the whole depth
  at the frontier a round trip later.
- On by default; `rollback_keepearly 0` is the control. `rollback_keepspec`'s
  report counts the early reruns and the tics they ran.
- Harness (`9524619` in the notes): `playtest.sh wwwindows` --
  WORLDWIDE mode with no switch passed, every report printed bare (no reset)
  at the race's start and after each 1000 tics, so a window is the
  difference of two reports; `wwwindows_noearly` the same with
  `rollback_keepearly 0`. Server: `worldwide`'s, `rollback_relabel` every 500
  tics.

**Predictions, driven, same session, `wwwindows_noearly` then `wwwindows`:**

- The control reads as 8.97 in its race windows: rebuilds on guessed tics,
  the server's sample newer by one, 5 to 15% of passes.
- With `rollback_keepearly`: **rebuilds for this machine's wrong input a
  handful a window**; early reruns about as many as the control's rebuilds,
  **1 to 2 tics each**; iterations with a pass over 8.3 ms a fifth of the
  control's or fewer. Drift 0.000 in both.
- Gibax should feel the stutter go -- the one measure no report gives.

### 8.99 `rollback_keepearly` measured: nothing to fix in the race, and busy before it -- 8.97 misread

Measured on 2026-09-29, binary `6209f1786` (sha256 `e7344737...`, CI run
36634476627, the previous exe kept as `.bak_ab126a1`), Gibax's go-ahead
("oui, pousse, installe et lance les deux, je pilote"), **both driven**;
processor at 6%; no crash, no `PARANOIA` line on the server, 3 and 4 on the
client (at the join, as in 8.97). `wwwindows_noearly` (control) then
`wwwindows`, Skyscraper Leaps. Windows are differences of the cumulative
reports (`window_diff.py` in the scratch folder of the measuring machine).
Predictions in 8.98.

| race windows (1000 tics) | control: w0 / w1 / w2 | `rollback_keepearly`: w0 / w1 / w2 |
|---|---|---|
| passes kept | 999 / 1000 / 1000 | 994 / 987 / 1000 |
| rebuilt: an input differed | 1 / 0 / 0 | 6 / 13 / 0 |
| early reruns (tics) | -- | 5 (22) / 6 (37) / 0 |
| pass | 2.31 / 2.36 / 2.46 ms | 2.35 / 2.48 / 2.04 ms |
| frames drawn | 4106 / 4106 / 4105 | 4094 / 4086 / 4111 |
| iterations with a pass over 8.3 ms | 9 / 8 / 5 | 21 / 22 / 3 |
| gaps between frames of 8.3 to 16.7 ms | 959 / 987 / 1006 | 968 / 948 / 967 |
| drawn world moved against the clock | 2 / 7 / 0 | 2 / 4 / 0 |
| state fields off, karts put back | 0, 0 | 0, 0 |

Before the race (the join and the waiting map, 1599 passes): the control
rebuilt 199 times for a wrong input; with `rollback_keepearly`, 193 -- and
**1339 early reruns, 2793 tics**.

- **8.98's predictions fail, and so does 8.97's reading.** The control's race
  windows had 1, 0 and 0 rebuilds: **the 488 rebuilds of 8.97 were before the
  race**, in the join and the waiting map, not in the driving. There was
  nothing in the race for the early rerun to remove; it reran 5 and 6 times
  there, 4 to 6 tics each, and before the race it fired on most passes to no
  effect.
- **Gibax's feel, the same in both**: "pas trop senti une différence ... le
  kart avait toujours l'impression de rollback très très légèrement ... c'est
  jouable, pas un souci, mais ça se remarque". Maybe the interpolation, he
  suggests.
- **What the numbers show in the race, in both**: about **one gap in four
  between frames lasts 8.3 to 16.7 ms**, against under 8.3 for the rest --
  about 1000 a window, **one a tic**: the frame that carries the pass. The
  drawn world moving against the clock is rare (0 to 7 a window). No rebuild,
  no correction moving a kart.
- **So `rollback_keepearly` goes off by default**, to be read again when a
  rebuild in a race is what hurts. What Gibax feels is not a rebuild: the
  leads are the frame pacing (one longer frame a tic) and the interpolation
  -- what the view and the objects are interpolated from on a kept pass.

> ⚠ 8.102: the stutter was none of the leads above -- nothing was
> interpolated while a speculation was kept; fixed and measured in 8.103.

### 8.100 An instrument for what is drawn, and `rollback_keepearly` off by default

Written on 2026-09-29 before the night's stop (Gibax: "écris l'instrument et
désactive (a), et on reprend demain"), on the local branch `wip/drawn`,
**not pushed**. Syntax checked with no warning; an error injected at each of
the six changed places is reported by the compiler.

- **`c5d2eb0b8`**: `rollback_frames`' report now says, for the frames drawn
  while the local kart moves (over 2 units a tic), how the kart's **drawn**
  position (`R_InterpolateMobjState` at `rendertimefrac`) and the view's
  (`viewx`, `viewy`) stepped from the frame before, against the kart's speed
  and the time between the two frames: even, short (under half), long (over
  one and a half) or **backwards** -- split by whether the frame carried a
  pass. It counts with prediction on or off, so a race without prediction
  is its control.
- **`e0848fc7e`**: `rollback_keepearly` off by default (8.99); `1` turns it
  on.

**Predictions, for a driven `wwwindows` race and a driven race without
prediction, same session:** with prediction, the kart's irregular steps
(short, long, backwards) are **at least twice as frequent** as without, and
mostly in frames that carried a pass; backwards steps rare (under 1% of
frames) but not zero. If the view is regular where the kart is not, the
kart's interpolation is what Gibax sees; if both are irregular together, it
is the frame pacing.

⚠ Pushed on Gibax's go-ahead ("oui pousse"), one by one: `e5fb1c61a` (the
instrument) and `771bec680` (`rollback_keepearly` off) -- the two above,
put on top of the docs; the build to install is `771bec680`'s, CI run
36637283049. Not installed, not run. With them, the root README now shows
Gibax's Ring Racers Worldwide logo (`docs/RRW_logo.png`, `97eb69218`).

### 8.101 Where in a level the passes fall: an instrument

Written on 2026-09-30 on Gibax's go-ahead ("allez écris donc"), on the local
branch `wip/phases`, **not pushed**: `78ca2f8a3`. No compiler on this
machine: read line by line, not syntax-checked -- the CI will say.

**Gibax's question.** Prediction runs as soon as it is switched on --
could it stop at the end of a race (results, table) and start again when
POSITION begins, and would that spare the ~200 rebuilds before the race
(8.99)? Read in the code:

- **Already so outside a level**: `K_RollbackTwoClock` returns 0 when
  `gamestate != GS_LEVEL`, so the results, the table and the vote are never
  predicted.
- **Predicted without use, inside a level**: the title card's fly-in
  (`leveltime < introtime`; 108 + 5 tics with more than two players,
  `k_kart.c:367`), where the karts do not move, and the stretch between this
  machine's finish and the results, where whether its input still steers
  the kart is not read yet.
- **POSITION has to stay predicted**: karts drive in it (POSITION areas, a
  fault for crossing the line early, `k_kart.c:11330`); without prediction
  it would have the input lag back, and a jump at GO.
- **Stopping costs a jump each way**: off, the picture falls back to the
  confirmed world, a round trip behind; on again, it jumps forward by as
  much with a whole rebuild -- in the intro, a jump of the flying camera,
  unless it is done in the white fade that ends it (5 tics). And it is one
  more transition, where 8.96 and 8.97 found their bugs.
- **Where the rebuilds fall is not known**: the harness's "before the race"
  mixes the join, the waiting map `RR_TESTRUN` -- a level, where the kart
  drives -- and the race map's intro and POSITION. The `PARANOIA` lines are
  at the join (8.97).

**The instrument.** Every pass is filed under the level's phase at the
frontier it starts from -- `join` (this machine's player not in the game, or
spectating), `intro` (before `introtime`), `POSITION` (before `starttime`),
`race`, `finished` (exiting) -- and the phase only moves forward within a
level. Per phase: passes, kept, rebuilt for this machine's input, for
another's, for a correction, otherwise (a player coming or going, a
netxcmd, anything else). A `rollback_phases: <map> -- <phase> from leveltime
N, tic T` line when the phase changes, so the `PARANOIA` lines and the rest
of the log can be placed; a level's table (`ended`) when the next level or
a restart begins; the current level's (`so far`) at the end of
`rollback_keepspec`'s report, which `wwwindows` prints at every window.
Nothing prints while nothing is predicted: `frames_off` has none.

**Predictions, for a driven `wwwindows` race on a build with it:**

- **More than half of the rebuilds before the race fall in `join` and on
  the waiting map**; the race map's `intro` and `POSITION` together under
  20.
- In the race map's `intro`, this machine's input rebuilds next to nothing
  (its guess, the last input repeated, is right while nothing is pressed).
- The client's `PARANOIA` lines fall at the end of `join` on the waiting
  map.
- The race map's `race` phase reads like 8.99's windows: 0 or 1 rebuild in
  1000 tics.

If the first holds, stopping prediction in the intro gains little and the
join is where to look (*Next, in order*, item 2); if the intro holds most of
them, stopping there -- and starting again in its white fade -- is the fix.

⚠ Pushed the same day on Gibax's go-ahead ("pousse donc"), put on top of the
docs as `b1c0c7444`; CI run 36683749563. The build to install for the
measuring session is that one: the same `wwwindows` race reads 8.100's
drawn steps and this section's phases.

### 8.102 What is drawn: no interpolation at all while a speculation is kept -- found, fixed, not pushed

Measured on 2026-09-30, binary `b1c0c7444` (sha256 `a5c4b744...`, CI run
36683749563, the previous exe kept as `.bak_6209f17`), Gibax's go-ahead
("allez tu peux lancer"), **both driven**, same session, Skyscraper Leaps:
`wwwindows` (WORLDWIDE mode) then `frames_off` (no prediction). Processor at
17%; no crash. Predictions in 8.100 and 8.101.

**The drawn steps** (race windows; `wwwindows` by differences of its
cumulative reports):

| per 1000 tics | `wwwindows`, frames without a pass: even / short / long / back | with a pass | `frames_off`, without a pass | with a pass |
|---|---|---|---|---|
| kart, w0 | 5 / 3096 / 5 / 3 | 4 / 14 / 981 / 1 | 2877 / 47 / 186 / 2 | 977 / 17 / 3 / 2 |
| kart, w1 | 0 / 3070 / 0 / 0 | 0 / 10 / 977 / 0 | 2793 / 39 / 263 / 2 | 976 / 11 / 5 / 2 |
| kart, w2 | 0 / 3112 / 0 / 0 | 0 / 1 / 999 / 0 | 2821 / 28 / 263 / 0 | 986 / 9 / 4 / 0 |
| view | the same as the kart | the same | 2624 to 2707 even | 972 to 980 even |

- **With prediction, the kart and the view as drawn do not move between
  passes and jump a whole tic on the frame with one**: about 3100 "short"
  (still) steps and 980 "long" ones a window, 0 even. Without prediction,
  about 3800 even steps a window. **The drawn world moves at 35 Hz in steps
  on a 140-frame screen: the slight "rollback" Gibax sees.** 8.100's
  prediction holds and more (irregular steps not twice but twenty-odd times
  as many, the view with the kart: both "irregular together"), but its
  reading -- frame pacing -- is wrong: nothing is interpolated at all.
- **Why, read in the code**: `TryRunTics` marks the game stopped
  (`hu_stopped = true`) whenever its loop ran no confirmed tic, and the next
  frame is drawn with `timeisprogressing` false, so `rendertimefrac =
  FRACUNIT` (`d_main.cpp`): no interpolation. A kept pass runs no confirmed
  tic -- the speculation's are kept -- so **every kept pass was marked
  stopped**, and with `rollback_keepspec` nearly every pass is kept. The
  music's resync, which also reads `hu_stopped`, stood still too.
- **Fix, `9f3f8a1c1`** (local branch `wip/hustopped`, not pushed; syntax
  checked, errors injected at its four places reported): when the
  speculation ran at least one tic this pass (`K_RollbackSpeculatedLastPass`),
  `hu_stopped` goes back to false -- what is drawn moved on.
  **Prediction**: on a build with it, `wwwindows` draws the kart as
  `frames_off` does -- even steps the rule, "short" and "long" a few percent.

**Where the rebuilds fall** (`rollback_phases`, the client of `wwwindows`):

| map, phase (leveltime) | passes | kept | rebuilt: this machine's input / another's / other |
|---|---|---|---|
| RR_TESTRUN, join (575 to 788) | 213 | 209 | 2 / 0 / 2 |
| RR_TESTRUN, race (789 to 893) | 105 | 70 | 35 / 0 / 0 |
| RR_TESTRUN restarted, race (0 to 504) | 505 | 334 | 169 / 0 / 2 |
| RR_SKYSCRAPERLEAPS, race (0 to 776) | 777 | 512 | **253** / 10 / 2 |
| RR_SKYSCRAPERLEAPS, race (776 to 3770) | 3000 | 2996 | **4** / 0 / 0 |

- **8.101's first prediction fails**: the join rebuilt 2 times, the waiting
  map's play 204, and **the race map's first 777 tics 253** -- the intro,
  `POSITION` and the start -- against 4 in the 3000 after. The rebuilds are
  at the start of each level, not at the join.
- The instrument did not split `intro` and `POSITION` from `race` here:
  every level read as `race` from leveltime 0. Not examined.
- The client's `PARANOIA` line is at the join, as predicted.
- So 8.101's alternative stands: most of the pre-race rebuilds are at a
  level's start, where stopping prediction until the start (and starting it
  again at the white fade) would remove them -- once the phases are told
  apart.

### 8.103 The fix measured: the kart drawn in even steps, as without prediction

Pushed on Gibax's go-ahead ("allez") as `020b1d653` (`9f3f8a1c1` put on top of
the docs), CI run 36710607479 green; installed (sha256 `43e94680...`, the
previous exe kept as `.bak_b1c0c74`). `wwwindows`, **driven by Gibax**;
processor at 11%; no crash. Gibax finished the race inside the session (the
harness saw the next map), so window 2 may end after the finish line.
Prediction in 8.102.

| kart as drawn, per window | without a pass: even / short / long / back | with a pass | 8.102, same build before the fix | `frames_off` (8.102) |
|---|---|---|---|---|
| w1 | 2646 / 31 / 430 / 0 | 989 / 11 / 0 / 0 | 0 / 3070 / 0 / 0 and 0 / 10 / 977 / 0 | 2793 / 39 / 263 / 2 and 976 / 11 / 5 / 2 |
| w2 | 2561 / 31 / 507 / 3 | 979 / 13 / 4 / 1 | 0 / 3112 / 0 / 0 and 0 / 1 / 999 / 0 | 2821 / 28 / 263 / 0 and 986 / 9 / 4 / 0 |

- **Holds: the kart is drawn as without prediction** -- even steps the rule
  (about 3600 a window), short ones 1%, backwards 0 to 4; the view the same.
  The steps between passes were held still before and move now: the
  interpolation runs again.
- **Long steps a little more common than without prediction**: 430 to 507
  against about 263 a window. Not examined; a candidate is the pass's own
  frame pacing, since a long step is one frame covering more time than
  expected.
- Windows 1 and 2: every pass kept, no rebuild, a pass of 2.0 to 2.1 ms,
  4107 and 4111 frames. Window 0 held the race's start: 308 rebuilds, 689
  of 1000 kept -- 8.102's finding again (the rebuilds are at a level's
  start).

**So what Gibax saw as a slight rollback was no interpolation at all while
a speculation was kept**; it is gone. Left: the rebuilds at a level's start,
and the few extra long steps.

Gibax's feel of this race: "ouais c'est largement plus fluide".

### 8.104 The rebuilds at a level's start: no intro and no POSITION on the bench, and a second human at the keyboard

Read on 2026-09-30 from the logs of 8.102 and 8.103 and the code; nothing
launched.

- **The harness's races have no intro and no `POSITION`**: `K_TimerInit`
  sets `starttime = introtime = 0` in Free Play (`k_kart.c:384` to `388`,
  `M_NotFreePlay() == false`), and every level of 8.102 and 8.103 read as
  `race` from leveltime 0. So `rollback_phases` could not split them, and
  8.101's remedy -- stopping prediction until the start -- has nothing to
  act on in these races; it is for a real race's start.
- **Whose input the start's rebuilds were changes from race to race**: in
  8.102's, **this machine's** (253 in the race map's first 777 tics, 10
  another's); in 8.103's, **another human's** (298 in the first 718 tics,
  1 this machine's -- 887 wrong inputs of "people" in window 0). The only
  other human is the server's host, whose window opens 14 seconds before the
  client's, on the same machine: **keys pressed while the server window has
  the focus drive the host**, whose inputs the client can only repeat. Not
  checked with Gibax yet.
- After the first 700 to 800 tics, both races rebuilt next to nothing (4 and
  0 in the next 3000 tics).
- **Next, when the week's usage allows**: ask Gibax where the focus was at
  the start; run the start again with the server dedicated
  (`playtest.sh wwwindows dedicated`: no host player), so any rebuild left
  at the start is this machine's own -- and then read it tic by tic.

**Prediction, for `playtest.sh wwwindows dedicated` driven** (no host player,
no server window): no rebuild for another human's input at all; this
machine's at the race map's start **under 20** in its first 800 tics if
8.102's 253 came from the same stray keyboard, **over 100** if they are this
machine's own; 0 to 5 in the race after.

> ⚠ 8.105: with no host player the start's rebuilds stayed (265):
> this machine's own, from predicting on a loopback under the depth floor.

### 8.105 The level-start rebuilds: prediction on a loopback, and `rollback_twoclock` as a floor

Measured on 2026-09-30, binary `020b1d653` (8.103), Gibax's go-ahead ("oui"),
**driven**: `playtest.sh wwwindows dedicated` -- no host player, no server
window. Processor at 17%; no crash
(`playlog_wwwindows_20260930-182229_020b1d6.txt`). Prediction under 8.104.

- **Another human's input: 0 rebuilds**, as predicted. **This machine's at
  the race map's start: 265 in its first 809 tics**, then 5 in the 3000
  after -- "over 100": the start's rebuilds are this machine's own, not
  (only) the stray keyboard of 8.103.
- Of this machine's 455 wrong inputs before the race windows, **449 were run
  on the newest input guessed past the history** (`latency` in all, `angle`
  in 269, `turning` in 86).
- **Why, read in the logs and the scenario**: `rollback_history` found **1.04
  inputs in flight on average** until the race map's tic 809, against about
  7 afterwards; the speculation still ran **4.19 tics deep** --
  `rollback_twoclock 4` is the depth's floor (8.39). And **the client's
  scenario sets `rollback_lag 6` only after `wait 1600`**, while WORLDWIDE
  mode switches prediction on at the join: for the join, the waiting map and
  the race map's first 800 tics the client predicted **on a loopback**, a
  tic of round trip, so 3 of the 4 speculated tics repeated the newest input
  -- wrong at every change of a driver's input. The rebuilds stop exactly
  where the lag starts (tic 809, the report named `start`).
- The same holds for 8.97's and 8.99's pre-race rebuilds, and in part for
  8.102's and 8.103's starts: every WORLDWIDE scenario had it; the older
  ones switched prediction and lag on together.
- **Not only the bench's**: on a real connection with a round trip under
  `rollback_twoclock` tics -- a LAN, a near server -- the floor makes the
  speculation guess this machine's own input the same way, and keeping it
  rebuilds at every change.

**Remedies:**
1. Harness: `rollback_lag` first in the WORLDWIDE scenarios, so the whole
   session has its latency (a harness change).
2. Code: with `rollback_history`, let the depth be what the inputs in flight
   cover (plus the held lead) instead of never less than
   `rollback_twoclock` -- the floor then applies only without the history.
   A `src/` change; what the drawn world does on a near server would change
   with it: less ahead, as far ahead as the round trip.

### 8.106 No floor under the history's depth, the latency from the join, and a latency sweep

Written on 2026-09-30 on Gibax's go-ahead ("on fait dans l'ordre aller"),
after his two questions: why is it smoother with 171 ms than without, and was
any other latency ever tried? (8.105 answers the first; the second: **no --
all 33 client scenarios had `rollback_lag 6`**, and the WORLDWIDE ones ran
their first 1600 tics on a loopback by accident.)

1. **`737455184`** (local branch `wip/nofloor`, not pushed; syntax checked,
   an injected error reported): with `rollback_history` the depth is what the
   inputs in flight reach, one tic at least -- `rollback_twoclock` is no
   longer its floor, and stays the depth without the history.
2. **Harness (`700bbed`)**: `rollback_lag` first in the WORLDWIDE client
   scenarios (`worldwide`, `wwwindows`, `wwwindows_noearly`), and
   `playtest.sh <scenario> lag=<tics>` for any scenario -- a generated copy
   with every `rollback_lag` line swapped, the logs named `_lag<tics>`.
3. **The sweep, on a build with 1**: `playtest.sh wwwindows dedicated
   lag=N` for N = 0, 3, 6, 10, 15 (0, 86, 171, 286, 429 ms), driven, same
   session -- dedicated, so no host player takes a stray key.

**Predictions:**
- At 0, 3, 6 and 10 tics: **this machine's wrong inputs a handful a
  window**, none guessed past the history, the depth close to the round
  trip (about 1, 4, 8, 11 tics); no rebuild at the race map's start any more.
- At 15 tics: the round trip is past `rollback_history 12`'s cap, so the
  speculation stops short of the newest input's tic -- **the drawn world
  behind the input again** (the cap cuts every pass short) but no guessing,
  hence few rebuilds; Gibax should feel the input late by about 3 tics.
- The pass's cost grows with the depth only on rebuilds; kept passes stay at
  one tic and one save (about 2 ms on Skyscraper Leaps).

> ⚠ 8.107: at 15 tics the drawn world is about 5 tics behind, not 3 --
> the round trip there is 17.

### 8.107 The latency sweep: clean from 0 to 428 ms, and the history's cap at 15 tics

Pushed on Gibax's go-ahead ("oui, pousse, installe et lance les cinq, je
pilote") as `da5922575` (`737455184` put on top of the docs), CI run
36744374272 green; installed (sha256 `493b4700...`, the previous exe kept as
`.bak_020b1d6`). `playtest.sh wwwindows dedicated lag=N`, N = 0, 3, 6, 10,
15, **all five driven by Gibax**, one after the other; processor at 13%; no
crash; each log confirms its latency (`rollback_lag: holding every peer
packet for N tics`). Predictions in 8.106.

| latency | depth | inputs in flight | cut by the cap | kept, race windows | rebuilt at the race map's start (first ~800 tics) | pass | frames in 1000 tics |
|---|---|---|---|---|---|---|---|
| 0 (0 ms) | 1.97 to 2.02 | 1.54 to 1.89 | 0 | 1000 / 1000 / 1000 | **1** | 1.98 to 2.23 ms | 4110 to 4114 |
| 3 (85 ms) | 4.97 to 5.02 | 4.33 to 4.80 | 0 | 1000 / 1000 / 1000 | **2** | 1.88 to 2.46 | 4108 to 4111 |
| 6 (171 ms) | 7.99 to 8.06 | 7.45 to 7.95 | 0 to 6 | 1000 / 1000 / 999 | **4** | 2.10 to 2.22 | 4108 to 4113 |
| 10 (285 ms) | 11.96 to 12.01 | 11.40 to 11.94 | 0 | 1000 / 1000 / 1000 | **7** | 2.06 to 2.25 | 4111 to 4112 |
| 15 (428 ms) | 11.99 to 12.01 | 16.49 to 17.20 | **every pass** | 1000 / 1000 / 994 | **9** | 1.94 to 2.27 | 4081 to 4113 |

No kart state off and no kart put back in any window; the kart drawn in
even steps at every latency (2350 to 2610 even, 3 to 32 short, 466 to 757
long, 0 to 4 backwards a window, the frames without a pass).

- **Holds: from 0 to 10 tics, the depth follows the round trip** (about 2,
  5, 8, 12 for 1.5, 4.5, 7.7, 11.6 inputs in flight) and **nothing is
  guessed**: 0 wrong inputs of this machine's in 12 of 12 race windows but
  one (1, guessed, at 6 tics). **The start's rebuilds are gone**: 1 to 7 in
  the race map's first 800 tics, against 265 in 8.105 -- no floor, and the
  latency from the join.
- **At 15 tics the cap cuts every pass**: 12 tics deep for about 17 in
  flight, so **the drawn world is about 5 tics behind the newest input**
  (8.106 said about 3: the round trip at 15 tics of lag is 17, not 15). No
  guessing, few rebuilds (6 in the last window), but 49 jumps of the drawn
  world in that window. The latency a driver feels there is the cap's, not
  the network's alone.
- **The pass costs the same at every latency**: one tic and one save, about
  2 ms, 144 frames a second. A kept speculation's cost does not grow with
  the round trip; only a rebuild's would.

**So:** with the history's depth free of the floor, prediction is clean from
a loopback to 285 ms. Past about 340 ms, `rollback_history 12` is what
limits it; raising the cap (up to 34, 8.39) or setting it from the round
trip would carry it further, at the cost of deeper rebuilds when one comes.

Gibax's feel of the sweep: "au niveau du feeling c'était... parfait. En fait
j'ai senti aucun lag, j'ai même soupçonné qu'il y avait un problème" -- no lag
felt at any of the five latencies, the 428 ms one included.

### 8.108 Muted menus and a join printed 17 times: the "already heard" rule applied outside a tic

Reported by Gibax on 2026-09-30 after the sweep: the title sound and the
menus' sounds muted; the join message printed over and over. Read in the
logs and the code; nothing launched.

- **17 "`*Guest entered the game.`" for one join** in the 15-tic race's
  client log (`latest-log.txt`; "has joined" once). The line is written by
  a tic (`P_PlayerThink`, `p_user.c:3856`, `HU_AddChatText`), and every run
  of that tic -- the speculation's, its reruns, the confirmed run -- wrote
  it again. Sounds have had a rule since 8.73 (heard the first time this
  machine runs a tic); chat lines had none.
- **The muted sounds**: `K_RollbackSoundsSilenced` held a sound back when
  `gametic` was behind the sound horizon -- a tic already heard. But a kept
  pass hands the netcode the **frontier's** clock (`K_RollbackKeepArm`),
  and `NetUpdate` processes the menus' input then: `gametic` is the
  frontier's, behind the horizon, and every menu sound was taken for a
  rerun's. A map load run by the tic loop's netxcmds is in the same place.
  (If the title screen outside any level is muted too, this does not
  explain it -- to ask.)
- **Fix, `0e7382258`** (local branch `wip/echo`, not pushed; syntax checked,
  errors injected at its five places reported): the tic loop and the
  speculation say when `G_Ticker` runs a tic (`K_RollbackTicRunning`), and
  the horizon applies only then; `HU_AddChatText` follows the same rule.
  Chat from netxcmds runs before `G_Ticker` in the tic loop and is never
  held back.
- **Prediction**: on a build with it, one "entered the game" a join; the
  pause menu's and the title card's sounds heard in WORLDWIDE mode.

> ⚠ 8.109: pushed as `af4104553`, the sounds came back; the join line did
> not drop to one (3 on the next race). And the 17 was the 15-tic race
> only: the sweep's other races wrote it 1 to 4 times.

### 8.109 The fix on a race: the sounds back, the join line still three times

`af4104553` (the fix of 8.108, cherry-picked from `wip/echo`), installed
in both folders (sha256 `b724ec90…`, the previous exe kept as
`.bak_da59225`). `playtest.sh wwwindows dedicated`, RR_SkyscraperLeaps,
`rollback_lag 6` (171 ms), driven by Gibax, 2026-09-30 19:02
(`playlog_wwwindows_20260930-190215_af41045.txt`). No crash.

- **Sounds: prediction met.** Gibax, during the race: "LES SONS sont fix".
  The menus and the title are heard in WORLDWIDE mode.
- **Join line: prediction missed.** "has joined the game" once, but
  "`*Guest entered the game.`" 3 times on the client (twice in the join
  phase, once in POSITION, all on RR_TESTRUN before the race map). The
  server wrote it once.
- **The earlier count, redone over every kept log.** The 17 of 8.108 was
  the 15-tic race. The rest of the sweep (`da5922575`) wrote it 2, 4, 1
  and 4 times at 0, 3, 6 and 10 tics. The two `020b1d6` races wrote 5
  and 2, `b1c0c74`'s wwwindows 2. `frames_off` (no prediction) and
  `vanillajoin` wrote it once. At 6 tics, 3 is inside what the builds
  without the fix wrote (1, 2, 5, 2): **the fix has not been shown to change
  the join line below 15 tics**, and at 15 tics it has not been measured.
- **What the 3 mean.** The rule holds back a line only on a tic this
  machine has already run. `G_Ticker` has no early return, and the horizon
  moves at its end on every tic (`K_RollbackTicker`). So each copy was
  written on a tic past the horizon: a tic this machine had never run, in
  a world where the player had not yet entered. The join depends on an
  input. A spectator's `BT_ATTACK` toggles `PF_WANTSTOJOIN`
  (`p_user.c:4625`), and `K_CheckSpectateStatus` lets the player in during
  that same tic (`k_kart.c:16948`). A world where this machine's input
  landed on another tic joins on another tic. **Hypothesis, not measured.**
- **PARANOIA:** 2 `MT_PLAYER` mobjs go to `references=-1` (4 lines), as in
  every WORLDWIDE race since `6209f17` (3 to 4 lines). `frames_off` and
  `vanillajoin` have one mobj (2 lines), and the 15-tic race had 64 lines.
  This is the join-time alert already open, not something the fix brought.
- **The race, three windows of 1000 passes.**
  - 1000/1000 kept in each window, 0 rebuilt, 0 wrong inputs.
  - About 8.0 tics deep, 8.0 inputs in flight.
  - 2.05, 2.04 and 2.22 ms a pass, and about 4110 frames a window.
  - No gap between two drawn frames over 16.7 ms.
  - State off 0/2000, nothing put back.
  - The drawn kart without a pass: about 620 long steps a window,
    against 2330 to 2470 even. This is the long-step item already open.
- **Instrument, `d84878e97`** (local branch `wip/chat`, not pushed; syntax
  checked, errors injected at its three places reported).
  - `HU_AddChatText` asks `K_RollbackChatSilenced`: the same rule, which
    also counts the lines it holds back.
  - In two-clock mode, each line a tic writes prints `rollback_chat:` with:
    - the tic and its leveltime;
    - speculated or confirmed;
    - the gamestate;
    - the horizon, the frontier and the standing speculation's head;
    - the number held back so far.
- **Prediction for the instrument:**
  - The extra lines come from speculated passes.
  - Each one is on a tic at or past the horizon, and later than the one
    before.
  - A rebuild for this machine's input comes between two of them (the
    `rollback_phases` counts).
  - Two or more lines from confirmed passes would rule the hypothesis out.
    So would a line behind the horizon, which can only print when the
    WORLDWIDE branch of the rule is off.

> ⚠ 8.110: the next race wrote the join line once; the hypothesis above
> is still untested.

### 8.110 `rollback_chat` on a race: one line, on its first run; 28 PARANOIA lines at the join

`f13231534` (8.109's instrument), installed in both folders (sha256
`a5191154…`, the previous exe kept as `.bak_af41045`). `playtest.sh
wwwindows dedicated`, RR_SkyscraperLeaps, `rollback_lag 6`, driven by Gibax,
2026-09-30 19:17 (`playlog_wwwindows_20260930-191739_f132315.txt`). No
crash.

- **The join line: once.** The client wrote "`*Guest entered the game.`"
  once, as did the server. One `rollback_chat` line:
  - on tic 205 (leveltime 204), in a speculated pass;
  - the horizon at 205, the frontier at 197, the standing speculation's
    head at 205;
  - 0 lines held back so far.

  That is the join's first run, 8 tics above the frontier, in a
  speculation that was then kept. No rerun of that tic wrote it again.
- **So 8.109's hypothesis is neither confirmed nor refuted:** this race had
  no extra copy to read. Across races the count is 1 to 5 at 0 to 10 tics,
  and 17 at 15 tics. The instrument stays; it prints only when a tic writes
  a line in two-clock mode.
- **PARANOIA: 28 lines, 9 distinct `MT_PLAYER` mobjs** at `references=-1`
  (`P_SetTarget` then `P_RemoveThinkerDelayed`). All of them fall between
  the join and the first "Speeding off to level" (RR_TESTRUN, join and
  POSITION), and none in the race. It is the join-time alert already open,
  but with 9 mobjs where 1 or 2 is usual (the 15-tic race had 18, in 64
  lines).
  - Rebuilds for this machine's input: 3 in the join phase, 10 in
    POSITION, plus 3 otherwise.
  - 8.109's race had 2 and 7, plus 3 otherwise, with 2 mobjs.

  The count does not follow the rebuilds in any simple way.
- **The race.**

  | window | kept | rebuilt | frame gaps over 16.7 ms | jumps | pass |
  |---|---|---|---|---|---|
  | 0 | 998/1000 | 2, for this machine's input (1 replayed, 1 guessed) | 3 (one 16.7-28.6, two 33.3-50) | 7 (10 tics) | 2.29 ms |
  | 1 | 1000/1000 | 0 | 1 (33.3-50) | 0 | 2.23 ms |
  | 2 | 1000/1000 | 0 | 0 | 0 | 2.36 ms |

  - About 4100 frames a window, 8.0 tics deep.
  - State off 0 in 1976, 2000 and 2000 kart samples; nothing put back.
  - The drawn kart without a pass: 625 to 671 long steps a window, as
    before.

### 8.111 The `MT_PLAYER` alerts at the join: read in the code, and an instrument

Read on 2026-09-30, evening; nothing launched. The first item of
`ROADMAP.md`, *Next, in order*.

- **What the alert says.** `PARANOIA/P_SetTarget: … MT_PLAYER
  P_RemoveThinkerDelayed references=-1 (p_mobj.c:10831)`, then the same
  body from `P_RemoveThinkerDelayed`. Line 10831 is `P_MobjThinker` letting
  go of its `target` because that target was removed. So an object still
  pointed at a removed kart body whose count was already 0: **one
  reference was let go of without ever having been counted**.
- **Why no crash so far.** `P_RemoveThinkerDelayed` frees a thinker only at
  exactly 0, so a body at -1 is never freed; it leaks until the next load
  purges it. In the other order, the count reaches 0 first while a holder
  still points at the body. The body is then freed, and the holder reads
  freed memory. That is the use after free the audit feared.
- **A candidate, from the code.**
  - `g_tm.thing`, `g_tm.floorthing` and `g_tm.hitthing` hold counted
    references (`P_SetTarget`). `P_MapEnd` lets go of `thing` after every
    tic, **but not of `floorthing` or `hitthing`**. `floorthing` is the
    object whose top gave the last position check its floor, `hitthing` the
    same object when it blocked the move. Both are let go of at the start of
    every position check and every `P_MobjThinker`
    (`p_map.c:2361-2362`, `p_mobj.c:10907-10908`), so between tics they
    hold whatever the tic's last check left there.
  - A load of the network archive frees every object and brings the saved
    ones back through the level pools, likely at the same addresses. It
    does not touch those pointers.
  - A pointer left holding a freed object would then, at its next change
    -- the first thinker run after the load -- let go of whatever lives at
    that address: a reference it never held. If that is a kart's body, the
    body goes to -1 when it is removed and its holders let it go.
  - Every rebuild of the speculation is such a load, and the grid's start
    (join, POSITION) is where the rebuilds are. But
    `frames_off` and `vanillajoin`, which predict nothing, still had one
    body at -1. The join's own load (`CL_LoadReceivedSavegame`) is a load
    too. **A hypothesis: nothing measured.**
- **Instrument, `2ec71c310`** (local branch `wip/refs`, not pushed; syntax
  checked with and without `PARANOIA`, 20 injected errors reported where
  expected).
  - **Around every `P_LoadNetGame`** (restores, the join's, a resend's):
    what the three collision pointers hold before the load. After it, when
    one still holds the same address and a live object sits there, one
    line: `rollback_refs: the load of tic T left g_tm.<name> holding a
    <type> (player p) it freed; a <type> (player q) lives at that address
    now, with N references`.
  - **In a `PARANOIA` build**, a ledger per kart body, by the file and
    line `P_SetTarget` passes:
    - every change of its count from the first time it is seen, the
      loader's claims of `players[].mo` included;
    - the tic and what was running (confirmed tic, speculated tic, replay,
      load);
    - its removal.

    A body going below zero prints its ledger and what still points at it.
  - The `rollback_keepspec` report ends with the counts: loads, how often
    each collision pointer held an object and a live one sat at its address
    after, and bodies below zero. 12 prints of each kind at most.
- **Prediction, for `playtest.sh wwwindows dedicated` driven, and the
  `frames_off` control.** The alerts as before: 1 to 18 bodies in the
  WORLDWIDE race, 1 or 2 in the control.
  - **If the hypothesis holds**, three things together:
    - a `rollback_refs: the load of tic …` line before each body's alert,
      naming `floorthing` or `hitthing`, with a kart at the address after;
    - in that body's ledger, a `-1` at a line that lets go of
      `g_tm.floorthing` or `g_tm.hitthing` (`p_mobj.c:10907-10908`,
      `p_map.c:2361-2362`), on the first tic after the load, with no `+1`
      there since the body was first seen in it;
    - the one letting go of it at `p_mobj.c:10831` listed among what
      still points at it.
  - **If not**, the ledger names the site that let go without having
    taken, or the claim that is missing, and the hypothesis is dropped.
  - Nothing else changes: the instrument only counts and prints.

> ⚠ 8.112: the collision pointers held nothing at any of 37 loads -- the
> hypothesis is wrong. The ledgers named the cause: the load's own claim of
> the body, wiped by `P_AddThinker`.

### 8.112 The `MT_PLAYER` alerts explained: the load's claim of a body, wiped by `P_AddThinker`

`04db0cf71` (8.111's instrument), pushed on Gibax's go-ahead ("oui, pousse,
installe et lance les deux, je pilote"), CI run 36765959959 green, installed
in both folders (sha256 `65be9958…`, the previous exe kept as
`.bak_968dc20`). Processor at 6%. Both runs **driven by Gibax**, 6 tics of
lag, dedicated (no host player), no crash:
`playtest.sh wwwindows dedicated` at 21:34
(`playlog_wwwindows_20260930-213648_04db0cf.txt`), then the control
`playtest.sh frames_off dedicated`, with no prediction, at 21:37
(`playlog_frames_off_20260930-213949_04db0cf.txt`). Prediction in 8.111.

- **The collision pointers: the hypothesis is wrong.** In the WORLDWIDE
  race, `floorthing` and `hitthing` held an object at none of the 37 loads
  of the archive: `rollback_refs: 37 loads …; the collision pointers held an
  object at 0 of them`.
- **Bodies below zero: 3 in the WORLDWIDE race, 1 in the control.**
  - WORLDWIDE, tic 320, at POSITION's start on the waiting map: the
    joiner's spectator body, let go at the join (`p_user.c:3840`), and a
    bot's body removed to make room for the joiner (`d_clisrv.c:2683`).
  - WORLDWIDE, tic 1337, in the race: a body removed in a speculated tic.
  - Control, tic 93: again a bot's body removed at the join
    (`d_clisrv.c:2683`).
  - In every case, the last holder let go at `p_mobj.c:10831`
    (`MT_AMPAURA`, `MT_TRIPWIREAPPROACH`, `MT_BROLY`).
- **What the ledgers say.** In all four, the recorded changes **balance
  exactly**, and yet the count is -1:
  - 33 up and 33 down, then 21 and 21, 23298 and 23298, and 1599 and 1599
    in the control.
  - Each body was first seen in a load, through the claim at
    `p_saveg.cpp:5643`.

  So the count lost one change the ledger kept: the claim itself.
- **The cause, read in the code.** `LoadMobjThinker` claims `players[].mo`
  for the body it loads (`mobj->thinker.references++`, the fix of 8.93).
  It also counts the waypoint and tracker caps with `P_SetTarget`. Its
  caller then runs `P_AddThinker` (`p_saveg.cpp:6895`), which sets the
  count to 0 (`p_tick.c:343`).
  - **Every body a load of the network archive brings back is therefore one
    reference short**: each rebuild, and the join's own load, which is why
    the control, with no prediction, has it too.
  - When the body is let go -- the joiner's old body, a bot making room --
    the release of `players[].mo` takes a reference that is not there.
  - The last holders then take the count to -1, and the body is never
    freed. In the other order, it would be freed while still pointed at.
- **Fix, `7c3e996ca`** (local branch `wip/refsfix`, not pushed; syntax
  checked with and without `PARANOIA`, 2 injected errors reported). The
  count an object's loader left survives `P_AddThinker`. Other thinkers
  keep the 0 they had: their memory is not promised zeroed, while
  `P_AllocateMobj`'s is. `P_RelinkPointers` never relinks `players[].mo`,
  so nothing is counted twice.
- **In passing, 8.109's join lines, caught.** This race wrote "entered the
  game" three times.
  - The three `rollback_chat` lines are on tics 318, 320 and 322, each at
    the horizon and in a speculated pass, with the frontier at 312, 314 and
    316, and nothing held back.
  - So each copy came from a different world, in which the join landed on a
    tic that no earlier world had reached, 2 tics later each time.
  - The join phase had 3 rebuilds for this machine's input.

  8.109's hypothesis holds on its clauses: the join follows this machine's
  input, and each rebuild moved it later.
- **The race.**
  - 999, 1000 and 1000 passes kept of 1000, and 0 rebuilt for an input.
  - 2.1 to 2.4 ms a pass, about 4100 frames a window.
- **Also written, not pushed: `cbd6d35a4`** (local branch `wip/menu`;
  syntax checked, 3 injected errors reported). Gibax's ask:
  - `WORLDWIDE Mode` in Server Options > Advanced > Network Connection;
  - `worldwide` saved in the config -- a server variable, not a netvar, so
    a client saves only what it set itself;
  - on the host screen, `(WORLDWIDE: On/Off)` under `(Public: …)`.

  The harness starts every server with `+worldwide off`
  (`playtest.sh`, `soak.sh`), and the four WORLDWIDE scenarios turn it on.
- **Prediction for the fix**, the same two runs on a build with
  `7c3e996ca`:
  - **no body below zero** in either: no `MT_PLAYER` PARANOIA line and no
    `rollback_refs` dump;
  - the race's figures as before.

  A body still going below zero would carry its ledger, with a different
  site.

### 8.113 The fix of 8.112 not measured: a client nobody drives never joins

`89aba69fb` (`0412e7760`, 8.112's fix, and the menu entry), pushed on
Gibax's go-ahead, CI run 36778958363 green. Gibax was at work all session,
so **nothing was driven**. He approved each launch.

- **The exe on the bench was not the measuring one.** The sha check caught
  it before the first race. On the evening of 2026-09-30, at 21:48, after
  8.112's races, Gibax had put `04db0cf`'s **release-config** build in place
  to try it (artefact `ringracers-win64-release-…`, without the PARANOIA
  message). The measuring build sat beside it, renamed.
  - `playtest.sh` copies the main exe into `clienthome` when they differ,
    so the controls would have run on a build that cannot raise the alert.
  - On his word, his build is now `ringracers_release_rollback_netcode.exe`.
- **A client nobody drives stays a spectator, so the join's path never
  runs.** `wwwindows` and `frames_off`, dedicated, 6 tics of lag, on the
  measuring `04db0cf` (sha256 `65be9958…`):
  - "never entered the game" in both;
  - 0 PARANOIA lines and 0 bodies below zero, over 140 to 505 loads of the
    archive.

  A spectator enters the race only two ways:
  - its item button (`BT_ATTACK`, `p_user.c:4623`, which toggles
    `PF_WANTSTOJOIN` and is ignored while the player flashes);
  - the pause menu's *Enter Game* (`menus/transient/pause-game.c:518`).

  In 8.112 it was Gibax's press. Without a join, no bot is removed to make
  room and the joiner's spectator body is never let go: those are the two
  paths of 8.112's alerts. The prediction written before the races (at
  least one body below zero in each control) is wrong on that count, and
  nothing was measured about the fix.
- **A simulated press did not reach the race.** On Gibax's go-ahead, the
  harness pressed Space in the client's window, which was in front:
  - once, 2 s after its join line;
  - in a second race, up to 8 times, 3 s apart, until "entered the game".

  The client stayed a spectator both times. The waiting map counts 1
  rebuild for this machine's input, as the race with no press does, so the
  presses never reached the client's commands -- although the same injected
  keys drive the menus. Not explained. First lead: the TESTER profile's item
  button is not Space (not checked).
- **Harness.** Stopping a chain's top script left its children running, and
  the orphans started the next race. A chain is stopped as a whole tree.
- **The menu in the game** (`89aba69fb`; one instance, no server, keys on
  Gibax's go-ahead, captures sent to him):
  - Server Options > Advanced > Network Connection: `WORLDWIDE Mode` first,
    with its description, moving from Off to On;
  - on the host screen, `(WORLDWIDE: On)` in the warning colour, `Off` in
    the highlight colour.

  Measured on the 1080p capture, the line's glyphs filled rows 110 to 116,
  and the box's frame starts at 117. Moved to `98 + 9`, two rows clear on
  each side (`2209b7130`). The value was put back to Off.
- **The title's second pass** (not netcode), Gibax's asks:
  - the menu description in his words (`07cf52229`);
  - the flash at 0.6 s instead of 1.5 s (`8fa6263be`);
  - space behind the globe from the flash on (`c7a52b61e`): an optional
    `KTSWWSKY`, tiled and scrolled in place of the stock sky, with
    `KTSWWSET` taking its two speeds.

  In the notes' `titre/`, the ring is sharp, and centred on the stock logo,
  which sits at 155.5, not 160.

  Installed as `2209b7130` (CI run 36831987777, sha256 `37c60f41…`) with
  that pk3, and the title captured in a burst, without keys:
  - the flash, the ring and the globe come in as designed;
  - **space never shows**: the stock title runs a level behind its art (the
    title map), and `c7a52b61e` only replaced the sky where no title map
    runs. Then the attract demos cycle, and the title comes back;
  - `093a79aeb` draws space first from the flash on, over the title map
    too. Pushed on Gibax's go-ahead, CI run 36833865387, installed (sha256
    `ce744500…`) with the ring 2 px further left. Captured the same way:
    from the flash on, space in place of the level, until the attract
    demos. It scrolls left by 20 screen pixels in 1.8 s, 58 in 5.3 s (the
    left margin, matched exactly): about 11 a second at 1080p, as -1 in
    `F_SkyScroll`'s units gives (35/16 pixels a second, scaled by 5).
- **Next**, for the fix, one of two:
  - the same pair driven by Gibax, with `.bak_04db0cf` as the control in
    the same session;
  - a console command that sends the pause menu's *Enter Game*, called by
    the client scenarios, so that a race nobody drives joins too.

### 8.114 The branch ported onto the 2.4 release: read, not built

Gibax's ask: rebase the whole work onto the 2.4 release, on another branch,
to see how it goes, so that at worst this exe can be offered with the 2.4
game. Nothing was launched.

- **What the branch sits on.** Not `05cca02c9` as 8.30 read it: the August
  merges in its history are Kart Krew's. Its upstream base is `4bad15a40`,
  upstream master on 2026-08-31 and still its head. That is **137 commits
  past `v2.4`** (`7f895c9a7`, the last release; no tag after it). Ours on
  top: 364 commits, 39 files outside `docs/`, +14369/-271 lines, most of it
  `k_rollback.c` (9077) and `p_saveg.cpp` (+2217). Since 2.4, upstream
  touched 32 of those 39 files -- often only for type names.
- **The port.** A local branch `worldwide-2.4` from `v2.4`, in a separate
  worktree. The whole branch was squashed and applied by a three-way merge
  (`36a986f6f`). 15 files conflicted, in 37 regions:
  - 30 differed only by the type names upstream changed after 2.4
    (`INT32`/`UINT8`/`boolean` to `int32_t`/`uint8_t`/`dboolean`): our side
    taken.
  - 2 in `d_clisrv.c` differed by `SHORT`, renamed `LSBF_SHORT` after 2.4:
    our side, with `SHORT`.
  - 5 in `core/memory` and `z_zone` are the raw snapshots' (B2). They rely
    on the pool allocator's count of blocks in use, which came upstream
    after 2.4 (`d8b4e8a39`, memory statistics). Our side taken, with that
    counter's member and its initialisation.
  - `doomtype.h`: `dboolean` defined as `boolean`. Windows' `boolean` there
    is `BOOL`, so a macro, not a typedef.
- **Checked.** Every touched file, with and without PARANOIA: no error
  except upstream's own, in lines the port does not touch, under a check
  configuration without CURL, OpenGL or the ACSVM path. Not built: the CI's
  prebuilt SDK and toolchain are the development line's, and 2.4's CMake
  differs.
- **The wire, read against stock.** Since 8.28 the network savegame must
  keep the stock grammar. Of the 88 archive reads and writes the branch adds
  to `p_saveg.cpp`, each is one of three kinds:
  - gated to local snapshots (`localsnapshot`/`localrestore`, in the code
    or at the top of a helper);
  - in the raw snapshot path, local by nature;
  - the same bytes on the wire. `followerskin` is signed now; a polyobject's
    flags fall back to their spawn values; `onconveyor` is now read where
    every writer puts it, 2.4's included, which upstream's reader did not.

  So a WORLDWIDE client should read a stock 2.4 server's join savegame. Not
  run.
- **A stock 2.4 server is at hand.** The game folder's `ringracers.exe` is
  exactly `v2.4` (it carries the tag's commit).
- **Next.** Push `worldwide-2.4` (it touches `src/`: asked first) and make
  CI build it. Then run the bench on it as on the development line. Then
  run the compatibility cases against that stock exe: a WORLDWIDE client on
  a stock server, a stock client refused, a WORLDWIDE build hosting in
  vanilla mode, the leave.

### 8.115 `worldwide-2.4` builds

Pushed on Gibax's go-ahead, with the CI adjusted for the branch. Nothing was
launched.

- **The CI follows 2.4's dependencies.** 2.4 finds SDL2
  (`find_package(SDL2 CONFIG)`). Upstream moved to SDL3 after 2.4, and so had
  this workflow, its Alpine image and Kart Krew's prebuilt Windows SDK. The
  first run failed at configure in all three jobs. On this branch only:
  - Linux on Alpine 3.20, which still ships the real SDL2;
  - Windows with the same llvm-mingw toolchain and SDK, plus SDL2 2.30.9
    built from its repository, static;
  - WebM recording off, as 2.4's own CI built it.
- **Windows then failed after linking.** 2.4 copies the executable's DLLs
  next to it after the link. A fully static build has none, and
  `cmake -E copy_if_different` refuses a destination alone. Upstream
  dropped that step after 2.4. `25a580e96` adds
  `SRB2_CONFIG_COPY_RUNTIME_DLLS`, on by default, and the CI turns it off.
- **Run 36839857285 (`d822760ec`): green in all three jobs.** Linux (GCC)
  compiles and links the whole port. The Windows artefacts are
  `ringracers-win64-d822760ec…` (dev) and `ringracers-win64-release-…`
  (release-config, so version 2.4).
- **Next** is the same as 8.114's: the bench on this build, then the
  compatibility cases against the stock 2.4 exe.

### 8.116 The second ring, on the title and in the logo

Not netcode. Gibax drew a second ring (`ring_worldwide_v2.png`, in the code
repo's ignored `etc/`) and asked for it on the title and in
`docs/RRW_logo.png`. Nothing was launched.

- **What the picture is, measured.** It is his empty ring with three pieces
  of pixel art over it: the WORLDWIDE lettering and a chequered flag,
  mirrored on the left. Each piece is enlarged exactly 9 times: every run
  of colour is a multiple of 9, 116x17 and 14x14 pixels at the game's size.
  Rebuilt from the empty ring and the pieces, the picture is the same:
  the same opaque pixels, a mean difference of 0.18, the rest on single
  source columns at the flags' edges, a ninth of a game pixel. The empty
  ring itself is smoothed, on no grid (52,000 colours).
- **The title.** The notes' `titre/build_pk3.py` has a new default,
  `--ring-method parts`. It finds the pieces in the picture, brings the
  empty ring down on the lettering's grid (each game pixel the majority
  colour of its 9x9 cell, as 8.113's ring), then pastes the pieces pixel for
  pixel. The ring is 236x58. Gibax's flags sit 3.67 and 3.44 game pixels
  from the lettering; both are put at 4, symmetric. Its gold and its
  lettering share one centre, put at 152.5: between the first ring's
  lettering (152) and gold (153), measured on that ring's pk3. Its bottom
  is where the first ring's gold ended, row 190. Installed in the game
  folder (sha256 `d95906fe…`; the first ring's pk3 kept as
  `.bak_1001b`).
- **The logo.** Gibax's logo turned out to be three layers, each found in
  it with no difference over every pixel of theirs left visible:
  - `Worldwide_Earth.png` at 130x131, nearest neighbour, at (53, 0);
  - the game's own logo (`KTSBUMPR1`) at 210x78, nearest neighbour, at
    (15, 16);
  - the ring, its far half under the game's logo and its near half over it.

  The notes' `logo/build_logo.py` rebuilds it around the title's ring, at
  the game's size. Its witness rebuilds it with the first ring: identical
  over rows 0 to 63, and different only within that ring (rows 64 to 114),
  the layer replaced. The new ring is centred where the first was, on 119,
  and ends on the same row, 114.
- **Then, the same evening, Gibax asked for everything a few pixels lower
  and space a little faster.** Neither needs a new exe: `KTSWWSET`
  carries the lifts and the speeds. `build_pk3.py --drop 4`, the new
  default, lowers everything 4 px from the flash on. The logo and the
  characters (Tails, his tails, Eggman, the lightning) rise 20 and 8
  instead of 24 and 12. The globe and the ring go 4 px down in the pk3,
  with the ring's bottom on row 194. f_finale.c clamps the lifts at 0, so
  the game cannot draw them below their stock place: the script refuses a
  larger drop. Space scrolls at -2 instead of -1, twice as fast. Witnesses:
  `--drop 0 --sky-x -1` rebuilds the installed pk3, and `--ring-method logo`
  with those rebuilds the first ring's, both with identical contents. Built
  (sha256 `171b240b…`), and installed once the game was closed.

### 8.117 The system's keyboard layout, tested; the 2.4 install is 32-bit

Not netcode. The branches `azerty` (on `rollback-netcode`, SDL3) and
`azerty-2.4` (on `worldwide-2.4`, SDL2) type with the system's keyboard
layout, as SRB2 2.2.15 does: SDL's text input, on only while the console,
the chat or a menu text box is open, so the game's controls do not change.
Only ASCII is kept, since the game's fonts stop at `~`. The cvar
`textinput` turns it off ("Use System Keyboard Layout", in the HUD's online
options). Both built green on all three jobs (runs 36841739858 and
36841743401). `worldwide-2.4` was fast-forwarded to `azerty-2.4`
(`0a9877dd1`, run 36861427929 green).

- **The first launch stopped at start-up**, before any window.
  `ringracers_azerty-2.4.exe` (`b53ffebb…`) is 64-bit. The game folder's
  stock 2.4 is 32-bit (x86): its `ringracers.exe`, `exchndl.dll` and
  `mgwhelp.dll`. A 64-bit process cannot load them: `LoadLibraryA` fails
  with error 193, measured by loading them from that folder. 2.4 makes that
  fatal (`I_Error` in `init_exchndl`, then a SIGSEGV on the way out). After
  2.4, upstream only loads them in 32-bit MinGW builds
  (`__MINGW32__ && !__MINGW64__`, `src/sdl/i_main.cpp`), which is why the
  `rollback-netcode` builds never hit it. A second, clean copy of 2.4 had
  the same 32-bit files.
- **Relaunched with `-noexchndl`**, which skips that load and costs only the
  `.rpt` crash reports: it runs. **Gibax: AZERTY works in the console.**
  The menu text boxes, the chat and the cvar turned off are not tried yet.
- **Then, on Gibax's go-ahead** (both touch `src/`):
  - `azerty` merged into `rollback-netcode` (`1fcef131b`). It touches 12
    files of `src/`, and `rollback-netcode` had only moved in `docs/` since
    they split. CI run 36901738576: green, all three jobs. Not installed.
  - `worldwide-2.4` takes upstream's guard (`68f5eb582`). Only 32-bit MinGW
    loads the crash handler, and only there does the crash box ask for its
    `.rpt`. Upstream's commit (`c85a7e83b`) also turns on a DbgHelp
    handler for 64-bit through cpptrace, which 2.4's CI does not have: not
    taken. CI run 36901699571: green. The dev exe no longer holds the
    "exchndl.dll or mgwhelp.dll is missing" message, which `0a9877dd1`'s
    did. It holds its revision (`68f5eb5`) and the keyboard option.
    Installed as `ringracers_worldwide-2.4.exe` (`2c53237e…`) and launched
    in the same stock 2.4 folder, **without `-noexchndl`: it starts**, its
    window open and responding, and Gibax confirms.

### 8.118 The `MT_PLAYER` fix measured at the join, against its control

The fix of 8.112 (`0412e7760`), measured as 8.113 asked: driven by Gibax,
with the build before it as the control in the same session. Both runs:
`playtest.sh wwwindows dedicated`, 6 tics of lag, RR_SkyscraperLeaps,
driven by Gibax ("oui j'ai piloté"), no crash. Prediction written and
pushed before the first race (notes `aff5b50`): 0 bodies below zero and 0
`MT_PLAYER` alerts with the fix, worth something only if the join happens
and the control shows at least one.

- **The fix**: `093a79aeb` (sha256 `ce744500…`), which also carries
  `04db0cf`'s instrument. The code repository's HEAD is further on, but
  only by `azerty`'s 12 files (text input), so the harness's "not HEAD"
  warning does not apply. At 19:59
  (`playlog_wwwindows_20261001-195908_093a79a.txt`):
  - the client entered the game, on the waiting map (RR_TESTRUN), around
    tic 107;
  - **0 PARANOIA lines**, client and server;
  - **0 bodies below zero**, over 33 loads of the archive.
- **The control**: `04db0cf` (`65be9958…`), in place of the measuring exe
  for that run only, then put back (`ce744500…` checked in both folders).
  At 20:03 (`playlog_wwwindows_20261001-200333_04db0cf.txt`):
  - the client entered the game, around tic 87;
  - **2 bodies below zero** at the join, over the same 33 loads, with 4
    PARANOIA lines on the client and none on the server;
  - the same two as 8.112's: a bot's body removed to make room for the
    joiner (`d_clisrv.c:2683`), and the joiner's spectator body, let go at
    the join (`p_user.c:3840`). Both were first seen in a load, removed in
    a confirmed tic, and let go of a tic later at `p_mobj.c:10831` by their
    last holder (`MT_AMPAURA`, `MT_TRIPWIREAPPROACH`).
- **What it shows.** At the join, the fix holds: the same path, run in both
  races, leaves no count below zero with the fix, and two without it.
- **What it does not show.** 8.112's third body came in the middle of a
  race, removed in a speculated tic (tic 1337). Neither race reached that
  case. On Skyscraper Leaps, both rebuilt only before the first window,
  then ran some 3,000 tics with no rebuild at all. The fix covers every
  load of the archive, so that case should be gone too, but it has not
  been seen.

### 8.119 `rollback_join`: a client scenario that enters the race by itself

Written on Gibax's go-ahead ("écris la commande de jonction, pousse le"), so
that the join's path (8.112, 8.118) and anything else a racing client does
can be run with nobody driving.

- **What the menu does.** *Enter Game* in the pause menu sends `XD_SPECTATE`
  with the player and "join" (`M_HandleSpectateToggle`,
  `menus/transient/pause-game.c`). It sends it only for a spectator who has
  not already asked: the server's `Got_Spectate` first makes any player
  who is not a spectator spectate, then sets `PF_WANTSTOJOIN`.
- **`rollback_join`** (`k_rollback.c`) sends the same request for this
  machine's first player, under the same conditions as the menu: in a
  game, a spectator, not already asking, a gametype with spectators, team
  changes allowed. Otherwise it sends nothing and prints why. It changes
  nothing else, and nothing when it is not called.
- **`playtest.sh <scenario> join`** (notes, `harnais/`) writes a copy of the
  client scenario that calls it twice after its `rollback_lag` line (after
  105 tics, then 70 more), and takes those 175 tics off the next `wait`, so
  the windows fall where they did. The logs carry `_join`. The first `wait`
  after `rollback_lag` is at least 400 tics in every client scenario; only
  `vanillajoin`, which has no `rollback_lag` and whose join is refused, gets
  no call.
- Checked before pushing: the syntax, with the local gcc (`-fsyntax-only`,
  which caught an error put in on purpose); the generated
  copy of `wwwindows`, whose waits still add up to 1600.
- **Built and run.** CI run 36904726784 green (`656ab3c73`, dev sha256
  `b87157f5…`, which also carries the fix and `azerty`), installed on
  Gibax's go-ahead with `093a79aeb` kept as `.bak_093a79a`.
  `playtest.sh wwwindows dedicated join`, prediction pushed before (notes
  `143b95d`):
  - **21:15, not a test of the command.** Gibax joined by hand with his
    controller ("j'ai juste rejoins", "pas joué"). The client entered at tic
    60, before the first call. Both calls answered "player 9 is already in
    the game -- nothing sent", so the guard held, but nothing was sent.
  - **21:21, hands off** ("je touche à rien"). The first call sent
    ("player 9 asked to join the game"), and the second found the player
    already in. The server printed "*Guest entered the game.". On the race
    map the grid had 7 bots and the local kart, where the morning's
    spectating runs had 8: the bot removed to make room for a joiner
    (`d_clisrv.c:2683`), as in the evening's three runs joined by hand.
    **So `rollback_join` makes an unattended client race.** 0 bodies below
    zero over 93 loads of the archive, 0 PARANOIA lines, client and server:
    a third join with the fix, and a third 0.
- **The client printed no "entered the game" line**, so the harness took it
  for a spectator. Joined by hand, the line comes 2 or 3 times, each from a
  speculated tic (item 10 of the ROADMAP). Joined through the server
  command, it never comes, presumably because the join then happens in a
  confirmed tic, whose chat lines are not printed: not read yet. Since
  then, `playtest.sh` takes the server's log as the proof when the client's
  is silent: the client log gives the joiner's name, and the server's log
  must say that name entered the game. Checked on three past logs: this
  run, a run joined by hand, and a morning run that spectated, which it
  still flags.
- **Noted, not read.** A kart that has joined but is not driven made far
  more rebuilds than Gibax's two driven races of the evening. On Skyscraper
  Leaps, for its own input: 125 when joined by hand (21:15), 31 when joined
  by the command (21:21), against 4 to 5 driven. For the bots' inputs: 54
  and 24, against 0. Loads of the archive: 214 and 93, against 33.

### 8.120 `rollback_botsashuman`: the bots guessed as remote people

ROADMAP item 2(a), written on Gibax's go-ahead ("écris le mode bots prédits,
pousse-le"). Nobody else can drive on this machine. The bench has bots, and
the speculation never guesses a bot.

- **How each kart is guessed today** (`K_RollbackPredictInputs`). This
  machine's own input is not a guess: it is what is held now. A remote
  person's is their last input, repeated, with the received flag cleared. A
  bot's is computed from this machine's world by `K_BuildBotTiccmd`, which
  draws no random number. That was chosen because repeating a bot's input
  made every predicted tic wrong on a grid of bots. So a race of bots shows
  none of the rebuilds, nor the shaking, that a person's changes of input
  would bring, and the largest unknown (item 2) cannot be looked at
  unattended.
- **`rollback_botsashuman 1`** sends the bots, and only them, down the
  person's path: their last input, repeated. A human who finished the race
  and drives on bot movement keeps the computed one. Only the guess
  changes: the server sends every bot's real input as before, the
  confirmed world is the same, and the rebuilds count under "bots" as they
  did. `rollback_drift`'s grid line says when it is on.
- **The scenario `wwbots`** (notes, `harnais/`) is `wwwindows` with the
  switch on, and nothing else changed. Run it with `join`, and with
  `wwwindows join` as its control in the same session:
  `playtest.sh wwbots dedicated join`.
- **What to expect.** A bot recomputes its angle and its confirmations
  every tic, so far more wrong guesses than a person who holds a button:
  an upper bound, not a person's figure. The point is to see how the
  rebuilds and the drawn karts behave when the guesses are wrong often.
  The prediction proper is written before the first run.
- Checked before pushing: the syntax, with the local gcc, which caught an
  error put in on purpose; the scenario differs from `wwwindows` by the
  one line.
- **Built and run.** CI run 36924532419 green (`2c48c8105`, dev sha256
  `51f4deb7…`), installed on Gibax's go-ahead ("oui installe et lance les
  deux"), `656ab3c` kept as `.bak_656ab3c`. Nobody drove; the client joined
  through `rollback_join` in both (the server's log says so). Prediction
  pushed before (notes `942322c`). On Skyscraper Leaps, at 22:59 and
  23:01; the first four rows are the race phase, the rest count from the
  join:

  | | `wwbots join` | `wwwindows join`, the control |
  |---|---|---|
  | passes | 3787 | 3792 |
  | kept | 430 (11%) | 3786 (99.8%) |
  | rebuilt for a bot's input | 3351 | 0 |
  | rebuilt for this machine's input | 4 | 4 |
  | loads of the archive | 3384 | 30 |
  | bots' wrong inputs on the first wrong tic | 22056 | 0 |
  | the first wrong tic is the frontier's | 3377 of 3378 | 24 of 24 |
  | a pass | 7999 us, 28% of a tic | 1120 us, 4% |
  | frames drawn | 13083 | 18856 |
  | frames 16.7 to 28.6 ms apart | 3215 | 25 |
  | loop iterations past a tic, the next frame skipped | 16 | 0 |
  | bodies below zero, PARANOIA lines | 0, 0 | 0, 0 |

  The grid line said "the bots guessed as people" in `wwbots` only.
- **The prediction held**: over a thousand rebuilds for the bots (3351),
  under half the passes kept (11%), over a thousand loads (3384), passes
  dearer. The switch acts, and only on the bots: this machine's own input
  rebuilt 4 times in both.
- **What it says, as an upper bound.** With seven bots guessed by
  repetition, almost every pass rebuilds, always from the frontier. A pass
  then costs 8 ms at eight karts, seven times the control's. About 30%
  fewer frames are drawn, many of them over 16.7 ms apart, and 16 times the
  loop ran past a tic. A person holds a button for several tics where a bot
  changes its angle every tic, so a real second human should sit far below
  this. The cost of always rebuilding is still worth knowing for Phase B:
  28% of a tic at eight karts, against the 30% the gate allows at sixteen.
- **What it does not say.** The shaking: `rollback_frames` measures the
  local kart and the view, and the local kart never moved (0 counted in
  both races). How the remote karts are drawn, the part a second human
  would see, has no instrument yet. Next: such an instrument, or
  `wwbots` driven by Gibax, looking at the bots.

### 8.121 The other karts as drawn; races of up to sixteen karts

Written on Gibax's go-ahead ("écris l'instrument pour les karts distants,
pousse-le"), after 8.120: `rollback_frames` measured only the local kart and
the view, and the local kart is never a guess, so nothing measured how a
wrong guess is drawn. Gibax also asked whether sixteen karts could be run.

- **`rollback_frames` gains two lines**, "the other karts as drawn", one for
  the bots and one for the people. Every drawn frame, each kart but this
  machine's is taken at the place the renderer draws it
  (`R_InterpolateMobjState` at `rendertimefrac`), and its step from the
  frame before is classed against its speed and the time between the two
  frames, as the local kart's is (8.100): even, short (under half), long
  (over one and a half) or backwards, and whether the frame carried a pass.
  The same filters apply: moving over 2 units a tic, a step under 512 units,
  frames under 100 ms apart.
  - A kart is followed by its slot, not its body. A load of the archive may
    hand it a new body, and that frame is the one a rebuild could make
    shake.
  - Reset and printed with the rest of `rollback_frames`, by
    `rollback_twoclock`.
- **Sixteen karts.** A Match Race fills to `maxplayers` (`k_bot.cpp`), capped
  by `maxconnections` online, 16 by default and as saved here; the engine's
  `MAXPLAYERS` is 16. `bots 6` in the scenarios is the bots' level, not
  their number. `playtest.sh <scenario> karts=<n>` (notes, `harnais/`)
  runs a copy of the server scenario with `maxplayers n`, 2 to 16, and the
  logs carry `_k<n>`. No race measured before had more than nine.
- Checked before pushing: the syntax, with the local gcc and `-Wall -Wextra`,
  which caught an error put in on purpose; the substitution of `maxplayers`
  on `wwbots`'s server scenario.
- **Sixteen on a dedicated server stopped it.** CI run 36927812194 green
  (`d56763ca0`, dev sha256 `3cb778b6…`), installed on Gibax's go-ahead
  ("lance les trois, installe quand c'est vert"). With `maxplayers 16` the
  dedicated server stopped when the client connected: "assert failed:
  newplayernum < MAXPLAYERS" (`d_clisrv.c:4163`, upstream's code), then a
  segmentation fault, in both 16-kart races.
  - The bots take every slot they can. A dedicated server keeps slot 0 for
    itself (`SV_AddWaitingPlayers` searches from 1), so 15 slots, and
    sixteen bots' worth of room leaves none for a connecting player.
  - The function's fallback, "overwrite bots if there are NO other slots
    available", starts its search where the first one ended, past the last
    slot, so it never overwrites anything.
  - Upstream's behaviour, not this branch's, and worth knowing for any
    server that fills with bots. The third race was stopped by hand.
  - `playtest.sh karts=<n>` now runs `maxplayers n-1` until the race map,
    then n, so the client takes the slot left free and the race map's bots
    fill the grid around it. It refuses more than 15 on a dedicated server;
    sixteen needs a host.
- **Run, nobody driving, the client joined by `rollback_join` in each**, all
  on `d56763ca0` so they compare (HEAD had moved on by the title and icon
  only, 8.122). Prediction pushed before (notes `97a784b`, `4986331`).
  Skyscraper Leaps; passes and rebuilds are the race phase, the rest count
  from the join; "a pass" is `rollback_cost`'s whole pass, Phase B's
  measure:

  | | A: `wwbots`, 8 | T: `wwwindows`, 8 | B': `wwwindows`, 16 | C': `wwbots`, 16 |
  |---|---|---|---|---|
  | when | 23:25 | 23:38 | 23:41 | 23:44 |
  | server | dedicated | dedicated | with a host | with a host |
  | grid | 7 bots, local | 7 bots, local | host, 14 bots, local | host, 14 bots, local |
  | passes kept | 774 of 3774 (21%) | 3785 of 3791 (99.8%) | 3458 of 3796 (91%) | 1530 of 3794 (40%) |
  | rebuilt for another's input | 2994 | 0 | 331 | 2249 |
  | loads of the archive | 3023 | 32 | 762 | 2680 |
  | a pass | 12.7 ms (45%) | 2.6 ms (9%) | **6.2 ms (22%)** | 16.6 ms (58%) |
  | frames drawn | 13945 | 18859 | 17403 | 10755 |
  | iterations past a tic, a frame skipped | 22 | 0 | 21 | 866 |
  | bots drawn, frames with a pass: short | 55.6% | 1.2% | 3.8% | 51.3% |
  | ... long | 2.6% | 0.2% | 0.6% | 11.2% |
  | ... backwards | 1.2% | 0.08% | 0.16% | 1.8% |
  | bots drawn, frames without one: long | 51.7% | 18.8% | 31.3% | 45.8% |
  | bodies below zero, PARANOIA | 0, 0 | 0, 0 | 0, 0 | 0, 0 |

- **How a wrong guess is drawn: it shakes.** A against T, the same eight
  karts, the bots guessed or computed. In the frames that carry a pass the
  bots step short more than half the time (55.6% against 1.2%) and
  backwards fifteen times as often (1.2% against 0.08%). In the frames
  between, they make up the ground with long steps (51.7% against 18.8%):
  a short step, then a long one, the judder of a kart put back and run
  forward again. Some long steps without a pass are ordinary frame pacing,
  18.8% in the control.
- **Sixteen karts fit, with the bots computed.** B': 6.2 ms a pass, 22% of a
  tic, under the 30% gate. 91% of passes kept, against 99.8% at eight: the
  bots' computed inputs are wrong more often on a fuller grid (331
  rebuilds against 0). The bots drawn stay close to the control (3.8%
  short, 0.16% backwards with a pass). Not yet the gate's whole condition:
  the first 1:48 of the race, not late in it; Skyscraper Leaps, not a heavy
  map like Opulence (8.62); the host drawing its own window on the same
  machine.
- **Sixteen karts with every guess wrong do not.** C': 16.6 ms, 58% of a
  tic, and 866 frames skipped. That is the upper bound of 8.120 at sixteen.
- **Sixteen karts on Opulence do not fit either, bots computed** (Gibax's
  ask, "lance la 1, 16 karts sur Opulence"; `20cb1f2`, with a host, nobody
  driving, 23:56; prediction in the notes' session note, before it). Pass
  by pass, from the differences of the cumulative reports: 10.2, 10.7 and
  10.3 ms in the three windows, **36 to 37% of a tic, over the gate**; in
  the last window a save takes about 4.6 ms and the speculation about 5.5
  (about one tic a pass). 94.6% of the race's passes kept; the 195
  rebuilds for another's input all in the first window. 14126 frames, 2347
  of them 16.7 to 28.6 ms apart and 147 over 50; 200 loop iterations past
  a tic. 0 bodies below zero, 0 PARANOIA. Predicted 8 to 12 ms and over the
  gate: held; "later windows dearer" did not (flat).
- **Gibax saw the client stutter at sixteen karts**, under 144 frames a
  second. Read from B': the frames that carry a tic, 35 a second, cost 9.9
  ms on average at sixteen against 6.2 at eight, for 6.9 ms a frame at 144
  Hz; 529 of 4600 over 16.7 ms against 5. The frames between tics cost 2.9
  ms and are not the problem. The rest is the rebuilds (8.123) and the host
  drawing its own window on the same machine.
- **Against the prediction.** Held: T (99.8% kept, 1.07 ms of restore and
  speculation, the bots far steadier than A's); the 16-kart grid; B' under
  the gate; C' over it, with fewer frames and more skipped than B', and
  the bots' uneven steps over twice B''s; 0 below zero and 0 PARANOIA
  everywhere. Wrong: B' kept 91%, not over 95%, and its restore and
  speculation took 3.5 ms, not 2 to 3. C' kept 40%, not under 15%, and its
  restore and speculation took 10.5 ms, not about 15. A's backwards steps
  reached 1% only in the frames with a pass (0.44% in all).

### 8.122 The window's title and the icon

Gibax's asks: the window's title, in development and release builds alike,
and his icon (`etc/RRW_icon.png`, 40x34) in place of the game's. Pushed on
his go-ahead (`20cb1f21f`). CI run 36929724979 green; the dev exe (sha256
`4f6cfeea…`) holds the new title and every size of the .ico byte for byte.
Installed in both folders on Gibax's go-ahead, `d56763c` kept as
`.bak_d56763c`. Launched on his go-ahead: the window reads "Dr. Robotnik's
Ring Racers Worldwide Development EXE", and Gibax confirms the icon and
the name ("ça marche, l'icône s'affiche bien et bon nom").

- **The title** comes from `SDL_CreateWindow` (`sdl/i_video.cpp`): "Dr.
  Robotnik's Ring Racers Worldwide" before `VERSIONSTRING`, which is
  "Development EXE" or the version. The exe's `FileDescription` and
  `ProductName` (`win32/Srb2win.rc`, what the task manager and the file's
  properties show) say the same; Kart Krew's company name and copyright
  stay.
- **The icon.** On Windows the window and the taskbar take the exe's icon,
  `win32/Srb2win.ico`, built into it by `Srb2win.rc`; `sdl/SDL_icon.xpm` is
  compiled only on Unix (`USE_XPM_ICON`). Both are remade from Gibax's
  picture by the notes' `logo/make_icons.py`: the art enlarged six times
  pixel for pixel, whole multiples where they fit (128, 256), brought down
  smoothly below; the .ico laid out as the original (BMP to 128, PNG at
  256), each size read back as written; the XPM at 64x64, 243 colours.

### 8.123 The sixteen-kart rebuilds are the host's: a guessed person's latency stamp

Gibax asked for the 331 rebuilds of 8.121's sixteen-kart race (B') to be
explained ("fait les 331 rebuild à 16 karts"). Read in the logs, nothing
launched for it.

- **Not the bots.** B': "wrong inputs on it -- this machine 18, bots 2,
  people 753"; on Opulence (16 karts, 23:56): bots 0, people 621. The person
  is the host, p0, who is only there in the races with a host; the eight-kart
  races ran dedicated, with no person but this machine, hence 0. So not the
  grid's size but who is on it.
- **Only at the start.** Every one of them falls on the waiting map and in the
  race map's first 800 tics or so; the counts then do not move for 3000 tics.
  The host never spectated (nothing in the server's log), and its real inputs
  in the race are all zero.
- **Two kinds**: latency 753, angle 468, received 285, and 753 = 468 + 285.
  468 differ in the angle -- the host's camera through the intro and the
  countdown -- and 285 in **the latency stamp alone** (the received flag is
  left out of the decision to rebuild).
- **The stamp.** `G_BuildTiccmd` writes the sender's leveltime into
  `latency` (`g_build_ticcmd.cpp:171`), `G_Ticker` turns it into a lag
  (`g_game.c:1911`), and the game reads that lag: drift and angle leniency
  (`p_user.c:2394`, `2416`) and the roulette's fudge (`k_roulette.c:2041`).
  A person is guessed by repeating the last input, stamp included, so the
  lag simulated was a tic off on every guessed tic, and `K_SameInput`, a
  memcmp, rebuilt for it. A bot's stamp stays 0 (`K_BuildBotTiccmd` clears
  the command and never sets it).
- **The fix** (`K_RollbackPredictInputs`): a person guessed by repetition has
  the stamp moved on by one per guessed tic, as their machine stamps each
  tic. Not for bots, so `rollback_botsashuman` keeps its meaning. Wrong
  only where the server itself repeats a person's sample (R1, 8.92), which
  keeps the stamp.
- **Still open**: why nothing is guessed wrong after the start. Unchecked
  hypothesis: later, the client has already received the inputs for every
  tic it speculates (below `neededtic`), and guesses nothing, for anybody.
- **Prediction for the measurement** (B''s setup, the fix against
  `20cb1f2` in the same session): the 285 rebuilds for the stamp alone gone,
  so about 200 rebuilds for another's input instead of 331, and the
  latency field near 0 among people's wrong inputs; the angle's 468 stay.
  For a real second human, every guessed tic had the wrong stamp: this
  matters more there than with an idle host.
- Checked before pushing: the syntax, with the local gcc, which caught an
  error put in on purpose.
- **Measured** (Gibax: "oui pousse et lance la mesure"): CI run
  36932690677 green, `1b808d1e6` installed (sha256 `97db00ab…`, `20cb1f2`
  kept as `.bak_20cb1f2`). B''s setup, nobody driving, the control first,
  on `20cb1f2`, at 00:06, then the fix at 00:11:

  | | control `20cb1f2` | fix `1b808d1` |
  |---|---|---|
  | the person's wrong inputs | 743 | **450** |
  | ... differing in angle / latency / received | 465 / 741 / 278 | 449 / 448 / **1** |
  | rebuilt for another's input, waiting map | 410 | **143** |
  | ... race map | 314 | 299 |
  | loads of the archive | 752 | **476** |
  | a pass | 6.6 ms | **5.3 ms** |
  | bodies below zero, PARANOIA | 0, 0 | 0, 0 |

  - **The stamp-only guesses are gone**: those wrong in "received" and not
    in the angle, 278, down to 1. The person's wrong inputs fall by 39%,
    the archive's loads by 37%, the pass by 1.2 ms.
  - **Not where predicted.** The race map's rebuilds hardly move (314 to
    299, predicted about 200): there the host's wrong guesses are the
    angle's, the camera turning through the start. The gain is on the
    waiting map (410 to 143).
  - **Wrong in the prediction**: the latency field does not vanish from the
    remaining wrong guesses -- 448 of the 450 differ in it too, all of them
    the angle's. Those tics' stamps are not the previous one plus one; not
    read yet (the server repeating a sample, R1, would do it).

### 8.124 B2 toward on by default: the field named, the claim that stays, the Lua slope

Gibax: "Fait B2" -- raw snapshots on by default, which 8.95 left off until
its open points were understood (current state, open item 7). The save is
the pass's largest part at sixteen karts on Opulence (about 4.6 of 10.3 ms,
8.121), and B2 took it from 2.9 to 1.1 ms at nine (8.95). Step 1, written
on 2026-10-02, nothing launched for it:

- **The players-block difference (9 of 1137 restores, 8.94) gets named.**
  The verify mode said only "byte 2832, in the players block". The players
  block has no markers, but `rollback_test` already reads such an offset as
  a player and a field (`P_LocatePlayerField`, `P_NamePlayerField`). The
  raw restore's check now does the same, with the two byte values, so the
  next `wwraw` soak names the field instead of a byte.
- **The double claim: counted once, for the body that stays.** After a raw
  restore, every synced object carrying a player claimed its body, and
  counted a reference for it. Two objects can carry the same player; the
  later one becomes `players[].mo`, as in the network load, but the earlier
  one kept a reference the live game does not hold: 8.94's one kart at 67
  against 66. The raw load now points each player at its body first, then
  counts one reference per player, for the body that stays. If the live
  game held the earlier one instead, the verify will say so, with two
  counts off rather than one.
- **`floorspriteslope`: that snapshot goes the network way.** Only Lua makes
  one (`P_CreateFloorSpriteSlope`); the network archive saves and rebuilds
  it, and the raw copy would bring back a pointer to a plane that may be
  freed. A snapshot taken while any object has one is now a network one,
  restored the network way (each slot says which it is), and the report
  counts them. A race with no Lua never takes that path.
- **The level interpolators are not B2's gap.** The network load does not
  rebuild them either: they are made only when an effect starts
  (`p_floor.c`, `p_ceilng.c`, `p_polyobj.c`), and `p_saveg.cpp` makes none.
  Drawing only, as 8.88 said, and the same with or without raw snapshots.
- Checked before pushing: the syntax, `k_rollback.c` with the local gcc and
  `p_saveg.cpp` as C++20 with `PARANOIA`, each catching an error put in on
  purpose.
- **Soaked** (Gibax: "oui pousse et lance le soak"): CI run 36933711043
  green, `94c7bd4f9` installed (sha256 `56812887…`, `1b808d1` kept as
  `.bak_1b808d1`), `soak.sh wwraw map=RR_Opulence` unattended, 00:22
  (`soaklog_wwraw_RR_Opulence_20261002-002213_94c7bd4.txt`). Prediction in
  the notes before it.
  - **395 checks, 0 failures.** 1580 raw saves at 867 us, 1185 raw
    restores at 1829 us with their check.
  - **0 reference counts rebuilt differently**, as predicted. 0 snapshots
    went the network way, none expected without Lua. 0 PARANOIA.
  - **The players-block difference, named**: 9 of 1185 restores, as in
    8.94 (9 of 1137). Two places: player 5, a bot, 1005 bytes into the
    record (three restores at tic 2041), and player 6, a bot, 1012 bytes in
    (two at tic 2311) -- past the fields `P_NamePlayerField` names, but
    both times **0x00 became 0x20**, Carnival Night's pattern of 8.94.
  - **It is `itemRoulette.itemList.cap`**, read in the code. The archive
    writes 0 and 0 for a player whose item list was never allocated, else
    its capacity and length (`p_saveg.cpp:990-999`). A restore never frees
    or shrinks the list (the read code, "Growing only": both passes of a
    check share the block). So a bot that had its first roulette after the
    snapshot comes back with a length of 0 but a block of 32
    (`K_InitRoulette`'s first size), and the check's archive writes 32
    where the snapshot's wrote 0. Allocation bookkeeping, and **no gameplay
    reads it**: a roulette always starts in `K_FillItemRoulette`, which
    calls `K_InitRoulette` whatever the block, and `K_FillItemRouletteData`'s
    own `items == NULL` test only repeats that. The network restore keeps
    the block the same way: not B2's.
  - Predicted "a field of an object or of the roulette, not a position":
    held.
- **So B2's open points are closed or not B2's**: the players-block
  difference is the item list's capacity, harmless and shared with the
  network restore; the double claim counted once (0 counts off here; the
  leak soak, where 8.94 saw it, not run again yet); the Lua slope goes the
  network way; the level interpolators are the network load's too.
- **Next**: `soak.sh wwraw map=RR_Opulence` (verified raw snapshots,
  unattended) to name the players-block field and check the counts; then,
  if both are clean or understood, `rollback_rawsnap 1` by default, and the
  sixteen-kart race on Opulence again against 8.121's.
