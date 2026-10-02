# Roadmap to a playable alpha

Rewritten 2026-09-21. It replaces the 2026-09-10 original and the three
revision notes that had been stacked on top of it (2026-09-10, 2026-09-10
evening, 2026-09-20). Nothing they said was dropped: their conclusions are
folded into the phases below, and the order is brought up to date. The
evidence for every line lives in `WORLDWIDE.md`; this file only says what is
left, in what order, and what each step has to prove. *Where this starts
from* and *Next, in order* were rewritten twice on 2026-09-30, the second
time from an audit of what an alpha still needs, and again on 2026-10-02
from a second audit; their item numbers are kept, since `WORLDWIDE.md` and
the harness cite them.

The old phase numbering (1 to 7, in `ROLLBACK.md`) was retired on 2026-09-10:
it described an architecture where the authoritative clock ran ahead.

This file, `WORLDWIDE.md` and `COMMANDS.md` are kept **identical** in the public
code repository and in the private notes repository (`docs/` in both). The
docs entry point and `ROLLBACK.md` live in the private notes only.

## Where this starts from

State on 2026-10-02.

- **Solved, from the client's seat: input lag.** "Ça répond tout de suite"
  (8.36), and none felt at any latency from 0 to 428 ms in the sweep of
  8.107: "parfait".
- **Architecture settled.** `gametic` runs only confirmed tics; the speculation
  runs above it on a snapshot, is kept as it stands when the server confirms
  the inputs it ran (`rollback_keepspec`), and replays the inputs still in
  flight on the tics the server will give them (`rollback_history`, R1), as
  deep as they reach (8.106). A light correction channel replaces the stock
  full-state resend: **0 resends a race**, against 7 to 9 without it. A long
  pass still sends a sample each real tic, its stamps stepped
  (`rollback_ontime`, 8.130 to 8.133).
- **One switch.** A server's `worldwide On` turns all of it on for the
  clients that join it and refuses the others; run end to end (8.97).
- **The confirmed world holds.** 0.000 units and no kart state off in every
  driven race since 8.94, on Skyscraper Leaps and Opulence. The drift's
  mechanisms were found and fixed one by one (8.31, 8.58, 8.59, 8.76, 8.93).
  0 kart bodies below zero in every race since the `MT_PLAYER` fix
  (8.112, 8.118).
- **The picture.** The stutter Gibax still saw was no interpolation at all
  while a speculation was kept (8.102); fixed, the kart is drawn in even
  steps as without prediction (8.103): "largement plus fluide". The rebuilds
  at a level's start came from predicting on a loopback under a depth floor;
  gone (8.105 to 8.107).
- **The cost, at a real grid.** A kept pass is one tic and one save. With raw
  snapshots on by default (B2, 8.125), **fifteen karts on Opulence,
  dedicated, hold Phase B's gate to the race's end**: 5.6 to 7.3 ms a pass,
  at most 25% of a tic (8.132, 8.133); sixteen with a host, 6.2 to 6.8 ms
  over the race's first 1:48 (8.125). On this machine, a Ryzen 5 5600X.
- **The cascade, fixed.** A stall of the client -- an operating system's, a
  long rebuild -- set off rebuilds that fed themselves for minutes (8.125,
  8.126): the server files a sample when it arrives, one sample for a gap
  moved its filing, and the replay of this machine's own input ran one off
  (8.129). With `rollback_ontime`, on in WORLDWIDE mode since `8c9dd904e`,
  seven stalls of 100 ms leave no chain (8.131).
- **Unattended bench.** Bots move the world, `rollback_join` puts the client
  in the race, and a race runs to its end by itself (the idle kart is timed
  out); a stall on demand (`rollback_stall`) and a smaller machine on this
  one (`rollback_slowtic`) can be had.
- **A face of its own** (not netcode): a title screen with WORLDWIDE's Earth
  and ring from an optional `data/worldwide.pk3` (8.113, 8.116), and the
  window's title and icon (8.122).
- **Open:** a second human and a real network; the compatibility cases
  against a stock 2.4; smaller machines; sixteen karts with a host to the
  end; the join's chat line; the alpha kit. Ordered below.

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

State on 2026-10-02, from a second audit. The numbers are the 2026-09-30
audit's, kept because `WORLDWIDE.md` and the harness cite them; items 12 and
13 were added. **Every launch is asked for first.**

**Blocking an alpha:**

1. ~~**The `MT_PLAYER` alerts at the join**~~ -- explained (8.112), fixed
   (`0412e7760`), **measured at the join** (8.118): 0 bodies below zero
   against 2 for the build before, and in every race since, the unattended
   joins of `rollback_join` included. Left, not blocking: the case in the
   middle of a race (8.112's tic 1337), which no race has reached.
2. **A second human.** (a) Done: `rollback_botsashuman` and the `wwbots`
   scenario guess the bots as a remote human is guessed (8.120) -- as an
   upper bound, 79 to 89% of passes rebuild at eight karts, and the bots
   drawn shake: short steps in more than half the frames with a pass,
   backwards 15 times as often as when computed (8.121). (b) Two people on
   two machines, on a LAN. (c) The same over the Internet. (d) One of them
   driving on the host (Phase D). How remote karts are drawn is decided
   after that (Phase D).
3. **A real network in the harness**: jitter and loss -- `rollback_lag` only
   delays. The cascade showed what they do (8.129): the server files a
   sample on the tic it arrives, so a late or bunched one moves the filing,
   and the replay runs one sample off until the anchor catches up.
   `rollback_ontime` stops this machine's own stalls doing it (8.131); the
   network's jitter is left. Then R2, the samples filed by sequence number --
   a change to what a WORLDWIDE server does with a WORLDWIDE client, which
   the compatibility policy allows -- if R1 slips under it.
4. **The release base and its compatibility cases** (*Compatibility*,
   below): `worldwide-2.4`, ported onto `v2.4` (8.114), built by the CI, dev
   and release (8.115), starting in a stock 2.4 folder since `68f5eb582`
   (8.117), and **resynced from `rollback-netcode` at `8c9dd904e` on
   2026-10-02** (8.135, `b3c6cbb7d`, CI green). **The four cases checked
   against the stock 2.4 exe** (32-bit, ours 64-bit; 8.135): a stock client
   refused by a WORLDWIDE server; a WORLDWIDE client on a stock server,
   switched to the stock netcode, in the race; a WORLDWIDE build hosting in
   vanilla mode for a stock client; the leave putting the switches back --
   and a WORLDWIDE race on the 2.4 release, 1.5 ms a pass. Left: the refusal's
   text seen on screen (the stock 2.4 shows its photosensitivity warning at
   every start, over it), and a stock client in the race rather than
   watching (2.4 has no console command to join).
5. **The alpha kit** (Phase F): a zip of the release-config exe on 2.4,
   `worldwide.pk3` and a notice, the GPL and a link to the source, and none
   of Kart Krew's files; how to host (the menu entry exists since
   `89aba69fb`); how to join; what to report and how to send
   `latest-log.txt`; the list of what is known broken; a version label on
   the title in place of the development revision. Before a WORLDWIDE
   server advertises on the public list, read Kart Krew's server-list rules
   for modified builds (the game shows them before hosting publicly).
   `worldwide-2.4` is brought up to date again first if `rollback-netcode`
   has moved on.

**Strongly advised before announcing:**

6. ~~**Sixteen karts to the race's end**~~ (Phase B's gate): **held** --
   sixteen with a host on Opulence, to the race's end, 5.9 to 7.0 ms a
   pass, at most 24.5% of a tic, one rebuild in the race (8.136); fifteen,
   dedicated, likewise (8.133). Sixteen needs a host: a dedicated server
   has 15 slots, and upstream's code stops on an assert if bots take them
   all (its dump read, 8.128).
7. **The history's cap** (8.107): past about 340 ms of round trip,
   `rollback_history 12` leaves the drawn world behind the newest input
   (about 5 tics at 428 ms). Raise it (up to 34) or set it from the round
   trip; a rebuild then goes deeper. A worldwide lobby will have such
   players.
8. **Breadth, as far as the alpha's scope** (Phase C): items used on purpose
   (the roulette under speculation), and two or three more maps driven --
   water, polyobjects, executors. Battle, Grand Prix and Encore stay out of
   the alpha unless they are run.

**Then, not blocking:**

9. **R1's gaps.** B2 is done: on by default (8.125), its open points closed
   or not B2's (8.124), the sixteen-kart race on Opulence (8.125) and the
   leak soak again (8.127, 0 counts off). R1: the depth from the tics R1
   gives, and the instrument counting the same way (⚠ under 8.89; the
   measuring machine's `wip/histgaps` as a reference).
10. **Small, seen**: "`*Guest entered the game.`" printed more than once a
    join on the client -- 1 to 5 times, 17 at 15 tics, each rebuild moving
    the join later (8.109, 8.112), the other face of item 12; the drawn
    kart's long steps, 430 to 760 a window against about 263 without
    prediction (8.103, 8.107); `rollback_keepearly`, off, to remove or keep;
    a predicting client records no replay (8.96); a kart that joined but is
    not driven rebuilt far more than a driven one (8.119) -- most likely its
    stamp-only differences, which the stepped stamps of 8.132 change; to
    read again.
11. **Left open**: the Garden Top rider (8.56); Coastal Temple's resim
    failures (8.57); `chainorder_block` (8.58); a sound cut when a
    speculated tic removes its object (8.73); the network load not counting
    a delayed executor's caller (8.88); the slow save of 8.78 (2.5 to 2.9 ms
    since 8.84, never bisected); the correction-rate sweep
    (`rollback_correct 8`, `16`, `35`), owed since 2026-09-10; Phase A's
    first *Done when* clause.

**Added on 2026-10-02:**

12. ~~**The join's chat line**~~ -- fixed (`526de71e6`, 8.136): with the
    speculation kept, a line is written once, by the first run that has it,
    whatever the tic; the join through `rollback_join` now shows its
    "entered the game" once, where it showed none (8.128).
13. **Smaller machines** (strongly advised): a rebuild re-runs about eight
    tics, a hitch the size of eight of a machine's tics. C, a budget on a
    rebuild's cost, and a smaller machine on this one to measure it against
    (`rollback_rebuildbudget`, `rollback_slowtic`, 8.134), **measured**: on a
    machine 4 ms a tic slower, `rollback_ontime` holds (no cascade, 10.6 to
    12.6 ms a pass), and a 20 ms budget takes the hitches over 50 ms from 45
    to 15, for 187 moves of the drawn world back against 8. Off by default:
    which is better is for eyes on the laptop, the Steam Deck and the Steam
    Machine (Gibax, during the alpha). Phase E's calibration is the long
    answer.

**Order of work:** 4's cases, 6, 12 and 13's measurement are done (8.134 to
8.136); then 3; 2(b) to 2(d) with a second person, a LAN first (8.135's
loopback note); 7; 5; then the announcement.

**Branches** (Gibax, 2026-10-01). Work goes on `rollback-netcode`.
`worldwide-2.4` is the public alpha's branch, on the 2.4 release, brought up
to date from `rollback-netcode` for the alpha rather than as work goes on --
done once on 2026-10-02, on Gibax's word ("oui, l'étape 4"), at `8c9dd904e`
(8.135), and again before the kit if `rollback-netcode` has moved on. Side
work reaches it the same way: `azerty`, tested by Gibax (AZERTY in the
console), is merged into `rollback-netcode` (`1fcef131b`); its 2.4 build
(`0a9877dd1`) was merged into `worldwide-2.4` on Gibax's word. Since
`68f5eb582`, `worldwide-2.4` starts in a stock 2.4 folder, which is 32-bit,
without `-noexchndl` (8.117). **Proposed scope** (the audit's, not decided):
Race only, Windows, eight players at most unless 6 says sixteen, and the
known-broken list stated up front.

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
to 1.1 ms, a restore from about 6.5 to 1.9 ms, on by default since 8.125.
**The next lever is the tic itself.**

**Then two structural levers, in this order:**

1. **Predict less.** Odamex restores one player and the moving sectors, not two
   thousand objects; Rocket League separates the car from the ball. The larger
   win, and it also delivers partial correction, which the design asks for.
2. **Amortise.** NetPlus re-simulates only every N live tics
   (`cv_siminaccuracy`), trading freshness for a smoother CPU profile.

**Done when:** a pass fits in **30% of a tic** at sixteen karts, late in a race,
at the depth Phase D settles on.
**Status on 2026-10-02:** **met at sixteen karts**, with a host, on
Opulence, to the race's end -- 5.9 to 7.0 ms, at most 24.5% (8.136) -- and
at fifteen, dedicated (8.132, 8.133), on this machine. Left: the depth Phase
D settles on; a smaller machine pays more for every tic (11 to 13 ms a pass
4 ms a tic slower, 8.134, item 13).

---

## Phase C -- Breadth under prediction

**Needs B**: testing feel and correctness through a stutter tells you about the
stutter.

Not yet run under prediction, or not read on purpose:

- a full race, start to finish: several ran to their end in WORLDWIDE mode,
  nobody driving, the idle kart timed out and the results, the vote and the
  next map after (8.97, 8.126, 8.132, 8.133); the grid, the finish line and
  the results screen not read on purpose;
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
**On 2026-10-02:** the release base exists and is resynced (*Next, in
order*, item 4); the menu entry exists (`89aba69fb`); the four clauses of
*Done when* checked against the stock 2.4 exe (8.135), the refusal's text
on screen excepted. Left: step 7 and a mark in the server browser.

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
- **Phase B holds at fifteen karts on this machine** (a Ryzen 5 5600X); a
  smaller one pays more for every tic, and a rebuild's eight tics with it.
  Item 13 and Phase E exist for that.
- **The server files a sample by when it arrives** (8.129): a real network's
  jitter moves the filing, and the replay runs one off for a round trip.
  `rollback_ontime` covers this machine's own stalls only; item 3.
- **Most numbers in the journal are n=1.** The bench fixes that going forward, not
  retroactively.
- **A person is required for D**, and for every judgement of feel.
- **No second human has played it yet.** Every driven race is one person
  against bots; a remote human is guessed, and how that looks and what it
  costs is unknown (*Next, in order*, item 2).
