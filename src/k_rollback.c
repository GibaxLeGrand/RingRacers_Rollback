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
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h" // I_GetPreciseTime()
#include "m_random.h" // P_RandomFixed()
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
