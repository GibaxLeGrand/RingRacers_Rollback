// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_rollback.h
/// \brief Rollback netcode -- full world snapshot ring buffer (Phase 0)

#ifndef __K_ROLLBACK__
#define __K_ROLLBACK__

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

// How many confirmed tics back the ring buffer can hold.
// Starting point only: each slot costs a full netgame savegame, so the
// figure has to be re-decided once rollback_test has reported the real
// snapshot size and the cost of taking one.
#define ROLLBACK_TICS 20

void K_InitRollback(void);
void K_ClearRollback(void);

dboolean K_SaveGameState(tic_t tic);
dboolean K_LoadGameState(tic_t tic);

void K_RegisterRollbackStuff(void);

// Runs a resimulation check when the soak is on and one is due. Called once
// per tic; does nothing at all unless rollback_soak has been turned on.
void K_RollbackSoakTicker(void);

// Called once per tic, after P_Ticker. Keeps a snapshot of the tic when
// rollback_keep is on, then runs the soak. Does nothing otherwise.
void K_RollbackTicker(void);

/** Tells the rollback code that the server's inputs for a tic have arrived. */
void K_RollbackNoteArrival(tic_t tic);

/** How many tics a predicting client may run past the server. Zero when off. */
int32_t K_RollbackPredictAhead(void);

/** Fills a tic's inputs by repeating what each player was last known to hold. */
void K_RollbackPredictInputs(tic_t tic, int32_t ahead);

/** Records how far behind the server the client was when a tic loop began. */
void K_RollbackNoteTicLoop(int32_t behind);

/** Records the lead the client is left with when a tic loop finishes. */
void K_RollbackNoteTicLoopEnd(int32_t lead);

/** True while a correction is re-running tics. Sound and other outside-the-world
  * effects should sit those out: the tic already happened once. */
dboolean K_RollbackReplaying(void);

/** Tells the rollback code a netxcmd arrived for a tic, which may already have
  * been predicted. */
void K_RollbackNoteMessage(tic_t tic);

/** True when a tic must be re-run by the real loop, message and all. */
dboolean K_RollbackRewindWanted(tic_t *tic);

/** The loop has taken that rewind. */
void K_RollbackRewindTaken(void);

/** True when a tic already run has been contradicted; the oldest one via from. */
dboolean K_RollbackPending(tic_t *from);

/** Restores the oldest contradicted tic and replays to the present. */
void K_RollbackCorrect(void);

// How far back a rollback may rewind before the latency has to be paid for
// with input delay instead. Set by rollback_maxdepth.
int32_t K_RollbackMaxDepth(void);

// Records a kart taking a hit, while a resimulation check has two passes to
// compare. Does nothing the rest of the time.
void K_RollbackTraceHit(int32_t victim, uint16_t inflictor, uint16_t source);

// Records the end-of-tic copy of timeshit into timeshitprev, taken or not, with
// the two values that decide it. Does nothing outside a check.
void K_RollbackTraceHitCopy(int32_t victim, dboolean copied, int32_t hitlag, int32_t nullhitlag,
	uint8_t timeshit, uint8_t timeshitprev);

// Records what the camera lean was computed from, and what came out. Does
// nothing outside a check.
void K_RollbackTraceTilt(int32_t who, uint32_t vx, uint32_t vy,
	uint32_t pitch, uint32_t roll, uint32_t slope, uint32_t tilt);

// Counts a pair of objects being tested against each other, while a check has
// two passes to compare. Does nothing the rest of the time.
void K_RollbackTraceCollide(uint32_t one, uint32_t two);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ROLLBACK__
