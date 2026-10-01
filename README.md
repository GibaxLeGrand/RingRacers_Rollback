# Ring Racers Worldwide

**A client-side prediction netcode for [Dr. Robotnik's Ring Racers](https://www.kartkrew.org).**
An unofficial fork, maintained by Gibax, on the branch `rollback-netcode`.

<p align="center">
  <img src="docs/RRW_logo.png" width="238" alt="Ring Racers Worldwide logo">
</p>

> **Experimental, not ready for public play.** Every feature below sits behind
> a switch that is off by default, most measurements come from one driver on
> one machine, and the known limits are listed under
> [Where it stands](#where-it-stands). This fork is not affiliated with or
> endorsed by Kart Krew Dev: please do not report its bugs to them.

## Why

Stock Ring Racers plays online in lockstep: nobody's input is applied until
the server has it, so every player feels their own ping as input delay. Ring
Racers Worldwide keeps the server authoritative and the game deterministic,
and hides the round trip instead: your kart answers your input on the next
tic, the world you see is a prediction a few tics ahead of what the server has
confirmed, and the server's word corrects it.

It is **client-side prediction with server reconciliation**, not GGPO-style
peer-to-peer rollback: the game has an authoritative server and a consistency
check, and this design keeps both.

The goal is a community one: letting players race on servers far from them
-- a European on an American server, for a start -- in a game as demanding as
Ring Racers.

## How it works

- **Two clocks.** The confirmed world (`gametic`) runs only the tics the
  server has confirmed, in the stock lockstep, untouched. On top of it, a
  *speculation* runs a few tics ahead from a snapshot of the confirmed world,
  with your own inputs applied at once and everyone else's guessed. The
  speculation is what you see.
- **Snapshots and restores.** The confirmed world is saved to an in-memory
  ring of snapshots and restored before each confirmed tic, so the
  speculation never leaks into it. A large part of the work has been finding
  and fixing every piece of state a restore did not put back exactly.
- **Guessing the others.** Bots are recomputed from the local world, which is
  exact; a remote person's last input is repeated until the real one arrives.
- **A light correction channel.** Instead of the stock full-state resend
  (hundreds of KB and a visible hitch when a client parts from the server),
  the server sends each client a small packet with every kart's state a few
  times a second.
- **One server switch.** A server started with `worldwide On` advertises the
  mode, runs the correction channel, and accepts WORLDWIDE clients only. A
  WORLDWIDE client switches its prediction on or off by what the server it
  joins advertises: against a stock server it plays the stock netcode.

## What this fork adds

| Feature | Console switch | State |
|---|---|---|
| Two-clock prediction, own input applied at once | `rollback_twoclock` | measured, off by default |
| Speculation that never writes over tics already received | `rollback_cleancmds` | measured, **on** by default |
| Light state-correction channel in place of full resends | `rollback_correct` (server) | measured |
| Karts already where the server has them left alone | -- | measured |
| Replaying your inputs still in flight across the speculation | `rollback_history` | measured, off by default |
| Keeping the speculation when the server confirms it | `rollback_keepspec` | measured, off by default |
| No fixed input delay for the host or clients while predicting | -- | measured |
| WORLDWIDE mode: one server switch, clients follow, stock clients refused | `worldwide` (server) | built, not yet run |
| Raw snapshots of the level's object pools (track B2) | `rollback_poolcopy` | in progress |
| Restore fixes: item roulette, polyobjects, dynamic slopes, unlock progress, ACS references, sounds kept across restores, and more | -- | measured on seven maps |
| Diagnostics: snapshot round-trip and leak soaks, drift and blame logs, per-step cost of a pass and of a save | `rollback_test`, `rollback_soak`, `rollback_drift`, ... | in use |

The full list, with what each switch does, is in
[docs/COMMANDS.md](docs/COMMANDS.md).

## Where it stands

Measured so far, on a two-instance setup with a simulated 171 ms of latency,
one human driver and bots:

- **Input lag is gone** from the client's seat.
- **The confirmed world stays exact**: 0.000 units of drift on every kart
  sample of three 1000-tic windows, on two maps (Skyscraper Leaps and
  Opulence), and **no full-state resend** with the correction channel on.
- **Snapshots hold**: restore-and-replay soaks pass on seven maps covering
  water, polyobjects, linedef executors, ACS and dynamic slopes, after the
  fixes they found.
- **Cost is the open problem.** On a light map a pass costs a few
  milliseconds; on a heavy one (Opulence, about 3700 objects) the driven race
  runs at 54 to 65 frames a second, and every rebuild of the speculation is a
  hitch. Most of a pass is saving snapshots and re-running the map's
  decorations.

Never run under prediction yet: a race with two or more humans on separate
machines, a race played from the host's seat, sixteen karts, a full race from
grid to results, Battle, Grand Prix and Encore.

The measurement journal, with every figure and the prediction written before
each run, is [docs/WORLDWIDE.md](docs/WORLDWIDE.md) -- its *Current state*
block first.

## The plan

The detailed roadmap is [docs/ROADMAP.md](docs/ROADMAP.md). In order:

1. **Cost** -- the gate for an alpha: a pass must fit in about 30% of a tic at
   sixteen karts late in a race. In progress: keeping the speculation when it
   was right, then raw snapshots of the level's object pools (a memory copy in
   place of the field-by-field save), then predicting less.
2. **Compatibility**, under the rule *the server decides*: the WORLDWIDE mode
   switch (built), checked against stock builds, on a release base the public
   servers run.
3. **Breadth**: full races, every mode, items used on purpose, maps of every
   kind, several humans.
4. **Feel**: correction smoothing, remote karts, the prediction depth, a
   person hosting -- judged by people, not logs.
5. **A capability check** that tells a player whether their machine can keep
   up, and a recommended setting.
6. **An alpha** that someone other than the maintainer has played.

## How this project is made: AI-assisted, human-decided

This project is developed with the help of an AI coding assistant
(Anthropic's Claude, through Claude Code), under rules written into the
repository's agent instructions and applied at every step:

- **The AI assists; Gibax decides.** The AI does research work -- reading the
  Ring Racers code base, comparing it with other netcodes, tracing a
  mechanism down to a line -- writes code and instruments, and drafts the
  documentation. Analysing the need, choosing between solutions, and
  deciding what is done, kept or dropped are Gibax's.
- **Nothing is launched without Gibax's explicit go-ahead, every time**: the
  game, a test scenario, a soak. Gibax runs the tests and drives the races;
  the feel of a race is judged by a person, never inferred from a log.
- **Measure, don't assume.** Every change is tested on a binary verified by
  its hash, with the prediction written down *before* the run and a control
  in the same session. A result counts once Gibax has run it and read it.
- **Nothing is rewritten after the fact.** The journal is annotated, never
  edited: when a later result overturns an earlier one, the earlier one gets
  a pointer forward, so wrong guesses stay visible.
- **Every code change is pushed on Gibax's approval**, one subject per
  commit; commits written with the AI say so in their trailer.

### Why I use AI, and where it stops -- a note from Gibax

**Who is behind this.** I learned to program before the rise of AI, and I am
a C# developer by trade, not a C expert. Ring Racers is written in C and C++,
in an engine that comes from Doom Legacy, SRB2 and SRB2Kart. Alone, a project
of this size would have meant months, if not more, of teaching myself the
concepts -- netcode, prediction, determinism, this engine's internals -- and
then designing the code, which would easily have made it unviable. It takes
something away from the achievement, of course. But if I wait for someone
else to do it, nobody will. And I did not want to hand the project over to
somebody else just because C is not my strongest language.

**Why it went public.** It started as a small personal project. As it grew, I
decided to take it online, because I am convinced that progress in Ring
Racers' netcode can benefit everyone -- think of the rollback patches that
were added to many older fighting games. Letting a European play properly on
American servers would be a gift, especially in a game as demanding as Ring
Racers, which unfortunately has few players in Europe. I also decided to use
this project to find out how far generative AI can be pushed on real code, and
where it breaks.

**Kart Krew and the community.** I am not associated with Kart Krew, and I am
not trying to be: this is a fork, a project of my own. I am well aware of how
negatively generative AI is seen in the SRB2 community in general, and I fully
understand the movement against it. I have decided that this is not mine to
carry: if people don't like it, so be it -- they can boycott it and call it
"AI slop", and not necessarily wrongly.

**What the AI is for here.** I use it as a development assistant. It helps me
move faster and, above all, keeps me working within a structure: a written
prediction before every test, a journal that is never rewritten, one change
at a time. It reads code faster than I can, keeps track of what was measured
and when, and does a lot of the typing.

**Its limits are written down, not hidden.** It gets things wrong: many of the
predictions in the journal are its own, and many of them failed -- they stay
there, marked as such. It cannot test a game properly: it cannot play a race,
feel input delay or see a stutter, and a log only says what it was built to
measure. It can explain a mechanism with confidence and be wrong about it.
That is why nothing here counts until I have run it and checked it myself.

I don't believe a project coded entirely by AI can be good. Assuming an AI can
do everything on its own, with no human knowledge behind it to check its work
-- especially on code like Ring Racers -- greatly underestimates what human
verification brings. AI is a tool to speed up development and make the work
easier, not an automatic developer that does the job while nobody is looking.

**My commitment.** As long as I maintain this project, I will do everything I
can to make it as good as it can be -- ideally to the point where it can no
longer objectively be called AI slop.

A human always has to steer the AI, and steer it properly.

-- Gibax

## Trying it

There are no releases. Each push to `rollback-netcode` is built by GitHub
Actions (see [.github/workflows/build.yml](.github/workflows/build.yml)); the
Windows executables are the run's artifacts:

- `ringracers-win64-<sha>`: the development build every measurement uses. It
  can only join a server running the very same build.
- `ringracers-win64-release-<sha>`: release configuration, real version
  numbers.

Either needs the data files of an existing Ring Racers 2.4 installation: put
the executable next to them. To host in WORLDWIDE mode, set `worldwide On` in the
console (or `+worldwide On` on the command line) **before** anybody joins;
WORLDWIDE clients then switch themselves on when they join.

## Building from source

Ring Racers Worldwide builds like upstream Ring Racers: CMake, a C17/C++20
toolchain (GCC, Clang, MinGW), and SDL3 among its dependencies. The two
recipes below are the ones the CI runs.

### Linux

On Alpine Linux (3.24), which packages every dependency, SDL3 included:

    apk add build-base cmake ninja-build ninja-is-really-ninja \
        zlib-dev libpng-dev curl-dev libvpx-dev libogg-dev libvorbis-dev \
        libyuv-dev opus-dev sdl3-dev git
    cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSRB2_CONFIG_DEV_BUILD=ON
    cmake --build build/

Other distributions need the same libraries: libcurl, zlib, libpng, libogg,
libvorbis, libvpx, libyuv, libopus and **SDL3** (absent from Ubuntu 24.04
LTS, for one).

### Windows

The CI cross-compiles from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
against Kart Krew's prebuilt Windows dependencies (the `rrsdk-msys2-clang64`
package, checked against a known SHA-256); the toolchain file and the exact
flags are in [.github/workflows/build.yml](.github/workflows/build.yml).

Upstream documents another route, not tried on this fork: install
[vcpkg](https://vcpkg.io/en/), set
`VCPKG_ROOT`, and use a [CMake preset](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
from `CMakePresets.json`, for example:

    cmake --preset ninja-x86_mingw_static_vcpkg-develop
    cmake --build --preset ninja-x86_mingw_static_vcpkg-develop

`SRB2_CONFIG_DEV_BUILD=ON` gives the development build (version 0, extra
checks), `OFF` the release configuration.

## Upstream

Dr. Robotnik's Ring Racers is a kart racing video game by Kart Krew Dev,
originally based on the 3D Sonic the Hedgehog fangame
[Sonic Robo Blast 2](https://srb2.org/), itself based on a modified version of
[Doom Legacy](http://doomlegacy.sourceforge.net/). Its primary source
repository is [hosted on gitlab.com](https://gitlab.com/kart-krew-dev/ring-racers).

- [Kart Krew Dev Website](https://www.kartkrew.org/)
- [Kart Krew Dev Discord](https://www.kartkrew.org/discord)
- [SRB2 Forums](https://mb.srb2.org/)

Issues with this fork belong here, not upstream.

## Disclaimer

Dr. Robotnik's Ring Racers is a work of fan art made available for free without intent to profit or harm the intellectual property rights of the original works it is based on. Kart Krew Dev is in no way affiliated with SEGA Corporation. We do not claim ownership of any of SEGA's intellectual property used in Dr. Robotnik's Ring Racers.

Ring Racers Worldwide is an unofficial modification of Dr. Robotnik's Ring Racers, made under the same terms. It is not affiliated with or endorsed by Kart Krew Dev or SEGA.

## License

Ring Racers' source code, and this fork's, is available under the GNU General
Public License version 2.0 or higher. Contributions must be made available
under the GPL version 2.0, or public domain; integrations of third-party code
must be made to code under a compatible license.
