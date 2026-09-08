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

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ROLLBACK__
