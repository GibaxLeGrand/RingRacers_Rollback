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
#include "i_system.h" // I_GetPreciseTime()
#include "info.h"
#include "m_random.h" // P_RandomFixed()
#include "p_local.h" // thlist
#include "p_mobj.h"
#include "p_saveg.h"
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
	P_SaveNetGame(&save, true);

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
	precise_t started;
	uint32_t saveus, loadus, resaveus;
	int16_t before, afterperturb, afterload;
	size_t common, at;
	dboolean identical;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return;
	}

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

	P_RandomFixed(PR_UNDEFINED);
	P_RandomFixed(PR_UNDEFINED);
	P_RandomFixed(PR_UNDEFINED);

	afterperturb = Consistancy();

	started = I_GetPreciseTime();
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_test: K_LoadGameState failed\n");
		Z_Free(resaved);
		return;
	}
	loadus = K_PreciseToMicros(I_GetPreciseTime() - started);

	afterload = Consistancy();

	started = I_GetPreciseTime();
	if (!K_WriteSnapshot(resaved, gametic))
	{
		CONS_Printf("rollback_test: second K_WriteSnapshot failed\n");
		Z_Free(resaved);
		return;
	}
	resaveus = K_PreciseToMicros(I_GetPreciseTime() - started);

	// Compare the common prefix first, so a size mismatch still reports where
	// the two snapshots stopped agreeing rather than only that they differ.
	common = (original->used < resaved->used) ? original->used : resaved->used;
	for (at = 0; at < common; at++)
	{
		if (original->buffer[at] != resaved->buffer[at])
			break;
	}

	identical = (original->used == resaved->used && at == common);

	CONS_Printf("rollback_test: snapshot %s bytes, %s%% of the %s byte slot\n",
		sizeu1(original->used),
		sizeu2((original->used * 100) / ROLLBACK_BUFSIZE),
		sizeu3((size_t)ROLLBACK_BUFSIZE));

	CONS_Printf("rollback_test: save %u us, load %u us, re-save %u us\n",
		saveus, loadus, resaveus);

	if (identical)
	{
		CONS_Printf("rollback_test: round-trip IDENTICAL over all %s bytes\n",
			sizeu1(original->used));
	}
	else
	{
		if (original->used != resaved->used)
		{
			CONS_Printf("rollback_test: round-trip DIFFERS -- re-saved state is %s bytes, "
				"original was %s\n", sizeu1(resaved->used), sizeu2(original->used));
		}

		if (at < common)
		{
			CONS_Printf("rollback_test: first difference at byte %s, in the '%s' block "
				"(0x%02x became 0x%02x)\n",
				sizeu1(at),
				P_LocateSnapshotBlock(original->buffer, original->used, at),
				original->buffer[at], resaved->buffer[at]);

			K_PrintSnapshotContext("original", original->buffer, original->used, at);
			K_PrintSnapshotContext("re-saved", resaved->buffer, resaved->used, at);
		}
		else
		{
			CONS_Printf("rollback_test: the shorter snapshot is a prefix of the longer one, "
				"so what changed is at the end -- in the '%s' block\n",
				P_LocateSnapshotBlock(original->buffer, original->used, common));
		}
	}

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
}

void K_RegisterRollbackStuff(void)
{
	// A debug command rather than a plain one: it is a diagnostic, and being
	// one lists it in the pause menu's command list, which is where it can be
	// reached without typing into the console.
	COM_AddDebugCommand("rollback_test", Command_RollbackTest_f);
}
