# Roadmap to a playable alpha

Rewritten 2026-09-21. It replaces the 2026-09-10 original and the three
revision notes that had been stacked on top of it (2026-09-10, 2026-09-10
evening, 2026-09-20). Nothing they said was dropped: their conclusions are
folded into the phases below, and the order is brought up to date. The
evidence for every line lives in `WORLDWIDE.md`; this file only says what is
left, in what order, and what each step has to prove. *Where this starts
from* and *Next, in order* were rewritten twice on 2026-09-30, the second
time from an audit of what an alpha still needs.

The old phase numbering (1 to 7, in `ROLLBACK.md`) was retired on 2026-09-10:
it described an architecture where the authoritative clock ran ahead.

This file, `WORLDWIDE.md` and `COMMANDS.md` are kept **identical** in the public
code repository and in the private notes repository (`docs/` in both). The
docs entry point and `ROLLBACK.md` live in the private notes only.

## Where this starts from

- **Solved, from the client's seat: input lag.** "Ça répond tout de suite"
  (8.36), and none felt at any latency from 0 to 428 ms in the sweep of
  8.107: "parfait".
- **Architecture settled.** `gametic` runs only confirmed tics; the speculation
  runs above it on a snapshot, is kept as it stands when the server confirms
  the inputs it ran (`rollback_keepspec`), and replays the inputs still in
  flight on the tics the server will give them (`rollback_history`, R1), as
  deep as they reach (8.106). A light correction channel replaces the stock
  full-state resend: **0 resends a race**, against 7 to 9 without it.
- **One switch.** A server's `worldwide On` turns all of it on for the
  clients that join it and refuses the others; run end to end (8.97).
- **The confirmed world holds.** 0.000 units and no kart state off in every
  driven race since 8.94, on Skyscraper Leaps and Opulence. The drift's
  mechanisms were found and fixed one by one (8.31, 8.58, 8.59, 8.76, 8.93).
- **The picture.** The stutter Gibax still saw was no interpolation at all
  while a speculation was kept (8.102); fixed, the kart is drawn in even
  steps as without prediction (8.103): "largement plus fluide". The rebuilds
  at a level's start came from predicting on a loopback under a depth floor;
  gone (8.105 to 8.107).
- **The cost.** A kept pass is one tic and one save: about 2 ms on Skyscraper
  Leaps at every latency, 144 frames a second (8.107); 5.6 to 7.6 ms on
  Opulence, 127 to 137 frames a second (8.94, 8.95) -- against 25 to 35 ms
  and about 10 frames a second when every pass was rebuilt (8.62-8.77).
- **Unattended bench.** Six bots move the world without a driver, so a
  measurement can be repeated instead of being n=1.
- **A title screen of its own** (not netcode): a flash brings in WORLDWIDE's
  Earth and ring around the logo, from an optional `data/worldwide.pk3`
  (`968dc2063`).
- **Open:** everything that needs a second human or a real network; the
  `MT_PLAYER` alerts at the join; sixteen karts; the release base. The audit
  of 2026-09-30 orders them below.

## Ground rules for every step

- **No launch without Gibax's explicit go-ahead, every time**: the game, a
  `playtest.sh` scenario, a soak, the bench. Writing code and scenarios is
  free; running them is asked for.
- Measure on a binary verified by its sha. Write the prediction before the run.
  Keep a control in the same session.
- The harness (`playtest.sh`, `soak.sh`, the `*.cfg` scenarios) is versioned in
  the private notes repository (`harnais/`), never the public one: it carries
  local paths. The game folder comes from an environment variable.

## Next, in order

State on 2026-09-30, evening, from an audit of what an alpha still needs.
Items 1 and 2 of the list before it are done -- the stutter (8.102, 8.103)
and the rebuilds before the race (8.105 to 8.107) -- except the join's
alerts, item 1 here; its other items are folded below. **Every launch is
asked for first.**

**Blocking an alpha:**

1. **The `MT_PLAYER` alerts at the join**: explained (8.112).
   - The load of the network archive claims each kart body for
     `players[].mo`, then `P_AddThinker` sets the count back to 0.
   - Every body a rebuild or a join brings back is one reference short,
     which can end in a use after free.
   - Fix pushed (`0412e7760`), **measured at the join** (8.118). Driven
     by Gibax, it gave 0 bodies below zero, against 2 for the build before
     it in the same session.
   - Left: the case in the middle of a race (8.112's tic 1337), which
     neither race reached. Since `rollback_join` (8.119), the client
     scenarios join unattended (`playtest.sh <scenario> join`), so it can be
     looked for over longer and more races.
2. **A second human.** (a) A switch that guesses the bots as a remote human
   is guessed -- their last input repeated -- to see unattended the rebuilds
   and the shaking a remote human would cause: `rollback_botsashuman` and
   the `wwbots` scenario (8.120), run. As an upper bound, 79 to 89% of
   passes rebuild at eight karts, and the bots drawn shake: short steps in
   more than half the frames with a pass, backwards 15 times as often as
   when computed (8.121). (b) Two people on two
   machines, on a LAN. (c) The same over the Internet. (d) One of them
   driving on the host (Phase D). How remote karts are drawn is decided
   after that (Phase D).
3. **A real network in the harness**: jitter and loss -- `rollback_lag` only
   delays. Then R2, the samples filed by sequence number, if R1 slips when
   the server's filing is not steady.
4. **The release base** (*Compatibility*, below): ported onto `v2.4` on
   2026-10-01 (`worldwide-2.4`, WORLDWIDE.md 8.114): 15 conflicts, all
   resolved, the wire read as stock; CI builds it, dev and release (8.115).
   It starts in a stock 2.4 folder since `68f5eb582` (8.117). The stock
   2.4 exe is 32-bit and ours 64-bit, so every case below also crosses the
   two. Left: the bench on it, then against the stock 2.4 exe in the game
   folder -- a stock client refused with a readable message, a WORLDWIDE
   client playing delay-based on a vanilla server, a WORLDWIDE build hosting
   in vanilla mode for stock clients, and the leave putting the settings
   back. None checked yet.
5. **The alpha kit** (Phase F): a zip of the release-config exe on 2.4,
   `worldwide.pk3` and a notice, the GPL and a link to the source, and none
   of Kart Krew's files; how to host (the menu entry exists since
   `89aba69fb`); how to join; what to report and how to send
   `latest-log.txt`; the list of what is known broken; a version label on
   the title in place of the development revision. Before a WORLDWIDE
   server advertises on the public list, read Kart Krew's server-list rules
   for modified builds (the game shows them before hosting publicly).

**Strongly advised before announcing:**

6. **Sixteen karts late in a race** (Phase B's gate), measured up to nine
   only. If it does not fit, the alpha's lobby is capped -- eight -- and
   says so. **Measured on 2026-10-01 and 02** (8.121, 8.125), nobody
   driving, the race's first 1:48: Skyscraper Leaps 6.2 ms a pass at
   sixteen; Opulence 10.2 to 10.7 ms with network snapshots, **6.2 to 6.8
   with B2** (5.7 to 6.6 at fifteen, dedicated) -- under the gate. **To
   the race's end** (8.126, fifteen, dedicated): 5.9 to 7.5 ms a pass in
   every window, under the gate. But during a Windows Update install a
   cascade of rebuilds for this machine's own idle input, its stamp
   replayed one off, took windows to 12 and 18 ms -- the second time
   (00:35, 8.125). Left: the cascade -- its mechanism read, a first fix,
   `rollback_fill`, measured worse (8.129), a second, `rollback_ontime`,
   which never ran for want of a live clock (8.130) and, on one, **breaks
   the loop** (8.131: 16 rebuilds after seven stalls against 128, the
   windows under the gate), but its stamps made twins in a normal race,
   one window over the gate; with stamps that step, **the race to its end
   held the gate in every window** (8.132: 5.6 to 6.6 ms, one rebuild).
   Left: `rollback_ontime` on by default, the cap on a rebuild's cost (C),
   and sixteen with a host to the end.
   Sixteen needs a host (a dedicated server has 15 slots, and upstream's
   code stops if bots take them all).
7. **The history's cap** (8.107): past about 340 ms of round trip,
   `rollback_history 12` leaves the drawn world behind the newest input
   (about 5 tics at 428 ms). Raise it (up to 34) or set it from the round
   trip; a rebuild then goes deeper. A worldwide lobby will have such
   players.
8. **Breadth, as far as the alpha's scope** (Phase C): items used on purpose
   (the roulette under speculation), and two or three more maps driven --
   water, polyobjects, executors. Battle, Grand Prix and Encore stay out of
   the alpha unless they are run.

**Order of work:** 1 and 2(a), which need nobody; 3; 4; 2(b) to 2(d) with a
second person; 6 and 7; 5; then the announcement.

**Branches** (Gibax, 2026-10-01). Work goes on `rollback-netcode`.
`worldwide-2.4` is the public alpha's branch, on the 2.4 release. It is
**not** kept up to date as work goes on: once the phases before the alpha
are done, it is brought up to date from `rollback-netcode` for the alpha.
The same goes for side work: `azerty`, tested by Gibax (AZERTY in the
console), is merged into `rollback-netcode` (`1fcef131b`), and reaches the
alpha with that update. `azerty-2.4`, its 2.4 build, was already merged into
`worldwide-2.4` on Gibax's word (fast-forward to `0a9877dd1`). Since
`68f5eb582`, `worldwide-2.4` also starts in a stock 2.4 folder, which is
32-bit, without `-noexchndl` (WORLDWIDE.md 8.117). **Proposed scope** (the
audit's, not decided): Race only, Windows, eight players at most unless 6
says sixteen, and the known-broken list stated up front.

**Then, not blocking:**

9. **B2 on by default and R1's gaps.** B2: on by default (8.125), its open
   points closed or not B2's (8.124); the sixteen-kart race on Opulence
   (8.125) and the leak soak again (8.127, 0 counts off) done. R1:
   the depth from the tics R1 gives, and the instrument counting the same way
   (⚠ under 8.89; the measuring machine's `wip/histgaps` as a reference).
10. **Small, seen**: "`*Guest entered the game.`" printed more than once a
    join on the client -- 1 to 5 times, 17 at 15 tics, each rebuild moving
    the join later (8.109, 8.112); the drawn kart's long steps, 430 to 760 a
    window against about 263 without prediction (8.103, 8.107);
    `rollback_keepearly`, off, to remove or keep; a predicting client
    records no replay (8.96); joined through `rollback_join`, the client
    prints no "entered the game" line at all (8.119); a kart that joined but
    is not driven rebuilds far more than a driven one (8.119, not read).
    (The title's banner, pixelated, is gone with
    Gibax's second ring, drawn at the game's size, 8.116.)
11. **Left open**: the Garden Top rider (8.56); Coastal Temple's resim
    failures (8.57); `chainorder_block` (8.58); a sound cut when a
    speculated tic removes its object (8.73); the network load not counting
    a delayed executor's caller (8.88); the slow save of 8.78 (2.5 to 2.9 ms
    since 8.84, never bisected); the correction-rate sweep
    (`rollback_correct 8`, `16`, `35`), owed since 2026-09-10; Phase A's
    first *Done when* clause.

---

## Phase A -- Understand the drift

**No longer the gate for the alpha.** The correction channel absorbs the
divergence (0 resends, mean residual 0.13-0.86 units in the off/on/off
races, a kart being about 40 wide). Phase A is now what lowers the correction
rate and the residual, and what makes the stock consistency check agree
again. **On Skyscraper Leaps it does both: 0.000 units and 0 refusals with
`rollback_cleancmds` on throughout (8.44, 8.45).**

**Excluded, each by measurement:** the restore (0 contamination over 1400 round
trips, with and without bots); the archive (0/522 leak checks after the
roulette fix, 8.15; 1/260 after the second one, 8.34); tic determinism (0/330
resim checks); the synchronised RNG as a cause (it parts only after positions
have, 8.2); the damage path (same hits, same hashes, 8.8); the confirmed clock
running on a guess (8.9, 8.17, 8.20); late resends (0 arrivals, 8.17).

**Fixed, and the source on Skyscraper Leaps (8.31, 8.33, 8.35-8.38, 8.44,
8.45):** the speculation started on a tic the server had already sent -- the
netticbuffer reserve stopped the confirmed loop one short -- and overwrote the
local player's input in it with the current one, and every bot's input with
one recomputed from the client's world; the next pass ran that tic as
confirmed. `rollback_cleancmds` fixes it (64-85% wrong-input tics down to
0-0.1%), and is on by default. With it on from the first tic, a driven race
reads 0.000 units, 0 refusals, and blame samples identical on both machines
(8.44). The same-session control shows the order in which the worlds part
without it: the driven kart first, by thousandths of a unit, then a bot, then
the seed sum, which the channel never repairs (8.45). The off/on/off races
could not measure it, as 8.38 said: their on windows inherited the off
windows' divergence.

**Measured on two maps, driven** (8.40): Skyscraper Leaps, which has no
water, no polyobject, no linedef executor and no ACS, and Opulence, which has
dynamic slopes and 3700 objects. Five more are soaked only (8.51-8.62).

**Three more leaks, found and fixed:** the plane of a dynamic slope was never
archived, so the first tic after every restore steered the karts on a plane
some tics ahead (8.58); a reload never reset polyobject translucency and
flags (8.59) -- both measured (8.59, 8.62); and the correction channel's
put-back reordered collisions on Opulence (8.76), fixed and measured at
0.000 (8.77). Raw snapshots brought the slope planes back once, fixed again
(8.90-8.94).

**The relabel histogram's `+2` cluster** (8.27), read in both races of
2026-09-23: it falls before the measurement windows -- the host's before
`rollback_correct` is set, the client's before its `rollback_lag 6` --
harmless on the counts. 8.32's split mislabels it, because the waiting map
`RR_TESTRUN` is a level (8.44, 8.45).

**If another map drifts where Skyscraper Leaps does not, the next
instrument:** hash the program's global memory (the exe's `.data`/`.bss`)
just before a speculation and just after the restore, narrow a difference down
to an address, name it with the `.pdb` -- the blind spot every archive-based
check shares, since none of them look outside the archive.

**Done when:** zero `Game state reloaded` with the resend **not** suppressed
(`rollback_correct N 0`) over five unattended races and two driven ones -- or,
failing that, the drift explained down to a mechanism and its residual stated.
**Status on 2026-09-30:** the second is met on Skyscraper Leaps (mechanism
8.31, residual 0.000, 8.44-8.45) and on Opulence (8.77, 8.94). The first has not been run. It is now
expected to pass -- a race whose checksum never disagrees never fires a
resend -- which makes it a cheap confirmation, and the form the other maps'
check could take.

---

## Phase B -- Make it fit at a real grid

**The gate for the alpha.** Every cost figure is two to nine karts, mostly early
in a race. A Ring Racers grid is sixteen, and a snapshot grows from 120 KiB at
the start to 318 KiB three minutes in.

Measured: 4.94 ms a pass at two karts, 8.33 at eight, **9.5 ms at nine with
somebody driving -- 33% of a 28.6 ms tic**, already past the line below. The
restore alone was 8.6 ms at sixteen karts late in a race. **Assume it does not
fit, and measure.**

Measured on 2026-09-28 at nine karts on Opulence, 3700 objects (`WORLDWIDE.md`
8.62 to 8.77): a pass rebuilt every tic, 25 to 35 ms; kept by
`rollback_keepspec`, one tic and one save, 9 ms. The map's decorations, not
the karts, are 83% of a tic. Driven on 2026-09-29, with R1 and B2 (8.92,
8.94, 8.95): 99% of passes kept or more, a pass of 6.9 to 7.6 ms, 5.6 to 6.4
with raw snapshots -- one tic (about 4.2 ms) and one save (1.1 ms).

**A cheap lever first, found by reading** (`WORLDWIDE.md` 8.30):
`P_RelinkPointers`, 4.7 ms of an 8.6 ms restore, resolved every pointer with a
linear scan of all mobjs. **Now indexed** by `mobjnum` (2026-09-21), same
answer, and **measured under 0.1 ms** (8.34).

⚠ **`rollback_history` pulls the other way** (8.39): replaying the inputs in
flight takes the speculation from 4 tics to about 8 at 171 ms, an estimated 14
to 16 ms a pass at nine karts, and it grows with latency. With
`rollback_keepspec`, only a rebuild pays for the depth, and in a race with R1
there are 0 to 1 a window of 1000 tics (8.92, 8.99).

**Done: B2**, raw snapshots of the level pools (8.81-8.95): a save from 2.9
to 1.1 ms, a restore from about 6.5 to 1.9 ms, off by default until item 9
of *Next, in order*. **The next lever is the tic itself.**

**Then two structural levers, in this order:**

1. **Predict less.** Odamex restores one player and the moving sectors, not two
   thousand objects; Rocket League separates the car from the ball. The larger
   win, and it also delivers partial correction, which the design asks for.
2. **Amortise.** NetPlus re-simulates only every N live tics
   (`cv_siminaccuracy`), trading freshness for a smoother CPU profile.

**Done when:** a pass fits in **30% of a tic** at sixteen karts, late in a race,
at the depth Phase D settles on.

---

## Phase C -- Breadth under prediction

**Needs B**: testing feel and correctness through a stutter tells you about the
stutter.

Not yet run under prediction, or not read on purpose:

- a full race, start to finish: one ran to its end in WORLDWIDE mode (8.97);
  the grid, the finish line and the results screen not read on purpose;
- **Battle**, **Encore**, **Grand Prix** (grid hardcoded to eight, bots run
  differently);
- items and respawns used on purpose -- the soak replays frozen inputs and is
  blind to anything edge-triggered;
- maps with what the test map lacks: water, polyobjects, linedef executors, ACS
  (`harnais/maps.py` lists them; shortlist in `WORLDWIDE.md` 8.40). Seven
  maps soaked (8.51-8.62); driven, only Skyscraper Leaps and Opulence. The
  harness runs any scenario on any map (`map=<lump>`).

**Item policy:** do not predict the roulette's result. Let the reel spin under
speculation and commit the pick on a confirmed tic (`WORLDWIDE.md` §4). Prove it
with a deliberately driven item test, not with the soak.

**Done when:** each runs with no resends and no divergence that has not been
read and understood.

---

## Compatibility and capability advertising

**Policy, decided by Gibax on 2026-09-21: the server decides.**

- A server in **WORLDWIDE mode** runs client-side prediction and the correction
  channel, and accepts **WORLDWIDE clients only**.
- A **vanilla server** runs the stock delay-based netcode. A WORLDWIDE client may
  join it and then behaves **exactly as a vanilla client**.

This supersedes every earlier statement that stock compatibility was "abandoned"
(`WORLDWIDE.md` 8.4, 8.14) or "already satisfied" (this file before
2026-09-21).

**Already there, and to keep:**

- `packetversion`, `version`, `subversion` and `application` are untouched, so
  both kinds of server stay visible to both kinds of client in the browser
  (`d_clisrv.c:1681-1691`). Keep it that way.
- `PT_STATECORRECTION` is appended at the end of the packet enum
  (`d_clisrv.h:144`), so no stock packet number moved.
- `serverinfo_pak.kartvars` is a flag byte with three free bits (`0x04`, `0x08`,
  `0x10`), exchanged before any join; a vanilla server reports them unset.
- `askinfo_pak.time` already measures the ping before joining.

**Fixed in code, not yet verified:** the savegame. The roulette fields of 8.14
were written unconditionally, so a WORLDWIDE build and a stock one would pass
the version check and then misread each other's savegame by 16 bytes a player
(`WORLDWIDE.md` 8.28). They are now local-only, and the savegame is stock
grammar again (8.29).

**A prerequisite nobody had written down** (8.30): CI builds are `DEVELOP`, so
their `VERSION`/`SUBVERSION` are 0 and they cannot see a public server at all;
and the branch sits on upstream's development line (`v2.4-106`), not on a
release tag. "A WORLDWIDE client on a vanilla server" needs a release-config
build on the release base the public servers run.

**The work, in order:**

Steps 2 to 6 are done as WORLDWIDE mode (`WORLDWIDE.md` 8.80), and run end to
end (8.96, 8.97).

1. ~~Roulette fields in local snapshots only~~ -- done in code (8.29).
2. ~~**One server-side meaning of "WORLDWIDE mode".**~~ `worldwide On` on the
   server (8.80).
3. ~~**Advertise it.**~~ The `SV_WORLDWIDE` bit (`0x04`) in `kartvars`.
4. ~~**Refuse vanilla clients cleanly on a WORLDWIDE server.**~~ A client
   declares itself by five bytes after the stock join request, which a stock
   server ignores; a join without them gets a readable refusal (8.97, with
   `rollback_vanillajoin` standing in for a stock client).
5. ~~**Switch the client automatically.**~~ At the join, by the bit; undone
   when it leaves (the leave never checked).
6. ~~`K_RollbackPays()` asks the mode~~, through the correction rate the mode
   sets.
7. Optionally, a few bytes appended to `serverinfo_pak` (depth, correction rate)
   so the pre-join delay menu can recommend a value -- read against the length
   that actually arrived, never past it.

**Must not:** touch the four version fields, or read an appended field without
checking the packet was long enough to carry it.

**Done when:** a WORLDWIDE client joins a vanilla server and plays delay-based
without incident; joins a WORLDWIDE server and predicts; a vanilla client is
refused by a WORLDWIDE server with a readable message; a WORLDWIDE build hosting
in vanilla mode accepts vanilla clients. **Status on 2026-09-30:** the second
clause holds (8.97), and the third with a WORLDWIDE build standing in for a
stock one. Left: a real stock client, a vanilla server -- which needs the
release base -- step 7, a menu entry and a mark in the server browser.

---

## Client-local input delay knob (small, not scheduled)

A dial the **player** sets, kept entirely local: how many tics of buffer to hold
between their input and what gets simulated, even while two-clock covers the
round trip. GGPO calls it a local delay frame; it is the honest option for a
player on a bad line who prefers a stable picture to the last 60 ms.

**It must never reach the wire as `wantdelay`.** That was the original
`cv_mindelay` bug: the client asked the server to hold its own input, then could
not predict it. Closed by `K_RollbackPays()` (client side 2026-09-14, host side
2026-09-20 -- see `COMMANDS.md`, `rollback_twoclock`). Reusing the `cv_mindelay`
slider is an option, not a given.

**Depends on:** nothing but the two-clock pivot. **Done when:** a player can
hold N tics of their own buffer under two-clock, and the packet -- not the
setting -- shows no `wantdelay`.

The pre-join menu that offers this knob with a recommended value is step 7 of
the compatibility section.

---

## Phase D -- Feel

**Needs C**, and no log can finish it.

- **The stutter** (8.99): "le kart avait toujours l'impression de rollback
  très très légèrement". It was no interpolation at all while a speculation
  was kept (8.102); fixed, "largement plus fluide", the kart drawn in even
  steps as without prediction (8.103). Left: a few more long steps than
  without prediction (*Next, in order*, item 10).
- **Correction smoothing** (`rollback_smooth`): written, never measured ("maybe
  ça marche"). Since 8.94 no correction moves a kart in a driven race; it
  matters again with remote humans. Measure it against a control in the same
  session, or drop it.
- **Remote karts**: re-predicted every pass, a remote human guessed by
  repeating their last input. Smooth or jittery is for eyes to say, in a
  race with two people. If it jitters, two answers to weigh: smoothing the
  error toward each correction, or entity interpolation (Gambetta; Odamex's
  `cv_netsteadyplayers`, `histx/y/z`) -- remote karts drawn a little in the
  past between confirmed states, smooth but shown where they were, not where
  collisions are resolved, which a racing game feels. Lag compensation does
  not apply: the simulation is a deterministic lockstep, the confirmed world
  decides, and nobody's view can be rewound for them.
- **The depth**: "parfait" at every latency of the sweep, 0 to 428 ms
  (8.107); past about 340 ms the history's cap decides it (*Next, in order*,
  item 7). Which lead feels best feeds back into B.
- **The inputs still in flight** (`rollback_history`, 8.39): the speculation
  replays every input sent but not yet applied, instead of repeating the
  newest, so quick flicks and releases are drawn as the server will play
  them. Built, with R1 (8.89, 8.92), and on in WORLDWIDE mode; the driver
  felt it "mieux" (8.46, 8.50). Holds the drawn tic's lead over the clock so the
  picture does not judder (8.41), and counts the drawn world's jumps, as a
  control with the switch off too.
- **Somebody hosts and judges.** Every reactivity verdict so far was given from
  the client's seat, and the host was paying 170-200 ms until 2026-09-20. The
  bench cannot stand in for this.

**Done when:** a person prefers it to the control on three races, at a stated
latency, **at least one of them while hosting**.

---

## Phase E -- Capability check

**Needs B and D.** A short calibration run in the menus that measures restore and
replay cost on the player's machine and answers the only question a player has:
can this computer run it without stuttering. It proposes a depth; it does not
print microseconds.

**Done when:** "it stutters for me" arrives as a number and a recommended
setting.

---

## Phase F -- Alpha

**Needs all of the above.** What is missing is not code:

- a downloadable build (the CI already makes one per commit);
- a short list of what to report, in a player's words;
- a way to collect logs without asking for file paths;
- a statement of what is known broken, so reports are about the rest.

*Next, in order*, item 5, spells the kit out, with the audit's proposed
scope.

**Done when:** somebody who is not Gibax has played it and reported something
useful.

---

## Backlog -- latent defects, not blocking

- `botvars.diffincrease` is `int16_t` but archived with `WRITEUINT8`/`READUINT8`
  (`p_saveg.cpp:867`, `:1635`). Grand Prix only, between rounds.
- `K_HandleLapIncrement` reads `old_x`/`old_y` as simulation after a restore
  (`WORLDWIDE.md` 8.3). (`old_z` not being restored is fixed, 2026-09-21.)
- ~~Out-of-bounds read in the bot-overwrite search~~: fixed 2026-09-21.
- On Windows `latest-log.txt` ignores `-home`/`-logdir`, so two instances in one
  folder share a log (`ROLLBACK.md`, two-instance harness). Upstream code.
- **No upstream reporting** (Gibax, 2026-09-21): bugs in upstream code are not
  reported to Kart Krew. They are fixed here only when they hurt WORLDWIDE.
- ~~The harness is not versioned~~: versioned in the private notes, 2026-09-21.
- A predicting client records no replay (option A, 8.96). A kept tic could be
  written when it is kept, later.

---

## Not on the path

**Labelled inputs with a server-side input buffer.** Planned when the old loop
carried its prediction forward; the two-clock pivot removed the problem it was
for. Under the compatibility policy a WORLDWIDE server may change the wire
between WORLDWIDE peers, so this is no longer a compatibility question -- it is
a cost with nothing to buy. Reopen it only if Phase D shows remote karts need
real state rather than prediction. R2 (*Next, in order*, item 3) takes back a
narrow part of it -- the samples numbered, so the client knows which tic the
server filed each on -- with no server-side buffer.

**State streaming à la Odamex.** A different netcode, not a bigger version of
this one: per-entity deltas plus relevance, on the scale of everything done so
far. Only worth revisiting if D fails in a way B cannot pay for.

## Risks

- **Phase A may not be one field**: "state the archive does not carry" is a
  family, and the live lead may be an instrument artefact.
- **Phase B may have no answer at sixteen karts** on ordinary hardware. Phase E
  exists to say so out loud.
- **Most numbers in the journal are n=1.** The bench fixes that going forward, not
  retroactively.
- **A person is required for D**, and for every judgement of feel.
- **No second human has played it yet.** Every driven race is one person
  against bots; a remote human is guessed, and how that looks and what it
  costs is unknown (*Next, in order*, item 2).
