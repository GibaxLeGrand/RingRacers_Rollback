// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_rollback.c
/// \brief Rollback netcode -- full world snapshot ring buffer (Phase 0)
///
/// Phase 0 of the rollback netcode plan: a ring buffer of complete world
/// snapshots, indexed by the tic they were taken before.
///
/// Both directions reuse the existing netgame archiver as-is. P_SaveNetGame
/// is pure serialisation. P_LoadNetGame takes a `reloading` flag which does
/// exactly what restoring a state within a level needs: the level is kept in
/// place instead of going back through P_LoadLevel, and the RNG seeds are
/// restored from the archive rather than reset. Upstream added that path for
/// mid-game gamestate reloads and says in P_NetUnArchiveMisc that it exists
/// with rollback in mind, so nothing in p_saveg needs changing here.
///
/// The matching `resending` flag on the save side is what puts gametic in the
/// archive; the two flags have to be set together or the reader desynchronises
/// from the writer by four bytes.
///
/// Snapshots are still written field by field through the P_NetArchive*
/// functions. Bulk memcpy of whole structs with manual pointer relinking is
/// faster and considerably more fragile; that trade is not worth making before
/// the timings below say it is needed.
///
/// This does not yet hook into the tic loop. It provides the snapshot
/// primitives plus a rollback_test console command that measures the
/// round-trip and verifies it byte for byte.

#include "k_rollback.h"

#include "command.h"
#include "d_clisrv.h" // Consistancy(), playerdelaytable
#include "d_netcmd.h" // cv_mindelay
#include <stddef.h> // offsetof

#include "deh_tables.h" // MOBJTYPE_LIST, FREE_MOBJS
#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h" // players, playeringame
#include "i_system.h" // I_GetPreciseTime()
#include "info.h"
#include "k_grandprix.h" // grandprixinfo
#include "m_random.h" // P_RandomFixed()
#include "p_local.h" // thlist, P_Ticker()
#include "p_mobj.h"
#include "p_saveg.h"
#include "p_tick.h" // leveltime
#include "r_state.h" // sectors
#include "z_zone.h"

// Nominal snapshot size. NETSAVEGAMESIZE, which the netcode uses, is 768 KiB
// and sized for the worst a netgame savegame can be. Measured snapshots of a
// full sixteen-kart race come to 120 KiB, so the ring was reserving twenty
// megabytes to carry two and a half -- enough of the zone that an unrelated
// allocation failed and took the game down with "not enough memory for item
// roulette list".
//
// A quarter of that was still twice the largest snapshot then seen -- and it
// was not enough, because 120 KiB is what a race weighs seconds after the
// start. A soak run three minutes into the same race hit 318 KiB: the world
// accumulates objects as it is played, so a snapshot grows with the race. The
// guard caught it in the slack and said so, which is what the slack is for.
//
// Sized on that measurement rather than on the opening lap, with room for a
// race that goes further than the one measured.
#define ROLLBACK_BUFSIZE (512*1024)

// P_SaveNetGame writes through raw pointer macros with no bounds checking, so
// an oversized state cannot be stopped mid-write. Each slot therefore carries
// slack past its nominal size: a state that overruns ROLLBACK_BUFSIZE lands in
// the slack instead of in the next slot, and is caught before anything else
// has been corrupted.
#define ROLLBACK_SLACK (128*1024)

typedef struct
{
	uint8_t buffer[ROLLBACK_BUFSIZE + ROLLBACK_SLACK];

	// Beside the archive rather than inside it.
	//
	// A snapshot has two jobs and they want different things from the cameras.
	// Putting the world back wants them restored: a check replays three passes
	// of a tic and the world rewinds, so a camera that keeps all three runs
	// ahead and snaps back, once every soak interval -- visible as a jerk while
	// driving, and a real rollback would do the same over its own window. The
	// byte-for-byte comparison wants them gone: they are driven by a local view
	// nothing archives, and comparing them accounted for 43 of the 44
	// differences left in a played race.
	//
	// Held here, they are restored and not compared, which is what each job
	// asked for. Taking them out of the archive to satisfy the second was
	// giving up the first as well.
	camera_t cameras[MAXSPLITSCREENPLAYERS];

	size_t used;
	tic_t tic;
	int16_t gamemap;
	dboolean valid;
} rollbackslot_t;

static rollbackslot_t *rollbackring = NULL;

// What the tests last measured on this machine, so rollback_delay can price a
// rollback from real figures rather than from memory. Zero until then.
static uint32_t g_lastrestoreus;
static uint32_t g_lastresimus;

void K_InitRollback(void)
{
	if (rollbackring)
		return;

	// Twelve and a half megabytes at the current slot size, which is why the
	// ring is allocated on first use rather than at startup: a session that
	// never touches rollback never pays for it.
	rollbackring = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t) * ROLLBACK_TICS, PU_STATIC, NULL);
	if (!rollbackring)
		I_Error("K_InitRollback: not enough memory for the rollback ring buffer (%s KiB)",
			sizeu1((sizeof (rollbackslot_t) * ROLLBACK_TICS) / 1024));

	memset(rollbackring, 0, sizeof (rollbackslot_t) * ROLLBACK_TICS);
}

void K_ClearRollback(void)
{
	if (!rollbackring)
		return;

	Z_Free(rollbackring);
	rollbackring = NULL;
}

/** Writes a snapshot of the current state into a caller-supplied slot.
  *
  * Split out of K_SaveGameState so that rollback_test can take a second
  * snapshot without disturbing the ring.
  */
static dboolean K_WriteSnapshot(rollbackslot_t *slot, tic_t tic)
{
	savebuffer_t save = {0};

	if (gamestate != GS_LEVEL)
		return false;

	slot->valid = false; // in case the write below never completes

	if (P_SaveBufferFromExisting(&save, slot->buffer, sizeof (slot->buffer)) == false)
		return false;

	// resending, so that gametic goes into the archive. K_LoadGameState reads
	// it back, and the reader only looks for it when the writer wrote it.
	// local, because this snapshot is restored on the machine that took it:
	// the per-viewport visibility flags are worth keeping and must round-trip.
	P_SaveNetGame(&save, true, true);

	slot->used = (size_t)(save.p - save.buffer);

	// Deliberately not P_SaveBufferFree: that would Z_Free the ring slot out
	// from under us. The savebuffer_t is a view onto storage this module owns.

	if (slot->used > ROLLBACK_BUFSIZE)
		I_Error("K_WriteSnapshot: snapshot of %s bytes exceeds the slot size "
			"(caught in slack, nothing corrupted -- raise ROLLBACK_BUFSIZE)",
			sizeu1(slot->used));

	memcpy(slot->cameras, camera, sizeof (slot->cameras));

	slot->tic = tic;
	slot->gamemap = gamemap;
	slot->valid = true;

	return true;
}

dboolean K_SaveGameState(tic_t tic)
{
	if (gamestate != GS_LEVEL)
		return false;

	K_InitRollback();

	return K_WriteSnapshot(&rollbackring[tic % ROLLBACK_TICS], tic);
}

/** Puts the world back to what a slot holds, wherever the slot came from. */
static dboolean K_ReadSnapshot(rollbackslot_t *slot)
{
	savebuffer_t save = {0};

	if (slot == NULL || slot->valid == false)
		return false;

	if (P_SaveBufferFromExisting(&save, slot->buffer, slot->used) == false)
		return false;

	// reloading: keep the level in place, and keep the RNG seeds the archive
	// restores instead of resetting them. Both are required for a rollback --
	// replaying the same tics has to produce the same result.
	if (P_LoadNetGame(&save, true, true) == false)
		return false;

	memcpy(camera, slot->cameras, sizeof (slot->cameras));

	return true;
}

dboolean K_LoadGameState(tic_t tic)
{
	rollbackslot_t *slot;

	if (!rollbackring)
		return false;

	slot = &rollbackring[tic % ROLLBACK_TICS];

	// The ring is indexed modulo its length, so a stale slot answers to the
	// same index as the tic being asked for. Check the tic actually matches.
	if (!slot->valid || slot->tic != tic)
		return false;

	// Checked here rather than left to P_NetUnArchiveMisc, which would notice
	// the mismatch and recover by reloading the level -- exactly what rollback
	// exists to avoid, and far too slow to do per tic.
	if (slot->gamemap != gamemap)
		return false;

	return K_ReadSnapshot(slot);
}

// ----------------------------------------------------------------------------
// rollback_test
// ----------------------------------------------------------------------------

/** Converts a precise_t interval to microseconds.
  *
  * I_GetPrecisePrecision() is the counter's frequency in units per second, so
  * the interval scaled by a million and divided by that frequency gives
  * microseconds. Kept in 64 bits throughout: on a nanosecond-resolution
  * counter an interval of a single millisecond overflows 32 bits as soon as
  * it is scaled.
  */
static uint32_t K_PreciseToMicros(precise_t delta)
{
	return (uint32_t)((delta * (uint64_t)1000000) / I_GetPrecisePrecision());
}

/** Names a mobj type, for diagnostics. Never NULL. */
static const char *K_MobjTypeName(mobjtype_t type)
{
	const char *name = NULL;

	if (type >= MT_FIRSTFREESLOT)
	{
		if (type <= MT_LASTFREESLOT)
			name = FREE_MOBJS[type - MT_FIRSTFREESLOT];
	}
	else if (type < NUMMOBJTYPES)
	{
		name = MOBJTYPE_LIST[type];
	}

	return (name != NULL) ? name : "(unnamed type)";
}

// A comparison run inside a resimulation check cannot print as it goes: at
// that point nobody knows yet whether the check will fail, and a clean
// restore reports the same interpolation fields every time. So the findings
// wait here and are printed only if the check does fail.
// Six was not enough. A failing check has something to say about every player
// whose structure moved, and the six it had room for went to the first players
// in the table -- whose interpolation and HUD counters differ on every check,
// failing or not -- so the player the check was actually about never got a
// line.
// Forty was not enough either: a failing check dropped eleven to thirteen, and
// the ones it dropped were the short, useful lines that come last. Raised, and
// the report reordered so the cheap conclusions go in before the byte dumps.
#define HELD_MAX 64
static char g_held[HELD_MAX][160];
static uint32_t g_heldcount;
static uint32_t g_helddropped;
static dboolean g_holdfindings;

static void K_Finding(const char *text)
{
	if (g_holdfindings == false)
	{
		CONS_Printf("%s\n", text);
		return;
	}

	if (g_heldcount < HELD_MAX)
		strlcpy(g_held[g_heldcount++], text, sizeof (g_held[0]));
	else
		g_helddropped++;
}

static void K_ReleaseFindings(void)
{
	uint32_t i;

	for (i = 0; i < g_heldcount; i++)
		CONS_Printf("%s\n", g_held[i]);

	// A report that stops short without saying so reads like a report with
	// nothing more to say, which is how the half that mattered went unread.
	if (g_helddropped > 0)
		CONS_Printf("rollback: %u further findings were not kept\n", g_helddropped);

	g_heldcount = 0;
	g_helddropped = 0;
}

// ----------------------------------------------------------------------------
// The hit trace
//
// Neither the player structures nor the objects lose simulation state across a
// restore -- both were compared in memory, and what differs is interpolation
// and the thinker bookkeeping. So whatever makes a kart get hit on one pass and
// not on the other lives somewhere a structure walk does not reach: the globals
// the gameplay modules keep between them.
//
// Those cannot be enumerated the way a struct can. So stop hunting the state
// and record the event instead: every hit, with the tic it landed on and who
// caused it, once per pass. Where the two traces part company is the hit that
// differs, and explaining one hit is a far smaller job than explaining
// "something, somewhere".
// ----------------------------------------------------------------------------

// One event per player per tic now that the hit copy is recorded whether or not
// it happens, so a four-tic replay of a full grid needs sixty-four on its own.
#define TRACE_MAX 256

typedef struct
{
	tic_t when;
	uint8_t victim;
	uint16_t inflictor;
	uint16_t source;
	uint16_t a;
	uint16_t b;
	uint16_t c;
	uint16_t d;

	// For values that do not fit in sixteen bits: angles, coordinates. Reading
	// what a computation actually read beats reasoning about which of its
	// inputs could have moved.
	uint32_t v[6];
} tracehit_t;

static tracehit_t g_hittrace[2][TRACE_MAX];
static uint32_t g_hittracecount[2];
static uint32_t g_tracedropped[2];

/** Names an event kind, so a report reads as something that happened. */
static const char *K_TraceKindName(uint16_t kind)
{
	switch (kind)
	{
		case 0: return "a hit counted";
		case 1: return "a hit taken back";
		case 2: return "a damage judgement";
		case 3: return "the end-of-tic hit copy";
		case 4: return "the tilt computation";
		default: return "an event of an unknown kind";
	}
}
static int32_t g_hittracing = -1; // which pass is recording, -1 for none

// Collision pairs are counted rather than kept: there are hundreds a tic, and
// the question they answer is only whether the two passes examined the same
// ones. A differing count means a different set of pairs; the same count with
// a differing hash means the same pairs in a different order.
static uint32_t g_pairs[2];
static uint32_t g_pairhash[2];

void K_RollbackTraceCollide(uint32_t one, uint32_t two)
{
	if (g_hittracing < 0)
		return;

	g_pairs[g_hittracing]++;
	g_pairhash[g_hittracing] = ((g_pairhash[g_hittracing] ^ one) * 16777619u) ^ two;
}

void K_RollbackTraceHit(int32_t victim, uint16_t inflictor, uint16_t source)
{
	tracehit_t *hit;

	if (g_hittracing < 0)
		return;

	if (g_hittracecount[g_hittracing] >= TRACE_MAX)
	{
		// A trace that quietly stops recording compares equal to one that had
		// nothing more to record.
		g_tracedropped[g_hittracing]++;
		return;
	}

	hit = &g_hittrace[g_hittracing][g_hittracecount[g_hittracing]++];

	hit->when = leveltime;
	hit->victim = (uint8_t)victim;
	hit->inflictor = inflictor;
	hit->source = source;
	hit->a = 0;
	hit->b = 0;
	hit->c = 0;
	hit->d = 0;
}

/** Records the end-of-tic copy of timeshit into timeshitprev, taken or not.
  *
  * Every failing check a soak reports names timeshitprev, with timeshit at zero
  * on both sides and the hit trace recording no hit at all -- so nothing was
  * hit, and what differs is whether P_PlayerAfterThink copied. That copy is
  * skipped while the kart is in hitlag, in which case timeshitprev keeps
  * whatever it held; so the two passes disagree about being in hitlag.
  *
  * Recorded either way, rather than only when skipped, because a pass that
  * records nothing tells you nothing about what it decided from: with both
  * decisions in both traces the entries line up and the report shows hitlag and
  * nullHitlag on each side of the disagreement.
  */
void K_RollbackTraceHitCopy(int32_t victim, dboolean copied, int32_t hitlag, int32_t nullhitlag,
	uint8_t timeshit, uint8_t timeshitprev)
{
	tracehit_t *hit;

	if (g_hittracing < 0)
		return;

	if (g_hittracecount[g_hittracing] >= TRACE_MAX)
	{
		g_tracedropped[g_hittracing]++;
		return;
	}

	hit = &g_hittrace[g_hittracing][g_hittracecount[g_hittracing]++];

	hit->when = leveltime;
	hit->victim = (uint8_t)victim;
	hit->inflictor = 3;
	hit->source = (copied ? 0 : 1); // 1 means the copy was skipped
	hit->a = (uint16_t)((hitlag < 0) ? 0 : ((hitlag > 65535) ? 65535 : hitlag));
	hit->b = (uint16_t)((nullhitlag < 0) ? 0 : ((nullhitlag > 65535) ? 65535 : nullhitlag));

	// The values as well as the decision. Two passes that agree on every
	// decision and still end the tic with different timeshitprev differ over
	// what was copied, not over whether to copy -- and with only the decision
	// recorded there was no way to tell those apart.
	hit->c = timeshit;
	hit->d = timeshitprev;
}

/** Records what DoABarrelRoll read, and what it produced.
  *
  * player->tilt is archived, so a difference in it fails a check -- and it has
  * been failing them from a starting state the restore is known to reproduce,
  * with frozen inputs, on players that are not display players. Every input it
  * is built from is either archived or a renderer global that cannot move with
  * no frame drawn between the passes, which is a contradiction rather than an
  * explanation. So: record the inputs instead of arguing about them.
  */
void K_RollbackTraceTilt(int32_t who, uint32_t vx, uint32_t vy,
	uint32_t pitch, uint32_t roll, uint32_t slope, uint32_t tilt)
{
	tracehit_t *hit;

	if (g_hittracing < 0)
		return;

	if (g_hittracecount[g_hittracing] >= TRACE_MAX)
	{
		g_tracedropped[g_hittracing]++;
		return;
	}

	hit = &g_hittrace[g_hittracing][g_hittracecount[g_hittracing]++];

	hit->when = leveltime;
	hit->victim = (uint8_t)who;
	hit->inflictor = 4;
	hit->source = 0;
	hit->a = 0;
	hit->b = 0;
	hit->c = 0;
	hit->d = 0;
	hit->v[0] = vx;
	hit->v[1] = vy;
	hit->v[2] = pitch;
	hit->v[3] = roll;
	hit->v[4] = slope;
	hit->v[5] = tilt;
}

/** Says where two passes stopped agreeing about who got hit. */
static void K_ReportTrace(const char *cmd)
{
	if (g_pairs[0] != g_pairs[1] || g_pairhash[0] != g_pairhash[1])
	{
		CONS_Printf("%s: collision pairs -- live %u (hash %08x), replay %u (hash %08x)\n",
			cmd, g_pairs[0], g_pairhash[0], g_pairs[1], g_pairhash[1]);
	}
	else
	{
		CONS_Printf("%s: both passes examined the same %u collision pairs, in the same order\n",
			cmd, g_pairs[0]);
	}

	const uint32_t common = (g_hittracecount[0] < g_hittracecount[1]) ? g_hittracecount[0] : g_hittracecount[1];
	uint32_t i;

	for (i = 0; i < common; i++)
	{
		const tracehit_t *a = &g_hittrace[0][i];
		const tracehit_t *b = &g_hittrace[1][i];

		if (a->when == b->when && a->victim == b->victim
			&& a->inflictor == b->inflictor && a->source == b->source
			&& a->a == b->a && a->b == b->b && a->c == b->c && a->d == b->d
			&& memcmp(a->v, b->v, sizeof (a->v)) == 0)
			continue;

		if (a->inflictor == 4 || b->inflictor == 4)
		{
			CONS_Printf("%s: event %u differs -- live: tic %u, player %u, %s, "
				"view %08x/%08x, pitch %08x, roll %08x, slope %08x, tilt %08x\n",
				cmd, i, a->when, a->victim, K_TraceKindName(a->inflictor),
				a->v[0], a->v[1], a->v[2], a->v[3], a->v[4], a->v[5]);
			CONS_Printf("%s: event %u differs -- replay: tic %u, player %u, %s, "
				"view %08x/%08x, pitch %08x, roll %08x, slope %08x, tilt %08x\n",
				cmd, i, b->when, b->victim, K_TraceKindName(b->inflictor),
				b->v[0], b->v[1], b->v[2], b->v[3], b->v[4], b->v[5]);
			return;
		}

		// For a judgement, flags bit 1 means invincible and bit 4 inside
		// hitlag. For a skipped copy, the detail is hitlag and nullHitlag.
		CONS_Printf("%s: event %u differs -- live: tic %u, player %u, %s, flags %u, "
			"hitlag %u/%u, timeshit %u/%u\n",
			cmd, i, a->when, a->victim, K_TraceKindName(a->inflictor), a->source,
			a->a, a->b, a->c, a->d);
		CONS_Printf("%s: event %u differs -- replay: tic %u, player %u, %s, flags %u, "
			"hitlag %u/%u, timeshit %u/%u\n",
			cmd, i, b->when, b->victim, K_TraceKindName(b->inflictor), b->source,
			b->a, b->b, b->c, b->d);
		return;
	}

	if (g_hittracecount[0] != g_hittracecount[1])
	{
		const int32_t extra = (g_hittracecount[0] > g_hittracecount[1]) ? 0 : 1;
		const tracehit_t *only = &g_hittrace[extra][common];

		// Not a mobj type: the kind is what this field carries, and printing it
		// as a type named an object that had nothing to do with anything.
		CONS_Printf("%s: %u events live against %u on the replay -- the %s has %s at "
			"tic %u on player %u, hitlag %u/%u, timeshit %u/%u\n",
			cmd, g_hittracecount[0], g_hittracecount[1],
			(extra == 0 ? "live pass" : "replay"),
			K_TraceKindName(only->inflictor),
			only->when, only->victim, only->a, only->b, only->c, only->d);
	}
	else if (common > 0)
	{
		CONS_Printf("%s: both passes agree on all %u events, so the difference is elsewhere\n",
			cmd, common);
	}

	if (g_tracedropped[0] > 0 || g_tracedropped[1] > 0)
	{
		CONS_Printf("%s: the trace filled up -- %u events live and %u on the replay "
			"were not recorded, so this comparison is of the first %u only\n",
			cmd, g_tracedropped[0], g_tracedropped[1], (uint32_t)TRACE_MAX);
	}
}

// ----------------------------------------------------------------------------
// The objects, compared in memory
//
// The same instrument that cleared the player structures, pointed at mobjs.
// Comparing snapshots is blind to whatever the archive does not carry, which is
// exactly where the remaining divergence has to be; reading the structures
// themselves is not.
//
// Objects are matched by mobjnum, since a restore rebuilds them at new
// addresses and in new memory.
// ----------------------------------------------------------------------------

#define MOBJCOPY_MAX 16384

static uint8_t *g_mobjcopy;    // the structures as they were, back to back
static uint16_t *g_mobjslot;   // mobjnum -> its place in there, plus one
static uint32_t g_mobjcopies;

/** True when a run of differing bytes belongs to an address rather than a field.
  *
  * Alignment used to be the test, and it was wrong in both directions. A
  * pointer whose low byte happens to match starts its run at an offset that is
  * not a multiple of eight, and was reported as though it were a field -- which
  * is every one of the MT_RING lines a failing check prints, all of them
  * bprev and touching_sectorlist; and every field that begins on a multiple of
  * eight, which is most of the wide ones, was thrown away unseen as though it
  * were a pointer. Reading the whole aligned word as an address and asking
  * whether both sides look like one costs the same and mistakes neither for the
  * other.
  */
static dboolean K_LooksLikeAddress(uintptr_t v)
{
	return (v >= 0x10000 && (v % 8) == 0 && ((uint64_t)v >> 47) == 0);
}

static dboolean K_RunIsAddress(const uint8_t *was, const uint8_t *now, size_t at, size_t size)
{
	const size_t step = sizeof (void *);
	const size_t base = at - (at % step);
	uintptr_t a = 0, b = 0;

	if (base + step > size)
		return false;

	memcpy(&a, was + base, step);
	memcpy(&b, now + base, step);

	// Z_Malloc hands out aligned blocks well clear of the first page, and
	// nothing this process maps sits near the top of the address space. Zero
	// counts as one: relinking clears pointers and sets them, and requiring an
	// address on both sides reported all 128 of those as fields.
	return ((K_LooksLikeAddress(a) && K_LooksLikeAddress(b))
		|| (a == 0 && K_LooksLikeAddress(b))
		|| (b == 0 && K_LooksLikeAddress(a)));
}

/** Names the mobj_t field a byte offset lands in.
  *
  * Built from offsetof, not from the archiver. An archived record is written
  * under diff masks, so a place in one is not a field and reading it means
  * walking five hundred lines of archiver in step; an offset into the structure
  * is a field, and the compiler already knows where every one of them starts.
  * This is the object twin of P_NamePlayerField, and it exists because the last
  * replay residue is scenery -- a ring, a spring, an arrow sign -- reported as
  * "N bytes into mobj_t" and nothing more.
  *
  * The table is in declaration order, so the field an offset belongs to is the
  * last one that starts at or before it.
  *
  * \return the field name, or NULL past the end of the structure.
  */
static const char *K_NameMobjField(size_t into)
{
	static const struct { size_t at; const char *name; } fields[] =
	{
#define F(x) { offsetof(mobj_t, x), #x }
		F(thinker), F(x), F(y), F(z),
		F(old_x), F(old_y), F(old_z), F(old_x2),
		F(old_y2), F(old_z2), F(type), F(info),
		F(bnext), F(bprev), F(angle), F(pitch),
		F(roll), F(old_angle), F(old_pitch), F(old_roll),
		F(old_angle2), F(old_pitch2), F(old_roll2), F(rollangle),
		F(sprite), F(frame), F(sprite2), F(anim_duration),
		F(renderflags), F(spritexscale), F(spriteyscale), F(spritexoffset),
		F(spriteyoffset), F(old_spritexscale), F(old_spriteyscale), F(old_spritexoffset),
		F(old_spriteyoffset), F(floorspriteslope), F(lightlevel), F(touching_sectorlist),
		F(subsector), F(floorz), F(ceilingz), F(floorrover),
		F(ceilingrover), F(floordrop), F(ceilingdrop), F(radius),
		F(height), F(momx), F(momy), F(momz),
		F(pmomz), F(tics), F(state), F(flags),
		F(flags2), F(eflags), F(tid), F(tid_next),
		F(tid_prev), F(skin), F(color), F(snext),
		F(sprev), F(hnext), F(hprev), F(itnext),
		F(health), F(movedir), F(movecount), F(target),
		F(reactiontime), F(threshold), F(player), F(lastlook),
		F(spawnpoint), F(tracer), F(friction), F(movefactor),
		F(lastmomz), F(fuse), F(watertop), F(waterbottom),
		F(mobjnum), F(scale), F(old_scale), F(old_scale2),
		F(destscale), F(scalespeed), F(extravalue1), F(extravalue2),
		F(cusval), F(cvmem), F(standingslope), F(resetinterp),
		F(colorized), F(mirrored), F(shadowscale), F(whiteshadow),
		F(shadowcolor), F(sprxoff), F(spryoff), F(sprzoff),
		F(bakexoff), F(bakeyoff), F(bakezoff), F(bakexpiv),
		F(bakeypiv), F(bakezpiv), F(terrain), F(terrainOverlay),
		F(hitlag), F(waterskip), F(dispoffset), F(thing_args),
		F(thing_stringargs), F(special), F(script_args), F(script_stringargs),
		F(frozen), F(reappear), F(punt_ref), F(owner),
		F(po_movecount),
#undef F
	};
	const size_t count = sizeof (fields) / sizeof (fields[0]);
	size_t i;

	if (into >= sizeof (mobj_t))
		return NULL;

	for (i = 0; i < count; i++)
	{
		if (into < fields[i].at)
			return (i > 0) ? fields[i - 1].name : NULL;
	}

	return fields[count - 1].name;
}


/** Copies every archived object. Call while the save's mobjnums still stand. */
static void K_CopyMobjs(void)
{
	thinker_t *th;

	if (g_mobjcopy == NULL)
	{
		g_mobjcopy = (uint8_t *)Z_Malloc(sizeof (mobj_t) * 2048, PU_STATIC, NULL);
		g_mobjslot = (uint16_t *)Z_Malloc(sizeof (uint16_t) * MOBJCOPY_MAX, PU_STATIC, NULL);
	}

	if (g_mobjcopy == NULL || g_mobjslot == NULL)
		return;

	memset(g_mobjslot, 0, sizeof (uint16_t) * MOBJCOPY_MAX);
	g_mobjcopies = 0;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (mo->mobjnum == 0 || mo->mobjnum >= MOBJCOPY_MAX)
			continue;

		if (g_mobjcopies >= 2048)
			break;

		memcpy(g_mobjcopy + (sizeof (mobj_t) * g_mobjcopies), mo, sizeof (mobj_t));
		g_mobjslot[mo->mobjnum] = (uint16_t)(g_mobjcopies + 1);
		g_mobjcopies++;
	}
}

/** Reports what the restore did not put back, object by object. */
static void K_CompareMobjs(const char *cmd)
{
	thinker_t *th;
	uint32_t reported = 0;
	uint32_t missing = 0;
	uint32_t compared = 0;
	uint32_t addresses = 0;

	if (g_mobjcopy == NULL || g_mobjslot == NULL || g_mobjcopies == 0)
		return;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ] && reported < 6; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;
		const uint8_t *was;
		const uint8_t *now = (const uint8_t *)mo;
		size_t at;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (mo->mobjnum == 0 || mo->mobjnum >= MOBJCOPY_MAX)
			continue;

		if (g_mobjslot[mo->mobjnum] == 0)
		{
			// An object the restore produced that was not there before.
			missing++;
			continue;
		}

		was = g_mobjcopy + (sizeof (mobj_t) * (g_mobjslot[mo->mobjnum] - 1));
		compared++;

		for (at = 0; at < sizeof (mobj_t) && reported < 6; at++)
		{
			char before[32], after[32];
			size_t run, k;
			int32_t nb = 0, na = 0;

			if (was[at] == now[at])
				continue;

			for (run = 0; at + run < sizeof (mobj_t) && was[at + run] != now[at + run]; run++)
				;

			if (K_RunIsAddress(was, now, at, sizeof (mobj_t)))
			{
				addresses++;
				at += run;
				continue;
			}

			for (k = 0; k < run && k < 8; k++)
			{
				nb += snprintf(before + nb, sizeof (before) - nb, "%02x ", was[at + k]);
				na += snprintf(after + na, sizeof (after) - na, "%02x ", now[at + k]);
			}

			{
				char text[160];

				const char *field = K_NameMobjField(at);

				snprintf(text, sizeof (text),
					"%s: %s #%u, %s bytes into mobj_t -- %s: %s bytes, %s-> %s",
					cmd, K_MobjTypeName(mo->type), mo->mobjnum,
					sizeu1(at), (field != NULL) ? field : "past the end",
					sizeu2(run), before, after);
				K_Finding(text);
			}

			reported++;
			at += run;
		}
	}

	if (g_holdfindings == false || missing > 0)
	{
		CONS_Printf("%s: %u objects compared, %u appeared from nowhere, "
			"%u differences shown, %u runs were addresses\n",
			cmd, compared, missing, reported, addresses);
	}
}

/** Reports one mobj reference that the archive will not preserve.
  *
  * \return the running count of reports, incremented if this one was bad.
  */
static uint32_t K_CheckReference(const mobj_t *owner, const char *field, const mobj_t *ref, uint32_t reported)
{
	const char *why;

	if (ref == NULL)
		return reported; // nothing to preserve

	if (P_MobjWasRemoved(ref) || TypeIsNetSynced(ref->type) == false)
	{
		// mobjnum is only handed out to the mobjs the archive writes, but it
		// is never cleared, so an unarchived mobj can still be carrying a
		// number from an earlier save. The reader would then resolve the
		// reference to whichever archived mobj holds that number now.
		why = (ref->mobjnum != 0)
			? "is not archived but still carries a stale mobjnum -- restores as SOME OTHER OBJECT"
			: "is not archived -- restores as NULL";
	}
	else if (ref->mobjnum == 0)
	{
		why = "was not numbered by this save -- restores as NULL";
	}
	else
	{
		return reported; // survives the round trip
	}

	// Cap the output: on a busy map a single systematic cause would otherwise
	// bury everything else in the console.
	if (reported < 12)
	{
		CONS_Printf("rollback_test: %s->%s = %s (mobjnum %u) %s\n",
			K_MobjTypeName(owner->type), field,
			K_MobjTypeName(ref->type), ref->mobjnum, why);
	}

	return reported + 1;
}

/** Reports the mobj references a snapshot cannot preserve.
  *
  * Must be called with the mobjnums a save has just handed out still valid,
  * i.e. straight after P_SaveNetGame and before anything spawns or removes an
  * object.
  *
  * The archiver writes a pointer field whenever it is non-NULL, without
  * checking that its target is archived too. A pointer to an object the
  * archive skips therefore goes out as a number that means nothing on the way
  * back in -- which is one way for a round trip to come back changed.
  */
static void K_ReportLostReferences(void)
{
	thinker_t *th;
	uint32_t reported = 0;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		// An object the archive skips is not restored at all, so where its own
		// pointers lead does not matter.
		if (TypeIsNetSynced(mo->type) == false)
			continue;

		reported = K_CheckReference(mo, "target", mo->target, reported);
		reported = K_CheckReference(mo, "tracer", mo->tracer, reported);
		reported = K_CheckReference(mo, "hnext", mo->hnext, reported);
		reported = K_CheckReference(mo, "hprev", mo->hprev, reported);
		reported = K_CheckReference(mo, "itnext", mo->itnext, reported);
		reported = K_CheckReference(mo, "punt_ref", mo->punt_ref, reported);
		reported = K_CheckReference(mo, "owner", mo->owner, reported);
	}

	if (reported == 0)
		CONS_Printf("rollback_test: every mobj reference in this state is archived\n");
	else if (reported > 12)
		CONS_Printf("rollback_test: %u unarchived references in total (only the first 12 listed)\n", reported);
}

// ----------------------------------------------------------------------------
// Per-object comparison
//
// A byte offset into a snapshot says that something changed, not what. These
// archive each object on its own, before and after a restore, so a difference
// can be reported as an object and a diff bit instead of a number.
// ----------------------------------------------------------------------------

#define ROLLBACK_DIAGBYTES (256*1024)
#define ROLLBACK_DIAGRECS 8192

typedef struct
{
	uint32_t offset;
	uint32_t length;
	mobjtype_t type;
} diagrec_t;

typedef struct
{
	uint8_t *bytes;
	diagrec_t *recs;
	uint32_t count;
	dboolean truncated;
} diagset_t;

// The working buffers a resimulation check needs, kept between checks.
static rollbackslot_t *g_first, *g_second, *g_third;

/** Allocates the slots the tests compare in, on first use.
  *
  * They were allocated inside the resimulation check, which meant any other
  * command that used them wrote through a null pointer. rollback_replay did,
  * on its first run.
  */
static dboolean K_NeedScratch(void)
{
	if (g_first == NULL)
	{
		g_first = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
		g_second = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
		g_third = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
	}

	return (g_first != NULL && g_second != NULL && g_third != NULL);
}
static diagset_t g_recsfirst, g_recssecond, g_recsthird;


/** Makes sure the per-object comparison has somewhere to write.
  *
  * Allocated on first use and kept: a soak runs a check hundreds of times, and
  * taking three megabytes and giving them back each time fragments the zone
  * until something innocent cannot find room. Every command that wants the
  * per-object pass calls this, because the one that did not ended up reporting
  * that it had run out of memory when it had simply never asked.
  *
  * \return true when all three sets are usable.
  */
static dboolean K_NeedDiagSets(void)
{
	if (g_recsfirst.bytes == NULL)
	{
		g_recsfirst.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
		g_recsfirst.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);
		g_recssecond.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
		g_recssecond.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);
		g_recsthird.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
		g_recsthird.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);
	}

	return (g_recsfirst.bytes != NULL && g_recsfirst.recs != NULL
		&& g_recssecond.bytes != NULL && g_recssecond.recs != NULL
		&& g_recsthird.bytes != NULL && g_recsthird.recs != NULL);
}

/** Archives every object the snapshot holds, one record per object.
  *
  * Walks the same list in the same order as the archiver, so record N here is
  * record N of the snapshot, and the two captures line up object for object as
  * long as the restore rebuilds the list in archive order.
  */
static void K_CaptureRecords(diagset_t *set)
{
	thinker_t *th;
	size_t used = 0;

	set->count = 0;
	set->truncated = false;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;
		size_t wrote;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (TypeIsNetSynced(mo->type) == false)
			continue;

		// P_ArchiveMobjForDiagnostics wants room for a whole record.
		if (set->count >= ROLLBACK_DIAGRECS || used + 4096 > ROLLBACK_DIAGBYTES)
		{
			set->truncated = true;
			break;
		}

		wrote = P_ArchiveMobjForDiagnostics(set->bytes + used, ROLLBACK_DIAGBYTES - used, mo);
		if (wrote == 0)
		{
			set->truncated = true;
			break;
		}

		set->recs[set->count].offset = (uint32_t)used;
		set->recs[set->count].length = (uint32_t)wrote;
		set->recs[set->count].type = mo->type;
		set->count++;
		used += wrote;
	}
}

static uint32_t K_ReadLE32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Prints the diff masks of one object record.
  *
  * The record is a thinker class byte, then diff, then diff2 if diff carries
  * its top bit, then diff3 if diff2 carries its top bit -- MD_MORE and
  * MD2_MORE, which are private to p_saveg.c, hence the bare 1<<31 here.
  */
static void K_PrintRecordMasks(const char *label, const uint8_t *rec, uint32_t length)
{
	uint32_t diff = 0, diff2 = 0, diff3 = 0;

	if (length >= 5)
		diff = K_ReadLE32(rec + 1);
	if ((diff & 0x80000000) != 0 && length >= 9)
		diff2 = K_ReadLE32(rec + 5);
	if ((diff2 & 0x80000000) != 0 && length >= 13)
		diff3 = K_ReadLE32(rec + 9);

	CONS_Printf("rollback_test: %s diff %08x diff2 %08x diff3 %08x\n",
		label, diff, diff2, diff3);
}

/** Names the objects whose archived record changed across a restore. */
static void K_ReportRecordDifferences(const char *cmd, const diagset_t *before, const diagset_t *after)
{
	uint32_t common = (before->count < after->count) ? before->count : after->count;
	uint32_t reported = 0;
	uint32_t differing = 0;
	uint32_t i;

	if (before->count != after->count)
	{
		CONS_Printf("%s: %u objects archived before the restore, %u after\n",
			cmd, before->count, after->count);
	}

	for (i = 0; i < common; i++)
	{
		const uint8_t *a = before->bytes + before->recs[i].offset;
		const uint8_t *b = after->bytes + after->recs[i].offset;
		const uint32_t la = before->recs[i].length;
		const uint32_t lb = after->recs[i].length;

		if (la == lb && memcmp(a, b, la) == 0)
			continue;

		// A type mismatch means the two captures have drifted out of step, so
		// everything after this point is comparing unrelated objects.
		if (before->recs[i].type != after->recs[i].type)
		{
			CONS_Printf("%s: object %u is %s before the restore and %s after -- "
				"the objects no longer line up, so the rest of this comparison is meaningless\n",
				cmd, i, K_MobjTypeName(before->recs[i].type), K_MobjTypeName(after->recs[i].type));
			return;
		}

		differing++;

		// The first few in detail, the rest counted. A cap without a count is
		// how this project once kept six findings out of an unknown number and
		// said nothing about the others.
		if (reported < 3)
		{
			CONS_Printf("%s: object %u (%s) changed: %u bytes became %u\n",
				cmd, i, K_MobjTypeName(before->recs[i].type), la, lb);
			K_PrintRecordMasks("  before:", a, la);
			K_PrintRecordMasks("  after: ", b, lb);
			reported++;
		}
	}

	// How big the thing examined was, every time, so a number is never read
	// without knowing what it is a number out of.
	CONS_Printf("%s: %u objects compared, %u differed, %u shown in full\n",
		cmd, common, differing, reported);

	if (differing == 0 && before->count == after->count)
	{
		CONS_Printf("%s: every object came back identical, so what changed is "
			"outside the per-object records\n", cmd);
	}

	if (before->truncated || after->truncated)
		CONS_Printf("%s: note - the object capture hit its limit, later objects were not compared\n", cmd);
}

/** Prints the bytes around an offset of a snapshot, for reading a mismatch by hand.
  *
  * The window reaches well back from the offset because what identifies a
  * record is its header -- the thinker class byte and the diff masks that say
  * which fields follow -- and those sit before the field that differs.
  */
static void K_PrintSnapshotContext(const char *label, const uint8_t *buffer, size_t used, size_t at)
{
	char line[3*64 + 1];
	size_t start = (at > 47) ? (at - 47) : 0;
	size_t end = at + 16;
	size_t i;
	int32_t n = 0;

	if (end > used)
		end = used;

	for (i = start; i < end && n >= 0 && (size_t)n < sizeof (line) - 3; i++)
		n += snprintf(line + n, sizeof (line) - n, "%02x ", buffer[i]);

	CONS_Printf("rollback_test: %s from byte %s: %s\n", label, sizeu1(start), line);
}

/** Prints where the last restore spent its time.
  *
  * The restore is the expensive half of a rollback, and it costs about four
  * times more on a client drawing the game than on a dedicated server running
  * the same map -- so the interesting question is not the total but which step
  * carries the difference.
  */
static void K_PrintLoadProfile(const char *cmd)
{
	const loadstep_t *steps = NULL;
	const size_t count = P_GetLoadProfile(&steps);
	size_t i;

	for (i = 0; i < count; i++)
	{
		// Steps that cost nothing worth reporting only bury the ones that do.
		if (steps[i].us < 100)
			continue;

		CONS_Printf("%s: restore step %-20s %u us\n", cmd, steps[i].name, steps[i].us);
	}
}

/** Hashes the order objects appear in, rather than what they contain.
  *
  * A restore rebuilds every object from the archive, so the world it produces
  * holds the same values -- rollback_test proves that byte for byte. What it
  * cannot hold is the order the live world had arrived at: objects are recreated
  * in archive order and re-linked into the sector and blockmap chains in that
  * order, while the live chains reflect where everything has moved since it
  * spawned.
  *
  * That matters because collision detection walks those chains. Two worlds
  * holding identical objects in a different order can resolve a hit
  * differently, which is exactly the shape of the failure the soak reports:
  * repeatable, gameplay-affecting, and invisible to a comparison of the
  * archive.
  *
  * Three chains hold objects and three can disagree: the thinker list the
  * simulation runs down, the blockmap cells collision walks, and the sector
  * lists. They are relinked by different code, so they have to be asked
  * separately.
  */
#define K_ORDER_THINKERS 0
#define K_ORDER_BLOCKMAP 1
#define K_ORDER_SECTORS 2

static uint32_t K_HashOrder(int32_t which)
{
	uint32_t hash = 2166136261u; // FNV-1a, for no reason beyond being short
	thinker_t *th;

	if (which == K_ORDER_SECTORS)
	{
		size_t s;

		for (s = 0; s < numsectors; s++)
		{
			const mobj_t *mo;

			for (mo = sectors[s].thinglist; mo != NULL; mo = mo->snext)
			{
				if (mo->mobjnum == 0 || TypeIsNetSynced(mo->type) == false)
					continue;

				hash = (hash ^ mo->mobjnum) * 16777619u;
			}
		}

		return hash;
	}

	if (which == K_ORDER_BLOCKMAP)
	{
		int32_t cell;

		if (blocklinks == NULL)
			return 0;

		for (cell = 0; cell < bmapwidth * bmapheight; cell++)
		{
			const mobj_t *mo;

			for (mo = blocklinks[cell]; mo != NULL; mo = mo->bnext)
			{
				// Only what the archive carries. A restore does not recreate the
				// rest, so counting it would report a different set as a different
				// order, which is a different problem with a different fix.
				if (mo->mobjnum == 0 || TypeIsNetSynced(mo->type) == false)
					continue;

				hash = (hash ^ mo->mobjnum) * 16777619u;
			}
		}

		return hash;
	}

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (mo->mobjnum == 0 || TypeIsNetSynced(mo->type) == false)
			continue;

		hash = (hash ^ mo->mobjnum) * 16777619u;
	}

	return hash;
}

/** Compares the player structures in memory across a restore.
  *
  * The archive cannot answer this question about itself. Comparing snapshots
  * only ever compares what the archive carries, so a field it does not carry is
  * equal on both sides by construction and invisible however hard you look.
  * Reading the structures themselves has no such blind spot.
  *
  * Pointers legitimately differ -- a restore rebuilds objects at new addresses
  * -- so the offsets reported have to be read against d_player.h rather than
  * trusted blindly. Everything else that differs is state a restore lost.
  */
static uint8_t *g_playercopy[3];

// Which player the archive comparison blamed, so the structure comparison can
// start there. -1 until it says.
static int32_t g_blamedplayer = -1;

/** Keeps a copy of the player structures as they stand.
  *
  * Two of them, because the comparison cannot run where a copy is taken. The
  * third pass overwrites the players before anything has said which player is
  * worth looking at, and it is the archive comparison, further down, that says.
  */
static void K_CopyPlayers(int32_t which)
{
	if (g_playercopy[which] == NULL)
		g_playercopy[which] = (uint8_t *)Z_Malloc(sizeof (player_t) * MAXPLAYERS, PU_STATIC, NULL);

	if (g_playercopy[which] != NULL)
		memcpy(g_playercopy[which], players, sizeof (player_t) * MAXPLAYERS);
}

/** Names the steps of a restore that changed the player structures.
  *
  * "players" is meant to. Any step after it writing over what that one put
  * back is the fault being hunted, and a step name is a far smaller thing to
  * read through than a whole restore.
  */
static void K_ComparePlayers(const char *cmd, const uint8_t *was, const uint8_t *now, int32_t blamed);

static uint32_t K_CountFieldRuns(const uint8_t *was, const uint8_t *now)
{
	uint32_t fields = 0;
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		const uint8_t *a = was + (sizeof (player_t) * i);
		const uint8_t *b = now + (sizeof (player_t) * i);
		size_t at;

		if (playeringame[i] == false)
			continue;

		for (at = 0; at < sizeof (player_t); )
		{
			size_t run;

			if (a[at] == b[at])
			{
				at++;
				continue;
			}

			for (run = 0; at + run < sizeof (player_t) && a[at + run] != b[at + run]; run++)
				;

			if (K_RunIsAddress(a, b, at, sizeof (player_t)) == false)
				fields++;

			at += run;
		}
	}

	return fields;
}

static void K_ReportRestoreSteps(const char *cmd)
{
	const loadstep_t *steps = NULL;
	const size_t count = P_GetLoadProfile(&steps);
	const char *culprit = NULL;
	size_t culpritat = 0;
	char names[160];
	char line[224];
	int32_t n = 0;
	size_t i;

	if (count == 0 || steps == NULL)
		return;

	names[0] = '\0';

	for (i = 1; i < count; i++)
	{
		if (steps[i].playerhash == steps[i - 1].playerhash)
			continue;

		if (n < (int32_t)sizeof (names) - 24)
		{
			n += snprintf(names + n, sizeof (names) - n, "%s%s",
				(n > 0 ? ", " : ""), steps[i].name);
		}
	}

	snprintf(line, sizeof (line), "%s: the restore changed the players at: %s",
		cmd, (n > 0 ? names : "no step after the first"));
	K_Finding(line);

	// Which of those changed a *field*. The steps that rebuild the world
	// legitimately rewrite every pointer a player holds, so a hash moving at
	// "thinkers" says nothing on its own -- and every one of them would have to
	// be read by hand to find out which.
	n = 0;
	names[0] = '\0';

	for (i = 1; i < count; i++)
	{
		const uint8_t *before = P_GetProfilePlayers(i - 1);
		const uint8_t *after = P_GetProfilePlayers(i);
		uint32_t fields;

		if (before == NULL || after == NULL)
			continue;

		fields = K_CountFieldRuns(before, after);

		if (fields == 0)
			continue;

		if (culprit == NULL && strcmp(steps[i].name, "players") != 0)
		{
			culprit = steps[i].name;
			culpritat = i;
		}

		if (n < (int32_t)sizeof (names) - 32)
		{
			n += snprintf(names + n, sizeof (names) - n, "%s%s (%u)",
				(n > 0 ? ", " : ""), steps[i].name, fields);
		}
	}

	snprintf(line, sizeof (line), "%s: fields, not addresses, changed at: %s",
		cmd, (n > 0 ? names : "no step"));
	K_Finding(line);

	// And what the first step that had no business doing so actually wrote.
	if (culprit != NULL)
	{
		K_ComparePlayers(culprit, P_GetProfilePlayers(culpritat - 1),
			P_GetProfilePlayers(culpritat), -1);
	}
}

/** Says which of a player's attached objects appeared or vanished.
  *
  * The record's flags word is a bit per object a player has hold of, and a
  * difference in it means one of them was attached on one pass and not the
  * other. Reading which bit that was took a hex window and the enum by hand;
  * the pointers are right here in the two captures, so ask them instead.
  *
  * Only whether a pointer is null is compared. The addresses themselves differ
  * across a restore by design.
  */
static void K_ReportAttachments(const char *cmd, const uint8_t *was, const uint8_t *now)
{
	static const struct { size_t at; const char *name; } attach[] =
	{
		{ offsetof(player_t, awayview.mobj), "awayview.mobj" },
		{ offsetof(player_t, followmobj), "followmobj" },
		{ offsetof(player_t, follower), "follower" },
		{ offsetof(player_t, skybox.viewpoint), "skybox.viewpoint" },
		{ offsetof(player_t, skybox.centerpoint), "skybox.centerpoint" },
		{ offsetof(player_t, hoverhyudoro), "hoverhyudoro" },
		{ offsetof(player_t, ballhogreticule), "ballhogreticule" },
		{ offsetof(player_t, stumbleIndicator), "stumbleIndicator" },
		{ offsetof(player_t, wavedashIndicator), "wavedashIndicator" },
		{ offsetof(player_t, trickIndicator), "trickIndicator" },
		{ offsetof(player_t, whip), "whip" },
		{ offsetof(player_t, hand), "hand" },
		{ offsetof(player_t, ringShooter), "ringShooter" },
		{ offsetof(player_t, flickyAttacker), "flickyAttacker" },
		{ offsetof(player_t, powerup.flickyController), "powerup.flickyController" },
		{ offsetof(player_t, powerup.barrier), "powerup.barrier" },
		{ offsetof(player_t, stoneShoe), "stoneShoe" },
		{ offsetof(player_t, toxomisterCloud), "toxomisterCloud" },
		{ offsetof(player_t, flybot), "flybot" },
	};

	int32_t i;
	size_t k;

	if (was == NULL || now == NULL)
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		const uint8_t *a = was + (sizeof (player_t) * i);
		const uint8_t *b = now + (sizeof (player_t) * i);

		if (playeringame[i] == false)
			continue;

		for (k = 0; k < sizeof (attach) / sizeof (attach[0]); k++)
		{
			uintptr_t pa = 0, pb = 0;
			char text[160];

			memcpy(&pa, a + attach[k].at, sizeof (pa));
			memcpy(&pb, b + attach[k].at, sizeof (pb));

			if ((pa != 0) == (pb != 0))
				continue;

			snprintf(text, sizeof (text),
				"%s: player %d has %s on the %s and not on the %s",
				cmd, i, attach[k].name,
				(pa != 0) ? "live pass" : "replay",
				(pa != 0) ? "replay" : "live pass");
			K_Finding(text);
		}
	}
}

/** Says which bytes of which player structure differ between two captures.
  *
  * The archive cannot answer this question about itself. Comparing snapshots
  * only ever compares what the archive carries, so a field it does not carry is
  * equal on both sides by construction and invisible however hard you look.
  * Reading the structures themselves has no such blind spot.
  *
  * Offsets are reported, not names: read them against the layout the build's
  * .pdb gives -- dt player_t under cdb -- rather than by counting through
  * d_player.h, which is how two fields came to be reported at one offset.
  */
static void K_ComparePlayers(const char *cmd, const uint8_t *was, const uint8_t *now, int32_t blamed)
{
	uint32_t fields = 0, addresses = 0, examined = 0, reported = 0;
	int32_t order;
	char text[160];

	if (was == NULL || now == NULL)
		return;

	// The blamed player first. A quota spent from player zero upwards is a
	// quota spent on interpolation and HUD counters, which differ on every
	// check because no archive carries them -- and the player the check is
	// about is usually well down the table.
	for (order = -1; order < MAXPLAYERS; order++)
	{
		const int32_t i = (order < 0) ? blamed : order;
		const uint8_t *a;
		const uint8_t *b;
		uint32_t here = 0;
		size_t at;

		if (i < 0 || i >= MAXPLAYERS || playeringame[i] == false)
			continue;

		if (order >= 0 && i == blamed)
			continue;

		a = was + (sizeof (player_t) * i);
		b = now + (sizeof (player_t) * i);
		examined++;

		for (at = 0; at < sizeof (player_t); )
		{
			size_t run;

			if (a[at] == b[at])
			{
				at++;
				continue;
			}

			for (run = 0; at + run < sizeof (player_t) && a[at + run] != b[at + run]; run++)
				;

			if (K_RunIsAddress(a, b, at, sizeof (player_t)))
			{
				addresses++;
			}
			else
			{
				fields++;

				// The values, not just the offset: "three bytes differ" does not
				// say whether a field was lost, truncated or merely moved. A few
				// per player, so one noisy player cannot fill the report.
				if (here < 3 && reported < 12)
				{
					char before[32], after[32];
					size_t k;
					int32_t nb = 0, na = 0;

					for (k = 0; k < run && k < 8; k++)
					{
						nb += snprintf(before + nb, sizeof (before) - nb, "%02x ", a[at + k]);
						na += snprintf(after + na, sizeof (after) - na, "%02x ", b[at + k]);
					}

					snprintf(text, sizeof (text),
						"%s: player %d, %s bytes into player_t: %s bytes, %s-> %s",
						cmd, i, sizeu1(at), sizeu2(run), before, after);
					K_Finding(text);

					here++;
					reported++;
				}
			}

			at += run;
		}
	}

	// Offsets to read the lines above against. Printed as numbers rather than
	// passed through sizeu, which keeps one buffer per name and quietly hands
	// back the same number twice when a line asks for the same buffer more than
	// once -- which is how cmd and faultflash came to be reported at 892 alike.
	if (fields > 0)
	{
		snprintf(text, sizeof (text),
			"%s: offsets -- cmd %u, oldcmd %u, tilt %u, karthud %u, timeshitprev %u, roundconditions %u",
			cmd,
			(unsigned)offsetof(player_t, cmd), (unsigned)offsetof(player_t, oldcmd),
			(unsigned)offsetof(player_t, tilt), (unsigned)offsetof(player_t, karthud),
			(unsigned)offsetof(player_t, timeshitprev), (unsigned)offsetof(player_t, roundconditions));
		K_Finding(text);
	}

	// What was looked at, not only what was found. A run of field lines says
	// nothing about whether the scan reached the player that mattered.
	snprintf(text, sizeof (text),
		"%s: %u players examined -- %u runs differ as fields, %u as addresses, %u shown",
		cmd, examined, fields, addresses, reported);
	K_Finding(text);
}

/** Prints the first few archived objects in the order the lists hold them.
  *
  * A hash says the order changed; this says how. Reversed, rotated or shuffled
  * are three different faults with three different fixes, and the sequence
  * makes the difference obvious where a number cannot.
  */
static void K_PrintOrder(const char *cmd, const char *when)
{
	char line[128];
	thinker_t *th;
	int32_t n = 0;
	int32_t shown = 0;
	int32_t counted = 0;

	line[0] = 0;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (mo->mobjnum == 0 || TypeIsNetSynced(mo->type) == false)
			continue;

		counted++;

		if (shown < 12)
		{
			n += snprintf(line + n, sizeof (line) - n, "%u ", mo->mobjnum);
			shown++;
		}

	}

	// The population comes first on purpose. An instrument that examined
	// nothing looks exactly like one that found nothing wrong, and I have
	// already read the first as the second three times in a day.
	CONS_Printf("%s: thinker list %s: %d archived objects, first: %s\n",
		cmd, when, counted, line);
}

/** Prints who is on the grid.
  *
  * Every measurement below scales with this, and it is not something to be
  * counted off a screenshot: a Grand Prix grid is a fixed eight, a Match Race
  * fills to maxplayers, and the two are easy to mistake for each other.
  */
static void K_PrintGrid(const char *cmd)
{
	uint32_t racers = 0, bots = 0, spectators = 0;
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] == false)
			continue;

		if (players[i].spectator)
			spectators++;
		else
		{
			racers++;
			if (players[i].bot)
				bots++;
		}
	}

	// The map belongs on this line as much as the grid does: a measurement
	// taken on a bare test map and read as one from a real course is wrong by
	// more than the grid size, and nothing else here would say so.
	CONS_Printf("%s: %s, %u racers (%u of them bots), %u spectators, %s\n",
		cmd, G_BuildMapName(gamemap), racers, bots, spectators,
		(grandprixinfo.gp ? "Grand Prix" : "not a Grand Prix"));
}

/** Says how two snapshots of what ought to be the same state compare.
  *
  * Shared by both tests: one puts a state through the archive and back, the
  * other runs the same tics twice, and both then ask the same question.
  *
  * \return true when the two are byte for byte the same.
  */
static dboolean K_ReportComparison(const char *cmd, const char *what,
	const rollbackslot_t *a, const char *labela,
	const rollbackslot_t *b, const char *labelb,
	const diagset_t *recsa, const diagset_t *recsb, dboolean records)
{
	// Walk the shorter of the two first, so a length mismatch still reports
	// where they stopped agreeing rather than only that they differ.
	const size_t shared = (a->used < b->used) ? a->used : b->used;
	size_t at;

	for (at = 0; at < shared; at++)
	{
		if (a->buffer[at] != b->buffer[at])
			break;
	}

	if (a->used == b->used && at == shared)
	{
		CONS_Printf("%s: %s IDENTICAL over all %s bytes\n",
			cmd, what, sizeu1(a->used));
		return true;
	}

	if (a->used != b->used)
	{
		CONS_Printf("%s: %s DIFFERS -- %s is %s bytes, %s was %s\n",
			cmd, what, labelb, sizeu1(b->used), labela, sizeu2(a->used));
	}

	if (at < shared)
	{
		CONS_Printf("%s: first difference at byte %s, in the '%s' block "
			"(0x%02x became 0x%02x)\n",
			cmd, sizeu1(at),
			P_LocateSnapshotBlock(a->buffer, a->used, at),
			a->buffer[at], b->buffer[at]);

		K_PrintSnapshotContext(labela, a->buffer, a->used, at);
		K_PrintSnapshotContext(labelb, b->buffer, b->used, at);

		// The players block has no markers inside it, so an offset there means
		// nothing until it is read as a player and a distance into its record.
		{
			uint8_t who;
			size_t into;

			if (P_LocatePlayerField(at, &who, &into))
			{
				const char *field = P_NamePlayerField(a->buffer, a->used, who, into);

				CONS_Printf("%s: that is player %u (%s), %s bytes into their record -- %s\n",
					cmd, who,
					(playeringame[who] ? player_names[who] : "not in game"),
					sizeu1(into),
					(field != NULL) ? field : "past the fields this can name");

				// Where the structure comparison should look first.
				g_blamedplayer = (int32_t)who;
			}
		}
	}
	else
	{
		CONS_Printf("%s: the shorter one is a prefix of the longer, so what changed "
			"sits at the end -- in the '%s' block\n",
			cmd, P_LocateSnapshotBlock(a->buffer, a->used, shared));
	}

	// Which object, and which of its fields -- the byte offset above says
	// neither on its own.
	if (records)
		K_ReportRecordDifferences(cmd, recsa, recsb);
	else if (recsa == NULL || recsb == NULL)
	{
		// Not the same thing as running out of memory, and mistaking one for the
		// other cost two readings of a log: this caller never asks for the
		// per-object pass at all.
		CONS_Printf("%s: no per-object comparison here -- this command does not collect one\n",
			cmd);
	}
	else
		CONS_Printf("%s: no memory for the per-object comparison\n", cmd);

	return false;
}

static void K_FreeDiagSet(diagset_t *set)
{
	Z_Free(set->bytes);
	Z_Free(set->recs);
	set->bytes = NULL;
	set->recs = NULL;
}

/** Console command: rollback_test
  *
  * Snapshots the current state, perturbs it, restores it, then snapshots it a
  * second time and compares the two snapshots byte for byte. Reports the
  * snapshot size and the cost of each step.
  *
  * What the byte comparison proves: everything the archiver writes survives
  * being read back and written out again unchanged. That covers every field of
  * every mobj, thinker, sector and player the archive touches -- far more than
  * Consistancy() looks at, and without depending on MOBJCONSISTANCY being
  * compiled in.
  *
  * What it does NOT prove: that the archive captures everything the game
  * simulates. A field nobody archives is absent from both snapshots alike, so
  * it compares equal while still being lost across a real rollback. Catching
  * those needs a resimulation test, which comes with the tic loop hook.
  *
  * The perturbation is load-bearing rather than decorative: without it, a load
  * that silently did nothing at all would still compare equal. Disturbing the
  * RNG stream guarantees the state genuinely diverged before the restore,
  * since the seeds are part of the archive.
  *
  * One difference is expected rather than a defect: LUA_Archive walks Lua
  * tables with lua_next, whose order depends on the table's internal layout,
  * and unarchiving rebuilds those tables from scratch. On a map with Lua
  * ExtVars the two snapshots can disagree in the Lua archive for that reason
  * alone. It sits between the waypoints and RNG markers, so
  * P_LocateSnapshotBlock attributes it to "waypoints" -- read a difference
  * reported there with that in mind.
  */
static void Command_RollbackTest_f(void)
{
	rollbackslot_t *original, *resaved;
	diagset_t recsbefore = {0}, recsafter = {0};
	dboolean records = false;
	precise_t started;
	uint32_t saveus, loadus, resaveus;
	int16_t before, afterperturb, afterload;
	uint32_t thinkerorder, blockmaporder, sectororder;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return;
	}

	K_PrintGrid("rollback_test");

	// Somewhere to put the second snapshot that is not part of the ring.
	// Transient: a megabyte is not worth holding on to between invocations of
	// a diagnostic command.
	resaved = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
	if (!resaved)
	{
		CONS_Printf("rollback_test: could not allocate the comparison buffer\n");
		return;
	}

	before = Consistancy();

	started = I_GetPreciseTime();
	if (!K_SaveGameState(gametic))
	{
		CONS_Printf("rollback_test: K_SaveGameState failed\n");
		Z_Free(resaved);
		return;
	}
	saveus = K_PreciseToMicros(I_GetPreciseTime() - started);

	original = &rollbackring[gametic % ROLLBACK_TICS];

	// Straight after the save, while the mobjnums it handed out still mean
	// something. The ordering below reads them too, which is why it cannot be
	// taken any earlier: before the save every mobjnum is zero, and comparing
	// against nothing reports a change every time.
	K_ReportLostReferences();

	thinkerorder = K_HashOrder(K_ORDER_THINKERS);
	blockmaporder = K_HashOrder(K_ORDER_BLOCKMAP);
	sectororder = K_HashOrder(K_ORDER_SECTORS);
	K_PrintOrder("rollback_test", "before the restore");
	K_CopyPlayers(0);
	K_CopyMobjs();

	// Same window: the per-object records depend on that numbering too. Taken
	// outside the timed sections, and read-only, so neither the measurements
	// nor the state are affected. A failure here costs the object-level
	// report, nothing else.
	recsbefore.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
	recsbefore.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);
	recsafter.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
	recsafter.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);

	records = (recsbefore.bytes && recsbefore.recs && recsafter.bytes && recsafter.recs);
	if (records)
		K_CaptureRecords(&recsbefore);

	P_RandomFixed(PR_UNDEFINED);
	P_RandomFixed(PR_UNDEFINED);
	P_RandomFixed(PR_UNDEFINED);

	afterperturb = Consistancy();

	started = I_GetPreciseTime();
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_test: K_LoadGameState failed\n");
		Z_Free(resaved);
		K_FreeDiagSet(&recsbefore);
		K_FreeDiagSet(&recsafter);
		return;
	}
	loadus = K_PreciseToMicros(I_GetPreciseTime() - started);
	g_lastrestoreus = loadus;

	afterload = Consistancy();

	if (thinkerorder == 2166136261u || blockmaporder == 2166136261u)
	{
		// The empty hash. Something was measured before it existed.
		CONS_Printf("rollback_test: the order reading saw no archived objects "
			"beforehand, so it says nothing about ordering\n");
	}
	else
	{
		CONS_Printf("rollback_test: thinker order %s, blockmap order %s, sector order %s\n",
			(K_HashOrder(K_ORDER_THINKERS) == thinkerorder ? "kept" : "CHANGED"),
			(K_HashOrder(K_ORDER_BLOCKMAP) == blockmaporder ? "kept" : "CHANGED"),
			(K_HashOrder(K_ORDER_SECTORS) == sectororder ? "kept" : "CHANGED"));
	}

	K_PrintOrder("rollback_test", "after the restore ");
	K_ComparePlayers("rollback_test", g_playercopy[0], (const uint8_t *)players, -1);
	K_CompareMobjs("rollback_test");

	K_PrintLoadProfile("rollback_test");

	if (records)
		K_CaptureRecords(&recsafter);

	started = I_GetPreciseTime();
	if (!K_WriteSnapshot(resaved, gametic))
	{
		CONS_Printf("rollback_test: second K_WriteSnapshot failed\n");
		Z_Free(resaved);
		K_FreeDiagSet(&recsbefore);
		K_FreeDiagSet(&recsafter);
		return;
	}
	resaveus = K_PreciseToMicros(I_GetPreciseTime() - started);

	CONS_Printf("rollback_test: snapshot %s bytes, %s%% of the %s byte slot\n",
		sizeu1(original->used),
		sizeu2((original->used * 100) / ROLLBACK_BUFSIZE),
		sizeu3((size_t)ROLLBACK_BUFSIZE));

	CONS_Printf("rollback_test: save %u us, load %u us, re-save %u us\n",
		saveus, loadus, resaveus);

	K_ReportComparison("rollback_test", "round-trip",
		original, "original", resaved, "re-saved",
		&recsbefore, &recsafter, records);

	CONS_Printf("rollback_test: consistancy before=%d perturbed=%d afterload=%d -- %s\n",
		before, afterperturb, afterload,
		(afterload == before) ? "PASS" : "FAIL");

	// A PASS means nothing if the perturbation was invisible to Consistancy()
	// in the first place -- say it out loud rather than report a false pass.
	if (afterperturb == before)
	{
		CONS_Printf("rollback_test: WARNING - perturbing the RNG did not change "
			"Consistancy(), so the consistancy line proves nothing.\n");
	}

#ifndef MOBJCONSISTANCY
	CONS_Printf("rollback_test: note - this build has MOBJCONSISTANCY off, so the "
		"consistancy figure covers only player positions, held item and the RNG "
		"seeds. The byte comparison is the real result.\n");
#endif

	Z_Free(resaved);
	K_FreeDiagSet(&recsbefore);
	K_FreeDiagSet(&recsafter);
}

// ----------------------------------------------------------------------------
// rollback_resim, and the soak that runs it by itself
// ----------------------------------------------------------------------------

/** Runs a number of tics with the inputs held fixed.
  *
  * P_Ticker is the whole of a game tic: everything the world does in a
  * thirty-fifth of a second. What it does not do is fetch inputs -- G_Ticker
  * copies those out of netcmds beforehand, and netcmds holds the tic the game
  * is about to run, not the tics being replayed. So the caller freezes the
  * inputs once and they are re-applied here before every tic, which is what
  * makes two runs of the same tics comparable.
  *
  * Bots need nothing special: their commands are built by the netcode rather
  * than by P_Ticker, so through a replay they carry on with the frozen ones.
  * That is deterministic, which is all this asks of them.
  */
static void K_RunFrozenTics(int32_t tics, const ticcmd_t *frozen)
{
	int32_t n, i;

	for (n = 0; n < tics; n++)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
				players[i].cmd = frozen[i];
		}

		P_Ticker(true);
	}
}

/** Runs the same tics twice from the same state and compares where they end up.
  *
  * Snapshot, play N tics, snapshot, restore, play the same N tics again,
  * snapshot, compare the two endings byte for byte.
  *
  * This is the question rollback_test cannot answer. That one proves the
  * archive can read back what it wrote; this one proves the archive carries
  * everything the simulation needs. A field nobody archives is missing from
  * both sides of a round trip and compares equal, but a resimulation starting
  * from a state that lost it goes somewhere else -- which is the failure that
  * would end this approach.
  *
  * The world is put back where this found it, so a check does not leave the
  * level ahead of the tic the netcode believes it is on. Sounds and screen
  * effects from both passes do play, though: they are not part of the state,
  * so nothing rewinds them.
  *
  * \param verbose prints the grid and the timings even when nothing is wrong.
  *        A failure reports itself either way.
  * \return true if both passes ended in the same state.
  */
static dboolean K_ResimCheck(int32_t tics, dboolean verbose)
{
	// Where viewx moves. The tilt trace says it differs between the passes, and
	// the only code that writes it draws a frame -- which nothing between them
	// is supposed to do. Four readings say whether it moves across the restore
	// or across a tic, and one of those is a much smaller place to look.
	fixed_t vx[4], vy[4];
	rollbackslot_t *first, *second, *third;
	diagset_t recsfirst = {0}, recssecond = {0}, recsthird = {0};
	ticcmd_t frozen[MAXPLAYERS];
	precise_t started;
	uint32_t firstus, secondus;
	tic_t startedat;
	int32_t i;
	dboolean records;
	dboolean identical = false;
	dboolean repeatable = false;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return false;
	}

	if (tics < 1)
		tics = 1;

	// Past the ring's depth the exercise stops resembling a rollback.
	if (tics > ROLLBACK_TICS)
		tics = ROLLBACK_TICS;

	if (verbose)
		K_PrintGrid("rollback_resim");

	// Allocated once and kept. A soak runs this hundreds of times, and
	// taking three megabytes and giving them back on every check fragments
	// the zone until something innocent cannot find room -- which is exactly
	// how a soak killed a session with "not enough memory for item roulette
	// list", an allocation that had nothing to do with any of this.
	if (K_NeedScratch() == false)
	{
		CONS_Printf("rollback_resim: not enough memory for the comparison slots\n");
		return false;
	}

	K_NeedDiagSets();

	first = g_first;
	second = g_second;
	third = g_third;
	recsfirst = g_recsfirst;
	recssecond = g_recssecond;
	recsthird = g_recsthird;

	records = (recsfirst.bytes && recsfirst.recs && recssecond.bytes && recssecond.recs
		&& recsthird.bytes && recsthird.recs);

	// The inputs of the tic the game is sitting on, reused for every replayed
	// tic of both passes. Not what really happened over those tics, but the
	// same thing twice, which is what the comparison needs.
	for (i = 0; i < MAXPLAYERS; i++)
		frozen[i] = players[i].cmd;

	if (!K_SaveGameState(gametic))
	{
		CONS_Printf("rollback_resim: K_SaveGameState failed\n");
		goto done;
	}

	// The state the snapshot was taken from, so the restore can be asked
	// whether it reproduces it -- on its own, before a tic has had the chance
	// to move anything. The round-trip test only ever proved the archive
	// re-serialises to the same bytes, which a field the restore drops on the
	// floor passes just as happily.
	K_CopyPlayers(2);

	startedat = leveltime;

	g_holdfindings = true;
	g_heldcount = 0;
	g_helddropped = 0;
	g_blamedplayer = -1;

	// M_Random draws from the C library, whose state no archive can hold, and
	// the game uses it for decoration -- item debris picks its rollangle that
	// way. Two replays would then differ over something that is local by
	// design and that no other machine ever agreed on. Seeding it identically
	// before each pass keeps the question to the one being asked: does the
	// *archived* state reproduce.
	srand((unsigned int)gametic);
	g_hittracecount[0] = g_hittracecount[1] = 0;
	g_tracedropped[0] = g_tracedropped[1] = 0;
	g_pairs[0] = g_pairs[1] = 0;
	g_pairhash[0] = g_pairhash[1] = 2166136261u;
	g_hittracing = 0;

	vx[0] = viewx; vy[0] = viewy;

	started = I_GetPreciseTime();
	K_RunFrozenTics(tics, frozen);
	firstus = K_PreciseToMicros(I_GetPreciseTime() - started);

	vx[1] = viewx; vy[1] = viewy;

	// P_Ticker returns without doing anything while the game is paused, and
	// two passes of nothing compare equal. Say so instead of reporting a pass.
	if (leveltime == startedat)
	{
		CONS_Printf("rollback_resim: the world did not advance -- the game is paused, "
			"or the window is unfocused and pauseifunfocused is on\n");
		goto done;
	}

	if (!K_WriteSnapshot(first, gametic))
	{
		CONS_Printf("rollback_resim: could not snapshot the first pass\n");
		goto done;
	}

	if (records)
		K_CaptureRecords(&recsfirst);

	// The world as the first pass left it. The restore itself has been shown
	// clean often enough; what is unexplained is what the tic computes, so the
	// two ends are what to compare -- and this sees the fields the archive
	// does not carry, which a snapshot comparison never will.
	K_CopyPlayers(0);
	K_CopyMobjs();

	// Watched over this restore only: hashing every player at every step is
	// not something the game should pay for outside a check.
	P_ProfileWatchPlayers(true);

	if (!K_LoadGameState(gametic))
	{
		P_ProfileWatchPlayers(false);
		CONS_Printf("rollback_resim: could not get back to the starting state -- "
			"the level is left where the first pass ended\n");
		goto done;
	}

	vx[2] = viewx; vy[2] = viewy;

	K_ComparePlayers("rollback_resim restore", g_playercopy[2],
		(const uint8_t *)players, -1);
	K_ReportRestoreSteps("rollback_resim restore");
	P_ProfileWatchPlayers(false);

	srand((unsigned int)gametic);
	g_hittracing = 1;

	started = I_GetPreciseTime();
	K_RunFrozenTics(tics, frozen);
	secondus = K_PreciseToMicros(I_GetPreciseTime() - started);
	g_lastresimus = secondus / (uint32_t)tics;

	vx[3] = viewx; vy[3] = viewy;

	{
		char text[160];

		snprintf(text, sizeof (text),
			"rollback_resim: view %08x/%08x -> %08x/%08x over the first pass, "
			"%08x/%08x after the restore, %08x/%08x over the second",
			(uint32_t)vx[0], (uint32_t)vy[0], (uint32_t)vx[1], (uint32_t)vy[1],
			(uint32_t)vx[2], (uint32_t)vy[2], (uint32_t)vx[3], (uint32_t)vy[3]);
		K_Finding(text);
	}

	if (!K_WriteSnapshot(second, gametic))
	{
		CONS_Printf("rollback_resim: could not snapshot the second pass\n");
		goto done;
	}

	if (records)
		K_CaptureRecords(&recssecond);

	// The players as the second pass left them. The comparison itself waits for
	// the archive comparison below to say which player to start with.
	K_CopyPlayers(1);
	K_CompareMobjs("rollback_resim");

	// A third pass, from a restored state like the second. The first pass ran
	// from the live world, and what the archive does not carry -- decoration,
	// the C library's generator, anything nobody saves -- is left wherever the
	// pass before put it. So first against second answers "does a restored
	// world behave like the live one", while second against third answers "is
	// the replay repeatable at all". The two failures need different fixes and
	// look identical without this.
	if (K_LoadGameState(gametic))
	{
		// Not recorded. This pass exists to tell a repeatable replay from a
		// lossy restore, and leaving the trace armed folded its events into the
		// replay's tally -- which is how "the replay lands twice the hits"
		// came to be reported, and why every one of those figures was exactly
		// double.
		g_hittracing = -1;

		srand((unsigned int)gametic);
		K_RunFrozenTics(tics, frozen);

		if (K_WriteSnapshot(third, gametic) && records)
			K_CaptureRecords(&recsthird);
	}

	if (verbose)
	{
		CONS_Printf("rollback_resim: %d tics took %u us, then %u us -- %u us per tic\n",
			tics, firstus, secondus, secondus / (uint32_t)tics);
	}

	identical = (first->used == second->used
		&& memcmp(first->buffer, second->buffer, first->used) == 0);

	repeatable = (second->used == third->used
		&& memcmp(second->buffer, third->buffer, second->used) == 0);

	// Silence is the point of a soak: thousands of passes should say nothing,
	// so that the one failure is impossible to miss.
	if (verbose || identical == false)
	{
		if (identical == false)
			K_PrintGrid("rollback_resim");

		K_ReportComparison("rollback_resim", "resimulation",
			first, "first pass", second, "second pass",
			&recsfirst, &recssecond, records);

		K_ReportTrace("rollback_resim");

		// The player structures at the end of both passes, now that the
		// comparison above has named the player worth starting with.
		// Attachments first: one line that names an object, ahead of two dozen
		// lines of offsets and bytes.
		K_ReportAttachments("rollback_resim", g_playercopy[0], g_playercopy[1]);
		K_ComparePlayers("rollback_resim", g_playercopy[0], g_playercopy[1],
			g_blamedplayer);

		// What the restore itself did to the world, gathered before the replay
		// ran and worth reading now that it went somewhere else.
		K_ReleaseFindings();

		if (identical == false)
		{
			if (repeatable)
			{
				CONS_Printf("rollback_resim: but the two restored passes agree with each "
					"other, so the replay is repeatable and it is the restore that loses "
					"something the simulation uses\n");
			}
			else
			{
				CONS_Printf("rollback_resim: the two restored passes disagree as well, so "
					"the replay is not repeatable regardless of the restore\n");
				K_ReportComparison("rollback_resim", "replay",
					second, "second pass", third, "third pass",
					&recssecond, &recsthird, records);
			}
		}
	}

	// Back to where this found the world.
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_resim: WARNING - could not restore the starting state, "
			"so the level is now %d tics ahead of where it was\n", tics);
	}

done:
	g_hittracing = -1;
	g_holdfindings = false;

	return identical;
}

/** Console command: rollback_resim [tics] */
static void Command_RollbackResim_f(void)
{
	int32_t tics = 4;

	if (COM_Argc() > 1)
		tics = atoi(COM_Argv(1));

	K_ResimCheck(tics, true);
}

// ----------------------------------------------------------------------------
// The soak
//
// One resimulation on a starting grid proves very little. The archive only has
// to miss a field that nothing touches at the start of a race -- an item in
// flight, hitlag, a respawn, a lap counter -- for the check to pass every time
// and the approach to still be broken. So run it over and over, through whole
// races, and say nothing until something disagrees.
// ----------------------------------------------------------------------------

static int32_t g_soakinterval;  // tics between checks, 0 when off
static int32_t g_soaktics;      // tics resimulated per check
static dboolean g_soakbusy;     // a check is running; do not start another
static uint32_t g_soakchecks;
static uint32_t g_soakfailures;

/** Console command: rollback_soak [interval] [tics]
  *
  * With no arguments, reports what the soak has seen so far. An interval of 0
  * turns it off.
  */
static void Command_RollbackSoak_f(void)
{
	if (COM_Argc() <= 1)
	{
		if (g_soakinterval == 0)
		{
			CONS_Printf("rollback_soak: off. %u checks so far, %u failures.\n",
				g_soakchecks, g_soakfailures);
		}
		else
		{
			CONS_Printf("rollback_soak: every %d tics, resimulating %d. "
				"%u checks so far, %u failures.\n",
				g_soakinterval, g_soaktics, g_soakchecks, g_soakfailures);
		}
		return;
	}

	g_soakinterval = atoi(COM_Argv(1));

	if (g_soakinterval < 0)
		g_soakinterval = 0;

	if (COM_Argc() > 2)
		g_soaktics = atoi(COM_Argv(2));

	if (g_soaktics < 1)
		g_soaktics = 4;

	if (g_soakinterval == 0)
	{
		CONS_Printf("rollback_soak: stopped after %u checks, %u failures.\n",
			g_soakchecks, g_soakfailures);
		return;
	}

	g_soakchecks = 0;
	g_soakfailures = 0;

	CONS_Printf("rollback_soak: checking every %d tics, resimulating %d tics each time. "
		"Silence means agreement.\n", g_soakinterval, g_soaktics);
}

/** Runs a soak check when one is due. Called once per tic from G_Ticker.
  *
  * A check costs far more than the tic it runs in -- two resimulations and two
  * restores -- so the game will not keep real time while the soak is on. That
  * is fine where this is meant to run, which is a dedicated server with nobody
  * watching.
  */
// ----------------------------------------------------------------------------
// Keeping every tic, and replaying one with the inputs that really ran
//
// The soak replays with the inputs frozen, which is what makes its two passes
// comparable -- and blind to everything edge-triggered, because a button held
// through a frozen tic was never pressed during it. A rollback replays what
// actually happened, so the ring has to fill during ordinary play and the
// replay has to read the inputs back out of netcmds, where the netcode keeps
// 512 tics of them.
//
// Nothing here changes how the game runs. Keeping snapshots costs one save a
// tic and is off by default; replaying is a command, and it puts the world back
// where it found it.
// ----------------------------------------------------------------------------

static dboolean g_keeping;

/** Console command: rollback_keep <0|1> */
static void Command_RollbackKeep_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf("rollback_keep <0|1>: currently %s. Keeps a snapshot of "
			"every tic, so a replay can start from any of the last %d.\n",
			g_keeping ? "on" : "off", ROLLBACK_TICS - 1);
		return;
	}

	g_keeping = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_keep: %s\n", g_keeping ? "on" : "off");
}

/** Names the fields two inputs disagree on, decoded rather than left in hex.
  *
  * The snapshot comparison can only say "cmd, 204 bytes into their record",
  * which then wants a ticcmd laid out by hand -- and the two fields that turned
  * up that way, angle and bot.turnconfirm, are exactly the ones a bot computes
  * for itself. Naming them costs eleven lines.
  *
  * \return out, which is empty when the two agree.
  */
static const char *K_NameTiccmdDifferences(char *out, size_t outsize,
	const ticcmd_t *a, const ticcmd_t *b)
{
	int32_t n = 0;

	out[0] = '\0';

#define ROLLBACK_CMDFIELD(name, value) \
	if ((a->value) != (b->value) && n < (int32_t)outsize - 48) \
	{ \
		n += snprintf(out + n, outsize - n, "%s%s %d vs %d", \
			(n > 0 ? ", " : ""), name, (int32_t)(a->value), (int32_t)(b->value)); \
	}

	ROLLBACK_CMDFIELD("forwardmove", forwardmove)
	ROLLBACK_CMDFIELD("turning", turning)
	ROLLBACK_CMDFIELD("angle", angle)
	ROLLBACK_CMDFIELD("throwdir", throwdir)
	ROLLBACK_CMDFIELD("aiming", aiming)
	ROLLBACK_CMDFIELD("buttons", buttons)
	ROLLBACK_CMDFIELD("latency", latency)
	ROLLBACK_CMDFIELD("flags", flags)
	ROLLBACK_CMDFIELD("bot.turnconfirm", bot.turnconfirm)
	ROLLBACK_CMDFIELD("bot.spindashconfirm", bot.spindashconfirm)
	ROLLBACK_CMDFIELD("bot.itemconfirm", bot.itemconfirm)

#undef ROLLBACK_CMDFIELD

	return out;
}

/** Says whether the replay left behind the inputs the present was holding.
  *
  * cmd and oldcmd are inputs, not simulated state: the replay hands each tic the
  * input that ran, so it ends holding the last one, while the world it is
  * compared against was holding the input for the tic about to run. Putting them
  * back is part of leaving the world where this found it -- but a difference put
  * back silently is a difference nobody can see, so it is named and counted
  * first, and the count prints even when it is zero.
  *
  * \return how many differences were named.
  */
static uint32_t K_ReportReplayInputs(const char *cmd,
	const ticcmd_t *livecmd, const ticcmd_t *liveold)
{
	char fields[192];
	uint32_t named = 0;
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		K_NameTiccmdDifferences(fields, sizeof (fields), &livecmd[i], &players[i].cmd);

		if (fields[0] != '\0')
		{
			CONS_Printf("%s: player %d (%s) cmd, present vs replay -- %s\n",
				cmd, i, player_names[i], fields);
			named++;
		}

		K_NameTiccmdDifferences(fields, sizeof (fields), &liveold[i], &players[i].oldcmd);

		if (fields[0] != '\0')
		{
			CONS_Printf("%s: player %d (%s) oldcmd, present vs replay -- %s\n",
				cmd, i, player_names[i], fields);
			named++;
		}
	}

	CONS_Printf("%s: %u input difference(s) named, all put back before the comparison\n",
		cmd, named);

	return named;
}

/** Console command: rollback_replay [tics]
  *
  * Rewinds that many tics and replays them with the inputs that really ran,
  * then checks the world arrives where it already was. This is the operation a
  * rollback performs, done deliberately instead of in response to a packet, and
  * it is the first thing here that replays real inputs rather than frozen ones.
  */
static void Command_RollbackReplay_f(void)
{
	rollbackslot_t *present;
	precise_t started;
	uint32_t us;
	int32_t n = 4;
	int32_t i;
	ticcmd_t livecmd[MAXPLAYERS], liveold[MAXPLAYERS];
	int32_t ran = 0;
	tic_t from, t, now;
	tic_t ltbefore, ltafter, ltloaded;
	dboolean records;

	if (COM_Argc() > 1)
		n = atoi(COM_Argv(1));

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("rollback_replay: not in a level\n");
		return;
	}

	// One slot holds where the replay starts and one holds where it has to
	// arrive, so the ring cannot be asked for its whole length.
	if (n < 1 || n > ROLLBACK_TICS - 2)
	{
		CONS_Printf("rollback_replay: between 1 and %d tics\n", ROLLBACK_TICS - 2);
		return;
	}

	// A console command runs at the top of TryRunTics, before the tic loop, so
	// gametic is the tic about to run and the world is the one the tic before
	// it left. Rewinding from gametic replayed one tic too many, and the
	// comparison said so: the misc block, where leveltime lives, at byte 22.
	now = gametic - 1;

	if ((tic_t)n >= now)
	{
		CONS_Printf("rollback_replay: the game has not run that many tics yet\n");
		return;
	}

	if (K_NeedScratch() == false)
	{
		CONS_Printf("rollback_replay: not enough memory for the comparison slots\n");
		return;
	}

	// The byte offset alone said "the thinkers block", which is where the
	// per-object pass earns its keep: it names the object and its diff masks on
	// both sides. rollback_resim has had it from the start; this command was
	// reporting that it had none.
	records = K_NeedDiagSets();

	// Into a scratch slot rather than the ring: the ring belongs to whatever
	// the keeper put there, and a command has no business overwriting it.
	if (!K_WriteSnapshot(g_second, now))
	{
		CONS_Printf("rollback_replay: could not snapshot the present\n");
		return;
	}

	present = g_second;
	from = now - (tic_t)n;
	ltbefore = leveltime;

	// The input each player is holding for the tic the game is about to run --
	// read here, from the living world, because a local snapshot carries cmd and
	// oldcmd. Read after the restore below, this was the input of the tic the
	// replay starts from, and putting *that* back afterwards left the world
	// holding a pair it had never held. Six replays out of six said so, on a
	// bot's cmd.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		livecmd[i] = players[i].cmd;
		liveold[i] = players[i].oldcmd;
	}

	// Walks the living world, so it has to happen before the restore -- and
	// outside the timed region below, because it archives every object.
	if (records)
		K_CaptureRecords(&g_recsfirst);

	// And the structures themselves, keyed by mobjnum. The archived records are
	// written under diff masks, so a place in one is not a field; an offset into
	// mobj_t is, and the debugger turns it into a name from the pdb of the very
	// build that printed it. That is how the player side of this was read.
	K_CopyMobjs();

	if (!K_LoadGameState(from))
	{
		CONS_Printf("rollback_replay: no snapshot for tic %s -- turn rollback_keep "
			"on and let %d tics go by\n", sizeu1(from), n);
		return;
	}

	ltloaded = leveltime;

	// The inputs of each tic as the netcode recorded them, rather than one tic's
	// inputs repeated. netcmds holds BACKUPTICS of them, far more than the ring.
	started = I_GetPreciseTime();

	for (t = from + 1; t <= now; t++)
	{
		// Which tic this is, first, because the inputs are indexed by it. The
		// restore rewound gametic along with everything else -- the archive
		// carries it -- and P_Ticker does not touch it: TryRunTics is what
		// advances it, and this replays without going through TryRunTics. Left
		// alone, every replayed world ended up stamped with the tic it started
		// from, which is what the comparison kept reporting at byte 22 of the
		// misc block.
		gametic = t;

		// Through the step the live loop takes, rather than a raw copy out of
		// netcmds: G_MoveTiccmdsIntoPlayers turns the leveltime stamp a ticcmd
		// carries into the control lag the simulation reads, and a replay that
		// skipped it fed the stamp itself -- latency 130 where the live tic had
		// 2, and a bot's zero never written. Nothing has diverged on it yet,
		// because both of its readers clamp, but it is an input the simulation
		// reads.
		G_MoveTiccmdsIntoPlayers();

		P_Ticker(true);
		ran++;
	}

	// And forward to the tic the game is about to run, which is where the
	// world this was compared against stands.
	gametic = now + 1;

	// Stopped before anything is printed: CONS_Printf writes to the log as well
	// as the console, which costs milliseconds, and the per-tic figure below is
	// the whole point of the command.
	us = K_PreciseToMicros(I_GetPreciseTime() - started);
	ltafter = leveltime;

	if (records)
		K_CaptureRecords(&g_recssecond);

	// Before the present is put back, because this compares against the world
	// the replay arrived at.
	K_CompareMobjs("rollback_replay");

	// Named before they are put back, so the log still carries what the replay
	// had arrived at rather than what this wrote over it.
	K_ReportReplayInputs("rollback_replay", livecmd, liveold);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		players[i].cmd = livecmd[i];
		players[i].oldcmd = liveold[i];
	}

	if (!K_WriteSnapshot(g_first, now))
	{
		CONS_Printf("rollback_replay: could not snapshot the replay\n");
		return;
	}

	K_ReportComparison("rollback_replay", "replay", present, "the world as it was",
		g_first, "the replay", &g_recsfirst, &g_recssecond, records);

	// Where leveltime went, because the tic counter is what the comparison keeps
	// pointing at and two readings of it settle in one line what an afternoon of
	// reasoning could not: whether the restore lands where it should, and
	// whether a replayed tic advances the clock at all.
	CONS_Printf("rollback_replay: leveltime %s at the start, %s after the restore, "
		"%s after the replay\n",
		sizeu1((size_t)ltbefore), sizeu2((size_t)ltloaded), sizeu3((size_t)ltafter));

	// What it replayed, not just how long it took: a loop that ran no tics at
	// all would otherwise report a time and look like it had worked.
	CONS_Printf("rollback_replay: %s tics replayed, %s to %s, in %u us -- %u us per tic\n",
		sizeu1((size_t)ran), sizeu2((size_t)from + 1), sizeu3((size_t)now),
		us, us / (uint32_t)(ran > 0 ? ran : 1));

	// Back to where this found the world, whatever the replay decided.
	if (!K_ReadSnapshot(g_second))
	{
		CONS_Printf("rollback_replay: WARNING - could not restore the present\n");
	}
}

/** Called once per tic, after P_Ticker. */
void K_RollbackTicker(void)
{
	// After the tic, so the slot for tic N holds the world as N left it, which
	// is where N+1 starts. The soak's checks already save and load on that
	// convention.
	if (g_keeping && gamestate == GS_LEVEL && g_soakbusy == false)
		K_SaveGameState(gametic);

	K_RollbackSoakTicker();
}

void K_RollbackSoakTicker(void)
{
	if (g_soakinterval == 0 || gamestate != GS_LEVEL)
		return;

	// A check resimulates tics, and those tics must not start checks of their own.
	if (g_soakbusy)
		return;

	if ((leveltime % (tic_t)g_soakinterval) != 0)
		return;

	g_soakbusy = true;

	g_soakchecks++;

	if (K_ResimCheck(g_soaktics, false) == false)
	{
		g_soakfailures++;
		CONS_Printf("rollback_soak: FAILURE at leveltime %u -- %u of %u checks have failed\n",
			leveltime, g_soakfailures, g_soakchecks);
	}
	else if ((g_soakchecks % 10) == 0)
	{
		// Proof of life. Silence has to be distinguishable from a soak that is
		// not running at all, which is a mistake I have already made once.
		CONS_Printf("rollback_soak: %u checks, %u failures\n", g_soakchecks, g_soakfailures);
	}

	g_soakbusy = false;
}

// ----------------------------------------------------------------------------
// Input delay and rollback depth
//
// These two settings are the same trade seen from both ends, and the game
// already owns one of them.
//
// Ring Racers runs a delay-based netcode with what it calls a gentleman's
// delay: your own inputs are held back so that everyone applies them on the
// same tic, and the amount adapts to the connection. cv_mindelay is the floor
// you choose, target_lag raises it to cover the fastest opponent's ping, and
// MAXGENTLEMENDELAY caps the whole thing at a second. That is exactly what
// GGPO calls input delay, adaptive on top.
//
// Rollback does not replace it -- it changes what it has to cover. Delay pays
// for latency up front, in input lag, on every single tic. Rollback pays for it
// after the fact, in a restore and a replay, and only when a prediction turns
// out wrong. The useful arrangement is a small fixed delay to absorb jitter
// cheaply, with rollback covering the rest up to a depth we are willing to pay
// for -- and past that depth, the delay has to rise again, because a rollback
// deeper than a frame's budget would cost more than it saves.
//
// K_RollbackMaxDepth is that ceiling. Nothing enforces it yet: the tic loop
// hook that will read it does not exist. It lives here so the policy has one
// home, and so the number can be argued about against measurements rather than
// discovered by accident later.
// ----------------------------------------------------------------------------

static int32_t g_maxdepth = ROLLBACK_TICS;

int32_t K_RollbackMaxDepth(void)
{
	return g_maxdepth;
}

/** Console command: rollback_maxdepth [tics]
  *
  * How far back a rollback may rewind. Latency beyond this has to be paid for
  * with input delay instead.
  */
static void Command_RollbackMaxDepth_f(void)
{
	if (COM_Argc() > 1)
	{
		int32_t depth = atoi(COM_Argv(1));

		if (depth < 1)
			depth = 1;

		// The ring only holds so many tics; asking to rewind past its oldest
		// slot would find a stale state, not an old one.
		if (depth > ROLLBACK_TICS)
		{
			CONS_Printf("rollback_maxdepth: capped at %d, the depth of the snapshot ring\n",
				ROLLBACK_TICS);
			depth = ROLLBACK_TICS;
		}

		g_maxdepth = depth;
	}

	CONS_Printf("rollback_maxdepth: %d tics (%d ms of latency covered without input delay)\n",
		g_maxdepth, (g_maxdepth * 1000) / TICRATE);
}

/** Console command: rollback_delay
  *
  * Reports the two halves of the latency trade: what the game's own input
  * delay is doing right now, and what a rollback of the current depth would
  * cost against a tic's budget.
  */
static void Command_RollbackDelay_f(void)
{
	const uint32_t ticus = 1000000 / TICRATE;

	CONS_Printf("rollback_delay: input delay -- mindelay %d tics (your floor), "
		"engine ceiling %d\n",
		cv_mindelay.value, MAXGENTLEMENDELAY);

	if (netgame)
	{
		CONS_Printf("rollback_delay: this player is currently delayed %u tics%s\n",
			playerdelaytable[consoleplayer],
			(server_lagless ? ", server is lagless" : ""));
	}
	else
	{
		CONS_Printf("rollback_delay: offline, so nothing is being delayed\n");
	}

	CONS_Printf("rollback_delay: rollback depth %d tics (%d ms), tic budget %u us\n",
		g_maxdepth, (g_maxdepth * 1000) / TICRATE, ticus);

	if (g_lastrestoreus == 0 || g_lastresimus == 0)
	{
		CONS_Printf("rollback_delay: run rollback_test and rollback_resim to price a rollback "
			"on this machine\n");
	}
	else
	{
		const uint32_t worst = g_lastrestoreus + (g_lastresimus * (uint32_t)g_maxdepth);

		CONS_Printf("rollback_delay: measured here -- restore %u us, resimulation %u us per tic, "
			"so a full-depth rollback costs %u us, %u%% of a tic\n",
			g_lastrestoreus, g_lastresimus, worst, (worst * 100) / ticus);

		if (worst > ticus)
		{
			CONS_Printf("rollback_delay: that is over budget -- either lower the depth and "
				"raise mindelay to cover the difference, or make the restore cheaper\n");
		}
	}
}

void K_RegisterRollbackStuff(void)
{
	// Debug commands rather than plain ones: they are diagnostics, and being
	// so lists them in the pause menu's command list, which is where they can
	// be reached without typing into the console.
	COM_AddDebugCommand("rollback_test", Command_RollbackTest_f);
	COM_AddDebugCommand("rollback_resim", Command_RollbackResim_f);
	COM_AddDebugCommand("rollback_soak", Command_RollbackSoak_f);
	COM_AddDebugCommand("rollback_maxdepth", Command_RollbackMaxDepth_f);
	COM_AddDebugCommand("rollback_delay", Command_RollbackDelay_f);
	COM_AddDebugCommand("rollback_keep", Command_RollbackKeep_f);
	COM_AddDebugCommand("rollback_replay", Command_RollbackReplay_f);
}
