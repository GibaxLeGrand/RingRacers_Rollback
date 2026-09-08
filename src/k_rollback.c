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
#include "d_clisrv.h" // Consistancy()
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
#include "z_zone.h"

// Nominal snapshot size: the same figure the engine already trusts for a full
// netgame savegame of the same content.
#define ROLLBACK_BUFSIZE (NETSAVEGAMESIZE)

// P_SaveNetGame writes through raw pointer macros with no bounds checking, so
// an oversized state cannot be stopped mid-write. Each slot therefore carries
// slack past its nominal size: a state that overruns ROLLBACK_BUFSIZE lands in
// the slack instead of in the next slot, and is caught before anything else
// has been corrupted.
#define ROLLBACK_SLACK (256*1024)

typedef struct
{
	uint8_t buffer[ROLLBACK_BUFSIZE + ROLLBACK_SLACK];
	size_t used;
	tic_t tic;
	int16_t gamemap;
	dboolean valid;
} rollbackslot_t;

static rollbackslot_t *rollbackring = NULL;

void K_InitRollback(void)
{
	if (rollbackring)
		return;

	// Twenty megabytes at the current slot size, which is why the ring is
	// allocated on first use rather than at startup: a session that never
	// touches rollback never pays for it.
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

dboolean K_LoadGameState(tic_t tic)
{
	savebuffer_t save = {0};
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

	if (P_SaveBufferFromExisting(&save, slot->buffer, slot->used) == false)
		return false;

	// reloading: keep the level in place, and keep the RNG seeds the archive
	// restores instead of resetting them. Both are required for a rollback --
	// replaying the same tics has to produce the same result.
	return P_LoadNetGame(&save, true);
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

#define ROLLBACK_DIAGBYTES (1024*1024)
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
static void K_ReportRecordDifferences(const diagset_t *before, const diagset_t *after)
{
	uint32_t common = (before->count < after->count) ? before->count : after->count;
	uint32_t reported = 0;
	uint32_t i;

	if (before->count != after->count)
	{
		CONS_Printf("rollback_test: %u objects archived before the restore, %u after\n",
			before->count, after->count);
	}

	for (i = 0; i < common && reported < 3; i++)
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
			CONS_Printf("rollback_test: object %u is %s before the restore and %s after -- "
				"the objects no longer line up, so the rest of this comparison is meaningless\n",
				i, K_MobjTypeName(before->recs[i].type), K_MobjTypeName(after->recs[i].type));
			return;
		}

		CONS_Printf("rollback_test: object %u (%s) changed: %u bytes became %u\n",
			i, K_MobjTypeName(before->recs[i].type), la, lb);
		K_PrintRecordMasks("  before:", a, la);
		K_PrintRecordMasks("  after: ", b, lb);
		reported++;
	}

	if (reported == 0 && before->count == after->count)
	{
		CONS_Printf("rollback_test: every object came back identical, so what changed is "
			"outside the per-object records\n");
	}

	if (before->truncated || after->truncated)
		CONS_Printf("rollback_test: note - the object capture hit its limit, later objects were not compared\n");
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

	CONS_Printf("%s: %u racers (%u of them bots), %u spectators, %s\n",
		cmd, racers, bots, spectators,
		(grandprixinfo.gp ? "Grand Prix" : "not a Grand Prix"));
}

/** Says how two snapshots of what ought to be the same state compare.
  *
  * Shared by both tests: one puts a state through the archive and back, the
  * other runs the same tics twice, and both then ask the same question.
  *
  * eturn true when the two are byte for byte the same.
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
		K_ReportRecordDifferences(recsa, recsb);
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
	// something.
	K_ReportLostReferences();

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

	afterload = Consistancy();

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
// rollback_resim
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

/** Console command: rollback_resim [tics]
  *
  * Runs the same tics twice from the same state and compares where they end
  * up: snapshot, play N tics, snapshot, restore, play the same N tics again,
  * snapshot, compare the two endings byte for byte.
  *
  * This is the question rollback_test cannot answer. That one proves the
  * archive can read back what it wrote; this one proves the archive carries
  * everything the simulation needs. A field nobody archives is missing from
  * both sides of a round trip and compares equal, but a resimulation starting
  * from a state that lost it goes somewhere else -- which is the failure that
  * would end this approach, so it is worth finding early.
  *
  * It also measures a tic of simulation, which together with the restore cost
  * is what says how many tics of rollback fit in a frame.
  *
  * The world is put back where the command found it, so running this does not
  * leave the level ahead of the tic the netcode believes it is on. Sounds and
  * screen effects from both passes do play, though: they are not part of the
  * state, so nothing rewinds them.
  */
static void Command_RollbackResim_f(void)
{
	rollbackslot_t *first, *second;
	diagset_t recsfirst = {0}, recssecond = {0};
	ticcmd_t frozen[MAXPLAYERS];
	precise_t started;
	uint32_t firstus, secondus;
	tic_t startedat;
	int32_t tics = 4;
	int32_t i;
	dboolean records;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return;
	}

	if (COM_Argc() > 1)
	{
		tics = atoi(COM_Argv(1));

		if (tics < 1)
			tics = 1;

		// Past the ring's depth the exercise stops resembling a rollback.
		if (tics > ROLLBACK_TICS)
			tics = ROLLBACK_TICS;
	}

	K_PrintGrid("rollback_resim");

	first = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
	second = (rollbackslot_t *)Z_Malloc(sizeof (rollbackslot_t), PU_STATIC, NULL);

	recsfirst.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
	recsfirst.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);
	recssecond.bytes = (uint8_t *)Z_Malloc(ROLLBACK_DIAGBYTES, PU_STATIC, NULL);
	recssecond.recs = (diagrec_t *)Z_Malloc(sizeof (diagrec_t) * ROLLBACK_DIAGRECS, PU_STATIC, NULL);

	records = (recsfirst.bytes && recsfirst.recs && recssecond.bytes && recssecond.recs);

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

	startedat = leveltime;

	started = I_GetPreciseTime();
	K_RunFrozenTics(tics, frozen);
	firstus = K_PreciseToMicros(I_GetPreciseTime() - started);

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

	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_resim: could not get back to the starting state -- "
			"the level is left where the first pass ended\n");
		goto done;
	}

	started = I_GetPreciseTime();
	K_RunFrozenTics(tics, frozen);
	secondus = K_PreciseToMicros(I_GetPreciseTime() - started);

	if (!K_WriteSnapshot(second, gametic))
	{
		CONS_Printf("rollback_resim: could not snapshot the second pass\n");
		goto done;
	}

	if (records)
		K_CaptureRecords(&recssecond);

	CONS_Printf("rollback_resim: %d tics took %u us, then %u us -- %u us per tic\n",
		tics, firstus, secondus, secondus / (uint32_t)tics);

	K_ReportComparison("rollback_resim", "resimulation",
		first, "first pass", second, "second pass",
		&recsfirst, &recssecond, records);

	// Back to where the command found the world.
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_resim: WARNING - could not restore the starting state, "
			"so the level is now %d tics ahead of where it was\n", tics);
	}

done:
	Z_Free(first);
	Z_Free(second);
	K_FreeDiagSet(&recsfirst);
	K_FreeDiagSet(&recssecond);
}

void K_RegisterRollbackStuff(void)
{
	// Debug commands rather than plain ones: they are diagnostics, and being
	// so lists them in the pause menu's command list, which is where they can
	// be reached without typing into the console.
	COM_AddDebugCommand("rollback_test", Command_RollbackTest_f);
	COM_AddDebugCommand("rollback_resim", Command_RollbackResim_f);
}
