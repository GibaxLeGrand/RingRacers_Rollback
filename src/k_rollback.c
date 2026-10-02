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
#include "m_perfstats.h" // ps_thlist_times and the rest, summed over speculated tics

#include "command.h"
#include "d_clisrv.h" // Consistancy(), playerdelaytable
#include "d_net.h" // netlagtics
#include "d_netcmd.h" // cv_mindelay
#include <stddef.h> // offsetof

#include "deh_tables.h" // MOBJTYPE_LIST, FREE_MOBJS
#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h" // players, playeringame
#include "i_system.h" // I_GetPreciseTime()
#include "i_time.h" // I_GetTime()
#include "info.h"
#include "k_bot.h" // K_BuildBotTiccmd
#include "k_grandprix.h" // grandprixinfo
#include "m_random.h" // P_RandomFixed()
#include "p_local.h" // thlist, P_Ticker()
#include "p_mobj.h"
#include "p_saveg.h"
#include "p_tick.h" // leveltime
#include "r_state.h" // sectors
#include "r_main.h" // rendertimefrac
#include "r_fps.h" // R_InterpolateMobjState
#include "z_zone.h"
#include "s_sound.h" // S_NoteChannelOrigins, for a raw restore

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

	// The input each player was holding when this tic ran, kept beside the
	// archive for the same reason the cameras are: it is not part of the world,
	// it is the record of what moved it. A replay reads netcmds to find the
	// inputs of the tics it repeats, and nothing until now checked that what it
	// reads there is still what the tic actually used. On a listen server the
	// local player's slot in netcmds is written by the sending path, not by the
	// tic, so "it must be the same" is an assumption and this is the measurement.
	ticcmd_t usedcmds[MAXPLAYERS];

	size_t used;
	tic_t tic;
	int16_t gamemap;
	dboolean valid;

	// rollback_rawsnap (WORLDWIDE.md 8.88): a raw snapshot, in one buffer grown
	// as needed -- the archive without what the pools hold, the level pools,
	// the heads that point into them. `buffer` above then holds the full
	// archive only in verify mode (2), for the comparison; `used` is 0 in
	// mode 1. A slot made with Z_Malloc must start zeroed (Z_Calloc).
	uint8_t *raw;
	size_t rawcap;
	size_t rawnet, rawpools, rawheads;
	dboolean israw;
} rollbackslot_t;

static rollbackslot_t *rollbackring = NULL;

// rollback_rawsnap (WORLDWIDE.md 8.82, 8.83, 8.88): 0 network snapshots, as
// ever; 1 raw ones; 2 raw ones checked after every restore against the full
// archive of the same tic, byte for byte and reference count by count.
//
// 1 by default since WORLDWIDE.md 8.125: on Opulence a save went from 2.9 to
// 1.1 ms and a restore from 6.5 to 1.9 (8.95), and the open points were closed
// or found not to be B2's (8.124): the players-block difference is the item
// list's capacity, the double claim is counted once, Lua's floorspriteslope
// goes the network way. The tests compare archives and need 0 or 2; the
// harness's scenarios that run them set 0.
static int32_t g_rawsnap = 1;
static int16_t g_rawmap = -1;       // the level raw snapshots were last taken in
static uint32_t g_rawsaves, g_rawrestores, g_rawrefused;
static uint32_t g_rawfallbacks;   // snapshots that went the network way (K_RawUnsafe)
static uint64_t g_rawsaveus, g_rawrestoreus;
static uint64_t g_rawnetbytes, g_rawpoolbytes, g_rawheadbytes;
static uint32_t g_rawverified, g_rawbytesoff, g_rawcountchecks, g_rawcountbad;
static uint8_t *g_rawverifybuf;

static dboolean K_WriteRawSnapshot(rollbackslot_t *slot);
static dboolean K_ReadRawSnapshot(rollbackslot_t *slot);

dboolean K_RollbackRawSnapshots(void)
{
	// Also while a raw snapshot of this level may still be restored after the
	// switch went off: what it would bring back must still be there.
	return (g_rawsnap > 0 || g_rawmap == gamemap);
}

/** A slot's raw buffer, then the slot. */
static void K_FreeSlot(rollbackslot_t *slot)
{
	if (slot == NULL)
		return;

	if (slot->raw != NULL)
		Z_Free(slot->raw);

	Z_Free(slot);
}

/** The tests compare archives, which rollback_rawsnap 1 does not write. */
static dboolean K_RawSnapBlocksTests(const char *cmd)
{
	if (g_rawsnap != 1)
		return false;

	CONS_Printf("%s: compares archives, which rollback_rawsnap 1 does not write -- "
		"use rollback_rawsnap 2 (raw snapshots, verified) or 0\n", cmd);
	return true;
}

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
	int32_t i;

	if (!rollbackring)
		return;

	for (i = 0; i < ROLLBACK_TICS; i++)
	{
		if (rollbackring[i].raw != NULL)
			Z_Free(rollbackring[i].raw);
	}

	Z_Free(rollbackring);
	rollbackring = NULL;
}

/** Whether the world holds something a raw snapshot does not carry, so that
  * this one must go the network way: an object with a floorspriteslope. Only
  * Lua makes one (P_CreateFloorSpriteSlope); the network archive saves and
  * rebuilds it, and the raw copy would bring back a pointer to a plane that
  * may have been freed (WORLDWIDE.md 8.93, 8.124). */
static dboolean K_RawUnsafe(void)
{
	thinker_t *th;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (((mobj_t *)th)->floorspriteslope != NULL)
			return true;
	}

	return false;
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
	slot->israw = false;

	// rollback_rawsnap: the raw snapshot first -- its numbering is the one the
	// full archive below repeats, for synced objects -- then, in verify mode,
	// the full archive beside it.
	if (g_rawsnap > 0)
	{
		if (K_RawUnsafe())
		{
			g_rawfallbacks++;   // the network snapshot below, restored the network way
		}
		else
		{
			if (K_WriteRawSnapshot(slot) == false)
				return false;

			slot->israw = true;
		}
	}

	if (g_rawsnap == 1 && slot->israw)
	{
		slot->used = 0;
	}
	else
	{
		if (P_SaveBufferFromExisting(&save, slot->buffer, sizeof (slot->buffer)) == false)
			return false;

		// resending, so that gametic goes into the archive. K_LoadGameState reads
		// it back, and the reader only looks for it when the writer wrote it.
		// local, because this snapshot is restored on the machine that took it:
		// the per-viewport visibility flags are worth keeping and must round-trip.
		P_SaveNetGame(&save, true, true);

		slot->used = (size_t)(save.p - save.buffer);
	}

	// Deliberately not P_SaveBufferFree: that would Z_Free the ring slot out
	// from under us. The savebuffer_t is a view onto storage this module owns.

	if (slot->used > ROLLBACK_BUFSIZE)
		I_Error("K_WriteSnapshot: snapshot of %s bytes exceeds the slot size "
			"(caught in slack, nothing corrupted -- raise ROLLBACK_BUFSIZE)",
			sizeu1(slot->used));

	memcpy(slot->cameras, camera, sizeof (slot->cameras));

	{
		// Written after P_Ticker by the keeper, so this is the input the tic
		// being recorded ran on.
		int32_t i;

		for (i = 0; i < MAXPLAYERS; i++)
			slot->usedcmds[i] = players[i].cmd;
	}

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

	if (slot->israw)
		return K_ReadRawSnapshot(slot);

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

// ----------------------------------------------------------------------------
// rollback_rawsnap: raw snapshots (WORLDWIDE.md 8.82, 8.83, 8.88)
// ----------------------------------------------------------------------------

/** Takes a raw snapshot into the slot's raw buffer: the archive without what
  * the pools hold, the level pools, the heads pointing into them. */
static dboolean K_WriteRawSnapshot(rollbackslot_t *slot)
{
	const size_t netroom = ROLLBACK_BUFSIZE + ROLLBACK_SLACK;
	const size_t need = netroom + Z_LevelPoolSnapshotSize() + P_RawHeadsSize();
	const precise_t at = I_GetPreciseTime();
	savebuffer_t net = {0};

	if (slot->rawcap < need)
	{
		if (slot->raw != NULL)
			Z_Free(slot->raw);

		// With room to grow: the pools grow with the race (8.60), and a slot
		// reallocated every few tics would cost what the copy saves.
		slot->rawcap = need + need / 8;
		slot->raw = (uint8_t *)Z_Malloc(slot->rawcap, PU_STATIC, NULL);
	}

	if (P_SaveBufferFromExisting(&net, slot->raw, netroom) == false)
		return false;

	P_SaveNetGameRaw(&net);
	slot->rawnet = (size_t)(net.p - net.buffer);

	if (slot->rawnet > ROLLBACK_BUFSIZE)
		I_Error("K_WriteRawSnapshot: the archive part of a raw snapshot is %s bytes "
			"(caught in slack, nothing corrupted -- raise ROLLBACK_BUFSIZE)",
			sizeu1(slot->rawnet));

	slot->rawpools = Z_LevelPoolSnapshot(slot->raw + slot->rawnet, slot->rawcap - slot->rawnet);
	slot->rawheads = (slot->rawpools != 0)
		? P_SaveRawHeads(slot->raw + slot->rawnet + slot->rawpools, slot->rawcap - slot->rawnet - slot->rawpools)
		: 0;

	if (slot->rawpools == 0 || slot->rawheads == 0)
		return false;

	g_rawmap = gamemap;
	g_rawsaves++;
	g_rawsaveus += K_PreciseToMicros(I_GetPreciseTime() - at);
	g_rawnetbytes += slot->rawnet;
	g_rawpoolbytes += slot->rawpools;
	g_rawheadbytes += slot->rawheads;
	return true;
}

/** Verify mode: the archive of the world a raw restore made, against the full
  * archive taken beside the raw snapshot; and the recount against the counts
  * the copy brought back. Prints the first few of each kind of failure. */
static void K_VerifyRawRestore(const rollbackslot_t *slot)
{
	const rawcountcheck_t *c = P_GetRawCountCheck();
	savebuffer_t save = {0};
	size_t used, at;

	if (slot->used == 0)
		return;

	if (g_rawverifybuf == NULL)
		g_rawverifybuf = (uint8_t *)Z_Malloc(ROLLBACK_BUFSIZE + ROLLBACK_SLACK, PU_STATIC, NULL);

	if (P_SaveBufferFromExisting(&save, g_rawverifybuf, ROLLBACK_BUFSIZE + ROLLBACK_SLACK) == false)
		return;

	P_SaveNetGame(&save, true, true);
	used = (size_t)(save.p - save.buffer);
	g_rawverified++;

	if (used != slot->used || memcmp(g_rawverifybuf, slot->buffer, used) != 0)
	{
		g_rawbytesoff++;

		for (at = 0; at < used && at < slot->used; at++)
		{
			if (g_rawverifybuf[at] != slot->buffer[at])
				break;
		}

		if (g_rawbytesoff <= 5)
		{
			CONS_Printf("rollback_rawsnap: VERIFY tic %u -- after a raw restore the archive "
				"differs from the snapshot's at byte %s (%s against %s bytes), in the %s block\n",
				(uint32_t)slot->tic, sizeu1(at), sizeu2(used), sizeu3(slot->used),
				P_LocateSnapshotBlock(slot->buffer, slot->used, at));

			// The players block has no markers inside it: read the offset as a
			// player and a field, as rollback_test does (WORLDWIDE.md 8.124).
			// The record offsets are the archive written last's, which is the
			// check's own, laid out as the snapshot's up to the difference.
			if (at < used && at < slot->used)
			{
				uint8_t who;
				size_t into;

				CONS_Printf("rollback_rawsnap: VERIFY   the byte was 0x%02x, after the restore 0x%02x\n",
					slot->buffer[at], g_rawverifybuf[at]);

				if (P_LocatePlayerField(at, &who, &into))
				{
					const char *field = P_NamePlayerField(g_rawverifybuf, used, who, into);

					CONS_Printf("rollback_rawsnap: VERIFY   player %u (%s), %s bytes into the record -- %s\n",
						who, (playeringame[who] ? (players[who].bot ? "a bot" : "a person") : "not in game"),
						sizeu1(into), (field != NULL) ? field : "past the fields that can be named");
				}
			}
		}
	}

	g_rawcountchecks++;

	if (c->mismatched > 0 || c->listschanged)
	{
		uint32_t m;

		g_rawcountbad++;

		if (g_rawcountbad <= 5)
		{
			CONS_Printf("rollback_rawsnap: VERIFY tic %u -- %u of %u reference counts rebuilt "
				"differently from the live ones%s\n", (uint32_t)slot->tic,
				c->mismatched, c->thinkers,
				(c->listschanged ? ", and the thinker lists moved during the load" : ""));

			for (m = 0; m < c->shown; m++)
			{
				const rawcountmiss_t *miss = &c->miss[m];

				CONS_Printf("rollback_rawsnap:   list %d, %s #%u -- live %d, rebuilt %d\n",
					miss->list,
					(miss->mobjtype >= 0 ? K_MobjTypeName((mobjtype_t)miss->mobjtype) : "(not an object)"),
					miss->mobjnum, miss->was, miss->now);
			}
		}
	}
}

/** Puts back a raw snapshot: every part checked before anything is written,
  * then the pools, the heads, and the archive with the counts rebuilt. */
static dboolean K_ReadRawSnapshot(rollbackslot_t *slot)
{
	const uint8_t *pools = slot->raw + slot->rawnet;
	const uint8_t *heads = pools + slot->rawpools;
	const precise_t at = I_GetPreciseTime();
	savebuffer_t net = {0};
	dboolean ok;

	// The heads first: Z_LevelPoolRestore checks its own part and writes
	// nothing if it refuses, so after these two nothing can be refused.
	if (P_RawHeadsFit(heads, slot->rawheads) == false)
	{
		g_rawrefused++;
		return false;
	}

	// Which object each playing sound comes from, while the objects playing
	// them are still the ones in memory (S_NoteChannelOrigins, 8.73); and
	// which thinkers are alive, so Lua can forget the ones the restore takes
	// away (P_NoteRawLiving).
	S_NoteChannelOrigins();
	P_NoteRawLiving();

	if (Z_LevelPoolRestore(pools, slot->rawpools) == false)
	{
		g_rawrefused++;
		return false;
	}

	P_RestoreRawHeads(heads, slot->rawheads);

	if (P_SaveBufferFromExisting(&net, slot->raw, slot->rawnet) == false)
		return false;

	P_RawCountCheck(g_rawsnap == 2);
	ok = P_LoadNetGameRaw(&net);
	P_RawCountCheck(false);

	memcpy(camera, slot->cameras, sizeof (slot->cameras));

	g_rawrestores++;
	g_rawrestoreus += K_PreciseToMicros(I_GetPreciseTime() - at);

	if (g_rawsnap == 2)
		K_VerifyRawRestore(slot);

	return ok;
}

static void K_ReportRawSnap(void)
{
	static const char *const mode[3] = {
		"off -- network snapshots, as before",
		"on -- raw snapshots of the level pools",
		"on, verified -- raw snapshots, each restore checked against the full archive"
	};

	CONS_Printf("rollback_rawsnap: %s\n", mode[g_rawsnap]);

	if (g_rawsaves > 0)
	{
		CONS_Printf("rollback_rawsnap: %u raw saves, %u us each -- %s KB of archive, "
			"%s KB of pools, %s KB of heads\n", g_rawsaves,
			(uint32_t)(g_rawsaveus / g_rawsaves),
			sizeu1((size_t)(g_rawnetbytes / g_rawsaves / 1024)),
			sizeu2((size_t)(g_rawpoolbytes / g_rawsaves / 1024)),
			sizeu3((size_t)(g_rawheadbytes / g_rawsaves / 1024)));
	}

	if (g_rawfallbacks > 0)
	{
		CONS_Printf("rollback_rawsnap: %u snapshots went the network way -- an object had "
			"a floorspriteslope, which only Lua makes and the raw copy does not carry\n",
			g_rawfallbacks);
	}

	if (g_rawrestores > 0 || g_rawrefused > 0)
	{
		CONS_Printf("rollback_rawsnap: %u raw restores, %u us each (a verified one includes "
			"its check); %u refused (another level)\n", g_rawrestores,
			(uint32_t)(g_rawrestores ? g_rawrestoreus / g_rawrestores : 0), g_rawrefused);
	}

	if (g_rawverified > 0 || g_rawcountchecks > 0)
	{
		CONS_Printf("rollback_rawsnap: verified %u restores -- %u with the archive differing, "
			"%u with a reference count rebuilt differently\n",
			g_rawverified, g_rawbytesoff, g_rawcountbad);
	}
}

/** Console command: rollback_rawsnap [0|1|2]
  *
  * Client side, and the tests. 0: network snapshots, as before. 1, the
  * default since WORLDWIDE.md 8.125: raw
  * snapshots -- the level pools copied whole, with the heads pointing into
  * them, beside an archive of the rest -- restored at their own addresses,
  * every reference count rebuilt (WORLDWIDE.md 8.88). 2: the same, plus the
  * full archive beside each, and each restore checked against it byte for
  * byte, and its counts against the live ones; the tests (rollback_test, the
  * soaks) need 0 or 2. Setting it resets the counts; snapshots already taken
  * are read the way they were written. */
static void Command_RollbackRawSnap_f(void)
{
	if (COM_Argc() > 1)
	{
		const int32_t want = atoi(COM_Argv(1));

		g_rawsnap = (want <= 0) ? 0 : ((want >= 2) ? 2 : 1);
		g_rawsaves = g_rawrestores = g_rawrefused = g_rawfallbacks = 0;
		g_rawsaveus = g_rawrestoreus = 0;
		g_rawnetbytes = g_rawpoolbytes = g_rawheadbytes = 0;
		g_rawverified = g_rawbytesoff = g_rawcountchecks = g_rawcountbad = 0;
	}

	K_ReportRawSnap();
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
  * last one that starts at or before it, and it also records whether that field
  * holds pointers -- which settles the question K_RunIsAddress could only guess
  * at. That guess has been wrong in both directions twice in this file: an
  * address whose bytes do not look like one was reported as a field, and a field
  * whose bytes did was thrown away. The declaration knows.
  *
  * \return the field name, or NULL past the end of the structure. When
  *         ispointer is not NULL it is set to whether that field holds pointers.
  */
static const char *K_NameMobjField(size_t into, dboolean *ispointer)
{
	static const struct { size_t at; const char *name; dboolean ptr; } fields[] =
	{
#define F(x) { offsetof(mobj_t, x), #x, false }
#define P(x) { offsetof(mobj_t, x), #x, true }
		P(thinker), F(x), F(y), F(z),
		F(old_x), F(old_y), F(old_z), F(old_x2),
		F(old_y2), F(old_z2), F(type), P(info),
		P(bnext), P(bprev), F(angle), F(pitch),
		F(roll), F(old_angle), F(old_pitch), F(old_roll),
		F(old_angle2), F(old_pitch2), F(old_roll2), F(rollangle),
		F(sprite), F(frame), F(sprite2), F(anim_duration),
		F(renderflags), F(spritexscale), F(spriteyscale), F(spritexoffset),
		F(spriteyoffset), F(old_spritexscale), F(old_spriteyscale), F(old_spritexoffset),
		F(old_spriteyoffset), P(floorspriteslope), F(lightlevel), P(touching_sectorlist),
		P(subsector), F(floorz), F(ceilingz), P(floorrover),
		P(ceilingrover), F(floordrop), F(ceilingdrop), F(radius),
		F(height), F(momx), F(momy), F(momz),
		F(pmomz), F(tics), P(state), F(flags),
		F(flags2), F(eflags), F(tid), P(tid_next),
		P(tid_prev), P(skin), F(color), P(snext),
		P(sprev), P(hnext), P(hprev), P(itnext),
		F(health), F(movedir), F(movecount), P(target),
		F(reactiontime), F(threshold), P(player), F(lastlook),
		P(spawnpoint), P(tracer), F(friction), F(movefactor),
		F(lastmomz), F(fuse), F(watertop), F(waterbottom),
		F(mobjnum), F(scale), F(old_scale), F(old_scale2),
		F(destscale), F(scalespeed), F(extravalue1), F(extravalue2),
		F(cusval), F(cvmem), P(standingslope), F(resetinterp),
		F(colorized), F(mirrored), F(shadowscale), F(whiteshadow),
		F(shadowcolor), F(sprxoff), F(spryoff), F(sprzoff),
		F(bakexoff), F(bakeyoff), F(bakezoff), F(bakexpiv),
		F(bakeypiv), F(bakezpiv), P(terrain), P(terrainOverlay),
		F(hitlag), F(waterskip), F(dispoffset), F(thing_args),
		P(thing_stringargs), F(special), F(script_args), P(script_stringargs),
		F(frozen), F(reappear), P(punt_ref), P(owner),
		F(po_movecount),
#undef P
#undef F
	};
	const size_t count = sizeof (fields) / sizeof (fields[0]);
	size_t i;

	if (into >= sizeof (mobj_t))
	{
		if (ispointer != NULL)
			*ispointer = false;
		return NULL;
	}

	for (i = 0; i + 1 < count; i++)
	{
		if (into < fields[i + 1].at)
			break;
	}

	if (ispointer != NULL)
		*ispointer = fields[i].ptr;

	return fields[i].name;
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
	uint32_t mismatched = 0;
	uint32_t differing = 0;

	if (g_mobjcopy == NULL || g_mobjslot == NULL || g_mobjcopies == 0)
		return;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *mo = (const mobj_t *)th;
		const uint8_t *was;
		const uint8_t *now = (const uint8_t *)mo;
		dboolean pointerfield = false;
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

		// mobjnum is handed out afresh by every save and never cleared, so the
		// same number can name two different objects either side of a restore.
		// The type is the cheapest thing that catches it, and without it this
		// reported a ring's x, y and z as having moved when it was looking at two
		// different rings.
		if (((const mobj_t *)was)->type != mo->type)
		{
			mismatched++;
			continue;
		}

		compared++;

		for (at = 0; at < sizeof (mobj_t); at++)
		{
			char before[32], after[32];
			size_t run, k;
			int32_t nb = 0, na = 0;

			if (was[at] == now[at])
				continue;

			for (run = 0; at + run < sizeof (mobj_t) && was[at + run] != now[at + run]; run++)
				;

			// Asked of the declaration rather than of the bytes: a pointer
			// field legitimately differs after a restore, and whether these
			// particular bytes happen to look like an address says nothing.
			K_NameMobjField(at, &pointerfield);

			if (pointerfield)
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

			if (reported < 6)
			{
				char text[160];

				const char *field = K_NameMobjField(at, NULL);

				snprintf(text, sizeof (text),
					"%s: %s #%u, %s bytes into mobj_t -- %s: %s bytes, %s-> %s",
					cmd, K_MobjTypeName(mo->type), mo->mobjnum,
					sizeu1(at), (field != NULL) ? field : "past the end",
					sizeu2(run), before, after);
				K_Finding(text);
			}

			differing++;

			if (reported < 6)
				reported++;

			at += run;
		}
	}

	if (g_holdfindings == false || missing > 0)
	{
		CONS_Printf("%s: %u objects compared, %u skipped on a type mismatch, "
			"%u appeared from nowhere, %u field differences found and %u shown, "
			"%u runs were pointer fields\n",
			cmd, compared, mismatched, missing, differing, reported, addresses);
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
	uint32_t num;       // mobjnum, so the two captures can be checked for drift
	mobjtype_t type;

	// What the object is doing, taken from the living object at capture time.
	// A type alone does not name an effect: Labyrinth's leak soak failed on
	// pairs of MT_THOK, a type at least twenty spawn sites share, and by the
	// time the report is printed the world has been restored again, so the
	// object cannot be looked up then (WORLDWIDE.md 8.55).
	int32_t state;      // index into states[]
	int32_t sprite;
	uint32_t frame;
	mobjtype_t targettype;  // NUMMOBJTYPES when there is no target
	int8_t targetplayer;    // the target's player slot, -1 if none
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
		// Zeroed: a slot's raw buffer pointer must start NULL (rollback_rawsnap).
		g_first = (rollbackslot_t *)Z_Calloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
		g_second = (rollbackslot_t *)Z_Calloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
		g_third = (rollbackslot_t *)Z_Calloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
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
		set->recs[set->count].num = mo->mobjnum;
		set->recs[set->count].type = mo->type;
		set->recs[set->count].state = (mo->state != NULL) ? (int32_t)(mo->state - states) : -1;
		set->recs[set->count].sprite = (int32_t)mo->sprite;
		set->recs[set->count].frame = (uint32_t)mo->frame;
		set->recs[set->count].targettype = NUMMOBJTYPES;
		set->recs[set->count].targetplayer = -1;

		if (mo->target != NULL && P_MobjWasRemoved(mo->target) == false)
		{
			set->recs[set->count].targettype = mo->target->type;

			if (mo->target->player != NULL)
				set->recs[set->count].targetplayer = (int8_t)(mo->target->player - players);
		}

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

/** A state's name, the way K_MobjTypeName gives a type's. */
static const char *K_StateName(int32_t state)
{
	const char *name = NULL;

	if (state >= S_FIRSTFREESLOT && state <= S_LASTFREESLOT)
		name = FREE_STATES[state - S_FIRSTFREESLOT];
	else if (state >= 0 && state < S_FIRSTFREESLOT)
		name = STATE_LIST[state];

	return (name != NULL) ? name : "(unnamed state)";
}

/** What a captured object was doing: its state, sprite and frame, and what it
  * was aimed at -- which is what names an effect when its type does not. */
static void K_PrintRecordIdentity(const char *label, const diagrec_t *rec)
{
	const char *sprite = (rec->sprite >= 0 && rec->sprite < NUMSPRITES)
		? sprnames[rec->sprite] : "????";
	char who[24] = "";

	if (rec->targetplayer >= 0)
		snprintf(who, sizeof who, " (player %d)", rec->targetplayer);

	CONS_Printf("rollback_test: %s state %s, sprite %.4s frame %u, target %s%s\n",
		label, K_StateName(rec->state), sprite, rec->frame & FF_FRAMEMASK,
		(rec->targettype < NUMMOBJTYPES) ? K_MobjTypeName(rec->targettype) : "none",
		who);
}

/** Names the objects whose archived record changed across a restore. */
/** Says which archived object a byte offset in a snapshot falls inside.
  *
  * The snapshot has no index and its thinkers block cannot be walked without
  * mirroring five hundred lines of archiver, so this works the other way round.
  * The two snapshots are identical up to the first difference, so the bytes just
  * before it are a fingerprint, and the capture of the living world holds those
  * same bytes split into one record per object. Finding the fingerprint in the
  * capture names the object and the offset inside its record, with no knowledge
  * of the layout at all.
  *
  * It says how long a window it matched on and how many records matched it,
  * because a fingerprint that matches twice names nothing, and one that matches
  * nothing has to say so rather than point at record zero.
  */
static void K_LocateSnapshotRecord(const char *cmd, const rollbackslot_t *a,
	size_t at, const diagset_t *recs)
{
	static const size_t windows[] = { 32, 16, 8 };
	size_t w;

	if (recs == NULL || recs->bytes == NULL || recs->count == 0)
	{
		CONS_Printf("%s: no capture of the living world to locate that byte in\n", cmd);
		return;
	}

	for (w = 0; w < sizeof (windows) / sizeof (windows[0]); w++)
	{
		const size_t len = windows[w];
		const uint8_t *want;
		uint32_t hits = 0;
		uint32_t hitrec = 0;
		size_t hitinto = 0;
		uint32_t i;

		if (at < len)
			continue;

		want = a->buffer + (at - len);

		for (i = 0; i < recs->count; i++)
		{
			const uint8_t *rec = recs->bytes + recs->recs[i].offset;
			const size_t rl = recs->recs[i].length;
			size_t q;

			if (rl < len)
				continue;

			for (q = 0; q + len <= rl; q++)
			{
				if (memcmp(rec + q, want, len) == 0)
				{
					hits++;
					hitrec = i;
					hitinto = q + len;
				}
			}
		}

		if (hits == 0)
			continue;

		if (hits > 1)
		{
			CONS_Printf("%s: the %s bytes before the difference match %s records, "
				"so they name none of them\n",
				cmd, sizeu1(len), sizeu2((size_t)hits));
			continue;
		}

		CONS_Printf("%s: that byte is object %u (%s), %s bytes into its record "
			"of %u -- matched on a window of %s bytes\n",
			cmd, hitrec, K_MobjTypeName(recs->recs[hitrec].type),
			sizeu1(hitinto), recs->recs[hitrec].length, sizeu2(len));
		K_PrintRecordMasks("  its masks:", recs->bytes + recs->recs[hitrec].offset,
			recs->recs[hitrec].length);
		return;
	}

	CONS_Printf("%s: could not place that byte in any archived object\n", cmd);
}

static void K_ReportRecordDifferences(const char *cmd, const diagset_t *before, const diagset_t *after)
{
	uint32_t common = (before->count < after->count) ? before->count : after->count;
	uint32_t reported = 0;
	uint32_t differing = 0;
	uint32_t samenumber = 0;
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
			CONS_Printf("%s: object %u (%s) changed: %u bytes became %u, "
				"mobjnum %u before and %u after\n",
				cmd, i, K_MobjTypeName(before->recs[i].type), la, lb,
				before->recs[i].num, after->recs[i].num);
			K_PrintRecordIdentity("  before:", &before->recs[i]);
			K_PrintRecordIdentity("  after: ", &after->recs[i]);
			K_PrintRecordMasks("  before:", a, la);
			K_PrintRecordMasks("  after: ", b, lb);
			reported++;
		}
	}

	// Whether the two captures are even lined up. This walks both lists by
	// position and trusts that position N holds the same object on both sides,
	// which is only true while the lists agree -- and a shift inside a run of
	// hundreds of MT_RINGs slips straight past the type check above, making every
	// ring after it "differ". That is how this reported seven hundred changed
	// objects while the snapshot it was meant to be explaining differed by a
	// single byte.
	for (i = 0; i < common; i++)
	{
		if (before->recs[i].num == after->recs[i].num)
			samenumber++;
	}

	// How big the thing examined was, every time, so a number is never read
	// without knowing what it is a number out of.
	CONS_Printf("%s: %u objects compared, %u differed, %u shown in full, "
		"%u hold the same mobjnum on both sides\n",
		cmd, common, differing, reported, samenumber);

	if (samenumber < common)
	{
		CONS_Printf("%s: the two captures are NOT lined up, so the count above is "
			"not a count of objects that changed -- read the snapshot comparison "
			"instead\n", cmd);
	}

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

		// The gap between tilt and timeshitprev, unnamed until the first
		// rollback_leak soak failure put eight differing runs inside it --
		// offsets 110, 284, 292, 296, 332, 672, 680, 1016 -- across five
		// players, none of them nameable. Two lines so neither runs past the
		// buffer text[] is sized for.
		snprintf(text, sizeof (text),
			"%s: offsets -- speed %u, lastspeed %u, exiting %u, cmomx %u, "
			"cmomy %u, rmomx %u, rmomy %u",
			cmd,
			(unsigned)offsetof(player_t, speed), (unsigned)offsetof(player_t, lastspeed),
			(unsigned)offsetof(player_t, exiting), (unsigned)offsetof(player_t, cmomx),
			(unsigned)offsetof(player_t, cmomy), (unsigned)offsetof(player_t, rmomx),
			(unsigned)offsetof(player_t, rmomy));
		K_Finding(text);

		snprintf(text, sizeof (text),
			"%s: offsets -- totalring %u, realtime %u, laptime %u, laps %u, "
			"latestlap %u, timeshit %u, deadtimer %u",
			cmd,
			(unsigned)offsetof(player_t, totalring), (unsigned)offsetof(player_t, realtime),
			(unsigned)offsetof(player_t, laptime), (unsigned)offsetof(player_t, laps),
			(unsigned)offsetof(player_t, latestlap), (unsigned)offsetof(player_t, timeshit),
			(unsigned)offsetof(player_t, deadtimer));
		K_Finding(text);

		// The group of timer fields between karthud and the lives/xtralife pair,
		// never printed before: two of the last three failures put a differing
		// byte at 672 and 680 on the SAME player across two independent soak
		// runs, and this is the group those offsets sit inside.
		snprintf(text, sizeof (text),
			"%s: offsets -- seasaw %u, seasawcooldown %u, seasawdist %u, "
			"seasawangle %u, seasawangleadd %u, seasawmoreangle %u, seasawdir %u",
			cmd,
			(unsigned)offsetof(player_t, seasaw), (unsigned)offsetof(player_t, seasawcooldown),
			(unsigned)offsetof(player_t, seasawdist), (unsigned)offsetof(player_t, seasawangle),
			(unsigned)offsetof(player_t, seasawangleadd), (unsigned)offsetof(player_t, seasawmoreangle),
			(unsigned)offsetof(player_t, seasawdir));
		K_Finding(text);

		snprintf(text, sizeof (text),
			"%s: offsets -- turbine %u, turbineangle %u, turbineheight %u, "
			"turbinespd %u, cloud %u, cloudlaunch %u, cloudbuf %u",
			cmd,
			(unsigned)offsetof(player_t, turbine), (unsigned)offsetof(player_t, turbineangle),
			(unsigned)offsetof(player_t, turbineheight), (unsigned)offsetof(player_t, turbinespd),
			(unsigned)offsetof(player_t, cloud), (unsigned)offsetof(player_t, cloudlaunch),
			(unsigned)offsetof(player_t, cloudbuf));
		K_Finding(text);

		snprintf(text, sizeof (text),
			"%s: offsets -- tulip %u, tuliplaunch %u, tulipbuf %u, lives %u, "
			"xtralife %u",
			cmd,
			(unsigned)offsetof(player_t, tulip), (unsigned)offsetof(player_t, tuliplaunch),
			(unsigned)offsetof(player_t, tulipbuf), (unsigned)offsetof(player_t, lives),
			(unsigned)offsetof(player_t, xtralife));
		K_Finding(text);

		// Past roundconditions -- the second run's other new offsets, 1828 and
		// 1832, were never named at all: nothing after roundconditions had a
		// line. This is everything declared after it, to the end of the struct.
		snprintf(text, sizeof (text),
			"%s: offsets -- roundconditions %u, powerup %u, icecube %u, tally %u, "
			"darkness_start %u, darkness_end %u",
			cmd,
			(unsigned)offsetof(player_t, roundconditions), (unsigned)offsetof(player_t, powerup),
			(unsigned)offsetof(player_t, icecube), (unsigned)offsetof(player_t, tally),
			(unsigned)offsetof(player_t, darkness_start), (unsigned)offsetof(player_t, darkness_end));
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
	CONS_Printf("%s: %s, %u racers (%u of them bots), %u spectators, %s, %s\n",
		cmd, G_BuildMapName(gamemap), racers, bots, spectators,
		(grandprixinfo.gp ? "Grand Prix" : "not a Grand Prix"),
		(encoremode ? "Encore" : "not Encore"));
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
	size_t bytesdiffer = 0;
	size_t runs = 0;
	size_t last = 0;
	size_t scan;

	for (at = 0; at < shared; at++)
	{
		if (a->buffer[at] != b->buffer[at])
			break;
	}

	// How much differs, not only where it starts. Every report of this residue
	// so far has been a single offset, which says nothing about whether one
	// value moved or a thousand did -- and the per-object pass that was supposed
	// to answer that turned out to be comparing unrelated objects. One extra
	// pass over a hundred and fifty kilobytes settles it.
	for (scan = at; scan < shared; scan++)
	{
		if (a->buffer[scan] == b->buffer[scan])
			continue;

		bytesdiffer++;
		last = scan;

		if (scan == at || a->buffer[scan - 1] == b->buffer[scan - 1])
			runs++;
	}

	if (a->used == b->used && at == shared)
	{
		CONS_Printf("%s: %s IDENTICAL over all %s bytes\n",
			cmd, what, sizeu1(a->used));
		return true;
	}

	CONS_Printf("%s: %s bytes differ in %s runs, from byte %s to byte %s of %s\n",
		cmd, sizeu1(bytesdiffer), sizeu2(runs), sizeu3(at), sizeu4(last),
		sizeu5(shared));

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

	// Which object that first differing byte belongs to. Worked out from the
	// capture of the living world rather than from the record comparison, because
	// the record comparison lines the two captures up by position and has been
	// caught being out of step.
	if (records)
		K_LocateSnapshotRecord(cmd, a, at, recsa);

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

	if (K_RawSnapBlocksTests("rollback_test"))
		return;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return;
	}

	K_PrintGrid("rollback_test");

	// Somewhere to put the second snapshot that is not part of the ring.
	// Transient: a megabyte is not worth holding on to between invocations of
	// a diagnostic command.
	resaved = (rollbackslot_t *)Z_Calloc(sizeof (rollbackslot_t), PU_STATIC, NULL);
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
		K_FreeSlot(resaved);
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
		K_FreeSlot(resaved);
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
		K_FreeSlot(resaved);
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

	K_FreeSlot(resaved);
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
static dboolean g_replaying;   // defined with the replay, further down

static void K_RunFrozenTics(int32_t tics, const ticcmd_t *frozen)
{
	const dboolean wasreplaying = g_replaying;
	int32_t n, i;

	// Every tic run here is off the timeline -- the check restores the world
	// after it -- so it counts as a replay for whatever should sit replays out:
	// sounds, and the unlocks a tic writes to gamedata, which no restore puts
	// back. A leak soak on Carnival Night failed on a spray can the local kart
	// grabbed inside a check (WORLDWIDE.md 8.54).
	g_replaying = true;

	for (n = 0; n < tics; n++)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
				players[i].cmd = frozen[i];
		}

		P_Ticker(true);
	}

	g_replaying = wasreplaying;
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

/** Does a mispredicted speculation leave anything behind a restore cannot take?
  *
  * The resimulation check runs both of its passes on the SAME inputs, and that
  * makes it blind to the one thing the netcode actually does: it speculates on
  * *predicted* inputs, restores, and runs the confirmed tic on the real ones.
  * Anything living outside the archive, the players and the mobjs -- a static, a
  * cache, a global the tic writes and later reads -- would be left holding a
  * value computed from inputs that never happened, while two passes with
  * identical inputs recompute it identically and agree. 330 clean soak checks
  * next to 0.5 units of netplay drift is exactly that shape.
  *
  * Three runs of one tic on the real inputs, all from the same saved world:
  *
  *   B   a pristine reference, taken before anything else has run
  *   A1  after a pass of `tics` tics on the REAL inputs, restored
  *   A2  after a pass of `tics` tics on PERTURBED inputs, restored
  *
  * A1 != B says any extra pass pollutes, whatever it simulated. A1 == B with
  * A2 != B says it takes a *wrong* pass, which is the netplay case exactly.
  * Both are bugs, and they are different bugs.
  *
  * The perturbation is a neutral input, because that is what a client really
  * predicts for somebody who was doing nothing -- not an invented extreme.
  *
  * \param verbose prints the grid and the timings even when nothing is wrong.
  * \return true when neither pass left anything behind.
  */
static dboolean K_LeakCheck(int32_t tics, dboolean verbose)
{
	rollbackslot_t *first, *second, *third;
	diagset_t recsfirst = {0}, recssecond = {0}, recsthird = {0};
	ticcmd_t real[MAXPLAYERS], wrong[MAXPLAYERS];
	tic_t startedat;
	int32_t perturbed = 0;
	int32_t i;
	dboolean records;
	dboolean honest = false;
	dboolean predicted = false;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("You must be in a level to use this.\n");
		return false;
	}

	if (tics < 1)
		tics = 1;

	if (tics > ROLLBACK_TICS)
		tics = ROLLBACK_TICS;

	if (K_NeedScratch() == false)
	{
		CONS_Printf("rollback_leak: not enough memory for the comparison slots\n");
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

	for (i = 0; i < MAXPLAYERS; i++)
	{
		real[i] = players[i].cmd;
		wrong[i] = real[i];

		wrong[i].forwardmove = 0;
		wrong[i].turning = 0;
		wrong[i].throwdir = 0;
		wrong[i].aiming = 0;
		wrong[i].buttons = 0;

		if (playeringame[i] && memcmp(&wrong[i], &real[i], sizeof (ticcmd_t)) != 0)
			perturbed++;
	}

	// A check that cannot fail is worse than no check: on a parked grid every
	// input is already neutral, the two passes are the same pass, and a clean
	// result would mean nothing at all. Say so instead of reporting a pass.
	if (perturbed == 0)
	{
		if (verbose)
		{
			CONS_Printf("rollback_leak: nothing to perturb -- every input was "
				"already neutral, so the wrong pass would be the right one\n");
		}

		return true;
	}

	if (verbose)
		K_PrintGrid("rollback_leak");

	if (!K_SaveGameState(gametic))
	{
		CONS_Printf("rollback_leak: K_SaveGameState failed\n");
		return false;
	}

	startedat = leveltime;

	g_holdfindings = true;
	g_heldcount = 0;
	g_helddropped = 0;

	// ---- B: the reference, run on a freshly restored world -----------------
	//
	// Not the live world directly: A1 and A2's comparable tic always runs
	// after a K_LoadGameState (restoring after the detour), so a tic run
	// straight off the untouched live world would compare "never rebuilt"
	// against "rebuilt", which is a different question than the one this
	// check exists to ask. One restore here, matching the one every other
	// branch's comparable tic gets, isolates the actual variable: whether an
	// extra pass IN BETWEEN two otherwise-identical restores leaves anything.
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: could not restore before the reference\n");
		goto done;
	}

	srand((unsigned int)gametic);
	K_RunFrozenTics(1, real);

	if (leveltime == startedat)
	{
		CONS_Printf("rollback_leak: the world did not advance -- the game is "
			"paused, or the window is unfocused and pauseifunfocused is on\n");
		goto done;
	}

	if (!K_WriteSnapshot(first, gametic))
	{
		CONS_Printf("rollback_leak: could not snapshot the reference\n");
		goto done;
	}

	K_CopyPlayers(0);
	K_CopyMobjs();

	if (records)
		K_CaptureRecords(&recsfirst);

	// ---- A1: the same tic, after an honest pass of the same length ---------
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: could not get back to the starting state\n");
		goto done;
	}

	srand((unsigned int)gametic);
	K_RunFrozenTics(tics, real);

	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: could not get back after the honest pass\n");
		goto done;
	}

	srand((unsigned int)gametic);
	K_RunFrozenTics(1, real);

	if (!K_WriteSnapshot(second, gametic))
	{
		CONS_Printf("rollback_leak: could not snapshot after the honest pass\n");
		goto done;
	}

	K_CopyPlayers(1);

	if (records)
		K_CaptureRecords(&recssecond);

	// ---- A2: the same tic again, after a pass on inputs that never were ----
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: could not get back before the wrong pass\n");
		goto done;
	}

	srand((unsigned int)gametic);
	K_RunFrozenTics(tics, wrong);

	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: could not get back after the wrong pass\n");
		goto done;
	}

	srand((unsigned int)gametic);
	K_RunFrozenTics(1, real);

	if (!K_WriteSnapshot(third, gametic))
	{
		CONS_Printf("rollback_leak: could not snapshot after the wrong pass\n");
		goto done;
	}

	K_CopyPlayers(2);

	if (records)
		K_CaptureRecords(&recsthird);

	honest = (first->used == second->used
		&& memcmp(first->buffer, second->buffer, first->used) == 0);
	predicted = (first->used == third->used
		&& memcmp(first->buffer, third->buffer, first->used) == 0);

	if (verbose)
	{
		CONS_Printf("rollback_leak: %d tics, %d inputs perturbed -- honest pass "
			"%s, mispredicted pass %s\n",
			tics, perturbed,
			(honest ? "left nothing" : "LEFT SOMETHING"),
			(predicted ? "left nothing" : "LEFT SOMETHING"));
	}

	if (honest == false || predicted == false)
	{
		K_PrintGrid("rollback_leak");

		if (honest == false)
		{
			CONS_Printf("rollback_leak: a pass on the REAL inputs already changed "
				"the tic that followed it -- so it is not about mispredicting, it "
				"is about running extra tics at all\n");

			K_ReportComparison("rollback_leak", "extra pass",
				first, "reference", second, "after an honest pass",
				&recsfirst, &recssecond, records);
			K_ReportAttachments("rollback_leak", g_playercopy[0], g_playercopy[1]);
			K_ComparePlayers("rollback_leak", g_playercopy[0], g_playercopy[1], -1);
		}
		else
		{
			CONS_Printf("rollback_leak: the honest pass left nothing and the "
				"mispredicted one did -- the speculation carries something "
				"forward that the restore does not take back, and it depends on "
				"the inputs it ran\n");

			K_ReportComparison("rollback_leak", "misprediction",
				first, "reference", third, "after a wrong pass",
				&recsfirst, &recsthird, records);
			K_ReportAttachments("rollback_leak", g_playercopy[0], g_playercopy[2]);
			K_ComparePlayers("rollback_leak", g_playercopy[0], g_playercopy[2], -1);
			K_CompareMobjs("rollback_leak");
		}

		K_ReleaseFindings();
	}

done:
	// Back where this found it, whatever happened: a check must not leave the
	// level ahead of the tic the netcode believes it is on.
	if (!K_LoadGameState(gametic))
	{
		CONS_Printf("rollback_leak: WARNING - could not restore the starting "
			"state, so the level is now ahead of where it was\n");
	}

	g_holdfindings = false;

	return (honest && predicted);
}

/** Console command: rollback_leak [tics] */
static void Command_RollbackLeak_f(void)
{
	int32_t tics = 4;

	if (K_RawSnapBlocksTests("rollback_leak"))
		return;

	if (COM_Argc() > 1)
		tics = atoi(COM_Argv(1));

	K_LeakCheck(tics, true);
}

/** Console command: rollback_resim [tics] */
static void Command_RollbackResim_f(void)
{
	int32_t tics = 4;

	if (K_RawSnapBlocksTests("rollback_resim"))
		return;

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
static dboolean g_soakleak;     // leak checks rather than resimulation ones
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
			CONS_Printf("rollback_soak: every %d tics, %s %d. "
				"%u checks so far, %u failures.\n",
				g_soakinterval, (g_soakleak ? "leak-checking" : "resimulating"),
				g_soaktics, g_soakchecks, g_soakfailures);
		}
		return;
	}

	if (K_RawSnapBlocksTests("rollback_soak"))
		return;

	g_soakinterval = atoi(COM_Argv(1));

	if (g_soakinterval < 0)
		g_soakinterval = 0;

	if (COM_Argc() > 2)
		g_soaktics = atoi(COM_Argv(2));

	if (g_soaktics < 1)
		g_soaktics = 4;

	// A third argument picks the question. The resimulation check asks whether
	// a restored world behaves like the live one on the same inputs; the leak
	// check asks whether a pass on inputs that never happened leaves anything
	// behind. 330 clean checks of the first, beside half a unit of netplay
	// drift, is what made the second worth writing.
	g_soakleak = (COM_Argc() > 3 && atoi(COM_Argv(3)) != 0);

	if (g_soakinterval == 0)
	{
		CONS_Printf("rollback_soak: stopped after %u checks, %u failures.\n",
			g_soakchecks, g_soakfailures);
		return;
	}

	g_soakchecks = 0;
	g_soakfailures = 0;

	CONS_Printf("rollback_soak: checking every %d tics, %s %d tics each time. "
		"Silence means agreement.\n", g_soakinterval,
		(g_soakleak ? "leak-checking" : "resimulating"), g_soaktics);

	// What it is about to soak, said once at the start rather than only on the
	// first failure. A run that never fails otherwise records five hundred
	// checks against no map and no mode at all, which is a number nobody can
	// use -- and a run that switched mode with -random may not be on the map
	// its config named.
	K_PrintGrid("rollback_soak");
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

/** The inputs the ring recorded for a tic, or NULL if it does not hold that tic.
  *
  * The slot has to be asked which tic it is: a ring of twenty is overwritten
  * every twenty tics, and a comparison against the wrong tic's inputs would
  * disagree constantly and look like a finding.
  */
static const ticcmd_t *K_InputsAsUsed(tic_t tic)
{
	const rollbackslot_t *slot;

	if (rollbackring == NULL)
		return NULL;

	slot = &rollbackring[tic % ROLLBACK_TICS];

	if (slot->valid == false || slot->tic != tic)
		return NULL;

	return slot->usedcmds;
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

static dboolean g_replaying;     // true while a correction is re-running tics

// The two-clock mode.
//
// The authoritative clock, gametic, runs only tics the server has confirmed --
// the stock loop, untouched -- so consistancy[] always describes a world built
// entirely from inputs the server sent, and the client never announces the
// checksum of something it guessed. The speculation runs on top of that, from a
// snapshot, and is thrown away and rebuilt every pass.
//
// This is SRB2 NetPlus's shape, and it is here because the alternative was
// measured: hoisting gametic itself past neededtic made the client report
// speculative checksums, and the server answered with seven full state resends a
// race. Reporting the confirmed tic's index instead did not help, and the reason
// is the argument for this whole change -- consistancy[neededtic] had been
// computed when that tic was still a guess, and confirming it later recomputes
// nothing. **A checksum of a confirmed timeline requires actually keeping one.**
//
// It also deletes work rather than adding it: with the speculation rebuilt every
// pass from the latest inputs, there is nothing to detect and nothing to correct.
// No pending rollback, no rate limiter, no self-misprediction to centre.
static int32_t g_twoclock;          // tics of speculation; 0 = off
static tic_t g_confirmedtic;        // the frontier the speculation was spun from
static dboolean g_speculated;       // a speculation is standing and must be undone
static dboolean g_speculating;      // and one is being run right now
static uint32_t g_specpasses;       // speculations built
static uint32_t g_spectics;         // tics they ran
static uint32_t g_specstranded;     // times the confirmed world could not be restored
static uint32_t g_specnosave;       // times the frontier could not be saved
static uint32_t g_unspecus;         // microseconds spent putting the world back
static uint32_t g_specus;           // and running the speculation forward

// rollback_keepspec -- track A (WORLDWIDE.md 8.60, 8.73).
//
// A pass today puts the world back to the frontier, runs the confirmed tics and
// runs the speculation again from scratch: on Opulence five tics of 3.4 ms and a
// restore of 6, every pass, whether or not anything the speculation guessed was
// wrong (8.62, 8.69). Left standing instead, a pass only has to check that the
// tics the server has just confirmed carry the inputs the speculation ran them
// with; if so, the speculation's own run of them is the confirmed one, and the
// pass adds a tic at the front. Everything a kept tic owes the netcode -- the
// checksum after it, the inputs it ran -- is noted when the speculation runs it.
//
// A tic is never kept if it ran something a speculation cannot stand for: a
// netxcmd (only the authoritative loop runs them), a message a speculated tic
// raised and had refused, or a gamedata guard that changed what the tic did.
// Each of those rebuilds, and the loop runs the tic for real.
static dboolean g_keepspec;                 // the switch
static void K_PrintRefs(void);              // the karts' bodies' counts, 8.111

// rollback_keepearly (WORLDWIDE.md 8.97, 8.98): a tic the standing speculation
// ran on this machine's input is checked again as soon as that input is known
// -- the pass after the sample is made -- instead of when the server confirms
// the tic, a round trip later. Driven in WORLDWIDE mode, 469 of 488 rebuilds
// were a tic run on a guess past the history, each one the whole depth and a
// longer frame about four times a second. Off by default since 8.99: in the
// race there was nothing for it to remove -- those rebuilds were before it --
// and before the race it fired on most passes. 1 turns it on.
static dboolean g_keepearly = false;
static uint32_t g_keepearlyn;               // standing speculations run again from a tic past the frontier
static uint32_t g_keepearlytics;            // ... the tics they ran again
static dboolean g_keeparmed;                // this pass left the speculation standing
static dboolean g_keepkept;                 // and it was kept
static tic_t g_keephead;                    // the tic the standing speculation reached
static tic_t g_keepheadleveltime;           // and its leveltime
static tic_t g_keepupto;                    // the frontier a kept pass moves to
static uint32_t g_keeploads;                // P_NetLoadCount() when armed
static tic_t g_keeptic[ROLLBACK_TICS];      // which tic each record below is for
static tic_t g_keeplevel[ROLLBACK_TICS];    // leveltime at the start of it
static ticcmd_t g_keepcmds[ROLLBACK_TICS][MAXPLAYERS];   // the inputs it ran
static dboolean g_keepin[ROLLBACK_TICS][MAXPLAYERS];
static int16_t g_keepafter[ROLLBACK_TICS];  // Consistancy() after it
static dboolean g_keeptaint[ROLLBACK_TICS]; // it ran something only the loop may run
static struct rollbackkart_t g_keepkart[ROLLBACK_TICS][MAXPLAYERS];   // the karts at its start,
static dboolean g_keepkartok[ROLLBACK_TICS][MAXPLAYERS];              // in a correction's terms
static uint32_t g_keepcorrnoop;             // corrections due that changed nothing
static tic_t g_soundhorizon;                // the first tic this machine has not run yet

enum
{
	KEEP_KEPT, KEEP_CORRECTION, KEEP_AHEAD, KEEP_INPUT, KEEP_TEXTCMD, KEEP_TAINT,
	KEEP_MISSING, KEEP_RELOADED, KEEP_LEVELSTART, KEEP_NUMREASONS
};
static uint32_t g_keepcount[KEEP_NUMREASONS];

// The discriminator: save the frontier and restore it, and run no speculative
// tic at all. The world round-trips through the archive once a pass with nothing
// happening in between.
//
// Two suspects remain for a desync the two-clock pivot did not fix, and they want
// opposite work. Either the restore itself does not put everything back -- and it
// cannot be caught by any test here, because our oracle compares archives and
// what is missing is by definition not in one -- or the speculation has side
// effects that escape the world it happens in, a message posted in a timeline
// that is then discarded being the obvious one. This separates them: resyncs with
// nothing speculated indict the restore, and their absence clears it.
static dboolean g_nullspec;

// The speculation starting on tics the server has already sent: see
// K_RollbackPredictInputs. Counted whether or not the fix is on.
//
// On by default (WORLDWIDE.md 8.35, 8.36): confirmed at race scale to close
// the wrong-input mechanism (64-81% of confirmed tics down to under 0.1%),
// and confirmed to cost nothing felt -- what renders is the speculation above
// neededtic, which this switch never touches, so the driver reported no
// difference between it off and on in the same race.
static dboolean g_cleancmds = true;

// A stand-in for a second human (ROADMAP item 2(a)). A remote person is
// guessed by repeating their last input; a bot is not guessed at all, its
// input is computed from this machine's world (K_RollbackPredictInputs). With
// this on, the bots are guessed the way a person is, so a race nobody drives
// shows the rebuilds and the shaking remote people would bring. Client side,
// off by default; it changes only what the speculation guesses, never what
// the server sends.
static dboolean g_botsashuman;
static uint32_t g_recvwrites;   // local slots of an already-received tic written over
static uint32_t g_recvchanged;  // ... with an input that differed from the server's

// Replaying this machine's own sent-but-not-yet-applied inputs across the
// speculation, instead of repeating the newest one (rollback_history,
// WORLDWIDE.md 8.39). Off by default: its feel is for a driver to judge.
static int32_t g_histmax;       // 0 = off; else the deepest speculation it may ask for
static tic_t g_histbase;        // the first tic the server has not sent: replay starts there
static int32_t g_histunacked[MAXSPLITSCREENPLAYERS]; // inputs sent after the one last applied; -1 = no match
static uint32_t g_histpasses;   // passes that looked for the applied input
static uint32_t g_histmatched;  // ... and found it, for the first local player
static uint32_t g_histcapped;   // passes whose replay was cut short by the depth cap
static uint64_t g_histunackedsum;
static uint64_t g_histdepthsum;

// Where the replay and the server's filing part (WORLDWIDE.md 8.85). The replay
// assumes one sample a tic; NetUpdate makes one a call, however many tics went
// by, and the anchor tells samples apart by a stamp two of them can share.
// Counted with rollback_history on or off, reset with it.
static uint32_t g_samples;          // samples NetUpdate made in a level
static uint32_t g_samplelate;       // ... after more than one real tic
static uint32_t g_samplelatetics;   // ... the tics that got no sample of their own
static uint32_t g_samplesamestamp;  // ... with the same stamp as the one before
static uint32_t g_anchorambiguous;  // anchors that matched more than one sample

// R1 (WORLDWIDE.md 8.87, 8.89): the replay follows the server's filing. A
// sample made after k real tics leaves the tics in between to the one before
// it -- the server repeats a sample on a tic that got none of its own
// (SV_Maketic), or files the next one a tic later -- so each sample owns as
// many tics as real tics passed before the next one was made. The replay used
// to give every sample one tic, and ran one sample ahead after each late
// frame: 165 of 165 of this machine's wrong inputs in 8.87. Recorded beside
// the local history, one entry a sample, aged by the same NetUpdate calls.
static dboolean g_histreal = true;  // rollback_histreal; 0 is the control
static int32_t g_samplerealtics[MAXGENTLEMENDELAY];
static uint32_t g_samplehead;
static int32_t g_histheld[MAXSPLITSCREENPLAYERS]; // confirmed tics already holding the applied sample
static uint32_t g_histstretched;    // passes R1 laid out differently from one sample a tic

// rollback_stall: holds this client's loop once, or every so many tics, the
// way a busy machine does -- the cascade's trigger, on demand (8.129).
static int32_t g_stallms;           // 0 = off
static int32_t g_stallperiod;       // 0 = once
static tic_t g_stallnext, g_stalllast;
static uint32_t g_stalls;

// rollback_cascadelog: each gap and each rebuild for this machine's input, on
// a line of its own, dated by the level's clock and the real one, so a cascade
// can be laid out in time (8.129).
static dboolean g_cascadelog;

// What rollback_history holds steady is the drawn tic's lead over the clock,
// not the depth (WORLDWIDE.md 8.40, 8.41). The tic the newest input in flight
// lands on moves with the delay the server files this machine's inputs with --
// raw transit time, jittering by a tic or two -- and speculating to exactly it
// made the drawn world jump by as much every time it moved. So the lead is the
// largest one asked for in the last second: it rises at once when a pass asks
// for more, and comes down one tic a second at most. The depth is whatever
// reaches that lead from the frontier, so the frontier's own unevenness is
// absorbed too.
static dboolean g_histhold;     // a lead is being held
static int32_t g_histlead;      // the held lead: drawn tic minus I_GetTime()
static int32_t g_histpeak;      // the largest lead asked for since the last step
static tic_t g_histstepat;      // I_GetTime() of the last rise or step down
static uint32_t g_histrises;    // the lead raised: the drawn world jumps forward
static uint32_t g_histdrops;    // the lead lowered by a tic: it holds for a frame

// Does the drawn world move with the clock? The tic a pass leaves on screen,
// minus real time, stays put when it does; each change is the drawn world
// jumping forward or back by that many tics in one frame. Counted with
// rollback_history on or off, so its off windows are the control.
static tic_t g_lastpassat;      // I_GetTime() of the last pass
static dboolean g_drawnvalid;
static int32_t g_drawnoffset;   // the drawn tic minus I_GetTime(), last pass
static uint32_t g_drawnpasses;
static uint32_t g_drawnjumps;
static uint32_t g_drawnjumptics;

// Messages a speculated tic tried to send. localtextcmd is netcode state, not
// world state, so the archive does not carry it and a restore cannot take one
// back -- the server would apply a message from a timeline that was discarded.
// Counted rather than only refused, because a guard that never fires and a guard
// that works are indistinguishable from the outside.
static uint32_t g_suppressedxcmds;

// The light correction channel, receiving side.
//
// What this measures is the thing this branch has never been able to see: the
// gap between one client's confirmed world and the server's, on a named tic,
// for every kart, continuously. The consistency checksum only ever said "these
// differ" -- once per five seconds at best, because the resend it triggers has
// a cooldown, so nine refusals in a race is a floor and not a count.
//
// Measurement is separated from correction on purpose, and measurement is the
// default. A run that moves the karts changes the thing being measured, and
// this project has already read one confounded number as a fix.
static int32_t g_correctrate;       // tics between sends; 0 = off (server side)
static dboolean g_correctsuppress;  // and whether they replace the full resend
static dboolean g_correctapply;     // move the karts, or only measure them
static dboolean g_correctpending;
static uint32_t g_correcttic;
static uint8_t g_correctn;
static struct rollbackkart_t g_correctkart[MAXPLAYERS];

static uint32_t g_statecorrections; // corrections received off the wire
                                    // (not g_corrections: that name is already a
                                    // file-scope counter for the old loop's own
                                    // rollbacks, and a second tentative definition
                                    // of it would have been the same object)
static uint32_t g_correctused;      // ... measured against the tic they name
static uint32_t g_correctmissed;    // ... dropped because the loop stepped over it
static uint32_t g_driftsamples;     // kart-corrections measured
static uint64_t g_driftsum;         // total position error, in 1/65536 units
static uint64_t g_driftmax;
static int32_t g_driftworst = -1;
static uint32_t g_driftmoved;       // karts actually put back
static uint32_t g_driftrefused;     // ... and karts the move refused to place
static uint32_t g_driftsame;        // ... and karts already where the server had them

// ----------------------------------------------------------------------------
// WORLDWIDE mode (ROADMAP.md, compatibility, steps 2 to 6; WORLDWIDE.md 8.80)
// ----------------------------------------------------------------------------
//
// Everything in this file is behind console switches that the test harness sets
// on two machines: rollback_correct on the server, rollback_twoclock and its
// companions on the client. Nobody else can be asked to type those, and the
// host's delay exemption had to guess the mode from g_correctrate (8.26). The
// compatibility policy says the server decides, so the server has one switch,
// cv_worldwide, and the rest follows from it:
//
//   - a server in WORLDWIDE mode sends the light correction channel in place of
//     the full-state resend, says so in its server info (SV_WORLDWIDE), and
//     turns away a client that does not declare itself WORLDWIDE at join;
//   - a client reads that bit when it joins, and runs the prediction the driven
//     races settled on (playclient_keep.cfg, 8.78) against a server that has
//     it -- and none at all against one that does not: a stock server gets a
//     stock client.
//
// The console switches still work on top of it, for measuring: a rate set by
// rollback_correct wins over the mode's, and a client's switches can be moved
// after it has joined.
#define WORLDWIDE_CORRECTRATE 4   // tics between corrections: every driven race since 8.44
#define WORLDWIDE_TWOCLOCK 4      // the speculation's floor; rollback_history lifts it
#define WORLDWIDE_HISTORY 12      // how deep the history may take it (8.47, 8.48, 8.78)

static dboolean g_wwclient;       // this client's switches were set by joining a WORLDWIDE server
static dboolean g_wwkeepingwas;   // the snapshot keeper's switch before that join turned it on
static dboolean g_wwvanillajoin;  // rollback_vanillajoin: join without declaring, to test the refusal

dboolean K_WorldwideServer(void)
{
	return (server && netgame && cv_worldwide.value != 0);
}

dboolean K_WorldwideDeclare(void)
{
	return (g_wwvanillajoin == false);
}

/** Is anybody but this machine connected to it? */
static dboolean K_RemoteNodeInGame(void)
{
	int32_t node;

	for (node = 1; node < MAXNETNODES; node++)
	{
		if (nodeingame[node])
			return true;
	}

	return false;
}

/** cv_worldwide's callback. A client learns the mode from the server info when
  * it joins and keeps it for its whole stay, so a server that changed it under
  * connected players would part from every one of them: corrections starting
  * for clients that ignore them and resends stopping for clients that need
  * them, or the reverse. The change is only taken with nobody else connected,
  * which still lets a dedicated server's startup script or the harness set it
  * before anybody joins. */
void Worldwide_OnChange(void);
void Worldwide_OnChange(void)
{
	if (server && netgame && K_RemoteNodeInGame())
	{
		CONS_Alert(CONS_WARNING, "worldwide: can only change with nobody else connected -- "
			"everyone here joined %s\n",
			(cv_worldwide.value ? "a stock server" : "a WORLDWIDE server"));
		CV_StealthSetValue(&cv_worldwide, (cv_worldwide.value ? 0 : 1));
		return;
	}

	CONS_Printf("worldwide: %s\n",
		(cv_worldwide.value
			? "on -- corrections every " TOSTR2(WORLDWIDE_CORRECTRATE) " tics instead of "
				"full-state resends, and WORLDWIDE clients only"
			: "off -- the stock netcode, and any client"));
}

// ----------------------------------------------------------------------------
// What a pass costs, what a frame costs, and how often the guess was right
// ----------------------------------------------------------------------------
//
// Gibax felt stutter and dropped frames (WORLDWIDE.md 8.50) and asked where they
// come from (8.60). The pass cost above stops at the restore and the
// speculation: nothing timed the save, the confirmed tics or the network, no log
// holds a frame's duration, and nothing said whether rebuilding the speculation
// was needed at all -- whether the tics the server confirmed carried the inputs
// the speculation had guessed. Counted in a level, printed by rollback_twoclock
// and reset with its other counts.
static uint32_t g_saveus;                       // saving the frontier
static uint64_t g_stepus[ROLLBACK_NUMSTEPS];    // the rest of a pass, step by step
static uint32_t g_steppasses;                   // passes timed
static uint32_t g_confirmedrun;                 // confirmed tics those passes ran

#define ROLLBACK_FRAMEBUCKETS 6
static const uint32_t g_framebucketus[ROLLBACK_FRAMEBUCKETS - 1] =
	{ 8333, 16667, 28571, 33333, 50000 };
static uint32_t g_framework[2][ROLLBACK_FRAMEBUCKETS];  // [ran the tic loop] work before the sleep
static uint64_t g_frameworksum[2];
static uint32_t g_frameworkmax[2];
static uint32_t g_frameworkcount[2];
static uint32_t g_framegap[ROLLBACK_FRAMEBUCKETS];      // from one drawn frame to the next
static uint32_t g_framegapmax;
static uint32_t g_framesdrawn;
static uint32_t g_frameskips;                           // iterations that made the next frame skip
static precise_t g_lastdrawnat;

// The local kart and the view as drawn, frame to frame (WORLDWIDE.md 8.99,
// 8.100). In races where nothing is rebuilt and no kart put back, Gibax still
// sees the kart "rollback very slightly" -- something in what is drawn. Each
// drawn frame's step of the kart and of the view is set against the kart's
// speed and the time since the frame before: even, short (under half of it),
// long (over one and a half), or backwards; split by whether the frame
// carried a pass of the tic loop.
enum { DRAWSTEP_EVEN, DRAWSTEP_SHORT, DRAWSTEP_LONG, DRAWSTEP_BACK, NUMDRAWSTEPS };
static uint32_t g_drawkart[2][NUMDRAWSTEPS];   // [frame with a pass][class]
static uint32_t g_drawview[2][NUMDRAWSTEPS];
static fixed_t g_drawlastkx, g_drawlastky, g_drawlastvx, g_drawlastvy;
static dboolean g_drawlastok;

static int32_t K_DrawStepClass(fixed_t dx, fixed_t dy, fixed_t mx, fixed_t my, uint32_t gapus)
{
	const double fx = (double)dx / FRACUNIT, fy = (double)dy / FRACUNIT;
	const double vx = (double)mx / FRACUNIT, vy = (double)my / FRACUNIT;
	const double step = sqrt(fx * fx + fy * fy);
	const double expected = sqrt(vx * vx + vy * vy) * (double)gapus * TICRATE / 1000000.0;

	if (fx * vx + fy * vy < 0.0)
		return DRAWSTEP_BACK;

	if (step < 0.5 * expected)
		return DRAWSTEP_SHORT;

	if (step > 1.5 * expected)
		return DRAWSTEP_LONG;

	return DRAWSTEP_EVEN;
}

static void K_NoteDrawnKart(dboolean ranloop, uint32_t gapus)
{
	const int32_t who = g_localplayers[0];
	const int32_t which = ranloop ? 1 : 0;
	interpmobjstate_t st;
	mobj_t *mo;

	if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false || players[who].spectator
		|| players[who].mo == NULL || P_MobjWasRemoved(players[who].mo))
	{
		g_drawlastok = false;
		return;
	}

	mo = players[who].mo;
	R_InterpolateMobjState(mo, rendertimefrac, &st);

	// Moving -- over 2 units a tic -- and not a respawn or a teleport.
	if (g_drawlastok && gapus > 0 && gapus < 100000
		&& FixedHypot(mo->momx, mo->momy) > 2 * FRACUNIT)
	{
		const fixed_t kdx = st.x - g_drawlastkx, kdy = st.y - g_drawlastky;
		const fixed_t vdx = viewx - g_drawlastvx, vdy = viewy - g_drawlastvy;

		if (FixedHypot(kdx, kdy) < 512 * FRACUNIT)
		{
			g_drawkart[which][K_DrawStepClass(kdx, kdy, mo->momx, mo->momy, gapus)]++;
			g_drawview[which][K_DrawStepClass(vdx, vdy, mo->momx, mo->momy, gapus)]++;
		}
	}

	g_drawlastkx = st.x;
	g_drawlastky = st.y;
	g_drawlastvx = viewx;
	g_drawlastvy = viewy;
	g_drawlastok = true;
}

// The other karts as drawn, frame to frame (WORLDWIDE.md 8.121): what a
// second human would see of the rest of the grid. The local kart above is
// never a guess, so it cannot show how a wrong one is drawn; these can. Each
// kart's step is classed as the local kart's is, and counted by who drives
// it -- a bot or a person -- since the speculation guesses the two
// differently (8.120). A kart is followed by its slot, not its body: a load
// of the archive may hand it a new body, and that frame is the one to see.
static uint32_t g_drawothers[2][2][NUMDRAWSTEPS];   // [bot, person][frame with a pass][class]
static fixed_t g_drawotherx[MAXPLAYERS], g_drawothery[MAXPLAYERS];
static dboolean g_drawotherok[MAXPLAYERS];

static void K_NoteDrawnOthers(dboolean ranloop, uint32_t gapus)
{
	const int32_t which = ranloop ? 1 : 0;
	int32_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		interpmobjstate_t st;
		mobj_t *mo;

		if (i == g_localplayers[0] || playeringame[i] == false || players[i].spectator
			|| players[i].mo == NULL || P_MobjWasRemoved(players[i].mo))
		{
			g_drawotherok[i] = false;
			continue;
		}

		mo = players[i].mo;
		R_InterpolateMobjState(mo, rendertimefrac, &st);

		// Moving -- over 2 units a tic -- and not a respawn or a teleport, as
		// for the local kart.
		if (g_drawotherok[i] && gapus > 0 && gapus < 100000
			&& FixedHypot(mo->momx, mo->momy) > 2 * FRACUNIT)
		{
			const fixed_t dx = st.x - g_drawotherx[i], dy = st.y - g_drawothery[i];

			if (FixedHypot(dx, dy) < 512 * FRACUNIT)
				g_drawothers[players[i].bot ? 0 : 1][which][K_DrawStepClass(dx, dy, mo->momx, mo->momy, gapus)]++;
		}

		g_drawotherx[i] = st.x;
		g_drawothery[i] = st.y;
		g_drawotherok[i] = true;
	}
}

// The inputs the standing speculation ran, tic by tic, checked against what the
// server confirms for the same tics on the next pass.
#define ROLLBACK_GUESSMAX 32
static ticcmd_t g_guesscmds[ROLLBACK_GUESSMAX][MAXPLAYERS];
static dboolean g_guessin[ROLLBACK_GUESSMAX][MAXPLAYERS];
static tic_t g_guessfrom;               // the first tic recorded
static int32_t g_guesstics;             // how many are
static uint32_t g_guessmovedat;         // g_driftmoved when the speculation was built
static uint32_t g_guesspasses;          // passes that confirmed at least one guessed tic
static uint32_t g_guessright;           // ... and every input on them was the one guessed
static uint32_t g_guessrightmoved;      // ... right, yet a correction moved a kart
static uint32_t g_guessfirstwrong[5];   // the first wrong tic, from the frontier: 0-3, 4+
static uint32_t g_guesswrong[3];        // wrong inputs by who: this machine, bots, people

// Which fields a wrong guess got wrong, by who (WORLDWIDE.md 8.62: half the
// passes had one, this machine's idle kart and the bots alike, and the
// candidates -- the latency stamp, a bot input built from another tic's
// world -- differ in which field they touch).
enum
{
	GUESSFIELD_FORWARD, GUESSFIELD_TURNING, GUESSFIELD_ANGLE, GUESSFIELD_THROWDIR,
	GUESSFIELD_AIMING, GUESSFIELD_BUTTONS, GUESSFIELD_LATENCY, GUESSFIELD_FLAGS,
	GUESSFIELD_BOT, GUESSFIELD_RECEIVED, NUMGUESSFIELDS
};
static const char *const g_guessfieldname[NUMGUESSFIELDS] =
	{ "forwardmove", "turning", "angle", "throwdir", "aiming", "buttons", "latency", "flags", "bot",
	  "received" };
static uint32_t g_guessfield[3][NUMGUESSFIELDS];

// rollback_keepspec's rebuilds for a wrong input, taken apart (WORLDWIDE.md
// 8.78: driven on Opulence, a third of the passes rebuilt, each one a hitch,
// and rollback_hits does not run while the speculation is kept). Who was wrong
// on the first wrong tic, in which fields, how far past the frontier -- and for
// this machine's own input, where the speculation had taken it from.
enum
{
	KEEPSRC_RECEIVED,   // the server had already sent the tic
	KEEPSRC_REPLAYED,   // an input sent and not yet applied (rollback_history)
	KEEPSRC_GUESSED,    // past the newest input: the newest, repeated
	KEEPSRC_NONE,       // no local player in the game
	NUMKEEPSRC
};
static uint8_t g_keepsrc[ROLLBACK_TICS];            // where each speculated tic's local input came from
static uint32_t g_keepmissat[5];                    // the first wrong tic, from the frontier: 0-3, 4+
static uint32_t g_keepmisswho[3];                   // wrong inputs on it: this machine, bots, people
static uint32_t g_keepmissfield[3][NUMGUESSFIELDS];
static uint32_t g_keepmisssrc[NUMKEEPSRC];          // this machine's, by where they came from
static uint32_t g_keepmissrun;                      // tics already run that the rebuilds threw away
static uint32_t g_keepmissright;                    // ... of them before the first wrong tic

// This machine's wrong inputs, by where the sample the server applied sits in
// the history against the one the replay used (WORLDWIDE.md 8.85): newer means
// the replay fell behind (a sample lost at the server), older that it ran ahead
// (a tic the server filled by repeating, or an anchor that took a newer twin).
enum
{
	KEEPSHIFT_NEWER2, KEEPSHIFT_NEWER1, KEEPSHIFT_SAME, KEEPSHIFT_OLDER1, KEEPSHIFT_OLDER2,
	KEEPSHIFT_LOST, NUMKEEPSHIFT
};
static uint32_t g_keepmissshift[NUMKEEPSHIFT];
static uint32_t g_chatheld;     // defined with K_RollbackChatSilenced, further down

// Where a speculated tic spends its time (8.62: 3.2 to 3.5 ms a tic on
// Opulence, 1.3 on Skyscraper Leaps). The game times each part of a tic
// already (m_perfstats) and overwrites the figure every tic; these add them
// up over the speculated tics only.
static uint64_t g_spticus;                              // whole G_Ticker
static uint64_t g_spplayerus;                           // P_PlayerThink for everyone
static uint64_t g_splistus[NUM_ACTIVETHINKERLISTS];     // each thinker list
static uint64_t g_spacsus;                              // ACS
static uint64_t g_spluathinkus;                         // Lua ThinkFrame
static uint64_t g_spluahooks;                           // Lua mobj hook calls
static uint64_t g_spcheckpos;                           // P_CheckPosition calls
static uint32_t g_spticcount;

static int32_t K_FrameBucket(uint32_t us)
{
	int32_t b = 0;

	while (b < ROLLBACK_FRAMEBUCKETS - 1 && us >= g_framebucketus[b])
		b++;

	return b;
}

void K_RollbackNoteStep(rollbackstep_t step, precise_t *since)
{
	const precise_t now = I_GetPreciseTime();

	if (gamestate == GS_LEVEL && step < ROLLBACK_NUMSTEPS)
	{
		g_stepus[step] += K_PreciseToMicros(now - *since);

		if (step == ROLLBACK_STEP_CONFIRMED)
			g_steppasses++;
	}

	*since = now;
}

void K_RollbackNoteConfirmedTics(int32_t tics)
{
	if (gamestate == GS_LEVEL && tics > 0)
		g_confirmedrun += (uint32_t)tics;
}

void K_RollbackNoteFrame(precise_t work, dboolean ranloop, dboolean drew, dboolean skipnext)
{
	const uint32_t us = K_PreciseToMicros(work);
	const int32_t which = ranloop ? 1 : 0;

	if (gamestate != GS_LEVEL)
	{
		// A gap spent in a menu or a wipe is not a frame the race drew late.
		g_lastdrawnat = 0;
		g_drawlastok = false;
		memset(g_drawotherok, 0, sizeof g_drawotherok);
		return;
	}

	g_framework[which][K_FrameBucket(us)]++;
	g_frameworksum[which] += us;
	g_frameworkcount[which]++;

	if (us > g_frameworkmax[which])
		g_frameworkmax[which] = us;

	if (skipnext)
		g_frameskips++;

	if (drew)
	{
		const precise_t now = I_GetPreciseTime();

		g_framesdrawn++;

		if (g_lastdrawnat != 0)
		{
			const uint32_t gap = K_PreciseToMicros(now - g_lastdrawnat);

			g_framegap[K_FrameBucket(gap)]++;

			if (gap > g_framegapmax)
				g_framegapmax = gap;

			K_NoteDrawnKart(ranloop, gap);
			K_NoteDrawnOthers(ranloop, gap);
		}
		else
		{
			K_NoteDrawnKart(ranloop, 0);
			K_NoteDrawnOthers(ranloop, 0);
		}

		g_lastdrawnat = now;
	}
}

// The flag that says an input arrived is the one difference a guess always has,
// and it does not change what the tic does.
static dboolean K_SameInput(const ticcmd_t *a, const ticcmd_t *b)
{
	ticcmd_t x = *a, y = *b;

	x.flags &= ~TICCMD_RECEIVED;
	y.flags &= ~TICCMD_RECEIVED;

	return (memcmp(&x, &y, sizeof (ticcmd_t)) == 0);
}

// Which fields of a guessed input g differ from the real one r.
static void K_CountWrongFields(uint32_t *fields, const ticcmd_t *g, const ticcmd_t *r)
{
	if (g->forwardmove != r->forwardmove) fields[GUESSFIELD_FORWARD]++;
	if (g->turning != r->turning) fields[GUESSFIELD_TURNING]++;
	if (g->angle != r->angle) fields[GUESSFIELD_ANGLE]++;
	if (g->throwdir != r->throwdir) fields[GUESSFIELD_THROWDIR]++;
	if (g->aiming != r->aiming) fields[GUESSFIELD_AIMING]++;
	if (g->buttons != r->buttons) fields[GUESSFIELD_BUTTONS]++;
	if (g->latency != r->latency) fields[GUESSFIELD_LATENCY]++;
	if ((g->flags & ~TICCMD_RECEIVED) != (r->flags & ~TICCMD_RECEIVED))
		fields[GUESSFIELD_FLAGS]++;
	if (memcmp(&g->bot, &r->bot, sizeof (g->bot)) != 0)
		fields[GUESSFIELD_BOT]++;
	if ((g->flags ^ r->flags) & TICCMD_RECEIVED)
		fields[GUESSFIELD_RECEIVED]++;
}

// One line per who with a wrong input, naming the fields.
static void K_PrintWrongFields(const char *prefix, const uint32_t *wrong,
	uint32_t fields[3][NUMGUESSFIELDS])
{
	static const char *const whoname[3] = { "this machine", "bots", "people" };
	int32_t who, f;

	for (who = 0; who < 3; who++)
	{
		char line[400];
		size_t len;

		if (wrong[who] == 0)
			continue;

		len = (size_t)snprintf(line, sizeof line, "%s: %s's wrong inputs differ in --",
			prefix, whoname[who]);

		for (f = 0; f < NUMGUESSFIELDS && len < sizeof line; f++)
		{
			if (fields[who][f] > 0)
			{
				len += (size_t)snprintf(line + len, sizeof line - len, " %s %u",
					g_guessfieldname[f], fields[who][f]);
			}
		}

		CONS_Printf("%s\n", line);
	}
}

// Called as a speculation is about to be built, after the confirmed loop: the
// tics it confirmed since the last one, [from, to), are checked against the
// inputs the last speculation guessed for them. Had every one been right and
// no correction moved a kart, the last speculation's first tics would have
// been these tics exactly, and rebuilding them was work thrown away.
static void K_CheckGuesses(tic_t from, tic_t to)
{
	dboolean any = false, right = true;
	int32_t firstwrong = -1;
	tic_t t;
	int32_t i;

	for (t = from; t < to; t++)
	{
		int32_t k;

		if (t < g_guessfrom)
			continue;

		k = (int32_t)(t - g_guessfrom);

		if (k >= g_guesstics)
			break;

		any = true;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (g_guessin[k][i] == false || playeringame[i] == false)
				continue;

			if (K_SameInput(&g_guesscmds[k][i], &netcmds[t % BACKUPTICS][i]))
				continue;

			right = false;

			if (firstwrong < 0)
				firstwrong = k;

			{
				const int32_t who = (i == g_localplayers[0]) ? 0 : (players[i].bot ? 1 : 2);
				const ticcmd_t *g = &g_guesscmds[k][i], *r = &netcmds[t % BACKUPTICS][i];

				g_guesswrong[who]++;
				K_CountWrongFields(g_guessfield[who], g, r);
			}
		}
	}

	if (any == false)
		return;

	g_guesspasses++;

	if (right)
	{
		g_guessright++;

		if (g_driftmoved != g_guessmovedat)
			g_guessrightmoved++;
	}
	else
	{
		g_guessfirstwrong[(firstwrong < 4) ? firstwrong : 4]++;
	}
}

// The inputs one speculated tic is about to run, as G_Ticker will read them.
static void K_RecordGuess(tic_t tic)
{
	int32_t k, i;

	if (g_guesstics == 0)
		g_guessfrom = tic;

	k = (int32_t)(tic - g_guessfrom);

	if (k < 0 || k >= ROLLBACK_GUESSMAX)
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		g_guessin[k][i] = playeringame[i];

		if (playeringame[i])
			g_guesscmds[k][i] = netcmds[tic % BACKUPTICS][i];
	}

	g_guesstics = k + 1;
}

static void K_NoteSpeculatedTic(precise_t whole)
{
	int32_t i;

	if (gamestate != GS_LEVEL)
		return;

	g_spticus += K_PreciseToMicros(whole);
	g_spplayerus += K_PreciseToMicros(ps_playerthink_time);

	for (i = 0; i < NUM_ACTIVETHINKERLISTS; i++)
		g_splistus[i] += K_PreciseToMicros(ps_thlist_times[i]);

	g_spacsus += K_PreciseToMicros(ps_acs_time);
	g_spluathinkus += K_PreciseToMicros(ps_lua_thinkframe_time);
	g_spluahooks += (uint64_t)ps_lua_mobjhooks;
	g_spcheckpos += (uint64_t)ps_checkposition_calls;
	g_spticcount++;
}

// ---- rollback_objprofile ----
//
// A speculated tic on Opulence is 83% the objects' thinker list (8.66), and
// the map has 2199 rings, 401 gems and coins with a Lua hook, the karts and
// their effects. Which of them is the question; the game only times the list
// as a whole.
dboolean g_rollbackobjprofile;
static uint64_t g_objus[NUMMOBJTYPES];
static uint32_t g_objthinks[NUMMOBJTYPES];
static uint32_t g_objtics;

void K_RollbackNoteObjectThink(int32_t type, precise_t spent)
{
	if (type < 0 || type >= NUMMOBJTYPES || gamestate != GS_LEVEL)
		return;

	g_objus[type] += K_PreciseToMicros(spent);
	g_objthinks[type]++;
}

void K_RollbackNoteObjectTic(void)
{
	if (gamestate == GS_LEVEL)
		g_objtics++;
}

// ---- where in a level the passes fall ----
//
// Asked by Gibax on 2026-09-30: prediction runs through the whole level, the
// title card's fly-in and the stretch after the finish included, where this
// machine's input moves nothing -- could it stop there, and start again at
// POSITION? The rebuilds before the race (about 200 in 1599 passes in
// WORLDWIDE mode, WORLDWIDE.md 8.99) mix the join, the waiting map, and the
// race map's intro and POSITION, so first where they fall. Every pass is filed
// under the phase of the level at the frontier it starts from; within a level
// the phase only moves forward, so a pass that reads the head a few tics ahead
// cannot file it back and forth. A line is printed when the phase changes --
// so a PARANOIA line, or anything else in the log, can be placed -- and a
// level's table when the next level, or the same one restarted, begins.
enum
{
	KPHASE_JOIN,        // this machine's player not in the game yet, or spectating
	KPHASE_INTRO,       // the title card and the camera's fly-in (leveltime < introtime)
	KPHASE_POSITION,    // POSITION (leveltime < starttime), where karts drive
	KPHASE_RACE,
	KPHASE_FINISHED,    // this machine's player has crossed the line (exiting)
	NUMKPHASES
};
enum
{
	KPC_PASSES, KPC_KEPT,
	KPC_SELF, KPC_OTHERS, KPC_CORRECTION,   // rebuilt: this machine's input, another's, a correction
	NUMKPC
};
static const char *const g_phasename[NUMKPHASES] = {
	"join", "intro", "POSITION", "race", "finished"
};
static uint32_t g_phasecount[NUMKPHASES][NUMKPC];
static tic_t g_phasefrom[NUMKPHASES], g_phaseto[NUMKPHASES];   // the leveltimes seen in each
static int32_t g_phase = -1;        // the phase of the latest pass; -1 before any in this level
static int16_t g_phasemap;          // the level it is in
static tic_t g_phaselevel;          // the latest leveltime seen, to see a restart

static const char *K_PhaseMapName(int32_t map)
{
	const char *name = G_BuildMapName(map);

	return (name != NULL) ? name : "?";
}

static void K_PrintPhases(const char *when)
{
	int32_t ph;

	for (ph = 0; ph < NUMKPHASES; ph++)
	{
		const uint32_t *c = g_phasecount[ph];
		const uint32_t named = c[KPC_KEPT] + c[KPC_SELF] + c[KPC_OTHERS] + c[KPC_CORRECTION];

		if (c[KPC_PASSES] == 0)
			continue;

		CONS_Printf("rollback_phases: %s, %s, %s (leveltime %u to %u) -- %u passes, %u kept; "
			"rebuilt for this machine's input %u, another's %u, a correction %u, otherwise %u\n",
			K_PhaseMapName(g_phasemap), when, g_phasename[ph],
			(uint32_t)g_phasefrom[ph], (uint32_t)g_phaseto[ph], c[KPC_PASSES], c[KPC_KEPT],
			c[KPC_SELF], c[KPC_OTHERS], c[KPC_CORRECTION],
			(c[KPC_PASSES] > named) ? c[KPC_PASSES] - named : 0);
	}
}

static void K_ResetPhases(void)
{
	memset(g_phasecount, 0, sizeof g_phasecount);
	g_phase = -1;
}

/** The phase of the level at a frontier whose leveltime is lt, never behind
  * the phase of the passes before it in the same level. A new level -- or the
  * same one restarted, its clock back near the start -- prints the last one's
  * table and starts another. */
static int32_t K_PassPhase(tic_t lt)
{
	const int32_t who = g_localplayers[0];
	int32_t ph;

	if (g_phase >= 0 && (gamemap != g_phasemap || lt + 2*TICRATE < g_phaselevel))
	{
		K_PrintPhases("ended");
		K_ResetPhases();
	}

	if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false || players[who].spectator)
		ph = KPHASE_JOIN;
	else if (players[who].exiting)
		ph = KPHASE_FINISHED;
	else if (lt < introtime)
		ph = KPHASE_INTRO;
	else if (lt < starttime)
		ph = KPHASE_POSITION;
	else
		ph = KPHASE_RACE;

	if (ph < g_phase)
		ph = g_phase;

	if (ph != g_phase)
	{
		CONS_Printf("rollback_phases: %s -- %s from leveltime %u, tic %u\n",
			K_PhaseMapName(gamemap), g_phasename[ph], (uint32_t)lt, (uint32_t)gametic);
		g_phase = ph;
		g_phasemap = gamemap;
		g_phasefrom[ph] = lt;
	}

	g_phaseto[ph] = lt;
	g_phaselevel = lt;
	return ph;
}

/** Switches rollback_keepspec, and forgets every record and count it had.
  * Shared by the command and by WORLDWIDE mode. */
static void K_SetKeepSpec(dboolean on)
{
	g_keepspec = on;
	K_ResetPhases();
	memset(g_keepcount, 0, sizeof g_keepcount);
	memset(g_keeptic, 0xff, sizeof g_keeptic);
	g_keepcorrnoop = 0;
	memset(g_keepmissat, 0, sizeof g_keepmissat);
	memset(g_keepmisswho, 0, sizeof g_keepmisswho);
	memset(g_keepmissfield, 0, sizeof g_keepmissfield);
	memset(g_keepmisssrc, 0, sizeof g_keepmisssrc);
	memset(g_keepmissshift, 0, sizeof g_keepmissshift);
	g_keepmissrun = g_keepmissright = 0;
	g_keepearlyn = g_keepearlytics = 0;
}

static void Command_RollbackKeepSpec_f(void)
{
	static const char *const why[KEEP_NUMREASONS] = {
		"kept", "a correction was due", "the server confirmed past the head",
		"an input differed", "a netxcmd", "a tic the loop must run",
		"no record of a tic", "a gamestate was loaded", "the level was starting"
	};
	int32_t r;
	uint32_t armed = 0;

	if (COM_Argc() > 1)
		K_SetKeepSpec(atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_keepspec: %s%s\n", g_keepspec ? "on" : "off",
		(g_keepspec && (g_twoclock <= 0 || g_histmax <= 0 || g_cleancmds == false))
			? " -- but it keeps nothing without rollback_twoclock, and guesses this "
				"machine's own input wrong every pass without rollback_history and "
				"rollback_cleancmds"
			: "");

	for (r = 0; r < KEEP_NUMREASONS; r++)
		armed += g_keepcount[r];

	CONS_Printf("rollback_keepspec: %u passes left the speculation standing, %u kept it "
		"(%u of them through a correction that changed nothing)\n",
		armed, g_keepcount[KEEP_KEPT], g_keepcorrnoop);

	// A tic's chat line not written because the tic was a rerun's -- the join's
	// "entered the game" with rollback_join, if 8.128 reads it right.
	CONS_Printf("rollback_keepspec: %u chat lines a tic wrote were held back as a rerun's\n",
		g_chatheld);

	for (r = 1; r < KEEP_NUMREASONS; r++)
	{
		if (g_keepcount[r] > 0)
			CONS_Printf("rollback_keepspec: rebuilt %u times because %s\n", g_keepcount[r], why[r]);
	}

	CONS_Printf("rollback_keepspec: rollback_keepearly %s -- %u standing speculations run "
		"again from a tic this machine's newest input changed, %u tics in all\n",
		g_keepearly ? "on" : "off", g_keepearlyn, g_keepearlytics);

	K_PrintRefs();

	if (g_phase >= 0)
		K_PrintPhases("so far");

	if (g_keepcount[KEEP_INPUT] == 0)
		return;

	CONS_Printf("rollback_keepspec: the first wrong tic, from the frontier -- 0: %u, 1: %u, "
		"2: %u, 3: %u, 4 or more: %u; wrong inputs on it -- this machine %u, bots %u, people %u\n",
		g_keepmissat[0], g_keepmissat[1], g_keepmissat[2], g_keepmissat[3], g_keepmissat[4],
		g_keepmisswho[0], g_keepmisswho[1], g_keepmisswho[2]);
	CONS_Printf("rollback_keepspec: this machine's were run on -- a tic already received %u, "
		"an input replayed from the history %u, the newest input guessed past it %u\n",
		g_keepmisssrc[KEEPSRC_RECEIVED], g_keepmisssrc[KEEPSRC_REPLAYED],
		g_keepmisssrc[KEEPSRC_GUESSED]);
	CONS_Printf("rollback_keepspec: the sample the server applied there, against the one "
		"replayed -- newer by 2 or more %u, newer by 1 %u, the same to the anchor %u, "
		"older by 1 %u, older by 2 or more %u, not in the history %u\n",
		g_keepmissshift[KEEPSHIFT_NEWER2], g_keepmissshift[KEEPSHIFT_NEWER1],
		g_keepmissshift[KEEPSHIFT_SAME], g_keepmissshift[KEEPSHIFT_OLDER1],
		g_keepmissshift[KEEPSHIFT_OLDER2], g_keepmissshift[KEEPSHIFT_LOST]);
	K_PrintWrongFields("rollback_keepspec", g_keepmisswho, g_keepmissfield);
	CONS_Printf("rollback_keepspec: those rebuilds threw away %u tics already run; "
		"starting from the first wrong tic would have kept %u of them\n",
		g_keepmissrun, g_keepmissright);
}

/** Console command: rollback_keepearly [0|1]
  *
  * Client side, with rollback_keepspec and rollback_history. On: a
  * tic the standing speculation ran on this machine's input is run again from
  * its saved start as soon as the input R1 gives it changes (WORLDWIDE.md 8.98).
  * Off (the default since 8.99): only when the server confirms it. */
static void Command_RollbackKeepEarly_f(void)
{
	if (COM_Argc() > 1)
	{
		g_keepearly = (atoi(COM_Argv(1)) != 0);
		g_keepearlyn = g_keepearlytics = 0;
	}

	CONS_Printf("rollback_keepearly: %s -- %u standing speculations run again early, "
		"%u tics in all\n", g_keepearly ? "on" : "off", g_keepearlyn, g_keepearlytics);
}

static void Command_RollbackObjProfile_f(void)
{
	enum { SHOWN = 15 };
	dboolean taken[NUMMOBJTYPES];
	uint64_t total = 0, shown = 0;
	int32_t n, t;

	if (COM_Argc() > 1)
	{
		g_rollbackobjprofile = (atoi(COM_Argv(1)) != 0);
		memset(g_objus, 0, sizeof g_objus);
		memset(g_objthinks, 0, sizeof g_objthinks);
		g_objtics = 0;
	}

	CONS_Printf("rollback_objprofile: %s, over %u tics\n",
		g_rollbackobjprofile ? "on" : "off", g_objtics);

	if (g_objtics == 0)
		return;

	for (t = 0; t < NUMMOBJTYPES; t++)
		total += g_objus[t];

	CONS_Printf("rollback_objprofile: %u us a tic in the objects' list, timed "
		"object by object (the timing adds its own cost)\n",
		(uint32_t)(total / g_objtics));

	memset(taken, 0, sizeof taken);

	for (n = 0; n < SHOWN; n++)
	{
		int32_t best = -1;

		for (t = 0; t < NUMMOBJTYPES; t++)
		{
			if (taken[t] == false && g_objus[t] > 0 && (best < 0 || g_objus[t] > g_objus[best]))
				best = t;
		}

		if (best < 0)
			break;

		taken[best] = true;
		shown += g_objus[best];

		CONS_Printf("rollback_objprofile: %-26s %5u a tic, %5u us a tic, %u.%02u us each\n",
			K_MobjTypeName((mobjtype_t)best),
			g_objthinks[best] / g_objtics,
			(uint32_t)(g_objus[best] / g_objtics),
			(uint32_t)(g_objus[best] / (g_objthinks[best] ? g_objthinks[best] : 1)),
			(uint32_t)((g_objus[best] * 100 / (g_objthinks[best] ? g_objthinks[best] : 1)) % 100));
	}

	CONS_Printf("rollback_objprofile: every other type together, %u us a tic\n",
		(uint32_t)((total - shown) / g_objtics));
}

static void K_ResetPassCosts(void)
{
	g_saveus = 0;
	memset(g_stepus, 0, sizeof g_stepus);
	g_steppasses = g_confirmedrun = 0;

	memset(g_framework, 0, sizeof g_framework);
	memset(g_frameworksum, 0, sizeof g_frameworksum);
	memset(g_frameworkmax, 0, sizeof g_frameworkmax);
	memset(g_frameworkcount, 0, sizeof g_frameworkcount);
	memset(g_framegap, 0, sizeof g_framegap);
	g_framegapmax = g_framesdrawn = g_frameskips = 0;
	g_lastdrawnat = 0;
	memset(g_drawkart, 0, sizeof g_drawkart);
	memset(g_drawview, 0, sizeof g_drawview);
	g_drawlastok = false;
	memset(g_drawothers, 0, sizeof g_drawothers);
	memset(g_drawotherok, 0, sizeof g_drawotherok);

	g_guesspasses = g_guessright = g_guessrightmoved = 0;
	memset(g_guessfirstwrong, 0, sizeof g_guessfirstwrong);
	memset(g_guesswrong, 0, sizeof g_guesswrong);
	memset(g_guessfield, 0, sizeof g_guessfield);

	g_spticus = g_spplayerus = g_spacsus = g_spluathinkus = 0;
	g_spluahooks = g_spcheckpos = 0;
	memset(g_splistus, 0, sizeof g_splistus);
	g_spticcount = 0;

	P_ResetSaveProfile();
}

/** Copies the level pools `times` times into a scratch buffer and says what a
  * copy weighs and costs (WORLDWIDE.md 8.82) -- the raw snapshot's save half,
  * measured on the world as it stands. With `roundtrip`, also restores the
  * last copy onto the world it was taken from, which changes nothing, times
  * that, and copies again to check the two copies match byte for byte. Only in
  * a level. Returns false if nothing could be measured. */
static dboolean K_TimePoolCopy(uint32_t times, dboolean roundtrip,
	size_t *bytes, uint32_t *saveus, uint32_t *restoreus, dboolean *same)
{
	const size_t size = Z_LevelPoolSnapshotSize();
	uint8_t *copy, *again = NULL;
	precise_t at;
	uint64_t ticks = 0;
	uint32_t t;

	*bytes = size;
	*saveus = *restoreus = 0;
	*same = false;

	if (gamestate != GS_LEVEL || times == 0)
		return false;

	copy = (uint8_t *)Z_Malloc(size, PU_STATIC, NULL);
	if (roundtrip)
		again = (uint8_t *)Z_Malloc(size, PU_STATIC, NULL);

	for (t = 0; t < times; t++)
	{
		at = I_GetPreciseTime();
		Z_LevelPoolSnapshot(copy, size);
		ticks += I_GetPreciseTime() - at;
	}

	*saveus = (uint32_t)((ticks * 1000000) / I_GetPrecisePrecision() / times);

	if (roundtrip)
	{
		dboolean restored;

		at = I_GetPreciseTime();
		restored = Z_LevelPoolRestore(copy, size);
		*restoreus = K_PreciseToMicros(I_GetPreciseTime() - at);

		*same = (restored
			&& Z_LevelPoolSnapshotSize() == size
			&& Z_LevelPoolSnapshot(again, size) == size
			&& memcmp(copy, again, size) == 0);

		Z_Free(again);
	}

	Z_Free(copy);
	return true;
}

/** Console command: rollback_poolcopy [times]
  *
  * Diagnostic, in a level. Times a copy of the four level pools -- what a raw
  * snapshot (track B2) would save in place of the network archive's thinker
  * and sector-node sections -- then restores it onto the unchanged world and
  * checks the round trip is exact. Changes nothing: the world is put back as
  * it already was. */
static void Command_RollbackPoolCopy_f(void)
{
	const uint32_t times = (COM_Argc() > 1 && atoi(COM_Argv(1)) > 0) ? (uint32_t)atoi(COM_Argv(1)) : 10;
	size_t bytes;
	uint32_t saveus, restoreus;
	dboolean same;

	if (K_TimePoolCopy(times, true, &bytes, &saveus, &restoreus, &same) == false)
	{
		CONS_Printf("rollback_poolcopy: only in a level\n");
		return;
	}

	CONS_Printf("rollback_poolcopy: the level pools are %s KB; a copy takes %u us "
		"(mean of %u), putting it back %u us; the round trip is %s\n",
		sizeu1(bytes / 1024), saveus, times, restoreus,
		(same ? "exact" : "NOT EXACT -- the pool snapshot is broken"));
}

/** A local save, step by step (WORLDWIDE.md 8.81): the mean time and size of
  * each step over every local save since the counts were reset, the rollback
  * tests' own included. Printed four steps a line; a step under 5 us and 1 KB
  * on average is left out and counted in the last line. */
static void K_ReportSaveProfile(void)
{
	const savestep_t *steps;
	uint32_t saves;
	const size_t n = P_GetSaveProfile(&steps, &saves);
	uint64_t totalus = 0, totalbytes = 0, smallus = 0;
	char line[512];
	size_t i;
	int32_t onthisline = 0;

	if (saves == 0 || n == 0)
		return;

	for (i = 0; i < n; i++)
	{
		totalus += steps[i].us;
		totalbytes += steps[i].bytes;
	}

	CONS_Printf("rollback_save: per local save, over %u -- %u us, %u KB\n", saves,
		(uint32_t)(totalus / saves), (uint32_t)(totalbytes / saves / 1024));

	line[0] = 0;

	for (i = 0; i < n; i++)
	{
		const uint32_t us = (uint32_t)(steps[i].us / saves);
		const uint32_t bytes = (uint32_t)(steps[i].bytes / saves);
		char one[96];

		if (us < 5 && bytes < 1024)
		{
			smallus += steps[i].us;
			continue;
		}

		snprintf(one, sizeof one, "%s%s %u us %u KB", (onthisline ? ", " : ""),
			(steps[i].name ? steps[i].name : "?"), us, bytes / 1024);
		strlcat(line, one, sizeof line);

		if (++onthisline == 4)
		{
			CONS_Printf("rollback_save: %s\n", line);
			line[0] = 0;
			onthisline = 0;
		}
	}

	if (onthisline > 0)
		CONS_Printf("rollback_save: %s\n", line);

	CONS_Printf("rollback_save: the smaller steps together, %u us\n",
		(uint32_t)(smallus / saves));

	// What a raw snapshot of the thinkers would copy instead (track B2): every
	// thinker and sector node lives in one of the four level pools, and a pool
	// grows by whole chunks. As the pools stand at this report.
	{
		levelpoolinfo_t pools[Z_LEVELPOOLS];
		size_t p;

		Z_LevelPoolInfo(pools);

		for (p = 0; p < Z_LEVELPOOLS; p++)
		{
			CONS_Printf("rollback_save: level pool of %u-byte blocks -- %u in use (%u KB), "
				"%u chunks of %u blocks (%u KB)\n",
				(uint32_t)pools[p].blocksize, (uint32_t)pools[p].allocated,
				(uint32_t)((pools[p].allocated * pools[p].blocksize) / 1024),
				(uint32_t)pools[p].chunks, (uint32_t)pools[p].blocksperchunk,
				(uint32_t)((pools[p].chunks * pools[p].blocksperchunk * pools[p].blocksize) / 1024));
		}
	}

	// And what copying them costs here, now: the raw snapshot's save half
	// (WORLDWIDE.md 8.82), set against the save above. A copy only.
	{
		size_t bytes;
		uint32_t saveus, restoreus;
		dboolean same;

		if (K_TimePoolCopy(5, false, &bytes, &saveus, &restoreus, &same))
		{
			CONS_Printf("rollback_save: a raw copy of the level pools -- %s KB, %u us\n",
				sizeu1(bytes / 1024), saveus);
		}
	}

	if (g_rawsnap > 0 || g_rawsaves > 0)
		K_ReportRawSnap();
}

static void K_PrintBuckets(const char *what, const uint32_t *buckets)
{
	CONS_Printf("rollback_frames: %s -- under 8.3 ms %u, 8.3-16.7 %u, 16.7-28.6 %u, "
		"28.6-33.3 %u, 33.3-50 %u, over 50 %u\n", what,
		buckets[0], buckets[1], buckets[2], buckets[3], buckets[4], buckets[5]);
}

static void K_ReportPassCosts(void)
{
	const uint32_t passes = g_steppasses ? g_steppasses : 1;
	const uint32_t spec = g_specpasses ? g_specpasses : 1;
	const uint32_t restore = g_unspecus / spec, save = g_saveus / spec, forward = g_specus / spec;
	const uint32_t net = (uint32_t)(g_stepus[ROLLBACK_STEP_NET] / passes);
	const uint32_t corr = (uint32_t)(g_stepus[ROLLBACK_STEP_CORRECTION] / passes);
	const uint32_t conf = (uint32_t)(g_stepus[ROLLBACK_STEP_CONFIRMED] / passes);
	int32_t w;

	CONS_Printf("rollback_cost: per pass, over %u passes -- restore %u us, network %u, "
		"correction %u, confirmed tics %u (%u.%02u tics a pass), save %u, speculation %u "
		"(%u.%02u tics a pass): %u us against 28571 for a whole tic\n",
		g_steppasses, restore, net, corr, conf,
		g_confirmedrun / passes, (g_confirmedrun * 100 / passes) % 100,
		save, forward, g_spectics / spec, (g_spectics * 100 / spec) % 100,
		restore + net + corr + conf + save + forward);

	for (w = 1; w >= 0; w--)
	{
		const uint32_t n = g_frameworkcount[w] ? g_frameworkcount[w] : 1;

		CONS_Printf("rollback_frames: %u loop iterations %s the tic loop -- work before "
			"the sleep %u us on average, %u at worst\n",
			g_frameworkcount[w], w ? "that ran" : "that did not run",
			(uint32_t)(g_frameworksum[w] / n), g_frameworkmax[w]);
		K_PrintBuckets(w ? "their work, with a pass" : "their work, without one",
			g_framework[w]);
	}

	K_PrintBuckets("between two drawn frames", g_framegap);
	CONS_Printf("rollback_frames: %u frames drawn, the longest gap %u us; %u iterations "
		"ran past a tic, so the frame after each was skipped\n",
		g_framesdrawn, g_framegapmax, g_frameskips);
	CONS_Printf("rollback_frames: the local kart as drawn, frame to frame while it moves -- "
		"without a pass: even %u, short %u, long %u, backwards %u; with one: even %u, "
		"short %u, long %u, backwards %u\n",
		g_drawkart[0][DRAWSTEP_EVEN], g_drawkart[0][DRAWSTEP_SHORT],
		g_drawkart[0][DRAWSTEP_LONG], g_drawkart[0][DRAWSTEP_BACK],
		g_drawkart[1][DRAWSTEP_EVEN], g_drawkart[1][DRAWSTEP_SHORT],
		g_drawkart[1][DRAWSTEP_LONG], g_drawkart[1][DRAWSTEP_BACK]);
	CONS_Printf("rollback_frames: the view, likewise -- without a pass: even %u, short %u, "
		"long %u, backwards %u; with one: even %u, short %u, long %u, backwards %u\n",
		g_drawview[0][DRAWSTEP_EVEN], g_drawview[0][DRAWSTEP_SHORT],
		g_drawview[0][DRAWSTEP_LONG], g_drawview[0][DRAWSTEP_BACK],
		g_drawview[1][DRAWSTEP_EVEN], g_drawview[1][DRAWSTEP_SHORT],
		g_drawview[1][DRAWSTEP_LONG], g_drawview[1][DRAWSTEP_BACK]);
	{
		int32_t k;

		for (k = 0; k < 2; k++)
		{
			uint32_t (*c)[NUMDRAWSTEPS] = g_drawothers[k];

			CONS_Printf("rollback_frames: the other karts as drawn, %s, frame to frame "
				"while they move -- without a pass: even %u, short %u, long %u, backwards %u; "
				"with one: even %u, short %u, long %u, backwards %u\n",
				(k ? "people" : "bots"),
				c[0][DRAWSTEP_EVEN], c[0][DRAWSTEP_SHORT], c[0][DRAWSTEP_LONG], c[0][DRAWSTEP_BACK],
				c[1][DRAWSTEP_EVEN], c[1][DRAWSTEP_SHORT], c[1][DRAWSTEP_LONG], c[1][DRAWSTEP_BACK]);
		}
	}

	CONS_Printf("rollback_hits: %u passes confirmed tics the speculation had guessed: "
		"%u with every input right (%u of them with a kart moved by a correction), "
		"%u with one wrong\n",
		g_guesspasses, g_guessright, g_guessrightmoved, g_guesspasses - g_guessright);
	CONS_Printf("rollback_hits: the first wrong tic, from the frontier -- 0: %u, 1: %u, "
		"2: %u, 3: %u, 4 or more: %u; wrong inputs -- this machine %u, bots %u, "
		"people %u\n",
		g_guessfirstwrong[0], g_guessfirstwrong[1], g_guessfirstwrong[2],
		g_guessfirstwrong[3], g_guessfirstwrong[4],
		g_guesswrong[0], g_guesswrong[1], g_guesswrong[2]);

	K_PrintWrongFields("rollback_hits", g_guesswrong, g_guessfield);

	if (g_spticcount > 0)
	{
		const uint64_t n = g_spticcount;
		const uint64_t lists = g_splistus[THINK_DYNSLOPE] + g_splistus[THINK_POLYOBJ]
			+ g_splistus[THINK_MAIN] + g_splistus[THINK_MOBJ] + g_splistus[THINK_DYNSLOPEDEMO];
		const uint64_t named = g_spplayerus + lists + g_spacsus + g_spluathinkus;

		CONS_Printf("rollback_tic: per speculated tic, over %u -- the whole tic %u us: "
			"player thinks %u, thinker lists %u (slopes %u, polyobjects %u, main %u, "
			"objects %u), ACS %u, Lua ThinkFrame %u, the rest %u\n",
			g_spticcount, (uint32_t)(g_spticus / n), (uint32_t)(g_spplayerus / n),
			(uint32_t)(lists / n), (uint32_t)(g_splistus[THINK_DYNSLOPE] / n),
			(uint32_t)(g_splistus[THINK_POLYOBJ] / n), (uint32_t)(g_splistus[THINK_MAIN] / n),
			(uint32_t)(g_splistus[THINK_MOBJ] / n), (uint32_t)(g_spacsus / n),
			(uint32_t)(g_spluathinkus / n),
			(uint32_t)((g_spticus > named ? g_spticus - named : 0) / n));
		CONS_Printf("rollback_tic: %u Lua mobj hook calls and %u P_CheckPosition calls "
			"a speculated tic\n",
			(uint32_t)(g_spluahooks / n), (uint32_t)(g_spcheckpos / n));
	}

	K_ReportSaveProfile();
}

// Spikes.
//
// Five unattended races put the mean residual at 0.05 to 0.33 units and the
// *worst* single sample at 4, 36, 38, 47 and 50 -- on five different karts. A
// steady numeric creep does not vary six-fold between identical races and does
// not land on roughly one kart width. That pattern says discrete events, not
// floating point: something resolved differently on the two machines, once.
//
// So each sample past a tenth of a kart says so, with the momentum on both
// sides beside it. That comparison is the discriminator the whole hunt needs:
// **momentum agreeing while position differs is accumulated drift, momentum
// differing is an event resolved differently** -- a collision, a bump, a
// hazard. Two different bugs, and one line of text separates them.
#define ROLLBACK_SPIKE (4 * FRACUNIT)
#define ROLLBACK_SPIKEMAX 40
static uint32_t g_driftspikes;

// And the state beside the kinematics, below a spike. The history race's two
// bots were 15 and 22 units out at the first correction that printed anything,
// one of them flashing on this machine only -- and nothing said since when
// (WORLDWIDE.md 8.46). A state field that differs while the position still
// agrees is where such a divergence starts, so it is counted on every sample
// and printed, capped. A world that agrees prints nothing.
#define ROLLBACK_STATEMAX 40
static uint32_t g_driftstates;       // kart samples with a state field differing
static uint32_t g_driftstatefirst;   // the correction tic of the first one

// Damage outcomes resolved on confirmed tics, and a hash of which ones.
//
// The collision tally this replaces counted twenty-seven million proximity
// tests per race and could not answer anything: PIT_CheckThing fires on every
// pair of objects that come near each other, so the count depends on who is
// near whom, which depends on the divergence it was meant to date. Its offset
// between the two machines moved by 837k and then by -757k, and that was the
// instrument, not the game.
//
// Damage events are a few dozen per race, and each one is a decision both
// machines must reach the same way. Forty spikes named flashing (13),
// tumbleBounces (7) and hitlag (9) -- every one of them written by the damage
// path, every one of them showing the *server* holding a hit the client never
// took.
//
// Running, and rebased once: the client joined after the map started, so it
// missed events the server counted. The first correction that lands on its own
// tic hands over the server's count and hash, and from that boundary the two
// machines fold the same events in the same order -- so the pair is an equality
// test, not an offset to interpret. rollback_damagelog, run on both machines,
// then dates any parting to a tic and an object.
static uint32_t g_livedamages;
static uint32_t g_livedamagehash;
static uint32_t g_srvdamages;
static uint32_t g_srvdamagehash;

// One line per damage event, capped: a race that spends its whole log on this
// has no room left for the spikes.
#define ROLLBACK_DAMAGELOGMAX 400
static dboolean g_damagelog;
static uint32_t g_damagelogged;
static dboolean g_damagebase;

// Every ticcmd every player actually ran a confirmed tic on, folded once
// per player per tic -- the general case K_RollbackNoteArrival does not
// cover, since that one only compares a LATE RESEND of an already-run tic
// against what ran, and a resend of a tic already consumed is rare on a
// clean local link (it has read zero every race on this branch so far).
// This folds the FIRST delivery, the one that actually mattered.
static uint32_t g_liveinputs;
static uint32_t g_liveinputhash;
static uint32_t g_srvinputs;
static uint32_t g_srvinputhash;

// Far higher than the damage log's cap: this folds every in-game player every
// confirmed tic, not a handful of hits a race, and the first race that turned
// it on found the hash already parted within the first ~700 tics past the
// baseline while the damage log was still comfortably inside ITS cap. 20000
// lines covers roughly 2500 tics of an eight-player race -- past where the
// first split has shown up so far, with room to spare.
#define ROLLBACK_INPUTLOGMAX 20000
static dboolean g_inputlog;
static uint32_t g_inputlogged;
static dboolean g_inputbase;


/** True while a correction is re-running tics that have already been played.
  *
  * Anything that reaches outside the simulation -- sound, most obviously --
  * should sit the replay out. The tic happened once already.
  */
dboolean K_RollbackReplaying(void)
{
	// A speculation counts. It is re-run from scratch every pass, so a sound
	// started in one would be started again, and again, for as long as the tic
	// stays unconfirmed. The sound arrives when the authoritative loop reaches
	// that tic for real -- later by the lead, and once.
	return (g_replaying || g_speculating);
}

dboolean K_RollbackOffTimeline(void)
{
	if (g_speculating)
		g_keeptaint[gametic % ROLLBACK_TICS] = true;

	return (g_replaying || g_speculating);
}

static dboolean g_intic;    // G_Ticker is running a tic

void K_RollbackTicRunning(dboolean running)
{
	g_intic = running;
}

dboolean K_RollbackSoundsSilenced(void)
{
	if (g_replaying)
		return true;

	// With the speculation kept, most tics are never run again for real, so
	// waiting for the confirmed run would mean never hearing them. A tic sounds
	// the first time this machine runs it, and not again on a rebuild. Only a
	// tic: a menu's sound comes between tics, while a kept pass has handed the
	// netcode the frontier's clock, and gametic is behind the horizon then --
	// the menus and the title card went silent (WORLDWIDE.md 8.108).
	if (g_keepspec && g_twoclock > 0 && gamestate == GS_LEVEL)
		return (g_intic && gametic < g_soundhorizon);

	return g_speculating;
}

static uint32_t g_chatheld;     // chat lines held back as a rerun's (declared above)

dboolean K_RollbackChatSilenced(void)
{
	const dboolean silenced = K_RollbackSoundsSilenced();

	if (silenced)
	{
		g_chatheld++;
		return true;
	}

	// A line a tic writes on a tic this machine has not run: the first run of
	// that tic, in this world. One join still wrote three (WORLDWIDE.md 8.109),
	// so each of them landed on a tic past the horizon -- say which.
	if (g_intic && g_twoclock > 0)
	{
		CONS_Printf("rollback_chat: a tic's line written on tic %u (leveltime %u, %s, gamestate %d) -- "
			"the horizon at %u, the frontier at %u, the standing speculation's head at %u; "
			"%u held back so far\n",
			(unsigned)gametic, (unsigned)leveltime,
			g_speculating ? "speculated" : "confirmed", (int)gamestate,
			(unsigned)g_soundhorizon, (unsigned)g_confirmedtic, (unsigned)g_keephead,
			(unsigned)g_chatheld);
	}

	return false;
}

// ----------------------------------------------------------------------------
// The karts' bodies' reference counts (WORLDWIDE.md 8.111)
// ----------------------------------------------------------------------------

// A body whose count goes below zero was let go of once more than it was held:
// a holder that took it without counting, or a stale holder letting go of
// whatever lives where the body it held used to. On the client, 1 to 18 bodies
// a race between the join and the race map, never explained (8.97, 8.110).
//
// Two things are followed. Every change of a body's count, site by site, from
// the first time this machine sees the body: P_SetTarget passes its caller's
// file and line in a PARANOIA build -- the build that prints the alert. And
// the three collision pointers, which hold counted references: P_MapEnd lets
// go of g_tm.thing after every tic, but g_tm.floorthing and g_tm.hitthing
// keep theirs, and a restore frees every object and brings them back, maybe
// at the same addresses. A pointer left holding a freed object then lets go,
// at its next change, of whatever lives at that address.

#define REFS_BODIES 48      // bodies followed at once
#define REFS_SITES 20       // sites a body's ledger keeps apart
#define REFS_PRINTS 12      // bodies below zero, and stale pointers, printed whole

enum
{
	REFCTX_CONFIRMED,
	REFCTX_SPECULATED,
	REFCTX_REPLAY,
	REFCTX_LOAD,
	REFCTX_BETWEEN,
};

static const char *const g_refctxname[] = {
	"a confirmed tic", "a speculated tic", "a replay", "a load", "between tics"
};

typedef struct
{
	const char *file;
	int32_t line;
	uint32_t up, down;
} refsite_t;

typedef struct
{
	mobj_t *mo;                 // NULL: a free slot
	tic_t seen;                 // the gametic it was first seen on
	uint8_t seenctx;
	tic_t removed;              // and removed on, when removedctx is not -1
	int8_t removedctx;
	int32_t nsites;
	uint32_t otherup, otherdown; // changes from sites past the table
	refsite_t site[REFS_SITES];
} refbody_t;

static refbody_t g_refbody[REFS_BODIES];
static dboolean g_inload;
static uint32_t g_refloads;         // loads of the archive so far
static uint32_t g_refnegative;      // counts gone below zero
static uint32_t g_refprinted;
static uint32_t g_refunfollowed;    // bodies seen with the table full

static const char *const g_tmname[3] = { "thing", "floorthing", "hitthing" };
static mobj_t *g_tmheld[3];         // what each held when the load began
static mobjtype_t g_tmheldtype[3];
static int32_t g_tmheldplayer[3];
static uint32_t g_tmheldn[3];       // loads it held an object at
static uint32_t g_tmreused[3];      // and a live object sat at that address after
static uint32_t g_tmreusedbody[3];  // a kart's body
static uint32_t g_tmprinted;

static uint8_t K_RefContext(void)
{
	if (g_inload)
		return REFCTX_LOAD;
	if (g_replaying)
		return REFCTX_REPLAY;
	if (g_speculating)
		return REFCTX_SPECULATED;
	if (g_intic)
		return REFCTX_CONFIRMED;
	return REFCTX_BETWEEN;
}

static const char *K_RefBaseName(const char *file)
{
	const char *slash = strrchr(file, '/');
	const char *back = strrchr(file, '\\');

	if (back != NULL && (slash == NULL || back > slash))
		slash = back;

	return (slash != NULL) ? slash + 1 : file;
}

static int32_t K_RefPlayer(const mobj_t *mo)
{
	return (mo->player != NULL) ? (int32_t)(mo->player - players) : -1;
}

static refbody_t *K_RefBody(mobj_t *mo, dboolean create)
{
	refbody_t *empty = NULL;
	int32_t i;

	for (i = 0; i < REFS_BODIES; i++)
	{
		if (g_refbody[i].mo == mo)
			return &g_refbody[i];
		if (empty == NULL && g_refbody[i].mo == NULL)
			empty = &g_refbody[i];
	}

	if (create == false)
		return NULL;

	if (empty == NULL)
	{
		g_refunfollowed++;
		return NULL;
	}

	memset(empty, 0, sizeof *empty);
	empty->mo = mo;
	empty->seen = gametic;
	empty->seenctx = K_RefContext();
	empty->removedctx = -1;
	return empty;
}

void K_RollbackRefTrace(mobj_t *mo, int32_t delta, const char *file, int32_t line)
{
	refbody_t *b = K_RefBody(mo, true);
	int32_t i;

	if (b == NULL)
		return;

	for (i = 0; i < b->nsites; i++)
	{
		if (b->site[i].line == line
			&& (b->site[i].file == file || strcmp(b->site[i].file, file) == 0))
			break;
	}

	if (i == b->nsites)
	{
		if (b->nsites == REFS_SITES)
		{
			if (delta > 0)
				b->otherup++;
			else
				b->otherdown++;
			return;
		}

		b->site[i].file = file;
		b->site[i].line = line;
		b->nsites++;
	}

	if (delta > 0)
		b->site[i].up++;
	else
		b->site[i].down++;
}

void K_RollbackRefRemoved(mobj_t *mo)
{
	refbody_t *b = K_RefBody(mo, false);

	if (b == NULL)
		return;

	b->removed = gametic;
	b->removedctx = (int8_t)K_RefContext();
}

void K_RollbackRefFreed(thinker_t *th)
{
	refbody_t *b = K_RefBody((mobj_t *)th, false);

	if (b != NULL)
		b->mo = NULL;
}

static void K_RefAppend(char *buf, size_t size, size_t *len, const char *text)
{
	const size_t n = strlen(text);

	if (*len + n + 1 >= size)
		return;

	memcpy(buf + *len, text, n + 1);
	*len += n;
}

void K_RollbackRefNegative(mobj_t *mo, const char *file, int32_t line)
{
	const refbody_t *b = K_RefBody(mo, false);
	char buf[1536], part[160];
	size_t len = 0;
	thinker_t *th;
	int32_t i, holders = 0;

	g_refnegative++;

	if (g_refprinted >= REFS_PRINTS)
		return;

	g_refprinted++;

	if (b == NULL)
		snprintf(part, sizeof part, "not followed");
	else if (b->removedctx < 0)
		snprintf(part, sizeof part, "first seen on tic %u, in %s; not removed",
			(unsigned)b->seen, g_refctxname[b->seenctx]);
	else
		snprintf(part, sizeof part, "first seen on tic %u, in %s; removed on tic %u, in %s",
			(unsigned)b->seen, g_refctxname[b->seenctx],
			(unsigned)b->removed, g_refctxname[b->removedctx]);

	CONS_Printf("rollback_refs: a kart's body went below zero -- %p (player %d), "
		"references %d, let go of at %s:%d on tic %u, in %s; %s\n",
		(void *)mo, K_RefPlayer(mo), mo->thinker.references,
		K_RefBaseName(file), line, (unsigned)gametic, g_refctxname[K_RefContext()], part);

	// Its ledger: which sites took a reference and which let one go.
	buf[0] = '\0';
	if (b != NULL)
	{
		for (i = 0; i < b->nsites; i++)
		{
			snprintf(part, sizeof part, "%s%s:%d +%u -%u", (i > 0) ? ", " : "",
				K_RefBaseName(b->site[i].file), b->site[i].line,
				b->site[i].up, b->site[i].down);
			K_RefAppend(buf, sizeof buf, &len, part);
		}
		if (b->otherup || b->otherdown)
		{
			snprintf(part, sizeof part, ", elsewhere +%u -%u", b->otherup, b->otherdown);
			K_RefAppend(buf, sizeof buf, &len, part);
		}
	}
	CONS_Printf("rollback_refs:   its count by site since -- %s\n", (len > 0) ? buf : "(nothing)");

	// And what still points at it: the one letting go of it now among them,
	// since P_SetTarget changes the pointer only after the count.
	len = 0;
	buf[0] = '\0';
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		const mobj_t *m = (const mobj_t *)th;
		const struct { mobj_t *p; const char *name; } fields[] = {
			{ m->target, "target" }, { m->tracer, "tracer" }, { m->hnext, "hnext" },
			{ m->hprev, "hprev" }, { m->itnext, "itnext" }, { m->terrainOverlay, "terrainOverlay" },
			{ m->punt_ref, "punt_ref" }, { m->owner, "owner" },
		};
		size_t f;

		if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		for (f = 0; f < sizeof fields / sizeof fields[0]; f++)
		{
			if (fields[f].p != mo)
				continue;
			snprintf(part, sizeof part, "%s%s.%s", (holders > 0) ? ", " : "",
				K_MobjTypeName(m->type), fields[f].name);
			K_RefAppend(buf, sizeof buf, &len, part);
			holders++;
		}
	}
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].mo == mo)
		{
			snprintf(part, sizeof part, "%splayers[%d].mo", (holders > 0) ? ", " : "", i);
			K_RefAppend(buf, sizeof buf, &len, part);
			holders++;
		}
	}
	{
		mobj_t *const tm[3] = { g_tm.thing, g_tm.floorthing, g_tm.hitthing };
		for (i = 0; i < 3; i++)
		{
			if (tm[i] != mo)
				continue;
			snprintf(part, sizeof part, "%sg_tm.%s", (holders > 0) ? ", " : "", g_tmname[i]);
			K_RefAppend(buf, sizeof buf, &len, part);
			holders++;
		}
	}
	CONS_Printf("rollback_refs:   still pointing at it (%d) -- %s\n", holders, (len > 0) ? buf : "nothing");
}

/** Every object is freed and brought back: what the collision pointers hold
  * now, and none of the bodies followed survives as itself. */
void K_RollbackRefLoadBegin(void)
{
	mobj_t *const held[3] = { g_tm.thing, g_tm.floorthing, g_tm.hitthing };
	int32_t i;

	for (i = 0; i < 3; i++)
	{
		g_tmheld[i] = held[i];
		g_tmheldtype[i] = (held[i] != NULL) ? held[i]->type : MT_NULL;
		g_tmheldplayer[i] = (held[i] != NULL) ? K_RefPlayer(held[i]) : -1;
	}

	for (i = 0; i < REFS_BODIES; i++)
		g_refbody[i].mo = NULL;

	g_inload = true;
}

static mobj_t *K_RefLiveAt(mobj_t *mo)
{
	thinker_t *th;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if ((mobj_t *)th != mo)
			continue;
		return (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed) ? NULL : mo;
	}

	return NULL;
}

void K_RollbackRefLoadEnd(dboolean loaded)
{
	mobj_t *const now[3] = { g_tm.thing, g_tm.floorthing, g_tm.hitthing };
	char was[64], is[64];
	int32_t i;

	g_inload = false;

	if (loaded == false)
		return;

	g_refloads++;

	for (i = 0; i < 3; i++)
	{
		mobj_t *live;

		if (g_tmheld[i] == NULL || now[i] != g_tmheld[i])
			continue;

		g_tmheldn[i]++;

		live = K_RefLiveAt(now[i]);
		if (live == NULL)
			continue;

		g_tmreused[i]++;
		if (live->type == MT_PLAYER)
			g_tmreusedbody[i]++;

		if (g_tmprinted >= REFS_PRINTS)
			continue;

		g_tmprinted++;
		snprintf(was, sizeof was, "%s (player %d)", K_MobjTypeName(g_tmheldtype[i]), (int)g_tmheldplayer[i]);
		snprintf(is, sizeof is, "%s (player %d)", K_MobjTypeName(live->type), (int)K_RefPlayer(live));
		CONS_Printf("rollback_refs: the load of tic %u left g_tm.%s holding a %s it freed; "
			"a %s lives at that address now, with %d references -- the next change of "
			"g_tm.%s takes one of them\n",
			(unsigned)gametic, g_tmname[i], was, is, live->thinker.references, g_tmname[i]);
	}
}

static void K_PrintRefs(void)
{
	CONS_Printf("rollback_refs: %u loads of the archive; the collision pointers held an "
		"object at %u of them (thing %u, floorthing %u, hitthing %u), and a live object sat "
		"at its address after the load at %u (a kart's body at %u); %u karts' bodies' counts "
		"went below zero%s\n",
		g_refloads, g_tmheldn[0] + g_tmheldn[1] + g_tmheldn[2],
		g_tmheldn[0], g_tmheldn[1], g_tmheldn[2],
		g_tmreused[0] + g_tmreused[1] + g_tmreused[2],
		g_tmreusedbody[0] + g_tmreusedbody[1] + g_tmreusedbody[2],
		g_refnegative,
#ifdef PARANOIA
		(g_refunfollowed > 0) ? " (some bodies not followed, the table full)" : ""
#else
		" -- counted only in a PARANOIA build"
#endif
		);
}

/** True when two inputs say the player pressed different things.
  *
  * Not a memcmp. A ticcmd also carries latency and flags, and neither is
  * something anybody pressed: latency is a transport measurement that G_Ticker
  * rewrites on arrival, and the flags say how the input travelled, not what it
  * was. A rollback exists to correct what was *pressed*, so that is what this
  * compares -- otherwise every single arrival looks like a contradiction and the
  * detector cries wolf on every tic.
  */
static dboolean K_InputsDiffer(const ticcmd_t *a, const ticcmd_t *b)
{
	return (a->forwardmove != b->forwardmove
		|| a->turning != b->turning
		|| a->angle != b->angle
		|| a->throwdir != b->throwdir
		|| a->aiming != b->aiming
		|| a->buttons != b->buttons
		|| a->bot.turnconfirm != b->bot.turnconfirm
		|| a->bot.spindashconfirm != b->bot.spindashconfirm
		|| a->bot.itemconfirm != b->bot.itemconfirm);
}


#define ROLLBACK_FEEDKEPT 6

/** What the inputs looked like on the way through a replay.
  *
  * netcmds is a mailbox, not a record: D_Clearticcmd zeroes the flags of every
  * acknowledged tic. So a replay reading it gets the right buttons with the
  * wrong flags, and p_user reads one of those flags to decide whether an input
  * was dropped in transit. This counts the drift rather than assuming it away.
  */
typedef struct
{
	uint32_t checked;    // tics the ring still held a record for
	uint32_t unknown;    // tics it did not
	uint32_t disagreed;  // player-tics where netcmds differed from the record
	uint32_t us;         // how long the replay took
	tic_t loaded;        // leveltime immediately after the restore
	struct { tic_t tic; int32_t who; ticcmd_t used, fed; } wrong[ROLLBACK_FEEDKEPT];
} rollbackfeed_t;

/** The correcting half of a rollback: restore a tic, replay forward to now.
  *
  * Pulled out of rollback_replay so the console command and the tic loop run the
  * same code rather than two readings of the same idea. Everything this project
  * has learned about replaying a tic by hand lives in here: gametic is kept in
  * step because the tic loop is what normally advances it, the inputs go in
  * through G_MoveTiccmdsIntoPlayers rather than by raw copy, and they come from
  * the keeper's record rather than from netcmds, which loses its flags the
  * moment a tic is acknowledged.
  *
  * \param feed optional; filled with what the inputs looked like on the way past.
  * \return tics replayed, or -1 when there is no snapshot for that tic.
  */
static int32_t K_RollbackTo(tic_t from, tic_t now, rollbackfeed_t *feed)
{
	rollbackfeed_t discard;
	precise_t started;
	int32_t ran = 0;
	int32_t i;
	tic_t t;

	if (feed == NULL)
		feed = &discard;

	memset(feed, 0, sizeof (*feed));

	// A replayed tic makes the same sounds the tic made the first time, and a
	// correction every few tics turns that into every sound firing over and over.
	// Gibax heard it before any counter reported it: "le son est complètement
	// buggé". Silencing the replay is the standard treatment -- the tics already
	// happened once, audibly, and the point of re-running them is the world, not
	// the noise.
	g_replaying = true;

	if (!K_LoadGameState(from))
	{
		g_replaying = false;
		return -1;
	}

	feed->loaded = leveltime;

	// The inputs of each tic as the netcode recorded them, rather than one tic's
	// inputs repeated. netcmds holds BACKUPTICS of them, far more than the ring.
	started = I_GetPreciseTime();

	for (t = from + 1; t <= now; t++)
	{
		const ticcmd_t *used;

		// Which tic this is, first, because the inputs are indexed by it and
		// because the restore rewound gametic along with everything else. The
		// archive carries it, and nothing inside a tic advances it: TryRunTics
		// does, and this replays without going through TryRunTics.
		gametic = t;

		used = K_InputsAsUsed(t);

		if (used == NULL)
		{
			feed->unknown++;
		}
		else
		{
			feed->checked++;

			// What netcmds would have handed this tic, against what the tic
			// really ran on. Compared before the truth is written in, because
			// afterwards there is nothing left to compare.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i] == false)
					continue;

				if (K_InputsDiffer(&netcmds[t % BACKUPTICS][i], &used[i]) == false)
					continue;

				if (feed->disagreed < ROLLBACK_FEEDKEPT)
				{
					feed->wrong[feed->disagreed].tic = t;
					feed->wrong[feed->disagreed].who = i;
					feed->wrong[feed->disagreed].used = used[i];
					feed->wrong[feed->disagreed].fed = netcmds[t % BACKUPTICS][i];
				}

				feed->disagreed++;
			}

			// And put the truth where the tic will look for it. netcmds is a
			// mailbox rather than a record -- D_Clearticcmd empties the flags of
			// every acknowledged tic -- so a replay that trusted it fed the
			// simulation an input that looked dropped in transit, and p_user
			// steers differently when it thinks that. This is also the shape the
			// real correction takes: write the input that actually arrived, then
			// run the tic normally.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i])
					netcmds[t % BACKUPTICS][i] = used[i];
			}
		}

		// The whole tic, not just the simulation. G_Ticker moves the inputs into
		// the players, calls P_Ticker itself, and does a list of other things a
		// tic does -- K_CheckSpectateStatus among them, which is what advances
		// spectatorReentry. Driving P_Ticker alone meant every one of those was
		// missed, and they were being found one at a time: gametic, then the
		// input step, then the laugh track, then spectatorReentry. Four is
		// enough to stop treating them as separate bugs.
		G_Ticker(true);

		// And the checksum of the world this tic just produced. The tic loop
		// writes it right after each tic and a replay goes around the loop, so
		// without this the stored value for every replayed tic still describes
		// the world before the correction -- and that is the value that gets sent
		// to the server and compared. A correction that silently invalidates the
		// evidence of its own success is worse than no correction.
		D_RecordConsistancy(t + 1);

		ran++;
	}

	// And forward to the tic the game is about to run, which is where the
	// world this was compared against stands.
	gametic = now + 1;

	// Stopped before anything is printed: CONS_Printf writes to the log as well
	// as the console, which costs milliseconds, and the per-tic figure below is
	// the whole point of the command.


	// Stopped before any caller prints anything: CONS_Printf writes to the log
	// as well as the console and costs milliseconds, and the per-tic figure is
	// the whole point of measuring at all.
	feed->us = K_PreciseToMicros(I_GetPreciseTime() - started);
	g_replaying = false;

	return ran;
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
	uint32_t us;
	int32_t n = 4;
	int32_t i;
	ticcmd_t livecmd[MAXPLAYERS], liveold[MAXPLAYERS];
	rollbackfeed_t feed;
	int32_t ran = 0;
	tic_t from, now;
	tic_t ltbefore, ltafter, ltloaded;
	dboolean records;

	if (K_RawSnapBlocksTests("rollback_replay"))
		return;

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

	ran = K_RollbackTo(from, now, &feed);

	if (ran < 0)
	{
		CONS_Printf("rollback_replay: no snapshot for tic %s -- turn rollback_keep "
			"on and let %d tics go by\n", sizeu1(from), n);
		return;
	}

	ltloaded = feed.loaded;
	us = feed.us;
	ltafter = leveltime;

	if (records)
		K_CaptureRecords(&g_recssecond);

	// Before the present is put back, because this compares against the world
	// the replay arrived at.
	K_CompareMobjs("rollback_replay");

	// Whether this replay was even given the right inputs to repeat. Everything
	// else it reports is about what the world did with them.
	{
		char fields[192];
		uint32_t k;

		// Not a failure line. The replay runs on the record, so this measures how
		// far netcmds has drifted from what really happened -- which is the size
		// of the problem phase 3's detect gesture has to solve, and that deserves
		// a number rather than a shrug.
		CONS_Printf("rollback_replay: %u tics checked, %u not recorded; netcmds "
			"disagreed with what really ran on %u player-tics, replayed on the "
			"record instead\n",
			feed.checked, feed.unknown, feed.disagreed);

		for (k = 0; k < feed.disagreed && k < ROLLBACK_FEEDKEPT; k++)
		{
			K_NameTiccmdDifferences(fields, sizeof (fields),
				&feed.wrong[k].used, &feed.wrong[k].fed);

			CONS_Printf("rollback_replay: tic %s, player %d fed something else -- %s\n",
				sizeu1((size_t)feed.wrong[k].tic), feed.wrong[k].who,
				(fields[0] != '\0') ? fields : "same fields, different padding");
		}
	}

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
	// Not while speculating. These tics are thrown away and re-derived next pass,
	// so keeping them would fill the ring with worlds nobody will ever go back to
	// -- and the one slot that matters, the confirmed frontier, is written by
	// K_RollbackSpeculate itself.
	//
	// Nor in two-clock mode at all: its only restore is to the frontier, and
	// K_RollbackSpeculate saves the frontier itself, into the same slot, a few
	// microseconds after this would -- the same state written twice a pass. On
	// Opulence that was 2.3 ms of a pass that already took the whole tic
	// (WORLDWIDE.md 8.62).
	if (g_keeping && g_twoclock == 0 && gamestate == GS_LEVEL && g_soakbusy == false
		&& g_speculating == false)
		K_SaveGameState(gametic);

	// The first tic not yet run, for K_RollbackSoundsSilenced. A soak's extra
	// tics are not the timeline and do not move it.
	if (g_replaying == false && g_soakbusy == false && gametic + 1 > g_soundhorizon)
		g_soundhorizon = gametic + 1;

	K_RollbackSoakTicker();
}

void K_RollbackSoakTicker(void)
{
	if (g_soakinterval == 0 || gamestate != GS_LEVEL)
		return;

	// A soak compares archives, which rollback_rawsnap 1 does not write: one
	// started before the switch waits until it is set back to 0 or 2.
	if (g_rawsnap == 1)
		return;

	// A check resimulates tics, and those tics must not start checks of their own.
	if (g_soakbusy)
		return;

	if ((leveltime % (tic_t)g_soakinterval) != 0)
		return;

	g_soakbusy = true;

	g_soakchecks++;

	if ((g_soakleak ? K_LeakCheck(g_soaktics, false)
			: K_ResimCheck(g_soaktics, false)) == false)
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

// The detect half of a rollback, counting only. Nothing acts on this yet.
static uint32_t g_arrivals;      // inputs that arrived for a tic already run
static uint32_t g_contradicted;  // of those, how many said something different
static uint32_t g_unrecorded;    // and how many the ring could no longer vouch for
static uint32_t g_blame[MAXPLAYERS];  // which player's input the guess got wrong
static dboolean g_localwrong;    // and whether one of them was us
static uint32_t g_selfnamed;     // how many self-mispredictions have been spelled out
static dboolean g_rewindwanted;  // a netxcmd landed on a tic we had already predicted
static tic_t g_rewindto;
static uint32_t g_rewinds;       // how many times that has happened

// Enough to see the shape without filling the log: the same field wrong every
// time says something different from a different field each time.
#define ROLLBACK_SELFNAMED 8
static tic_t g_lastcorrection;   // when the world was last reconciled
static uint32_t g_deferred;      // corrections the rate limit held back

// How often the world is reconciled for somebody else's misprediction.
//
// Correcting on every contradicted input cost 2476 rollbacks in two minutes,
// each replaying twelve tics -- seventeen milliseconds of replay against a
// 28.6 ms budget, permanently, which is what the stutter was. Most of those
// contradictions are an angle moving by a hair.
//
// A car is predictable: left alone, the engine's own physics carries a kart
// forward on its momentum and heading, which is dead reckoning done by the
// simulation rather than by us. What that buys is time -- the predicted world
// stays close for a while, so the correction can be paid for once every so often
// instead of on every packet.
//
// ⚠ It cannot be skipped altogether. This is deterministic lockstep: an input
// that is wrong and never corrected is baked into our world for good, and two
// worlds that drift apart never come back together. So this is a rate, not a
// tolerance, and every reconciliation makes the world exact again.
#define ROLLBACK_RECONCILE_EVERY 12
static tic_t g_correctfrom;      // the oldest tic that would have to be replayed
static dboolean g_havecorrection;
static uint32_t g_corrections;   // rollbacks the loop has actually performed
static uint32_t g_replayedtics;  // tics those rollbacks replayed
static uint32_t g_unreachable;   // corrections wanted from before the ring reaches

// The inputs this machine ran its own player on, kept by tic.
//
// When the server's copy of one of them comes back, this says *which* of our
// samples it is, and therefore how far out of step our labelling is. Deeper than
// the snapshot ring on purpose: the question is about the network's round trip,
// not about how far back a correction can reach.
#define ROLLBACK_LOCALTRAIL 64
static tic_t g_trailtic[ROLLBACK_LOCALTRAIL];
static ticcmd_t g_trailcmd[ROLLBACK_LOCALTRAIL];
static dboolean g_trailheld[ROLLBACK_LOCALTRAIL];

// Where the server put an input against where we ran it. Two guesses at this
// offset -- mindelay at two tics, then maketic labelling at twelve -- were both
// wrong, and both were guesses. A histogram cannot be guessed at.
#define ROLLBACK_OFFSPAN 24
static uint32_t g_offsetseen[(2 * ROLLBACK_OFFSPAN) + 1];
static uint32_t g_offsetfar;   // matched, but further out than the histogram reaches
static uint32_t g_offsetlost;  // the server used an input we never ran a tic on

// PT_CLIENTCMD's own relabelling: an arriving ticcmd is filed under `faketic`
// (the server's send schedule plus the requested gentleman's delay), not
// under the tic number the client itself tagged it with. Vanilla machinery,
// present before this branch and orthogonal to it -- but if that relabelling
// is not perfectly steady from packet to packet, the same real input can end
// up dated differently on the two machines' confirmed clocks, which is a
// question the input tally's own hash cannot answer by itself (it folds the
// tic number in, so a relabelled sample simply reads as content that
// differs -- this says whether relabelling is the reason).
#define ROLLBACK_RELABELSPAN 24
static uint32_t g_relabelseen[(2 * ROLLBACK_RELABELSPAN) + 1];
static uint32_t g_relabelfar;
static uint32_t g_relabelcount;

// How the server filed each player's samples (WORLDWIDE.md 8.85): a tic later
// than they arrived because the slot was taken, over one already filed, and the
// tics that got none and repeated the one before. In a level only; never reset,
// like the histogram above.
static uint32_t g_filed[MAXPLAYERS];
static uint32_t g_filedshifted[MAXPLAYERS];
static uint32_t g_filedover[MAXPLAYERS];
static uint32_t g_filedrepeat[MAXPLAYERS];
static int64_t g_relabelsum;
static int32_t g_relabelmin;
static int32_t g_relabelmax;

// The same histogram split by who sent the packet and whether a race was on.
// A listen server sends its own clientpak, and the delay exemption only applies
// in GS_LEVEL, so the +2 cluster of 8.27 may be the host before the race
// starts -- this says so or refutes it.
#define RELABEL_HOST_LEVEL 0
#define RELABEL_HOST_OTHER 1
#define RELABEL_REMOTE_LEVEL 2
#define RELABEL_REMOTE_OTHER 3
static uint32_t g_relabelsplit[4][(2 * ROLLBACK_RELABELSPAN) + 1];
static uint32_t g_relabelsplitcount[4];

/** How far apart two tics are, either way round.
  *
  * tic_t is unsigned, so the ordinary subtraction is a trap.
  */
static tic_t K_TicDistance(tic_t a, tic_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

/** Keeps the input a predicted tic ran our own player on. */
static void K_RollbackTrailLocal(tic_t tic, const ticcmd_t *cmd)
{
	const size_t at = (size_t)(tic % ROLLBACK_LOCALTRAIL);

	g_trailtic[at] = tic;
	g_trailcmd[at] = *cmd;
	g_trailheld[at] = true;
}

/** Finds which tic, if any, this machine ran a given input on.
  *
  * Walks the whole trail rather than indexing it, because the question is "did
  * we ever run this input, and when" and the answer wanted is the tic. Ties are
  * ordinary -- somebody holding still makes the same sample over and over -- so
  * the match nearest the tic asked about wins, which is the reading most
  * favourable to the code being right.
  *
  * \return true when found, with the tic through *at.
  */
static dboolean K_RollbackTrailFind(const ticcmd_t *cmd, tic_t about, tic_t *at)
{
	dboolean found = false;
	tic_t best = 0;
	size_t i;

	for (i = 0; i < ROLLBACK_LOCALTRAIL; i++)
	{
		if (g_trailheld[i] == false)
			continue;

		// A slot is only overwritten once every ROLLBACK_LOCALTRAIL tics, so an
		// entry from a previous level survives until its turn comes round, and
		// an input matches by value however old it is. Anything further away
		// than the trail is long cannot be the sample this arrival is about.
		if (K_TicDistance(g_trailtic[i], about) >= ROLLBACK_LOCALTRAIL)
			continue;

		if (K_InputsDiffer(&g_trailcmd[i], cmd))
			continue;

		if (found == false
			|| K_TicDistance(g_trailtic[i], about) < K_TicDistance(best, about))
		{
			best = g_trailtic[i];
			found = true;
		}
	}

	if (found && at != NULL)
		*at = best;

	return found;
}

/** Called when the server's inputs for a tic land on a client.
  *
  * A rollback needs to know one thing from the network: that a tic it has
  * already run was run on the wrong input. This is where that becomes visible,
  * and for now it only counts -- correcting is the next piece, and a detector
  * that has never been watched is not one to hang a correction on.
  */
void K_RollbackNoteArrival(tic_t tic)
{
	const ticcmd_t *used;
	int32_t i;

	if (g_keeping == false || gamestate != GS_LEVEL)
		return;

	// Not yet run, so nothing to contradict: this is simply the input arriving
	// in time, which is the ordinary case and not interesting.
	if (tic >= gametic)
		return;

	g_arrivals++;

	used = K_InputsAsUsed(tic);

	if (used == NULL)
	{
		// The ring has been round since. Counted rather than ignored, because a
		// rollback that cannot reach back far enough is a real limit and not a
		// non-event.
		g_unrecorded++;
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] == false)
			continue;

		if (K_InputsDiffer(&netcmds[tic % BACKUPTICS][i], &used[i]) == false)
			continue;

		g_contradicted++;
		g_blame[i]++;

		// Our own input is never a guess -- it came off this machine's keyboard.
		// If it disagrees with what the server used, something is wrong that
		// waiting will not fix, and it is the one case worth a rollback at once.
		if (i == g_localplayers[0])
		{
			g_localwrong = true;

			// Which field, not just how often. Every contradiction measured so
			// far has been this machine disagreeing with itself, and two guesses
			// at the size of the offset -- mindelay, then maketic -- both missed.
			// The ticcmd namer has existed since this morning; this is what it
			// was for.
			if (g_selfnamed < ROLLBACK_SELFNAMED)
			{
				char fields[192];

				K_NameTiccmdDifferences(fields, sizeof (fields),
					&used[i], &netcmds[tic % BACKUPTICS][i]);

				CONS_Printf("rollback_detect: tic %s, our own input -- used %s\n",
					sizeu1((size_t)tic),
					(fields[0] != '\0') ? fields : "the same fields, different padding");

				g_selfnamed++;
			}

			// And where the server put this input against where we ran it. The
			// namer says which field disagrees; this says whether the whole
			// sample is simply in the wrong place, and by how much. The two
			// answers want opposite fixes: a repeated offset is a labelling
			// error, while a sample we never ran at all means the passes are
			// spending them unevenly.
			{
				tic_t ranon = 0;

				if (K_RollbackTrailFind(&netcmds[tic % BACKUPTICS][i], tic, &ranon))
				{
					const int32_t off = (int32_t)((int64_t)tic - (int64_t)ranon);

					if (off >= -ROLLBACK_OFFSPAN && off <= ROLLBACK_OFFSPAN)
						g_offsetseen[off + ROLLBACK_OFFSPAN]++;
					else
						g_offsetfar++;
				}
				else
				{
					g_offsetlost++;
				}
			}
		}

		if (g_havecorrection == false || tic < g_correctfrom)
		{
			g_correctfrom = tic;
			g_havecorrection = true;
		}

		break;
	}
}

/** Console command: rollback_detect
  *
  * What the network has been saying about tics already run.
  */
static void Command_RollbackDetect_f(void)
{
	CONS_Printf("rollback_detect: %u inputs arrived for tics already run, "
		"%u of them contradicted what was used, %u came too late for the ring\n",
		g_arrivals, g_contradicted, g_unrecorded);

	if (g_havecorrection)
	{
		CONS_Printf("rollback_detect: the oldest tic needing a replay is %s, "
			"which is %s tics back from %s\n",
			sizeu1((size_t)g_correctfrom),
			sizeu2((size_t)(gametic - g_correctfrom)), sizeu3((size_t)gametic));
	}
	else if (g_contradicted > 0)
	{
		// Contradictions were seen and the loop has already dealt with them:
		// K_RollbackCorrect clears the pending mark as it acts. Saying "nothing
		// has contradicted anything" underneath a line reporting eleven of them
		// is the report contradicting itself.
		CONS_Printf("rollback_detect: none of them still pending -- the loop corrected them as they came\n");
	}
	else
	{
		CONS_Printf("rollback_detect: nothing has contradicted anything yet\n");
	}

	// Which player the guess was wrong about. "Ninety-two percent of tics
	// contradicted" does not say whether that is one player every tic or every
	// player occasionally, and those want opposite fixes. Named, because a local
	// player mispredicting *itself* would be a bug of mine rather than a hard
	// problem about guessing what other people are about to press.
	{
		int32_t i;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (g_blame[i] == 0)
				continue;

			CONS_Printf("rollback_detect: player %d (%s)%s was mispredicted %u times\n",
				i, (playeringame[i] ? player_names[i] : "gone"),
				(i == g_localplayers[0]) ? " -- THIS MACHINE" : "",
				g_blame[i]);

			g_blame[i] = 0;
		}
	}

	// Where the server put our own input against where we ran it.
	//
	// One offset repeated says the labelling is out of step, and by how much,
	// which is a small fix. A sample the server used that this machine never ran
	// a tic on says something else entirely: a pass makes one sample and the tic
	// loop can spend several tics on it, so the ones in between are made, sent,
	// and never used here. Those two readings want opposite fixes, they have been
	// argued about twice without either being measured, and this is the
	// measurement.
	{
		uint32_t placed = 0;
		int32_t off;

		for (off = -ROLLBACK_OFFSPAN; off <= ROLLBACK_OFFSPAN; off++)
			placed += g_offsetseen[off + ROLLBACK_OFFSPAN];

		if (placed > 0 || g_offsetfar > 0 || g_offsetlost > 0)
		{
			CONS_Printf("rollback_detect: of our own inputs the server sent back, "
				"%u were samples this machine did run a tic on, %u were further "
				"out than %d tics, and %u it never ran at all\n",
				placed, g_offsetfar, ROLLBACK_OFFSPAN, g_offsetlost);

			for (off = -ROLLBACK_OFFSPAN; off <= ROLLBACK_OFFSPAN; off++)
			{
				const uint32_t seen = g_offsetseen[off + ROLLBACK_OFFSPAN];

				if (seen == 0)
					continue;

				CONS_Printf("rollback_detect:   the server used it %d tic(s) %s, %u times\n",
					(off < 0 ? -off : off),
					(off > 0 ? "later than we did" :
						(off < 0 ? "earlier than we did" : "on the very tic we did")),
					seen);
			}
		}
	}

	g_arrivals = g_contradicted = g_unrecorded = g_selfnamed = 0;
	g_offsetfar = g_offsetlost = 0;
	memset(g_offsetseen, 0, sizeof (g_offsetseen));
	g_havecorrection = false;
}

// ----------------------------------------------------------------------------
// The loop: predict, detect, correct.
//
// Off unless rollback_loop is turned on, and then only on a client. The three
// gestures are one mechanism and only make sense together: a client that runs
// ahead without correcting drifts away from the server and gets thrown off, and
// a client that could correct but never runs ahead has nothing to correct --
// which is exactly what the detector reported, three runs in a row, before any
// of this existed.
// ----------------------------------------------------------------------------

static int32_t g_loopahead;      // how many tics the client may run past the server
static uint32_t g_predicted;     // tics actually run before the server confirmed them
static int32_t g_furthestahead;  // the most it ever got in front
static uint32_t g_loops;         // times the tic loop was entered
static int32_t g_mostbehind;     // the most confirmed tics waiting to be run
static int64_t g_behindsum;      // to say what "typically" means
static int32_t g_bestlead;       // the furthest ahead the loop ever ENDED a pass
static int32_t g_worstlead;      // and the furthest behind
static int64_t g_leadsum;        // to say what it typically ends at
static uint32_t g_leadsamples;

// How many predicted tics one pass of the tic loop runs.
//
// The player's controls are sampled once a pass: NetUpdate calls Local_Maketic
// at the top of TryRunTics and returns early when no real tic has elapsed. The
// tic loop underneath it then runs as many tics as the network has handed it,
// plus the prediction depth -- and every predicted tic in that pass is run on
// that one sample, while the server, which receives one sample a pass and spends
// one a tic, has a distinct input for each of them.
//
// That is the shape the namer printed: our own turning and angle frozen across
// four tics while the server's moved on every one. Counted here rather than
// assumed, because "the loop runs up to twelve tics in one pass" is an inference
// from a symptom, and two fixes built on inferences of exactly that kind have
// already been written, measured and reverted.
static uint32_t g_passes;           // tic loop passes taken while the loop was on
static uint32_t g_passespredicting; // of those, passes that predicted anything
static uint32_t g_passesburst;      // and passes that predicted more than one tic
static int32_t g_worstburst;        // the most predicted tics one pass ever ran
static int64_t g_burstsum;          // to say what a predicting pass typically runs

static dboolean g_pacing;           // one predicted tic to a pass; off by default
static dboolean g_smoothing;        // ease a correction rather than teleport it

/** How far ahead a predicted client may get.
  *
  * Bounded by the ring, because a correction cannot reach further back than the
  * oldest snapshot, and by the depth policy, because a rollback deeper than a
  * tic's budget costs more than it saves. Two slots are kept back: one holds
  * where a replay starts and one where it has to arrive.
  */
int32_t K_RollbackPredictAhead(void)
{
	int32_t cap = K_RollbackMaxDepth();

	if (g_loopahead <= 0)
		return 0;

	if (cap > ROLLBACK_TICS - 2)
		cap = ROLLBACK_TICS - 2;

	return (g_loopahead < cap) ? g_loopahead : cap;
}

/** Records how far behind the server the client was when a tic loop began.
  *
  * A client only has something to predict when it runs out of confirmed tics. On
  * a loopback there is no latency to run out of: the server's tics arrive before
  * they are needed, so the client is permanently a little behind and the
  * prediction ceiling is never reached. That is a claim about the network rather
  * than about the code, and this is the number that decides it.
  */
void K_RollbackNoteTicLoop(int32_t behind)
{
	if (g_loopahead <= 0)
		return;

	g_loops++;
	g_behindsum += behind;

	if (behind > g_mostbehind)
		g_mostbehind = behind;
}

/** Records the lead the client is left with when a tic loop finishes.
  *
  * The counters said the loop predicts thirty tics in four thousand passes, and
  * three fixes aimed at *how* it predicts changed nothing a player could feel.
  * What none of them measured is whether the client ever holds a lead at all:
  * predicting requires gametic to have reached neededtic, and if something pulls
  * it back every pass then no amount of tuning the prediction will matter.
  *
  * Called after the loop rather than before it, which is the half the "how far
  * behind did it start" number could never see.
  */
void K_RollbackNoteTicLoopEnd(int32_t lead)
{
	if (g_loopahead <= 0)
		return;

	if (g_leadsamples == 0 || lead > g_bestlead)
		g_bestlead = lead;

	if (g_leadsamples == 0 || lead < g_worstlead)
		g_worstlead = lead;

	g_leadsum += lead;
	g_leadsamples++;
}

/** Records how many predicted tics one pass of the tic loop ran.
  *
  * A pass makes exactly one sample of the player's controls, so a pass that
  * predicts several tics spends that single sample on all of them. This is the
  * count that says whether that happens at all, and how badly.
  */
void K_RollbackNotePass(int32_t predicted)
{
	if (g_loopahead <= 0)
		return;

	g_passes++;

	if (predicted <= 0)
		return;

	g_passespredicting++;
	g_burstsum += predicted;

	if (predicted > 1)
		g_passesburst++;

	if (predicted > g_worstburst)
		g_worstburst = predicted;
}

/** True when the loop may run only one predicted tic per pass.
  *
  * The candidate fix for the frozen input column, and off by default so the
  * measurement above can be taken with it off and again with it on inside one
  * played race. Confirmed tics are never held back by it: they carry their own
  * inputs, and a client that has fallen behind has to be free to catch up.
  */
dboolean K_RollbackPacing(void)
{
	return (g_loopahead > 0 && g_pacing);
}

int32_t K_RollbackTwoClock(void)
{
	if (g_twoclock <= 0 || client == false || gamestate != GS_LEVEL)
		return 0;

	return g_twoclock;
}

/** Is two-clock mode switched on here, whatever role this machine plays?
  *
  * K_RollbackTwoClock() answers a deliberately narrower question -- *is
  * speculation running on this machine* -- and its `client == false` is right
  * for that: a server is authoritative and has nothing to predict. The delay
  * policy needs the other question, and asking the narrow one for it is what
  * the measurement below caught.
  */
static dboolean K_RollbackTwoClockConfigured(void)
{
	return (g_twoclock > 0 && gamestate == GS_LEVEL);
}

/** The server-side half of the same question.
  *
  * ⚠ There is no single "Worldwide is on" state on a machine, and not knowing
  * that is what made the first version of this fix a no-op: two-clock is a
  * *client* switch (rollback_twoclock, set in playclient_*.cfg) and the
  * correction channel is a *server* one (rollback_correct, set in
  * playserver_*.cfg). A host running Worldwide therefore has g_twoclock == 0
  * for its entire life, so asking g_twoclock alone exempted nobody and the
  * race came back indistinguishable from the one before it -- rollbackpays 0
  * at every print, same 6<->7 oscillation.
  *
  * g_correctrate is the nearest thing the server has to "I am running
  * Worldwide". It is a proxy and it is worth naming as one: the question the
  * delay policy really wants is *are my clients predicting*, which the server
  * cannot answer today. That is the capability advertising already scoped on
  * the roadmap, and when it lands this predicate should ask it instead.
  *
  * It has landed as WORLDWIDE mode (8.80): K_RollbackCorrectRate() now answers
  * for it too, and a server in that mode lets in only clients that declare
  * themselves WORLDWIDE -- which predict as soon as they join it. So the proxy
  * is now the question, whenever the mode rather than rollback_correct set it.
  */
static dboolean K_RollbackCorrectingHere(void)
{
	return (K_RollbackCorrectRate() > 0 && gamestate == GS_LEVEL);
}

/** The mindelay/gentleman's-delay exemption used to ask K_RollbackPredictAhead()
  * alone, which only ever answers for the old loop: rollback_twoclock zeroes
  * g_loopahead on the way in (see Command_RollbackTwoClock_f -- the two are
  * mutually exclusive by construction), so once the pivot took over that check
  * always came back false and the fixed delay kept charging for a round trip
  * the speculation was already covering. This asks both.
  *
  * ⚠ The paragraph that used to sit here argued this was safe to call from
  * either side, because K_RollbackTwoClock() returns 0 off a dedicated server
  * where `client == false`. That is true and it is the harmless half. `client`
  * is `(!server)` (d_clisrv.h), so it is false on a *listen* server too -- and
  * there node 0 is a person holding a controller. Measured before changing
  * anything (WORLDWIDE.md 8.24): rollbackpays read 0 on the host at every
  * print of a full race, so the host fell back to raw ping and charged itself
  * 6-7 tics of gentleman's delay -- 170-200 ms, the exact cost this branch
  * exists to remove -- while the remote client was charged none. The comment
  * checked the case where the answer did not matter and generalised to the
  * case where it did.
  *
  * The delay's whole purpose is to equalise: without it a host has instant
  * input while remote players eat their ping. Once those remote players
  * predict, their input lag is already ~0, so keeping the host's handicap does
  * not equalise anything -- it inverts the asymmetry it was built to prevent.
  *
  * ⚠ Not settled by this, and worth saying plainly: a host exempted here has
  * an edge over any remote player who is *not* running Worldwide. That is the
  * same trade the client-side exemption already makes, and the honest fix is
  * the capability advertising already scoped on the roadmap, not this
  * predicate. (WORLDWIDE mode, 8.80, closes it for the mode: a server running
  * it refuses every client that has not declared itself WORLDWIDE.)
  */
dboolean K_RollbackPays(void)
{
	return (K_RollbackPredictAhead() > 0)
		|| K_RollbackTwoClockConfigured()
		|| K_RollbackCorrectingHere();
}

dboolean K_RollbackSpeculating(void)
{
	return g_speculating;
}

dboolean K_RollbackPredicting(void)
{
	return (K_RollbackTwoClock() > 0 || K_RollbackPredictAhead() > 0);
}

void K_RollbackNoteSuppressedXCmd(void)
{
	g_suppressedxcmds++;

	// The message is gone; the tic that raised it must be run by the loop.
	if (g_speculating)
		g_keeptaint[gametic % ROLLBACK_TICS] = true;
}

void K_RollbackUnspeculate(void)
{
	precise_t started;

	if (g_speculated == false)
		return;

	g_speculated = false;

	started = I_GetPreciseTime();

	if (K_LoadGameState(g_confirmedtic) == false)
	{
		// The confirmed world is gone -- the ring went round, or the map changed
		// under us. Counted rather than papered over: the authoritative loop is
		// about to run from whatever the speculation left, which is the one
		// situation this mode exists to prevent, and it must be visible.
		g_specstranded++;
		return;
	}

	gametic = g_confirmedtic;

	g_unspecus += K_PreciseToMicros(I_GetPreciseTime() - started);
}

/** Which of this machine's own inputs are sent but not yet applied.
  *
  * The server files every input this machine sends under a tic of its own
  * choosing, about a round trip after it was made, and the confirmed world has
  * run none of the ones still in flight. Repeating the newest input across the
  * speculation replaces all of them with it: a turn released a few tics ago is
  * shown as already over, and the kart stops short of where the server will put
  * it (WORLDWIDE.md 8.39).
  *
  * The newest tic the server has sent carries the input it applied there, and
  * that input still holds the leveltime stamp G_BuildTiccmd gave it -- the
  * server copies it untouched. Finding that sample in the local history says
  * which inputs came after it: those are the ones still to be applied, oldest
  * first. Newest match wins, so a run of identical samples is undercounted, not
  * overcounted.
  */
/** The age in a local player's history of the newest sample, from age from on,
  * that the anchor would take for cmd; -1 when none. */
/** Whether two samples are the same to the anchor. Not the angle:
  * D_ResetTiccmdAngle rewrites it across the whole history. */
static dboolean K_SameSample(const ticcmd_t *a, const ticcmd_t *b)
{
	return (a->latency == b->latency
		&& a->forwardmove == b->forwardmove
		&& a->turning == b->turning
		&& a->buttons == b->buttons);
}

static int32_t K_HistoryAgeOf(uint8_t ss, const ticcmd_t *cmd, int32_t from)
{
	int32_t age;

	for (age = (from > 0 ? from : 0); age < MAXGENTLEMENDELAY; age++)
	{
		if (K_SameSample(D_LocalTiccmdAge(ss, age), cmd))
			return age;
	}

	return -1;
}

/** R1: how many real tics passed before the sample of this age was made, so how
  * many tics the sample before it owns at the server. 1 when unknown. */
static int32_t K_SampleRealtics(int32_t age)
{
	int32_t k;

	if (age < 0 || age >= MAXGENTLEMENDELAY)
		return 1;

	k = g_samplerealtics[(g_samplehead - (uint32_t)age) % MAXGENTLEMENDELAY];
	return (k > 0) ? k : 1;
}

/** R1: the age of the sample the server will have applied `d` tics past the
  * first tic it has not sent. The applied sample (age `unacked`) owns as many
  * tics as real tics passed before the next one, of which it has already had
  * `held` among the tics received; each sample after it owns the real tics of
  * the one after that; the newest owns every tic past them. */
static int32_t K_HistoryAgeForTic(int32_t unacked, int32_t held, tic_t d, dboolean *first)
{
	int32_t age = unacked;
	int32_t own = K_SampleRealtics(age - 1) - held;

	// Whether the tic is the sample's first: the server files a sample on one
	// tic and repeats it on the ones after, with TICCMD_RECEIVED cleared
	// (SV_Maketic). The applied sample's first tic is behind; the newest's
	// later tics are a guess, where a new sample is likelier than a repeat.
	*first = false;

	for (;;)
	{
		if (own > 0)
		{
			if (d < (tic_t)own)
			{
				*first = (age != unacked && d == 0);
				return age;
			}

			d -= (tic_t)own;
		}

		age--;

		if (age <= 0)
		{
			*first = true;
			return 0;
		}

		own = K_SampleRealtics(age - 1);
	}
}

static void K_RollbackMapHistory(void)
{
	const tic_t base = D_NeededTic();
	int32_t i;

	g_histbase = base;

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		g_histunacked[i] = -1;
		g_histheld[i] = 1;
	}

	if (base == 0)
		return;

	for (i = 0; i <= (int32_t)splitscreen; i++)
	{
		const int32_t who = g_localplayers[i];
		const ticcmd_t *applied;
		int32_t age;

		if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false)
			continue;

		applied = &netcmds[(base - 1) % BACKUPTICS][who];
		age = K_HistoryAgeOf((uint8_t)i, applied, 0);
		g_histunacked[i] = age;

		// R1: the received tics just before, still holding the same sample --
		// ones the server filled by repeating it -- are tics it already owned.
		// netcmds keeps a received tic until its slot comes round again.
		{
			int32_t held = 1;

			while (held < MAXGENTLEMENDELAY && base > (tic_t)held + 1
				&& K_SameSample(&netcmds[(base - 1 - (tic_t)held) % BACKUPTICS][who], applied))
			{
				held++;
			}

			g_histheld[i] = held;
		}

		// Counted: a pass R1 lays out differently from one sample a tic.
		if (i == 0 && g_histreal && age > 0)
		{
			int32_t a;
			dboolean stretched = (K_SampleRealtics(age - 1) - g_histheld[0] > 0);

			for (a = age - 1; a > 0 && stretched == false; a--)
				stretched = (K_SampleRealtics(a - 1) != 1);

			if (stretched)
				g_histstretched++;
		}

		// Another sample the anchor cannot tell from it: the newer was taken.
		if (i == 0 && age >= 0 && K_HistoryAgeOf(0, applied, age + 1) >= 0)
			g_anchorambiguous++;
	}
}

/** The input a local player's slot gets on a speculated tic: the next sent-but-
  * unapplied one while any are left, then the newest. Without rollback_history,
  * always the newest, as before. */
static const ticcmd_t *K_RollbackLocalCmdFor(uint8_t ss, tic_t tic, dboolean *received)
{
	const int32_t unacked = g_histunacked[ss];
	int32_t age = 0;

	*received = true;

	if (g_speculating && g_histmax > 0 && unacked > 0 && tic >= g_histbase)
	{
		if (g_histreal)
			age = K_HistoryAgeForTic(unacked, g_histheld[ss], tic - g_histbase, received);
		else if (tic - g_histbase < (tic_t)unacked)
			age = unacked - 1 - (int32_t)(tic - g_histbase);
	}

	return D_LocalTiccmdAge(ss, age);
}

static int32_t K_SpeculationDepth(int32_t ahead, tic_t frontier);
static void K_RunSpeculatedTic(tic_t frontier, dboolean savestart);
static void K_NoteDrawnOffset(void);
static dboolean K_KeepUseCorrection(tic_t frontier);
static void K_KeepNoteKarts(int32_t s);

dboolean K_RollbackKeepArm(void)
{
	const int32_t s = (int32_t)(g_confirmedtic % ROLLBACK_TICS);

	g_keeparmed = g_keepkept = false;

	if (g_keepspec == false || g_twoclock <= 0 || g_nullspec || g_speculated == false
		|| gamestate != GS_LEVEL || g_keeptic[s] != g_confirmedtic)
		return false;

	// The world stays at the speculation's head; the netcode is handed the
	// frontier's clock, which is what it builds and sends this machine's input
	// by -- the tic it reports, and the leveltime stamp in the input.
	g_keephead = gametic;
	g_keepheadleveltime = leveltime;
	gametic = g_confirmedtic;
	leveltime = g_keeplevel[s];
	g_keeploads = P_NetLoadCount();
	g_keeparmed = true;

	return true;
}

dboolean K_RollbackKeepArmed(void)
{
	return g_keeparmed;
}

/** The keep decision's comparison: K_SameInput, and for this machine's own
  * players TICCMD_RECEIVED as well. The kart's steering reads that flag
  * (P_UpdatePlayerAngle, the "missed a single tic" rule), and a tic the server
  * filled by repeating a sample has it cleared (SV_Maketic): kept with it set,
  * the kart steered by a hair otherwise than on the server (WORLDWIDE.md 8.92).
  * Not for others: a guess about another player is marked unreceived on
  * purpose, and would never match. */
static dboolean K_KeepSameInput(int32_t player, const ticcmd_t *a, const ticcmd_t *b)
{
	int32_t i;

	if (K_SameInput(a, b) == false)
		return false;

	for (i = 0; i <= (int32_t)splitscreen; i++)
	{
		if (g_localplayers[i] == player)
			return ((a->flags ^ b->flags) & TICCMD_RECEIVED) == 0;
	}

	return true;
}

/** A rebuild for a wrong input: who was wrong on the first wrong tic, and how.
  * Returns KPC_SELF when this machine's own input was among them, KPC_OTHERS
  * when only others' were, -1 when none differed (a player came or went). */
static int32_t K_NoteKeepMiss(tic_t from, tic_t tic)
{
	const int32_t s = (int32_t)(tic % ROLLBACK_TICS);
	const int32_t at = (int32_t)(tic - from);
	int32_t found = -1;
	int32_t p;

	g_keepmissat[(at < 4) ? at : 4]++;
	g_keepmissright += (uint32_t)at;
	g_keepmissrun += (uint32_t)(g_keephead - from);

	for (p = 0; p < MAXPLAYERS; p++)
	{
		const ticcmd_t *ran = &g_keepcmds[s][p], *real = &netcmds[tic % BACKUPTICS][p];
		int32_t who;

		if (playeringame[p] == false || g_keepin[s][p] == false || K_KeepSameInput(p, ran, real))
			continue;

		who = (p == g_localplayers[0]) ? 0 : (players[p].bot ? 1 : 2);
		g_keepmisswho[who]++;
		K_CountWrongFields(g_keepmissfield[who], ran, real);

		if (found != KPC_SELF)
			found = (who == 0) ? KPC_SELF : KPC_OTHERS;

		if (who == 0 && g_keepsrc[s] < NUMKEEPSRC)
			g_keepmisssrc[g_keepsrc[s]]++;

		if (who == 0)
		{
			const int32_t ranage = K_HistoryAgeOf(0, ran, 0);
			const int32_t realage = K_HistoryAgeOf(0, real, 0);

			if (ranage < 0 || realage < 0)
				g_keepmissshift[KEEPSHIFT_LOST]++;
			else if (realage - ranage <= -2)
				g_keepmissshift[KEEPSHIFT_NEWER2]++;
			else if (realage - ranage == -1)
				g_keepmissshift[KEEPSHIFT_NEWER1]++;
			else if (realage == ranage)
				g_keepmissshift[KEEPSHIFT_SAME]++;
			else if (realage - ranage == 1)
				g_keepmissshift[KEEPSHIFT_OLDER1]++;
			else
				g_keepmissshift[KEEPSHIFT_OLDER2]++;

			if (g_cascadelog)
			{
				CONS_Printf("rollback_cascade: real tic %u, leveltime %u -- rebuilt for this "
					"machine's input on tic %u (%d from the frontier): ran stamp %u%s, the "
					"server's %u%s, its sample %s\n",
					(unsigned)I_GetTime(), (unsigned)leveltime, (unsigned)tic, at,
					(unsigned)ran->latency, (ran->flags & TICCMD_RECEIVED) ? "" : " (a repeat)",
					(unsigned)real->latency, (real->flags & TICCMD_RECEIVED) ? "" : " (a repeat)",
					(ranage < 0 || realage < 0) ? "not in the history"
						: (realage == ranage) ? "the same to the anchor"
						: va("%s by %d", (realage > ranage) ? "older" : "newer",
							(realage > ranage) ? realage - ranage : ranage - realage));
			}
		}
	}

	return found;
}

static int32_t K_KeepCheckTic(tic_t tic)
{
	const int32_t s = (int32_t)(tic % ROLLBACK_TICS);
	int32_t p;

	if (g_keeptic[s] != tic)
		return KEEP_MISSING;

	if (g_keeptaint[s])
		return KEEP_TAINT;

	for (p = 0; p < MAXPLAYERS; p++)
	{
		if (playeringame[p] != g_keepin[s][p])
			return KEEP_INPUT;

		if (playeringame[p] && K_KeepSameInput(p, &g_keepcmds[s][p], &netcmds[tic % BACKUPTICS][p]) == false)
			return KEEP_INPUT;
	}

	return KEEP_KEPT;
}

dboolean K_RollbackKeepDecide(tic_t upto, dboolean textcmds)
{
	const tic_t from = g_confirmedtic;
	int32_t reason = KEEP_KEPT;
	int32_t missed = -1;
	tic_t t;

	if (g_keeparmed == false)
		return false;

	g_keeparmed = false;

	if (P_NetLoadCount() != g_keeploads || gamestate != GS_LEVEL)
	{
		// A gamestate came from the server underneath the speculation: the
		// world is that one now, and there is nothing to put back.
		g_speculated = false;
		g_keepcount[KEEP_RELOADED]++;
		return false;
	}

	if (leveltime <= 1)
		reason = KEEP_LEVELSTART;
	else if (upto > g_keephead)
		reason = KEEP_AHEAD;        // the server confirmed past what was run
	else if (K_KeepUseCorrection(from) == false)
		reason = KEEP_CORRECTION;   // due now, and it moves a kart
	else if (textcmds)
		reason = KEEP_TEXTCMD;
	else
	{
		for (t = from; t < upto && reason == KEEP_KEPT; t++)
		{
			reason = K_KeepCheckTic(t);

			if (reason == KEEP_INPUT)
				missed = K_NoteKeepMiss(from, t);
		}

		// The new frontier's start has to be on file, to come back to. At the
		// head it is the world itself, which the extension saves.
		if (reason == KEEP_KEPT && upto < g_keephead
			&& (rollbackring == NULL || rollbackring[upto % ROLLBACK_TICS].valid == false
				|| rollbackring[upto % ROLLBACK_TICS].tic != upto))
			reason = KEEP_MISSING;
	}

	g_keepcount[reason]++;

	// Where in the level: the frontier's leveltime is the clock now.
	if (reason == KEEP_CORRECTION)
		g_phasecount[K_PassPhase(leveltime)][KPC_CORRECTION]++;
	else if (reason == KEEP_INPUT && missed >= 0)
		g_phasecount[K_PassPhase(leveltime)][missed]++;

	if (reason != KEEP_KEPT)
	{
		// Back to the frontier, and the pass runs as it always has.
		K_RollbackUnspeculate();
		return false;
	}

	g_keepkept = true;
	g_keepupto = upto;
	return true;
}

int16_t K_RollbackKeepConsistancy(tic_t tic)
{
	return g_keepafter[tic % ROLLBACK_TICS];
}

void K_RollbackKeepCommit(void)
{
	g_confirmedtic = g_keepupto;
	gametic = g_keephead;
	leveltime = g_keepheadleveltime;
}

/** The time a speculation took since started, less the saves made inside it.
  * With rollback_keepspec every speculated tic's start is saved from inside the
  * speculation, and those saves are already in g_saveus: rollback_cost printed
  * them twice (WORLDWIDE.md 8.77). */
static uint32_t K_SpeculationMicros(precise_t started, uint32_t savedbefore)
{
	const uint32_t all = K_PreciseToMicros(I_GetPreciseTime() - started);
	const uint32_t saves = g_saveus - savedbefore;

	return (all > saves) ? all - saves : 0;
}

/** rollback_keepearly: the first tic of the standing speculation, past what the
  * server has sent, whose input for this machine R1 now gives otherwise than
  * the speculation ran it; its saved start is put back, so the extension below
  * runs again from there. Nothing when every one still stands. */
static void K_KeepEarlyCheck(tic_t frontier)
{
	const int32_t who = g_localplayers[0];
	const tic_t head = gametic;
	const tic_t base = D_NeededTic();
	tic_t tic;

	if (g_keepearly == false || g_histmax <= 0
		|| who < 0 || who >= MAXPLAYERS || playeringame[who] == false)
		return;

	for (tic = (frontier > base) ? frontier : base; tic < head; tic++)
	{
		const int32_t s = (int32_t)(tic % ROLLBACK_TICS);
		dboolean received;
		ticcmd_t now;

		// No record of it: the server's confirmation will judge it.
		if (g_keeptic[s] != tic || g_keepin[s][who] == false)
			return;

		// What K_RollbackPredictInputs would give this tic now.
		g_speculating = true;
		now = *K_RollbackLocalCmdFor(0, tic, &received);
		g_speculating = false;

		if (received)
			now.flags |= TICCMD_RECEIVED;
		else
			now.flags &= ~TICCMD_RECEIVED;

		if (K_KeepSameInput(who, &g_keepcmds[s][who], &now))
			continue;

		if (K_LoadGameState(tic) == false)
			return;

		gametic = tic;
		g_keepearlyn++;
		g_keepearlytics += (uint32_t)(head - tic);
		return;
	}
}

/** Files this pass under the phase of the level at its frontier: with a kept
  * pass the world is at the head, as many tics past it as the frontier is
  * below. */
static void K_NotePassPhase(void)
{
	const tic_t above = g_keepkept ? gametic - g_confirmedtic : 0;
	const int32_t ph = K_PassPhase((leveltime > above) ? leveltime - above : 0);

	g_phasecount[ph][KPC_PASSES]++;

	if (g_keepkept)
		g_phasecount[ph][KPC_KEPT]++;
}

/** A kept pass: the frontier has moved on, the world is still at the head, and
  * only the tics the clock now asks for beyond it are run. */
static void K_KeepExtend(void)
{
	const tic_t frontier = g_confirmedtic;
	const int32_t s = (int32_t)(frontier % ROLLBACK_TICS);
	int32_t ahead = K_SpeculationDepth(K_RollbackTwoClock(), frontier);
	const uint32_t savedbefore = g_saveus;
	precise_t started;

	g_keepkept = false;

	started = I_GetPreciseTime();

	// The sample made this pass may land on a tic run on a guess: run again
	// from there now, rather than the whole depth when it is confirmed.
	K_KeepEarlyCheck(frontier);

	g_speculating = true;

	while (gametic < frontier + (tic_t)ahead)
		K_RunSpeculatedTic(frontier, true);

	g_speculating = false;

	// The whole speculation confirmed and nothing run past it: the frontier is
	// the head, and its start has to be on file for the next pass.
	if (gametic == frontier
		&& (rollbackring == NULL || rollbackring[s].valid == false || rollbackring[s].tic != frontier))
	{
		const precise_t at = I_GetPreciseTime();

		if (K_SaveGameState(frontier) == false)
			g_specnosave++;

		g_saveus += K_PreciseToMicros(I_GetPreciseTime() - at);
		g_keeptic[s] = frontier;
		g_keeplevel[s] = leveltime;
		g_keeptaint[s] = false;
		K_KeepNoteKarts(s);
	}

	g_speculated = true;
	g_specus += K_SpeculationMicros(started, savedbefore);
	g_specpasses++;

	K_NoteDrawnOffset();
}

static int32_t g_speclastpass;  // tics the last K_RollbackSpeculate ran

int32_t K_RollbackSpeculatedLastPass(void)
{
	return g_speclastpass;
}

void K_RollbackSpeculate(void)
{
	int32_t ahead = g_nullspec ? 0 : K_RollbackTwoClock();
	uint32_t savedbefore;
	precise_t started;
	int32_t i;

	g_speclastpass = 0;

	if (K_RollbackTwoClock() <= 0)
		return;

	K_NotePassPhase();

	if (g_keepkept)
	{
		K_KeepExtend();
		return;
	}

	// Before the frontier moves: were the tics just confirmed the ones the last
	// speculation guessed? (see K_CheckGuesses)
	if (gamestate == GS_LEVEL && g_guesstics > 0 && g_keepspec == false)
		K_CheckGuesses(g_confirmedtic, gametic);

	g_guesstics = 0;
	g_guessmovedat = g_driftmoved;

	// The frontier: the world exactly as the authoritative loop left it. Saved
	// before a single speculative tic runs, because this is what the next pass
	// has to come back to.
	g_confirmedtic = gametic;

	started = I_GetPreciseTime();

	if (K_SaveGameState(gametic) == false)
	{
		g_specnosave++;
		return;
	}

	g_saveus += K_PreciseToMicros(I_GetPreciseTime() - started);

	ahead = K_SpeculationDepth(ahead, gametic);
	savedbefore = g_saveus;

	started = I_GetPreciseTime();
	g_speculating = true;

	for (i = 0; i < ahead; i++)
	{
		// Your own input is not a guess and goes in as itself; everyone else is
		// predicted. Same step the old loop used, and the only part of it worth
		// keeping. The frontier's start is saved above; with rollback_keepspec
		// every later tic's start is saved too, so that any of them can become
		// the frontier without running again.
		K_RunSpeculatedTic(g_confirmedtic, i > 0);
	}

	g_speculating = false;
	g_speculated = true;

	g_specus += K_SpeculationMicros(started, savedbefore);
	g_specpasses++;

	K_NoteDrawnOffset();
}

/** How deep this pass speculates from a frontier: rollback_twoclock, or with
  * rollback_history what the inputs still in flight need (see below). */
static int32_t K_SpeculationDepth(int32_t ahead, tic_t frontier)
{
	int32_t i;

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		g_histunacked[i] = -1;

	// Replaying the inputs still in flight needs the speculation to reach the
	// tic the newest one will land on: the first tic the server has not sent,
	// plus one tic per input. That tic jitters with the server's filing delay,
	// so the lead over the clock is held instead (see g_histlead) and the depth
	// is what reaches it, rollback_history the ceiling. rollback_twoclock is
	// not its floor (WORLDWIDE.md 8.105): on a round trip shorter than it, the
	// tics past the newest input repeated it, a driver's next input made them
	// wrong, and a kept speculation was rebuilt at every change -- the start
	// of every WORLDWIDE race, run on a loopback. Needs rollback_cleancmds: without it the tic the applied
	// input is read from may hold this machine's own overwrite instead of the
	// server's.
	if (ahead > 0 && g_histmax > 0 && g_cleancmds)
	{
		const int32_t cap = (g_histmax > ahead) ? g_histmax : ahead;
		const tic_t now = I_GetTime();
		int32_t most = -1, want = 0, depth;

		K_RollbackMapHistory();

		for (i = 0; i <= (int32_t)splitscreen; i++)
		{
			if (g_histunacked[i] > most)
				most = g_histunacked[i];
		}

		g_histpasses++;

		if (g_histunacked[0] >= 0)
		{
			g_histmatched++;
			g_histunackedsum += (uint64_t)g_histunacked[0];
		}

		// The lead this pass asks for. With no match it asks for nothing, and
		// the held lead stands: falling back to rollback_twoclock would move
		// the drawn world by four tics or so, and back again on the next match.
		if (most >= 0)
			want = (int32_t)(g_histbase - now) + most;

		if (g_histhold == false || now - g_lastpassat > TICRATE)
		{
			// Start, or start again after a gap in the passes -- a map change,
			// a pause -- from what this pass asks for, or from the plain
			// speculation if it asks for nothing.
			g_histlead = (most >= 0) ? want : (int32_t)(frontier - now) + ahead;
			g_histpeak = g_histlead;
			g_histstepat = now;
			g_histhold = true;
		}
		else if (most >= 0 && want > g_histlead)
		{
			g_histlead = want;
			g_histpeak = want;
			g_histstepat = now;
			g_histrises++;
		}
		else
		{
			if (most >= 0 && want > g_histpeak)
				g_histpeak = want;

			// A second in which no pass asked for the whole lead: one tic down.
			if (now - g_histstepat >= TICRATE)
			{
				if (g_histpeak < g_histlead)
				{
					g_histlead--;
					g_histdrops++;
				}

				g_histpeak = (most >= 0) ? want : INT32_MIN;
				g_histstepat = now;
			}
		}

		depth = (int32_t)(now - frontier) + g_histlead;

		if (depth > cap)
		{
			depth = cap;
			g_histcapped++;
		}

		// As deep as the inputs in flight reach, one tic at least so there is
		// a speculation to keep.
		ahead = (depth > 1) ? depth : 1;

		g_histdepthsum += (uint64_t)ahead;
	}

	return ahead;
}

/** One speculated tic, from a frontier. With rollback_keepspec, also what a
  * later pass needs to keep it: its start saved (when asked), the leveltime and
  * inputs it ran from, and the checksum after it. */
static void K_KartFromWorld(int32_t slot, struct rollbackkart_t *k);

/** The karts at the start of a speculated tic, in a correction's terms. */
static void K_KeepNoteKarts(int32_t s)
{
	int32_t p;

	for (p = 0; p < MAXPLAYERS; p++)
	{
		g_keepkartok[s][p] = (playeringame[p] && players[p].mo != NULL
			&& P_MobjWasRemoved(players[p].mo) == false);

		if (g_keepkartok[s][p])
			K_KartFromWorld(p, &g_keepkart[s][p]);
	}
}

static int32_t K_LocalInputSource(tic_t tic)
{
	const int32_t who = g_localplayers[0];
	const int32_t unacked = g_histunacked[0];

	if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false)
		return KEEPSRC_NONE;

	if (tic < D_NeededTic() && g_cleancmds)
		return KEEPSRC_RECEIVED;

	if (g_histmax > 0 && unacked > 0 && tic >= g_histbase && tic - g_histbase < (tic_t)unacked)
		return KEEPSRC_REPLAYED;

	return KEEPSRC_GUESSED;
}

static void K_RunSpeculatedTic(tic_t frontier, dboolean savestart)
{
	const tic_t tic = gametic;
	const int32_t s = (int32_t)(tic % ROLLBACK_TICS);
	int32_t p;

	g_speclastpass++;

	if (g_keepspec)
	{
		if (savestart)
		{
			const precise_t at = I_GetPreciseTime();

			if (K_SaveGameState(tic) == false)
				g_specnosave++;

			g_saveus += K_PreciseToMicros(I_GetPreciseTime() - at);
		}

		g_keeptic[s] = tic;
		g_keeplevel[s] = leveltime;
		g_keeptaint[s] = false;
		K_KeepNoteKarts(s);
	}

	K_RollbackPredictInputs(tic, (int32_t)(tic - frontier));

	if (g_keepspec)
	{
		for (p = 0; p < MAXPLAYERS; p++)
		{
			g_keepin[s][p] = playeringame[p];

			if (playeringame[p])
				g_keepcmds[s][p] = netcmds[tic % BACKUPTICS][p];
		}

		g_keepsrc[s] = (uint8_t)K_LocalInputSource(tic);
	}
	else
	{
		K_RecordGuess(tic);
	}

	// The whole tic, through the live loop's own entry point. Driving
	// P_Ticker directly missed everything G_Ticker does around the
	// simulation, and that cost four separate bug hunts to learn once.
	{
		const precise_t ticat = I_GetPreciseTime();

		g_intic = true;
		G_Ticker(true);
		g_intic = false;
		K_NoteSpeculatedTic(I_GetPreciseTime() - ticat);
	}

	gametic++;
	g_spectics++;

	if (g_keepspec)
		g_keepafter[s] = Consistancy();
}

static void K_NoteDrawnOffset(void)
{
	// The drawn tic against the clock (see g_drawnoffset). A gap in the passes
	// starts the comparison again rather than counting as a jump.
	{
		const tic_t now = I_GetTime();
		const int32_t offset = (int32_t)(gametic - now);

		if (g_drawnvalid && now - g_lastpassat <= TICRATE)
		{
			g_drawnpasses++;

			if (offset != g_drawnoffset)
			{
				g_drawnjumps++;
				g_drawnjumptics += (uint32_t)((offset > g_drawnoffset)
					? offset - g_drawnoffset
					: g_drawnoffset - offset);
			}
		}

		g_drawnoffset = offset;
		g_drawnvalid = true;
		g_lastpassat = now;
	}
}

/** Console command: rollback_nullspec [0/1]
  *
  * Saves and restores the frontier every pass and speculates nothing, so the only
  * thing left running is the round trip through the archive. It answers one
  * question and no others: does restoring, on its own, inside the live loop,
  * change the world enough for the server to notice.
  */
static void Command_RollbackNullSpec_f(void)
{
	if (COM_Argc() > 1)
		g_nullspec = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_nullspec: %s\n",
		(g_nullspec
			? "on -- the frontier is saved and restored every pass, and nothing is speculated"
			: "off -- the speculation runs as usual"));
}

/** Console command: rollback_botsashuman [0/1]
  *
  * Client side. With it on, the speculation guesses each bot as it guesses a
  * remote person, by repeating its last input, instead of computing it
  * (g_botsashuman). Off by default.
  */
static void Command_RollbackBotsAsHuman_f(void)
{
	if (COM_Argc() > 1)
		g_botsashuman = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_botsashuman: %s\n",
		(g_botsashuman
			? "on -- the speculation guesses each bot as a remote person: its last input, repeated"
			: "off -- the speculation computes each bot's input from this machine's world"));
}

int32_t K_RollbackCorrectRate(void)
{
	if (g_correctrate > 0)
		return g_correctrate;   // set by hand: a measurement, and it wins

	return (K_WorldwideServer() ? WORLDWIDE_CORRECTRATE : 0);
}

dboolean K_RollbackCorrectSuppress(void)
{
	if (g_correctrate > 0)
		return g_correctsuppress;

	return K_WorldwideServer();
}

/** A key for one side of an event that means the same thing on every machine.
  *
  * Not mobjnum: that is handed out afresh by every P_SaveNetGame, and a client
  * taking a snapshot every tic renumbers constantly, so a hash over mobjnums
  * would differ between two machines that agreed about everything.
  */
static uint32_t K_EventKey(struct mobj_t *mo)
{
	const mobj_t *m = mo;

	if (m == NULL)
		return 0;

	if (m->player != NULL)
		return (uint32_t)(1 + (m->player - players));

	return 0x10000u + (uint32_t)m->type;
}

/** Names a mobj the way both machines would name it, for a log to be diffed. */
static const char *K_DescribeMobj(struct mobj_t *mo, char *buf, size_t len)
{
	const mobj_t *m = mo;

	if (m == NULL)
		snprintf(buf, len, "-");
	else if (m->player != NULL)
		snprintf(buf, len, "p%d", (int32_t)(m->player - players));
	else
		snprintf(buf, len, "t%d", (int32_t)m->type);

	return buf;
}

void K_RollbackNoteDamage(struct mobj_t *victim, struct mobj_t *inflictor,
	uint8_t damagetype)
{
	uint32_t key;

	if (gamestate != GS_LEVEL || K_RollbackReplaying())
		return;

	key = (K_EventKey(victim) * 251u) + (uint32_t)damagetype;

	g_livedamages++;
	g_livedamagehash = ((g_livedamagehash ^ key) * 16777619u)
		^ K_EventKey(inflictor);

	if (g_damagelog == true && g_damagelogged < ROLLBACK_DAMAGELOGMAX)
	{
		char v[32], f[32];

		g_damagelogged++;

		// The tic is the same number on both machines -- confirmed tics are
		// lockstep, whatever the wall clock says -- so these lines diff
		// directly, and the running hash marks where the two stories part even
		// when one side is missing an event rather than judging it differently.
		CONS_Printf("rollback_damage: tic %u #%u %s hit by %s type %u -- "
			"hash %08x" "\n",
			(uint32_t)gametic, g_livedamages,
			K_DescribeMobj(victim, v, sizeof v),
			K_DescribeMobj(inflictor, f, sizeof f),
			(uint32_t)damagetype, g_livedamagehash);
	}
}

void K_RollbackLiveDamages(uint32_t *count, uint32_t *hash)
{
	if (count != NULL)
		*count = g_livedamages;

	if (hash != NULL)
		*hash = g_livedamagehash;
}

/** Names a ticcmd's gameplay-relevant bytes, leaving out `latency`
  * (a local annotation about how old the sample was, not part of the
  * input) and the TICCMD_RECEIVED/TICCMD_BOT bits of `flags` (set
  * differently by construction on the two machines, not a disagreement
  * about what was pressed).
  */
static uint32_t K_InputKey(const ticcmd_t *cmd)
{
	uint32_t h = 2166136261u;

	h = (h ^ (uint8_t)cmd->forwardmove) * 16777619u;
	h = (h ^ (uint16_t)cmd->turning) * 16777619u;
	h = (h ^ (uint16_t)cmd->angle) * 16777619u;
	h = (h ^ (uint16_t)cmd->throwdir) * 16777619u;
	h = (h ^ (uint16_t)cmd->aiming) * 16777619u;
	h = (h ^ cmd->buttons) * 16777619u;
	h = (h ^ (uint8_t)cmd->bot.turnconfirm) * 16777619u;
	h = (h ^ (uint8_t)cmd->bot.spindashconfirm) * 16777619u;
	h = (h ^ (uint8_t)cmd->bot.itemconfirm) * 16777619u;

	return h;
}

/** Folds one player's ticcmd into the running input tally.
  *
  * Called once per in-game player, from the one place both client and
  * server run every confirmed tic: the shared TryRunTics loop, right
  * where it hands netcmds[] to P_Ticker for real. Not gated on
  * K_RollbackReplaying the way the damage tally is -- this loop never
  * runs during a speculation or a resimulation check, so the guard would
  * never fire, and a guard that can never fire is a claim about the code
  * that nothing tests.
  */
void K_RollbackNoteInput(uint32_t tic, uint8_t slot, const ticcmd_t *cmd)
{
	uint32_t key;

	if (gamestate != GS_LEVEL)
		return;

	key = ((uint32_t)slot * 2654435761u) ^ K_InputKey(cmd);

	g_liveinputs++;
	g_liveinputhash = ((g_liveinputhash ^ key) * 16777619u) ^ tic;

	if (g_inputlog == true && g_inputlogged < ROLLBACK_INPUTLOGMAX)
	{
		g_inputlogged++;

		CONS_Printf("rollback_input: tic %u p%d fwd %d turn %d angle %d "
			"btn %04x -- hash %08x" "\n",
			tic, (int32_t)slot, (int32_t)cmd->forwardmove, (int32_t)cmd->turning,
			(int32_t)cmd->angle, (unsigned)cmd->buttons, g_liveinputhash);
	}
}

void K_RollbackLiveInputs(uint32_t *count, uint32_t *hash)
{
	if (count != NULL)
		*count = g_liveinputs;

	if (hash != NULL)
		*hash = g_liveinputhash;
}

void K_RollbackNoteSample(int32_t realtics)
{
	const ticcmd_t *now, *before;

	// R1: every sample, in a level or not, so the ring keeps step with the
	// history's ages (CreateNewLocalCMD ages it once a call).
	g_samplehead++;
	g_samplerealtics[g_samplehead % MAXGENTLEMENDELAY] = (realtics > 1) ? realtics : 1;

	if (gamestate != GS_LEVEL)
		return;

	g_samples++;

	if (realtics > 1)
	{
		g_samplelate++;
		g_samplelatetics += (uint32_t)(realtics - 1);

		if (g_cascadelog)
		{
			CONS_Printf("rollback_cascade: real tic %u, leveltime %u -- a sample after %d real "
				"tics\n", (unsigned)I_GetTime(), (unsigned)leveltime, (int)realtics);
		}
	}

	now = D_LocalTiccmdAge(0, 0);
	before = D_LocalTiccmdAge(0, 1);

	if (now != NULL && before != NULL && now->latency == before->latency)
		g_samplesamestamp++;
}

void K_RollbackStallPoint(void)
{
	if (g_stallms <= 0 || gamestate != GS_LEVEL)
		return;

	// A new level: its clock starts again, and so does the count to the next.
	if (leveltime < g_stalllast)
		g_stallnext = (tic_t)g_stallperiod;

	g_stalllast = leveltime;

	if (leveltime < g_stallnext)
		return;

	I_Sleep((uint32_t)g_stallms);
	g_stalls++;

	CONS_Printf("rollback_stall: the loop held %d ms at leveltime %u (real tic %u)\n",
		(int)g_stallms, (unsigned)leveltime, (unsigned)I_GetTime());

	if (g_stallperiod > 0)
		g_stallnext = leveltime + (tic_t)g_stallperiod;
	else
		g_stallms = 0;
}

void K_RollbackNoteFiling(int32_t player, dboolean shifted, dboolean overwrote)
{
	if (player < 0 || player >= MAXPLAYERS || gamestate != GS_LEVEL)
		return;

	g_filed[player]++;

	if (shifted)
		g_filedshifted[player]++;

	if (overwrote)
		g_filedover[player]++;
}

void K_RollbackNoteRepeat(int32_t player)
{
	if (player < 0 || player >= MAXPLAYERS || gamestate != GS_LEVEL)
		return;

	g_filedrepeat[player]++;
}

void K_RollbackNoteRelabel(int32_t delta, dboolean fromhost, dboolean inlevel)
{
	const int32_t split = fromhost
		? (inlevel ? RELABEL_HOST_LEVEL : RELABEL_HOST_OTHER)
		: (inlevel ? RELABEL_REMOTE_LEVEL : RELABEL_REMOTE_OTHER);

	g_relabelsplitcount[split]++;

	if (delta >= -ROLLBACK_RELABELSPAN && delta <= ROLLBACK_RELABELSPAN)
		g_relabelsplit[split][delta + ROLLBACK_RELABELSPAN]++;

	if (g_relabelcount == 0 || delta < g_relabelmin)
		g_relabelmin = delta;

	if (g_relabelcount == 0 || delta > g_relabelmax)
		g_relabelmax = delta;

	g_relabelcount++;
	g_relabelsum += delta;

	if (delta >= -ROLLBACK_RELABELSPAN && delta <= ROLLBACK_RELABELSPAN)
		g_relabelseen[delta + ROLLBACK_RELABELSPAN]++;
	else
		g_relabelfar++;
}

/** Console command: rollback_relabel
  *
  * Server side. How far PT_CLIENTCMD's own faketic bookkeeping moved an
  * arriving ticcmd from the tic the client tagged it with. A single repeated
  * value is a constant label offset -- harmless, the two clocks simply count
  * from different zeroes. A spread says the offset itself moves from packet
  * to packet, which is the discriminator this exists to measure.
  */
static void Command_RollbackRelabel_f(void)
{
	int32_t off;

	if (g_relabelcount == 0)
	{
		CONS_Printf("rollback_relabel: no PT_CLIENTCMD packets relabelled yet "
			"-- server side only, and needs a real remote client" "\n");
		return;
	}

	CONS_Printf("rollback_relabel: %u packets, faketic - realstart ranged %d "
		"to %d, mean %d.%02d" "\n",
		g_relabelcount, g_relabelmin, g_relabelmax,
		(int32_t)(g_relabelsum / (int64_t)g_relabelcount),
		(int32_t)(((g_relabelsum < 0 ? -g_relabelsum : g_relabelsum) * 100
			/ (int64_t)g_relabelcount) % 100));

	if (g_relabelfar > 0)
	{
		CONS_Printf("rollback_relabel: %u further out than %d tics either way, "
			"not shown below" "\n",
			g_relabelfar, ROLLBACK_RELABELSPAN);
	}

	for (off = -ROLLBACK_RELABELSPAN; off <= ROLLBACK_RELABELSPAN; off++)
	{
		const uint32_t seen = g_relabelseen[off + ROLLBACK_RELABELSPAN];

		if (seen == 0)
			continue;

		CONS_Printf("rollback_relabel:   %+d tics, %u times" "\n", off, seen);
	}

	{
		static const char *const names[4] = {
			"host, in a race", "host, outside a race",
			"remote, in a race", "remote, outside a race"
		};
		int32_t s;

		for (s = 0; s < 4; s++)
		{
			char line[512];
			size_t used;

			if (g_relabelsplitcount[s] == 0)
				continue;

			used = (size_t)snprintf(line, sizeof line, "rollback_relabel: [%s] %u packets:",
				names[s], g_relabelsplitcount[s]);

			for (off = -ROLLBACK_RELABELSPAN; off <= ROLLBACK_RELABELSPAN && used < sizeof line; off++)
			{
				const uint32_t seen = g_relabelsplit[s][off + ROLLBACK_RELABELSPAN];

				if (seen != 0)
					used += (size_t)snprintf(line + used, sizeof line - used, " %+d x%u", off, seen);
			}

			CONS_Printf("%s\n", line);
		}
	}

	{
		int32_t p;

		for (p = 0; p < MAXPLAYERS; p++)
		{
			if (g_filed[p] == 0 && g_filedrepeat[p] == 0)
				continue;

			CONS_Printf("rollback_relabel: p%d -- %u samples filed, %u a tic late because "
				"the slot was taken, %u over one already there; %u tics got none and "
				"repeated the one before\n",
				p, g_filed[p], g_filedshifted[p], g_filedover[p], g_filedrepeat[p]);
		}
	}
}

/** Appends " name mine/theirs" to buf, but only when the two differ.
  *
  * Only the fields that differ get printed. The previous instrument on this
  * branch reported fifteen hundred differing fields per check and taught nobody
  * anything, because a wall of text in which everything is listed is a wall of
  * text in which the one that matters is invisible.
  */
static void K_NoteDiff(char *buf, size_t len, const char *name,
	int32_t mine, int32_t theirs)
{
	const size_t at = strlen(buf);

	if (mine == theirs || at + 48 >= len)
		return;

	snprintf(buf + at, len - at, " %s %d/%d", name, mine, theirs);
}

void K_RollbackNoteServerState(uint32_t tic, const struct rollbackkart_t *karts,
	uint8_t n, uint32_t damages, uint32_t damagehash,
	uint32_t inputs, uint32_t inputhash)
{
	if (n > MAXPLAYERS)
		n = MAXPLAYERS;

	// Newest wins. An older correction that overtook a newer one describes a
	// world this client has already left, and applying it would move the karts
	// backwards -- which is the one thing a correction must never do.
	if (g_correctpending && g_correcttic > tic)
		return;

	g_correcttic = tic;
	g_correctn = n;
	memcpy(g_correctkart, karts, n * sizeof (struct rollbackkart_t));
	g_srvdamages = damages;
	g_srvdamagehash = damagehash;
	g_srvinputs = inputs;
	g_srvinputhash = inputhash;

	g_correctpending = true;
	g_statecorrections++;
}

/** The distance between two fixed-point coordinates, in 1/65536 of a unit.
  *
  * Kept in fracunits rather than units because the first divergence measured on
  * this branch was seven thousandths of a unit, and a figure rounded to units
  * reports that as zero. In 64 bits so a difference of two coordinates at
  * opposite ends of a big map cannot wrap.
  */
static uint64_t K_FracError(int32_t a, int32_t b)
{
	const int64_t d = (int64_t)a - (int64_t)b;

	return (uint64_t)((d < 0) ? -d : d);
}

static const char *K_DescribeFrac(uint64_t frac, char *buf, size_t len);

static void K_UseCorrection(const struct rollbackkart_t *noted, const dboolean *notedok);
static const char *K_DescribeFrac(uint64_t frac, char *buf, size_t len);

void K_RollbackApplyServerState(void)
{
	if (g_correctpending == false)
		return;

	if (gamestate != GS_LEVEL)
	{
		g_correctpending = false;
		return;
	}

	// A correction names the tic the *server* was on when it sent it, and that is
	// ahead of the tic this client has confirmed by the trip time -- six tics at
	// 171 ms. Comparing the two worlds directly would measure six tics of
	// perfectly legitimate kart motion, which at racing speed is over a thousand
	// units and buries the fraction of a unit this exists to find. It would also
	// mean applying it teleported every kart into its own future.
	//
	// So a correction waits here until the confirmed clock reaches its tic. The
	// world it is compared against is then the same tic on both machines, which
	// is the only comparison that means anything.
	if (g_correcttic > (uint32_t)gametic)
		return;    // still pending; the next pass will come back to it

	g_correctpending = false;

	if (g_correcttic < (uint32_t)gametic)
	{
		// The loop ran several tics in one pass and stepped over the tic this
		// correction describes. Dropped rather than measured against the wrong
		// world, and counted, because a sample rate that quietly collapses looks
		// exactly like a drift that quietly went away.
		g_correctmissed++;
		return;
	}

	g_correctused++;

	K_UseCorrection(NULL, NULL);
}

/** A kart as a correction describes it, taken from this machine's world. */
static void K_KartFromWorld(int32_t slot, struct rollbackkart_t *k)
{
	const player_t *p = &players[slot];

	memset(k, 0, sizeof (*k));
	k->slot = (uint8_t)slot;
	k->x = p->mo->x;
	k->y = p->mo->y;
	k->z = p->mo->z;
	k->momx = p->mo->momx;
	k->momy = p->mo->momy;
	k->momz = p->mo->momz;
	k->angle = p->mo->angle;
	k->hitlag = p->mo->hitlag;
	k->rings = p->rings;
	k->itemtype = p->itemtype;
	k->itemamount = p->itemamount;
	k->spinouttimer = p->spinouttimer;
	k->nocontrol = p->nocontrol;
	k->flashing = p->flashing;
	k->spinouttype = p->spinouttype;
	k->tumbleBounces = p->tumbleBounces;
	k->wipeoutslow = p->wipeoutslow;
	k->justbumped = p->justbumped;
	k->offroad = p->offroad;
	k->speed = p->speed;
}

/** Every field a correction carries, the slot aside. */
static dboolean K_KartSame(const struct rollbackkart_t *a, const struct rollbackkart_t *b)
{
	return (a->x == b->x && a->y == b->y && a->z == b->z
		&& a->momx == b->momx && a->momy == b->momy && a->momz == b->momz
		&& a->angle == b->angle && a->hitlag == b->hitlag
		&& a->rings == b->rings && a->itemtype == b->itemtype && a->itemamount == b->itemamount
		&& a->spinouttimer == b->spinouttimer && a->nocontrol == b->nocontrol
		&& a->flashing == b->flashing && a->spinouttype == b->spinouttype
		&& a->tumbleBounces == b->tumbleBounces && a->wipeoutslow == b->wipeoutslow
		&& a->justbumped == b->justbumped && a->offroad == b->offroad && a->speed == b->speed);
}

/** What a correction does once it lands on its own tic: the baselines, the
  * measurement against the world it names, and putting back the karts that are
  * not where the server has them.
  *
  * \param noted NULL for the live world. Otherwise the karts a speculation
  *        kept across the pass noted at the start of that tic (the world itself
  *        is further on): measured, never applied -- the caller only passes
  *        them when every kart matches.
  */
static void K_UseCorrection(const struct rollbackkart_t *noted, const dboolean *notedok)
{
	const dboolean applying = (noted == NULL && g_correctapply);
	uint8_t k;


	// The first correction that lands on its own tic sets the baseline.
	//
	// Both machines measure at the same instant -- the packet names the tic the
	// server was about to run, and it is held here until this clock reaches that
	// same tic -- so at this boundary the two tallies differ only by the events
	// that happened before this client joined. Adopting the server's numbers
	// once turns a pair of counts with an unknown offset into an equality test:
	// from here the two fold the same events in the same order, so equal means
	// they agree, and the first tic where they part is the divergence.
	if (g_damagebase == false)
	{
		g_damagebase = true;
		g_livedamages = g_srvdamages;
		g_livedamagehash = g_srvdamagehash;

		CONS_Printf("rollback_damage: baseline at tic %u -- adopting the "
			"server's %u events (hash %08x); equal from here means agreement"
			"\n",
			g_correcttic, g_srvdamages, g_srvdamagehash);
	}

	// Same reasoning as the damage baseline, same boundary tic: both
	// machines have folded a different number of confirmed tics' worth of
	// input by the time this client joined, so the counts start with an
	// unknown offset that adopting the server's numbers once turns into an
	// equality test.
	if (g_inputbase == false)
	{
		g_inputbase = true;
		g_liveinputs = g_srvinputs;
		g_liveinputhash = g_srvinputhash;

		CONS_Printf("rollback_input: baseline at tic %u -- adopting the "
			"server's %u ticcmds (hash %08x); equal from here means agreement"
			"\n",
			g_correcttic, g_srvinputs, g_srvinputhash);
	}

	// P_MoveOrigin goes through P_CheckPosition, which parks the thing it is
	// testing in g_tm.thing and leaves it there for the caller. The ticker
	// brackets its own work with P_MapStart/P_MapEnd, and this runs outside the
	// ticker -- so it has to bracket itself, or the next legitimate P_MapStart
	// dies on "g_tm.thing set!". Which is exactly what the first run that
	// applied a correction did, within seconds of the first one arriving.
	//
	// Only around the applying path: the measuring path touches nothing, and the
	// drift figures already taken with it should stay comparable.
	if (applying)
		P_MapStart();

	for (k = 0; k < g_correctn; k++)
	{
		const struct rollbackkart_t *c = &g_correctkart[k];
		struct rollbackkart_t here;
		player_t *p;
		uint64_t err;
		char e[64];
		char st[256];

		if (c->slot >= MAXPLAYERS || playeringame[c->slot] == false)
			continue;

		p = &players[c->slot];

		if (noted != NULL)
		{
			if (notedok[c->slot] == false)
				continue;

			here = noted[c->slot];
		}
		else
		{
			if (p->mo == NULL || P_MobjWasRemoved(p->mo))
				continue;

			K_KartFromWorld(c->slot, &here);
		}

		err = K_FracError(here.x, c->x)
			+ K_FracError(here.y, c->y)
			+ K_FracError(here.z, c->z);

		g_driftsamples++;
		g_driftsum += err;

		if (err > g_driftmax)
		{
			g_driftmax = err;
			g_driftworst = (int32_t)c->slot;
		}

		// Which STATE differs, not which kinematics. A kart's momentum was
		// seen being re-derived wrong within four tics of being handed the
		// server's value, twenty tics running, so the cause is a state this
		// machine holds and the server does not. These name it -- on every
		// sample now, not only past a spike (see g_driftstates).
		st[0] = 0;

		K_NoteDiff(st, sizeof st, "spinout", here.spinouttimer, c->spinouttimer);
		K_NoteDiff(st, sizeof st, "spintype", here.spinouttype, c->spinouttype);
		K_NoteDiff(st, sizeof st, "noctl", here.nocontrol, c->nocontrol);
		K_NoteDiff(st, sizeof st, "flash", here.flashing, c->flashing);
		K_NoteDiff(st, sizeof st, "tumble", here.tumbleBounces, c->tumbleBounces);
		K_NoteDiff(st, sizeof st, "wipeout", here.wipeoutslow, c->wipeoutslow);
		K_NoteDiff(st, sizeof st, "bumped", here.justbumped, c->justbumped);
		K_NoteDiff(st, sizeof st, "offroad", here.offroad, c->offroad);
		K_NoteDiff(st, sizeof st, "speed", here.speed, c->speed);
		K_NoteDiff(st, sizeof st, "hitlag", here.hitlag, c->hitlag);
		K_NoteDiff(st, sizeof st, "item", here.itemtype, c->itemtype);

		if (st[0] != 0)
		{
			if (g_driftstates == 0)
				g_driftstatefirst = g_correcttic;

			g_driftstates++;
		}

		if (err >= ROLLBACK_SPIKE && g_driftspikes < ROLLBACK_SPIKEMAX)
		{
			g_driftspikes++;

			CONS_Printf("rollback_drift: SPIKE tic %u p%d off by %s -- "
				"mom here (%d,%d,%d) server (%d,%d,%d) -- damage %u/%u -- "
				"differs:%s" "\n",
				g_correcttic, (int32_t)c->slot,
				K_DescribeFrac(err, e, sizeof e),
				here.momx, here.momy, here.momz,
				c->momx, c->momy, c->momz,
				g_livedamages, g_srvdamages,
				(st[0] != 0 ? st : " nothing -- kinematics only"));
		}
		else if (st[0] != 0 && g_driftstates <= ROLLBACK_STATEMAX)
		{
			CONS_Printf("rollback_drift: STATE tic %u p%d off by %s -- "
				"differs:%s" "\n",
				g_correcttic, (int32_t)c->slot,
				K_DescribeFrac(err, e, sizeof e), st);
		}

		if (applying == false)
			continue;

		// A kart already exactly where the server has it is left alone. Putting
		// it back anyway was not free: P_MoveOrigin re-runs P_CheckPosition and
		// relinks the kart at the head of its blockmap and sector chains, which
		// the server -- never moving a kart -- does not do, every kart every
		// correction. Collision order then differs between the two machines
		// (WORLDWIDE.md 8.75).
		if (K_KartSame(&here, c))
		{
			g_driftsame++;
			continue;
		}

		// P_MoveOrigin rather than P_SetOrigin: it keeps the interpolation
		// origin, so the kart is drawn sliding to where the server says rather
		// than appearing there. It can also refuse, when the destination is
		// blocked -- and a correction that shoves a kart into geometry is worse
		// than one that is a tic late, so the refusal is counted, not forced.
		if (P_MoveOrigin(p->mo, c->x, c->y, c->z) == false)
		{
			g_driftrefused++;
		}
		else
		{
			g_driftmoved++;
		}

		p->mo->momx = c->momx;
		p->mo->momy = c->momy;
		p->mo->momz = c->momz;
		p->mo->angle = c->angle;
		p->mo->hitlag = c->hitlag;

		p->rings = c->rings;
		p->itemtype = c->itemtype;
		p->itemamount = c->itemamount;
	}

	if (applying)
		P_MapEnd();
}

/** For a speculation kept across a pass: a correction due at the frontier,
  * against the karts the speculation noted at the start of that tic. Consumed
  * if every kart already matches -- measured, nothing to put back, nothing to
  * rebuild. False if one differs: the pass rebuilds and the correction lands on
  * the live world as usual. A correction for a tic already passed is dropped,
  * as the loop would. */
static dboolean K_KeepUseCorrection(tic_t frontier)
{
	const int32_t s = (int32_t)(frontier % ROLLBACK_TICS);
	uint8_t k;

	if (g_correctpending == false || g_correcttic > (uint32_t)frontier)
		return true;    // nothing due

	if (g_correcttic < (uint32_t)frontier)
	{
		g_correctpending = false;
		g_correctmissed++;
		return true;
	}

	if (g_keeptic[s] != frontier)
		return false;

	for (k = 0; k < g_correctn; k++)
	{
		const struct rollbackkart_t *c = &g_correctkart[k];

		if (c->slot >= MAXPLAYERS || playeringame[c->slot] == false)
			continue;

		if (g_keepkartok[s][c->slot] == false || K_KartSame(&g_keepkart[s][c->slot], c) == false)
			return false;
	}

	g_correctpending = false;
	g_correctused++;
	g_keepcorrnoop++;

	K_UseCorrection(g_keepkart[s], g_keepkartok[s]);
	return true;
}

/** Prints a frac count as units with three decimals, into a caller's buffer. */
static const char *K_DescribeFrac(uint64_t frac, char *buf, size_t len)
{
	snprintf(buf, len, "%u.%03u units",
		(uint32_t)(frac / FRACUNIT),
		(uint32_t)(((frac % FRACUNIT) * 1000) / FRACUNIT));

	return buf;
}

/** Console command: rollback_correct [tics]
  *
  * Server side. Asks the server to send every client a light state correction
  * every N tics. Zero turns it off, which is stock behaviour: the only
  * correction is then the full-state resend -- unless the server runs
  * WORLDWIDE mode, which sends one every WORLDWIDE_CORRECTRATE tics in place
  * of the resend whenever this is 0. A rate set here wins over the mode's.
  */
static void Command_RollbackCorrect_f(void)
{
	if (COM_Argc() > 1)
	{
		const int32_t want = atoi(COM_Argv(1));

		g_correctrate = (want > 0) ? want : 0;

		// Whether corrections *replace* the full-state resend or merely run
		// beside it. Two separate questions, and this branch has repeatedly
		// answered one of them with a run that changed both.
		//
		//   rollback_correct N 0 -- send them, keep resending too. The control:
		//                           the resync count stays comparable with every
		//                           measurement taken before the channel existed.
		//   rollback_correct N   -- send them instead of resending. The change.
		g_correctsuppress = (COM_Argc() > 2) ? (atoi(COM_Argv(2)) != 0) : true;
	}

	if (g_correctrate > 0)
	{
		CONS_Printf("rollback_correct: sending a light correction every %d tics "
			"(%d a second), and %s\n", g_correctrate, TICRATE / g_correctrate,
			(g_correctsuppress
				? "NOT resending the full state on a mismatch"
				: "still resending the full state on a mismatch"));
	}
	else if (K_WorldwideServer())
	{
		CONS_Printf("rollback_correct: not set -- WORLDWIDE mode sends a light "
			"correction every %d tics, NOT resending the full state on a mismatch\n",
			WORLDWIDE_CORRECTRATE);
	}
	else
	{
		CONS_Printf("rollback_correct: off -- the only correction is the stock "
			"full-state resend\n");
	}
}

/** Console command: rollback_drift [0/1]
  *
  * Client side. Reports how far this client's confirmed world was from the
  * server's, as measured by every correction that arrived. The argument decides
  * whether the corrections are also applied; off by default, because measuring
  * and correcting in the same run gives a number about neither.
  */
/** Console command: rollback_inputlog [0/1]
  *
  * Run on BOTH machines. Prints one line per confirmed tic's ticcmd for
  * every player, with a running hash -- the input-side counterpart of
  * rollback_damagelog. The two logs diff tic for tic; the first hash that
  * parts names the tic and the player.
  */
static void Command_RollbackInputLog_f(void)
{
	if (COM_Argc() > 1)
	{
		g_inputlog = (atoi(COM_Argv(1)) != 0);
		g_inputlogged = 0;
	}

	CONS_Printf("rollback_inputlog: %s -- %u ticcmds so far, hash %08x" "\n",
		(g_inputlog ? "on" : "off"), g_liveinputs, g_liveinputhash);
}

/** Console command: rollback_damagelog [0/1]
  *
  * Run on BOTH machines. Prints one line per damage outcome resolved on a
  * confirmed tic, with a running hash. The two logs diff tic for tic, and the
  * first line where the hashes part is the first hit the two machines judged
  * differently -- the event, rather than the position error it shows up as.
  */
static void Command_RollbackDamageLog_f(void)
{
	if (COM_Argc() > 1)
	{
		g_damagelog = (atoi(COM_Argv(1)) != 0);
		g_damagelogged = 0;
	}

	CONS_Printf("rollback_damagelog: %s -- %u events so far, hash %08x" "\n",
		(g_damagelog ? "on" : "off"), g_livedamages, g_livedamagehash);
}

static void Command_RollbackDrift_f(void)
{
	char a[64], b[64];

	if (COM_Argc() > 1)
	{
		g_correctapply = (atoi(COM_Argv(1)) != 0);

		g_statecorrections = g_correctused = g_correctmissed = g_driftsamples = 0;
		g_driftspikes = 0;
		g_driftstates = g_driftstatefirst = 0;
		g_driftsum = g_driftmax = 0;
		g_driftworst = -1;
		g_driftmoved = g_driftrefused = g_driftsame = 0;
	}

	// Who was on the grid, and how each kart's input is obtained.
	//
	// The report has named p8 as the worst kart in two races running without
	// ever saying what p8 is, and the answer changes the diagnosis completely:
	// a client predicts a *person* by repeating their last input and a
	// *bot-driven* kart by computing one from its own world, and only the
	// second can disagree with the server about what the kart decided to do.
	// K_PlayerUsesBotMovement is true for `bot` and also for `exiting`, so a
	// kart that has finished the race switches mechanism mid-measurement.
	{
		char grid[256];
		int32_t g;

		grid[0] = 0;

		for (g = 0; g < MAXPLAYERS; g++)
		{
			char one[24];

			if (playeringame[g] == false)
				continue;

			snprintf(one, sizeof one, " p%d%s%s%s", g,
				(players[g].bot ? "-bot" : ""),
				(players[g].exiting ? "-exiting" : ""),
				(g == g_localplayers[0] ? "-LOCAL" : ""));

			strlcat(grid, one, sizeof grid);
		}

		CONS_Printf("rollback_drift: grid --%s%s\n", grid,
			(g_botsashuman ? " -- the bots guessed as people (rollback_botsashuman)" : ""));
	}

	CONS_Printf("rollback_drift: %s\n",
		(g_correctapply
			? "applying -- karts are moved to where the server says"
			: "measuring only -- nothing is moved"));

	CONS_Printf("rollback_drift: damage events on confirmed tics -- here %u "
		"(hash %08x), server %u (hash %08x). The offset from the join is not the "
		"signal; a CHANGE in it is, and rollback_damagelog dates it." "\n",
		g_livedamages, g_livedamagehash, g_srvdamages, g_srvdamagehash);

	CONS_Printf("rollback_drift: ticcmds on confirmed tics -- here %u (hash "
		"%08x), server %u (hash %08x). Equal means every player ran the same "
		"input on the same tic; rollback_inputlog names the first tic that "
		"does not." "\n",
		g_liveinputs, g_liveinputhash, g_srvinputs, g_srvinputhash);

	if (g_driftspikes > 0)
	{
		CONS_Printf("rollback_drift: %u samples past a tenth of a kart%s" "\n",
			g_driftspikes,
			(g_driftspikes >= ROLLBACK_SPIKEMAX ? " (printing stopped there)" : ""));
	}

	CONS_Printf("rollback_drift: %u corrections received, %u measured on the tic "
		"they name, %u stepped over, %u kart samples\n",
		g_statecorrections, g_correctused, g_correctmissed, g_driftsamples);

	if (g_driftsamples == 0)
	{
		CONS_Printf("rollback_drift: nothing measured -- is rollback_correct on "
			"at the server?\n");
		return;
	}

	CONS_Printf("rollback_drift: mean %s, worst %s on p%d\n",
		K_DescribeFrac(g_driftsum / g_driftsamples, a, sizeof a),
		K_DescribeFrac(g_driftmax, b, sizeof b),
		g_driftworst);

	// Printed at 0 too: "no state differed" is a reading, and its absence
	// would look the same as an exe that does not count.
	if (g_driftstates > 0)
	{
		CONS_Printf("rollback_drift: %u of %u kart samples had a state field "
			"differing, the first at tic %u%s\n",
			g_driftstates, g_driftsamples, g_driftstatefirst,
			(g_driftstates > ROLLBACK_STATEMAX ? " (printing stopped there)" : ""));
	}
	else
	{
		CONS_Printf("rollback_drift: 0 of %u kart samples had a state field "
			"differing\n", g_driftsamples);
	}

	if (g_correctapply)
	{
		CONS_Printf("rollback_drift: %u karts put back, %u refused because the "
			"destination was blocked, %u already where the server had them\n",
			g_driftmoved, g_driftrefused, g_driftsame);
	}
}

/** Console command: rollback_twoclock [tics]
  *
  * The pivot, behind its own switch and off by default. Mutually exclusive with
  * rollback_loop by construction rather than by checking: one advances the
  * authoritative clock and the other refuses to.
  */
/** Sets rollback_twoclock's depth, 0 for off, and zeroes its counts.
  * Shared by the command and by WORLDWIDE mode. */
static void K_SetTwoClock(int32_t want)
{
	// Turning it off leaves a speculation standing, and the world would keep
	// it for good. Put the confirmed world back on the way out.
	if (want <= 0 && g_speculated)
		K_RollbackUnspeculate();

	g_twoclock = (want > 0) ? want : 0;

	if (g_twoclock > 0)
	{
		// The old loop hoists gametic; this one refuses to. Running both
		// would be two answers to the same question.
		g_loopahead = 0;
		g_keeping = true;
	}

	g_specpasses = g_spectics = g_specstranded = g_specnosave = g_suppressedxcmds = 0;
	g_unspecus = g_specus = 0;
	K_ResetPassCosts();
}

static void Command_RollbackTwoClock_f(void)
{
	if (COM_Argc() > 1)
		K_SetTwoClock(atoi(COM_Argv(1)));

	CONS_Printf("rollback_twoclock: %d tics of speculation on top of the confirmed world%s\n",
		g_twoclock,
		(g_nullspec ? " -- but NULL SPECULATION is on, so none of them run" : ""));
	CONS_Printf("rollback_twoclock: %u speculations built, %u tics run by them, "
		"%u could not be saved, %u left the world stranded\n",
		g_specpasses, g_spectics, g_specnosave, g_specstranded);
	CONS_Printf("rollback_twoclock: %u us putting the world back, %u us running it "
		"forward -- %u us a pass, against 28571 for a whole tic\n",
		g_unspecus, g_specus,
		(uint32_t)(g_specpasses ? ((g_unspecus + g_specus) / g_specpasses) : 0));

	CONS_Printf("rollback_twoclock: %u messages were refused because a speculated "
		"tic raised them -- the archive cannot take a sent message back\n",
		g_suppressedxcmds);

	K_ReportPassCosts();
}

/** Fills a tic's inputs with the last thing each player was known to be doing.
  *
  * Repeat-last is the standard prediction and the right first one: it is correct
  * whenever nobody changed what they were holding, which is most tics. The
  * TICCMD_RECEIVED flag is cleared on the copy, because this input did not
  * arrive -- it was guessed, and p_user reads that flag to decide how much to
  * trust the angle it came with.
  */
static void K_RollbackCountReceivedWrites(tic_t tic)
{
	int32_t i;

	for (i = 0; i <= (int32_t)splitscreen; i++)
	{
		const int32_t who = g_localplayers[i];
		const ticcmd_t *mine, *server;

		if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false)
			continue;

		mine = D_LocalTiccmd((uint8_t)i);
		server = &netcmds[tic % BACKUPTICS][who];

		g_recvwrites++;

		if (mine->forwardmove != server->forwardmove
			|| mine->turning != server->turning
			|| mine->angle != server->angle
			|| mine->throwdir != server->throwdir
			|| mine->aiming != server->aiming
			|| mine->buttons != server->buttons)
		{
			g_recvchanged++;
		}
	}
}

void K_RollbackPredictInputs(tic_t tic, int32_t ahead)
{
	int32_t i;

	// A tic below neededtic has already arrived, with the server's input for
	// every player in it. In two-clock mode the speculation starts on such tics:
	// the netticbuffer reserve at the end of TryRunTics' loop only stands down for
	// the old loop, so the confirmed loop stops short of neededtic. The local
	// write below then replaced the server's input for this machine's own player
	// with the one held *now*, and the next pass ran that tic as confirmed on it
	// (WORLDWIDE.md 8.31). rollback_cleancmds leaves such tics exactly as they
	// arrived.
	if (tic < D_NeededTic())
	{
		K_RollbackCountReceivedWrites(tic);

		if (g_cleancmds)
			return;
	}

	// Counted, because "the loop is on" and "the loop is doing anything" are two
	// different claims and only one of them was ever printed. A prediction that
	// never fires and a detector that never sees look identical from the outside.
	g_predicted++;

	if (ahead > g_furthestahead)
		g_furthestahead = ahead;

	// Your own input is not a guess, and this is the half of a rollback that
	// removes input lag. The client runs the tic on what you are holding *now*,
	// straight out of D_LocalTiccmd, and predicts only what it cannot know --
	// everyone else. Without this the loop still waits a round trip for your own
	// button to come back from the server before it does anything with it, which
	// is precisely the delay rollback exists to remove, and a played race said so
	// in one sentence: still input lag.
	//
	// TICCMD_RECEIVED is set and it is not a lie: this is the genuine input, not
	// a repeat of an older one, and p_user reads that flag to decide how far to
	// trust the angle beside it. Except where R1 gives a sample a tic past its
	// first: the server repeats it there, with the flag cleared, and so does
	// the speculation (WORLDWIDE.md 8.92).
	for (i = 0; i <= (int32_t)splitscreen; i++)
	{
		const int32_t who = g_localplayers[i];
		dboolean received;

		if (who < 0 || who >= MAXPLAYERS || playeringame[who] == false)
			continue;

		netcmds[tic % BACKUPTICS][who] = *K_RollbackLocalCmdFor((uint8_t)i, tic, &received);

		if (received)
			netcmds[tic % BACKUPTICS][who].flags |= TICCMD_RECEIVED;
		else
			netcmds[tic % BACKUPTICS][who].flags &= ~TICCMD_RECEIVED;

		// Kept so that when the server sends this same input back, the arrival
		// can say which of our samples it was and what tic we spent it on.
		if (i == 0)
			K_RollbackTrailLocal(tic, &netcmds[tic % BACKUPTICS][who]);
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		ticcmd_t *to = &netcmds[tic % BACKUPTICS][i];

		if (playeringame[i] == false)
			continue;

		if ((to->flags & TICCMD_RECEIVED) != 0)
			continue;   // the real thing arrived first; nothing to guess

		// A bot's input is not unknown either. It is computed from the world by
		// K_BuildBotTiccmd, and this machine has a world -- so compute it rather
		// than guess it. Repeat-last is a decent guess about a person, who holds
		// a button for several tics at a time; it is a terrible one about a bot,
		// which recomputes its angle and its confirmations every single tic. On a
		// grid of fifteen bots that made every predicted tic wrong, every real
		// input arrive as a contradiction, and the predicted world run away hard
		// enough that the local input built from it was nonsense by the time the
		// server saw it.
		//
		// Safe to call here: the only P_Random in k_bot.cpp is in
		// K_UpdateMatchRaceBots, which picks a skin when a bot is created.
		// K_BuildBotTiccmd itself draws nothing, so predicting with it cannot
		// walk the synchronised RNG away from the server's.
		//
		// rollback_botsashuman takes the bots, and only them, down the
		// person's path below instead; a person who finished the race and
		// drives on bot movement keeps this one.
		if (K_PlayerUsesBotMovement(&players[i])
			&& (g_botsashuman == false || players[i].bot == false))
		{
			K_BuildBotTiccmd(&players[i], to);
			continue;
		}

		*to = netcmds[(tic - 1) % BACKUPTICS][i];
		to->flags &= ~TICCMD_RECEIVED;

		// The latency field is not pressed: it is the sender's leveltime when it
		// built the input (G_BuildTiccmd), which G_Ticker turns into a lag, and
		// the game reads that lag (drift and angle leniency in p_user.c, the
		// roulette's fudge). A person's machine stamps every tic's input with
		// that tic, so the stamp moves on by one a tic. Repeated as it was, a
		// guessed tic was wrong in it every time, and the speculation rebuilt
		// for a stamp nobody pressed (WORLDWIDE.md 8.123). A bot's stamp does
		// not move, so a bot guessed as a person (rollback_botsashuman) keeps
		// its own.
		if (players[i].bot == false)
			to->latency = (uint8_t)((to->latency + 1) & TICCMD_LATENCYMASK);
	}
}

/** Console command: rollback_cleancmds [0/1]
  *
  * Client side, two-clock mode. With it on, the speculation leaves tics the
  * server has already sent exactly as they arrived. Off by default so one race
  * can be read with it off, then on. The counts run either way and reset when
  * the switch is set.
  */
static void Command_RollbackCleanCmds_f(void)
{
	if (COM_Argc() > 1)
	{
		g_cleancmds = (atoi(COM_Argv(1)) != 0);
		g_recvwrites = g_recvchanged = 0;
	}

	CONS_Printf("rollback_cleancmds: %s\n",
		(g_cleancmds
			? "on -- the speculation does not touch tics the server has already sent"
			: "off -- the speculation writes the local input over tics already received"));
	CONS_Printf("rollback_cleancmds: %u local inputs %s over an already-received tic, "
		"%u of them different from what the server sent\n",
		g_recvwrites, (g_cleancmds ? "would have been written" : "written"), g_recvchanged);
}

/** Console command: rollback_history [maxdepth]
  *
  * Client side, two-clock mode. With a depth above 0, the speculation replays
  * this machine's own inputs that are sent but not yet applied by the server,
  * one per tic in the order they were made, and reaches as far as the newest
  * one needs -- held steady against the clock, up to maxdepth, never below
  * rollback_twoclock. 0 (the default) repeats the newest input over the
  * speculation, as before. Needs rollback_cleancmds on. Setting it resets the
  * counts, so one race can be read off, then on.
  *
  * The report also says how often the drawn world moved against the clock,
  * with the switch on or off: the off windows are the control.
  */
/** Sets rollback_history's deepest speculation, 0 for off, and zeroes its
  * counts. Shared by the command and by WORLDWIDE mode. */
static void K_SetHistory(int32_t want)
{
	g_histmax = (want <= 0) ? 0 : ((want > MAXGENTLEMENDELAY - 1) ? MAXGENTLEMENDELAY - 1 : want);
	g_histpasses = g_histmatched = g_histcapped = 0;
	g_histunackedsum = g_histdepthsum = 0;
	g_histhold = false;
	g_histrises = g_histdrops = 0;
	g_drawnvalid = false;
	g_drawnpasses = g_drawnjumps = g_drawnjumptics = 0;
	g_samples = g_samplelate = g_samplelatetics = g_samplesamestamp = 0;
	g_anchorambiguous = 0;
	g_histstretched = 0;
}

static void Command_RollbackHistory_f(void)
{
	if (COM_Argc() > 1)
		K_SetHistory(atoi(COM_Argv(1)));

	if (g_histmax <= 0)
	{
		CONS_Printf("rollback_history: off -- the speculation repeats the newest input\n");
	}
	else
	{
		CONS_Printf("rollback_history: on -- the speculation replays the inputs still in "
			"flight, up to %d tics deep%s\n", g_histmax,
			(g_cleancmds ? "" : " -- but rollback_cleancmds is OFF, so it does nothing"));
	}

	if (g_drawnpasses > 0)
	{
		CONS_Printf("rollback_history: the drawn world moved against the clock on %u of "
			"%u passes (%u%%), %u tics in all\n",
			g_drawnjumps, g_drawnpasses,
			(uint32_t)((uint64_t)g_drawnjumps * 100 / g_drawnpasses), g_drawnjumptics);
	}

	if (g_samples > 0)
	{
		CONS_Printf("rollback_history: %u samples made, %u after more than one real tic "
			"(%u tics got no sample of their own), %u with the same stamp as the one "
			"before\n", g_samples, g_samplelate, g_samplelatetics, g_samplesamestamp);
	}

	if (g_stalls > 0)
		CONS_Printf("rollback_history: rollback_stall held the loop %u times\n", g_stalls);

	if (g_histpasses == 0)
	{
		CONS_Printf("rollback_history: no speculation has looked for the applied input yet\n");
		return;
	}

	CONS_Printf("rollback_history: the anchor matched more than one sample on %u of %u "
		"passes\n", g_anchorambiguous, g_histpasses);
	CONS_Printf("rollback_history: R1 %s -- %u passes laid out differently from one "
		"sample a tic\n", (g_histreal ? "on" : "off"), g_histstretched);

	CONS_Printf("rollback_history: %u passes, the applied input found in %u (%u%%)\n",
		g_histpasses, g_histmatched,
		(uint32_t)((uint64_t)g_histmatched * 100 / g_histpasses));

	if (g_histmatched > 0)
	{
		const uint64_t in100 = g_histunackedsum * 100 / g_histmatched;

		CONS_Printf("rollback_history: %u.%02u inputs in flight on average -- the round "
			"trip, in tics\n", (uint32_t)(in100 / 100), (uint32_t)(in100 % 100));
	}

	{
		const uint64_t d100 = g_histdepthsum * 100 / g_histpasses;

		CONS_Printf("rollback_history: speculation %u.%02u tics deep on average, "
			"cut short by the cap on %u passes\n",
			(uint32_t)(d100 / 100), (uint32_t)(d100 % 100), g_histcapped);
	}

	CONS_Printf("rollback_history: lead over the clock raised %u times, lowered %u\n",
		g_histrises, g_histdrops);
}

/** True when the network has contradicted a tic that has already run.
  *
  * \return the oldest such tic through *from.
  */
dboolean K_RollbackPending(tic_t *from)
{
	if (g_havecorrection == false || g_loopahead <= 0)
		return false;

	// Somebody else's input being off by a hair is not worth rewinding the world
	// for, and doing it anyway was the whole cost of this loop. Their kart keeps
	// its momentum in the meantime, which is the engine dead-reckoning them for
	// free. Ours is different: it is not a prediction at all.
	if (g_localwrong == false && (gametic - g_lastcorrection) < ROLLBACK_RECONCILE_EVERY)
	{
		g_deferred++;
		return false;
	}

	if (from != NULL)
		*from = g_correctfrom;

	return true;
}

/** Restores the oldest contradicted tic and replays to the present.
  *
  * The same K_RollbackTo the test command uses, which is the point of having
  * given it a name: the loop and the test cannot drift apart into two readings
  * of the same idea.
  */
void K_RollbackCorrect(void)
{
	// Where every kart was drawn a moment ago, so a correction can be eased into
	// rather than jumped to. Odamex nudges a mispredicted player from the wrong
	// position to the right one over time (cl_prednudge) instead of snapping; ours
	// moves the world instantly, which is what a played race reported as "des
	// sauts nets".
	//
	// This touches the interpolation origin *only* -- old_x and friends, which the
	// renderer reads and the simulation never does. Writing the corrected
	// positions themselves would be inventing a world neither side agreed on,
	// which in lockstep is a desync produced on purpose. So the kart arrives
	// exactly where the server says; it just gets there over a tic instead of in
	// no time at all.
	fixed_t wasx[MAXPLAYERS], wasy[MAXPLAYERS], wasz[MAXPLAYERS];
	dboolean wasdrawn[MAXPLAYERS];
	tic_t from;
	int32_t ran;
	int32_t i;

	if (K_RollbackPending(&from) == false)
		return;

	g_havecorrection = false;

	if (gametic == 0 || from >= gametic)
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		wasdrawn[i] = false;

		if (g_smoothing == false || playeringame[i] == false)
			continue;

		if (players[i].mo == NULL || P_MobjWasRemoved(players[i].mo))
			continue;

		wasx[i] = players[i].mo->x;
		wasy[i] = players[i].mo->y;
		wasz[i] = players[i].mo->z;
		wasdrawn[i] = true;
	}

	ran = K_RollbackTo(from, gametic - 1, NULL);

	if (ran < 0)
	{
		// Further back than the ring reaches. Counted where it is counted, and
		// not hidden: this is the limit the depth cap exists to keep us inside.
		g_unreachable++;
		return;
	}

	// The restore rebuilt the world, so players[i].mo is a different object than
	// the one measured above -- keyed by player, never by pointer.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		mobj_t *mo;

		if (wasdrawn[i] == false || playeringame[i] == false)
			continue;

		mo = players[i].mo;

		if (mo == NULL || P_MobjWasRemoved(mo))
			continue;

		// Draw from where it was, to where it now is. Both orders of history, so
		// the second-order term does not put the jump back in.
		mo->old_x = mo->old_x2 = wasx[i];
		mo->old_y = mo->old_y2 = wasy[i];
		mo->old_z = mo->old_z2 = wasz[i];
		mo->resetinterp = false;
	}

	g_corrections++;
	g_replayedtics += (uint32_t)ran;
	g_lastcorrection = gametic;
	g_localwrong = false;
}

/** Console command: rollback_smooth [0/1]
  *
  * Off by default and deliberately separate from the loop, so one race can be
  * read for whether the resyncs fell -- which is a number -- and then again for
  * whether the snaps did, which only a person can say.
  */
static void Command_RollbackSmooth_f(void)
{
	if (COM_Argc() > 1)
		g_smoothing = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_smooth: %s\n",
		(g_smoothing
			? "on -- a correction is drawn from where the kart was to where it now is"
			: "off -- a correction moves the world at once, as it always has"));
}

/** Console command: rollback_loop [0/1]
  *
  * Turns the whole loop on. Off by default, so a build carrying it plays exactly
  * as a stock one until somebody asks.
  */
static void Command_RollbackLoop_f(void)
{
	if (COM_Argc() > 1)
	{
		g_loopahead = atoi(COM_Argv(1));

		if (g_loopahead < 0)
			g_loopahead = 0;

		// Running ahead without keeping snapshots would mean predicting with no
		// way back, which is worse than not predicting at all.
		if (g_loopahead > 0)
			g_keeping = true;
	}

	CONS_Printf("rollback_loop: running up to %d tics ahead of the server "
		"(the cap allows %d)\n", g_loopahead, K_RollbackPredictAhead());

	// The two conditions the predict block is gated on, printed rather than
	// assumed. The tic loop never counted a single pass, and the command that
	// says so runs on the same machine at the same point of the same function,
	// so whichever of these is false is the whole answer.
	CONS_Printf("rollback_loop: this machine is the %s, gamestate %d (a level is %d)\n",
		(client ? "client" : "server"), (int32_t)gamestate, (int32_t)GS_LEVEL);
	CONS_Printf("rollback_loop: %u tics were run before the server confirmed them, furthest ahead %d\n",
		g_predicted, g_furthestahead);
	CONS_Printf("rollback_loop: the loop ended its passes %d ahead at best, %d at "
		"worst, %d typically -- predicting needs a lead, and this is whether there "
		"ever is one\n",
		g_bestlead, g_worstlead,
		(int32_t)(g_leadsamples ? (g_leadsum / (int64_t)g_leadsamples) : 0));

	CONS_Printf("rollback_loop: over %u tic loops the server was ahead by %d at worst, %d on average -- a client with nothing to wait for has nothing to predict\n",
		g_loops, g_mostbehind,
		(int32_t)(g_loops ? (g_behindsum / (int64_t)g_loops) : 0));
	CONS_Printf("rollback_loop: %u corrections so far, %u tics replayed by them, "
		"%u reached further back than the ring\n",
		g_corrections, g_replayedtics, g_unreachable);
	CONS_Printf("rollback_loop: %u more were deferred -- somebody else's input off by "
		"a hair, which their momentum covers until the next reconciliation\n",
		g_deferred);
	CONS_Printf("rollback_loop: %u tics were handed back to the real loop because a "
		"message landed on them\n", g_rewinds);

	// One pass, one sample of the controls. Anything past the first predicted tic
	// in a pass runs on an input the server has no copy of for that tic, and this
	// says how often that happens rather than how often it might.
	CONS_Printf("rollback_loop: %u passes, %u predicted something, %u predicted more "
		"than one tic -- worst %d, %d typically. The controls are sampled once a "
		"pass, so every tic past the first in one runs on a sample the server does "
		"not have for it\n",
		g_passes, g_passespredicting, g_passesburst, g_worstburst,
		(int32_t)(g_passespredicting ? (g_burstsum / (int64_t)g_passespredicting) : 0));

	// Printed here as well as by rollback_pace, because the figures above mean
	// opposite things either side of it and a log read a week later has only
	// what was printed.
	CONS_Printf("rollback_loop: pacing was %s\n",
		(g_pacing ? "ON -- one predicted tic to a pass"
			: "off -- as many predicted tics in a pass as the depth allows"));
}

/** Console command: rollback_pace [0/1]
  *
  * One predicted tic to a pass, which is how many samples of the player's
  * controls a pass makes. Off by default, and separate from rollback_loop on
  * purpose: the counters above want reading once with it off and once with it on
  * inside the same race, or the comparison is between two different evenings.
  */
static void Command_RollbackPace_f(void)
{
	if (COM_Argc() > 1)
	{
		g_pacing = (atoi(COM_Argv(1)) != 0);

		// Every counter this changes is reset with it. A rate measured half
		// before a change and half after is not a rate of anything.
		g_passes = g_passespredicting = g_passesburst = 0;
		g_worstburst = 0;
		g_burstsum = 0;
		g_predicted = 0;
		g_furthestahead = 0;
		g_corrections = 0;
		g_replayedtics = 0;
		g_offsetfar = 0;
		g_offsetlost = 0;
		memset(g_offsetseen, 0, sizeof (g_offsetseen));
	}

	CONS_Printf("rollback_pace: %s\n",
		(g_pacing
			? "on -- at most one predicted tic per pass, so each one gets its own "
				"sample of the controls"
			: "off -- the loop predicts as far as it may in a single pass, sharing "
				"one sample between those tics"));
}

/** A message arrived for a tic this client has already predicted.
  *
  * Not an input: a netxcmd -- a chat line, a cvar change, somebody becoming a
  * spectator. Those are executed by ExtraDataTicker, which only the real tic loop
  * calls; the correction path drives G_Ticker directly and never reaches it. So
  * replaying such a tic internally would still not run the message, and the
  * client would stay out of step with the server about a piece of game state
  * rather than a position -- which is exactly what a host being spectated on the
  * server and still racing on the client looked like.
  *
  * The way out is not to replay it here but to *give the tic back to the normal
  * loop*: restore that far and let TryRunTics run forward again, ExtraDataTicker
  * included. Expensive, and netxcmds are rare enough for that to be the right
  * trade.
  */
void K_RollbackNoteMessage(tic_t tic)
{
	if (g_loopahead <= 0 || gamestate != GS_LEVEL)
		return;

	if (tic >= gametic)
		return;   // the normal loop will reach it on its own

	if (g_rewindwanted == false || tic < g_rewindto)
	{
		g_rewindto = tic;
		g_rewindwanted = true;
	}
}

/** Hands back a tic the loop must re-run for real, or false. */
dboolean K_RollbackRewindWanted(tic_t *tic)
{
	if (g_rewindwanted == false)
		return false;

	if (tic != NULL)
		*tic = g_rewindto;

	return true;
}

/** The loop has taken the rewind; forget it. */
void K_RollbackRewindTaken(void)
{
	g_rewindwanted = false;
	g_rewinds++;
}

/** Console command: rollback_lag [tics]
  *
  * Delays every packet from a peer by that many tics, on reception. Off by
  * default and only useful for testing: a loopback has no latency, a client with
  * no latency is never short of confirmed tics, and a client that is never short
  * has nothing to predict. Measured at one to two tics ahead at all times, which
  * is why the rollback loop could be switched on and still never fire.
  */
static void Command_RollbackLag_f(void)
{
	int32_t tics;
	uint32_t held, dropped;

	if (COM_Argc() > 1)
	{
		netlagtics = atoi(COM_Argv(1));

		if (netlagtics < 0)
			netlagtics = 0;
	}

	Net_LagStatus(&tics, &held, &dropped);

	CONS_Printf("rollback_lag: holding every peer packet for %d tics (%d ms), "
		"%u waiting now, %u dropped for want of room\n",
		tics, (tics * 1000) / TICRATE, held, dropped);

	if (dropped > 0)
	{
		// Said plainly, because a full queue turns an artificial delay into
		// artificial packet loss and the two would look the same in the results.
		CONS_Printf("rollback_lag: WARNING - packets were dropped, so this is no "
			"longer only a delay\n");
	}
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

/** Every switch WORLDWIDE mode sets on a client, back to off.
  *
  * A speculation still standing is forgotten, not put back: this runs when a
  * client joins a server or leaves one, and either way the world it would
  * restore is on its way out. rollback_cleancmds is left as it is -- on by
  * default, and it does nothing without rollback_twoclock. */
static void K_WorldwideClientOff(void)
{
	g_speculated = false;

	K_SetKeepSpec(false);
	K_SetHistory(0);
	K_SetTwoClock(0);
	g_correctapply = false;

	// Two-clock switches the snapshot keeper on and nothing switches it off,
	// and with two-clock off the keeper saves the whole world every tic
	// (K_RollbackTicker): left on, every game after a WORLDWIDE one -- alone,
	// or hosting -- would pay a save a tic for nothing.
	if (g_wwclient)
		g_keeping = g_wwkeepingwas;

	g_wwclient = false;
}

void K_WorldwideJoin(dboolean serverhasit)
{
	if (serverhasit == false)
	{
		// The policy's other half: a stock server gets a stock client, whatever
		// was switched on here before.
		K_WorldwideClientOff();
		CONS_Printf("worldwide: this server runs the stock netcode -- so does this client\n");
		return;
	}

	if (g_wwclient == false)
		g_wwkeepingwas = g_keeping;

	// What the driven keep race ran (playclient_keep.cfg, WORLDWIDE.md 8.78),
	// with the corrections applied rather than only measured.
	K_SetTwoClock(WORLDWIDE_TWOCLOCK);
	g_cleancmds = true;
	K_SetHistory(WORLDWIDE_HISTORY);
	K_SetKeepSpec(true);
	g_correctapply = true;
	g_wwclient = true;

	CONS_Printf("worldwide: this server runs WORLDWIDE mode -- predicting, "
		"rollback_twoclock %d, rollback_history %d, rollback_keepspec on, "
		"corrections applied\n", WORLDWIDE_TWOCLOCK, WORLDWIDE_HISTORY);
}

void K_WorldwideLeave(void)
{
	// Only what a join switched on. Switches set by hand outside a netgame are
	// someone measuring, and are theirs to turn off.
	if (g_wwclient)
		K_WorldwideClientOff();
}

/** Console command: rollback_histreal [0/1]
  *
  * Client side, with rollback_history. On (the default): the replay gives each
  * sample in flight as many tics as real tics passed before the next one was
  * made, as the server files them (R1, WORLDWIDE.md 8.87, 8.89). Off: one tic
  * each, as before -- the control. */
static void Command_RollbackHistReal_f(void)
{
	if (COM_Argc() > 1)
	{
		g_histreal = (atoi(COM_Argv(1)) != 0);
		g_histstretched = 0;
	}

	CONS_Printf("rollback_histreal: %s\n",
		(g_histreal
			? "on -- each sample in flight is replayed on as many tics as real tics passed before the next"
			: "off -- each sample in flight is replayed on one tic, as before R1"));
	CONS_Printf("rollback_histreal: %u passes laid out differently from one sample a tic\n",
		g_histstretched);
}

/** Console command: rollback_vanillajoin [0/1]
  *
  * Client side, for testing the refusal with a WORLDWIDE build: with 1, the
  * next join leaves out what a WORLDWIDE client adds to it, as a stock client
  * would, and a server in WORLDWIDE mode should turn it away. Off by default. */
static void Command_RollbackVanillaJoin_f(void)
{
	if (COM_Argc() > 1)
		g_wwvanillajoin = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_vanillajoin: %s\n",
		(g_wwvanillajoin
			? "on -- the next join does NOT declare this client WORLDWIDE, as a stock client would"
			: "off -- joins declare this client WORLDWIDE"));
}

// rollback_join: the pause menu's Enter Game, from the console. A client
// nobody drives stays a spectator, so the bench never ran the join's path
// (WORLDWIDE.md 8.113); the client scenarios call this instead. It asks only
// as the menu does, for a spectator not already waiting to join: the same
// message for a player in the race would make that player spectate
// (Got_Spectate, d_netcmd.c).
static void Command_RollbackJoin_f(void)
{
	const int32_t who = g_localplayers[0];
	uint8_t buf[2];

	if (!Playing() || who < 0 || who >= MAXPLAYERS || !playeringame[who])
	{
		CONS_Printf("rollback_join: not in a game yet -- nothing sent\n");
		return;
	}
	if (!players[who].spectator)
	{
		CONS_Printf("rollback_join: player %d is already in the game -- nothing sent\n", who);
		return;
	}
	if (players[who].pflags & PF_WANTSTOJOIN)
	{
		CONS_Printf("rollback_join: player %d has already asked to join -- nothing sent\n", who);
		return;
	}
	if (!G_GametypeHasSpectators() || !cv_allowteamchange.value)
	{
		CONS_Printf("rollback_join: the game takes no spectator in now -- nothing sent\n");
		return;
	}

	buf[0] = (uint8_t)who;
	buf[1] = 1; // join, as the menu's Enter Game
	SendNetXCmd(XD_SPECTATE, buf, sizeof buf);
	CONS_Printf("rollback_join: player %d asked to join the game\n", who);
}

/** Console command: rollback_stall [ms] [every]
  *
  * Client side, for testing: holds this machine's loop for ms milliseconds,
  * once at the next tic of a level, or every so many tics of it -- the busy
  * machine that set off 8.126's cascade, on demand (WORLDWIDE.md 8.129). 0 to
  * stop. */
static void Command_RollbackStall_f(void)
{
	if (COM_Argc() > 1)
	{
		g_stallms = atoi(COM_Argv(1));
		g_stallperiod = (COM_Argc() > 2) ? atoi(COM_Argv(2)) : 0;

		if (g_stallms < 0)
			g_stallms = 0;
		if (g_stallms > 2000)
			g_stallms = 2000;
		if (g_stallperiod < 0)
			g_stallperiod = 0;

		g_stallnext = (tic_t)g_stallperiod;
		g_stalllast = 0;
	}

	if (g_stallms <= 0)
		CONS_Printf("rollback_stall: off -- %u held so far\n", g_stalls);
	else if (g_stallperiod > 0)
		CONS_Printf("rollback_stall: %d ms every %d tics of a level, from leveltime %d -- %u held so far\n",
			(int)g_stallms, (int)g_stallperiod, (int)g_stallperiod, g_stalls);
	else
		CONS_Printf("rollback_stall: %d ms once, at the next tic of a level -- %u held so far\n",
			(int)g_stallms, g_stalls);
}

/** Console command: rollback_cascadelog [0/1]
  *
  * Client side. On: a line for each sample made after more than one real tic,
  * and for each rebuild for this machine's own input, dated by the level's
  * clock and the real one (WORLDWIDE.md 8.129). Off by default. */
static void Command_RollbackCascadeLog_f(void)
{
	if (COM_Argc() > 1)
		g_cascadelog = (atoi(COM_Argv(1)) != 0);

	CONS_Printf("rollback_cascadelog: %s\n",
		(g_cascadelog ? "on -- each gap and each rebuild for this machine's input, dated"
			: "off"));
}

void K_RegisterRollbackStuff(void)
{
	// Debug commands rather than plain ones: they are diagnostics, and being
	// so lists them in the pause menu's command list, which is where they can
	// be reached without typing into the console.
	COM_AddDebugCommand("rollback_test", Command_RollbackTest_f);
	COM_AddDebugCommand("rollback_resim", Command_RollbackResim_f);
	COM_AddDebugCommand("rollback_leak", Command_RollbackLeak_f);
	COM_AddDebugCommand("rollback_soak", Command_RollbackSoak_f);
	COM_AddDebugCommand("rollback_maxdepth", Command_RollbackMaxDepth_f);
	COM_AddDebugCommand("rollback_delay", Command_RollbackDelay_f);
	COM_AddDebugCommand("rollback_keep", Command_RollbackKeep_f);
	COM_AddDebugCommand("rollback_replay", Command_RollbackReplay_f);
	COM_AddDebugCommand("rollback_detect", Command_RollbackDetect_f);
	COM_AddDebugCommand("rollback_loop", Command_RollbackLoop_f);
	COM_AddDebugCommand("rollback_lag", Command_RollbackLag_f);
	COM_AddDebugCommand("rollback_pace", Command_RollbackPace_f);
	COM_AddDebugCommand("rollback_smooth", Command_RollbackSmooth_f);
	COM_AddDebugCommand("rollback_twoclock", Command_RollbackTwoClock_f);
	COM_AddDebugCommand("rollback_objprofile", Command_RollbackObjProfile_f);
	COM_AddDebugCommand("rollback_keepspec", Command_RollbackKeepSpec_f);
	COM_AddDebugCommand("rollback_keepearly", Command_RollbackKeepEarly_f);
	COM_AddDebugCommand("rollback_nullspec", Command_RollbackNullSpec_f);
	COM_AddDebugCommand("rollback_cleancmds", Command_RollbackCleanCmds_f);
	COM_AddDebugCommand("rollback_history", Command_RollbackHistory_f);
	COM_AddDebugCommand("rollback_blame", Command_RollbackBlame_f);
	COM_AddDebugCommand("rollback_correct", Command_RollbackCorrect_f);
	COM_AddDebugCommand("rollback_drift", Command_RollbackDrift_f);
	COM_AddDebugCommand("rollback_damagelog", Command_RollbackDamageLog_f);
	COM_AddDebugCommand("rollback_inputlog", Command_RollbackInputLog_f);
	COM_AddDebugCommand("rollback_relabel", Command_RollbackRelabel_f);
	COM_AddDebugCommand("rollback_vanillajoin", Command_RollbackVanillaJoin_f);
	COM_AddDebugCommand("rollback_join", Command_RollbackJoin_f);
	COM_AddDebugCommand("rollback_botsashuman", Command_RollbackBotsAsHuman_f);
	COM_AddDebugCommand("rollback_poolcopy", Command_RollbackPoolCopy_f);
	COM_AddDebugCommand("rollback_rawsnap", Command_RollbackRawSnap_f);
	COM_AddDebugCommand("rollback_histreal", Command_RollbackHistReal_f);
	COM_AddDebugCommand("rollback_stall", Command_RollbackStall_f);
	COM_AddDebugCommand("rollback_cascadelog", Command_RollbackCascadeLog_f);
}
