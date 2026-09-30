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

/** Records how many predicted tics one pass of the tic loop ran. */
void K_RollbackNotePass(int32_t predicted);

/** True when the loop may run only one predicted tic per pass of TryRunTics. */
dboolean K_RollbackPacing(void);

/** One kart's kinematics as the server had them, in the shape the simulation
  * side takes. The wire format is statekart_pak in d_clisrv.h; this exists so
  * that k_rollback.h does not have to know what a packet is. Plain integers
  * rather than fixed_t and angle_t for the same reason. */
struct rollbackkart_t
{
	uint8_t slot;
	int32_t x, y, z;
	int32_t momx, momy, momz;
	uint32_t angle;
	int32_t hitlag;
	int16_t rings;
	int8_t itemtype;
	uint8_t itemamount;

	uint16_t spinouttimer;
	uint16_t nocontrol;
	uint16_t flashing;
	uint8_t spinouttype;
	uint8_t tumbleBounces;
	uint8_t wipeoutslow;
	uint8_t justbumped;
	int32_t offroad;
	int32_t speed;
};

/** How many tics apart the server should send light state corrections. Zero --
  * the default -- means never, and the only correction available is the stock
  * full-state resend. Server side only; a client applies whatever arrives. */
int32_t K_RollbackCorrectRate(void);

/** True when a light correction should stand in for the stock full-state resend
  * rather than run beside it. False keeps stock behaviour, which is the control
  * a drift measurement needs. */
dboolean K_RollbackCorrectSuppress(void);

// WORLDWIDE mode (WORLDWIDE.md 8.80): the server decides. One switch on the
// server, cv_worldwide; a client follows what the server it joins advertises.

/** True on a server hosting a netgame in WORLDWIDE mode: it sends corrections
  * in place of full-state resends, advertises SV_WORLDWIDE, and refuses clients
  * that do not declare themselves WORLDWIDE. */
dboolean K_WorldwideServer(void);

/** True while rollback_rawsnap takes raw snapshots (WORLDWIDE.md 8.88): a
  * restore may then bring a removed object back at its own address, so what
  * it points to must outlive its removal. */
dboolean K_RollbackRawSnapshots(void);

/** Whether this client's join declares it WORLDWIDE. True unless
  * rollback_vanillajoin asks it to join as a stock client would. */
dboolean K_WorldwideDeclare(void);

/** Called by a client when it has the server info of the server it is joining:
  * switches prediction on against a server in WORLDWIDE mode, and everything off
  * against any other. */
void K_WorldwideJoin(dboolean serverhasit);

/** Called when this machine leaves a netgame: undoes what K_WorldwideJoin
  * switched on, and nothing else. */
void K_WorldwideLeave(void);

/** Files a correction that arrived from the server. Only stores it: the world
  * it has to be applied to is the confirmed one, which does not exist yet at
  * the moment a packet is read. */
void K_RollbackNoteServerState(uint32_t tic, const struct rollbackkart_t *karts,
	uint8_t n, uint32_t damages, uint32_t damagehash,
	uint32_t inputs, uint32_t inputhash);

struct mobj_t;

/** Records one damage outcome resolved on a confirmed tic, keyed by something
  * both machines agree on. Called from P_DamageMobj, and only when it returns
  * true: an attempt that was refused is not an event, and the two machines
  * refuse different attempts constantly without disagreeing about the world.
  *
  * Ignored while a speculation is running. A speculated hit is not a fact, and
  * counting one would walk this machine's tally away from the server's for a
  * reason that is not a bug -- the failure mode of every instrument here that
  * has read non-zero innocently. */
void K_RollbackNoteDamage(struct mobj_t *victim, struct mobj_t *inflictor,
	uint8_t damagetype);

/** This machine's running damage tally and hash, for the packet to carry. */
void K_RollbackLiveDamages(uint32_t *count, uint32_t *hash);

struct ticcmd_t;

/** Folds one player's ticcmd into the running input tally, for a confirmed
  * tic about to be run for real. Called once per in-game player from the
  * shared tic loop in d_clisrv.c, on both client and server, using the exact
  * ticcmd netcmds[] is about to hand to P_Ticker -- the first delivery, not
  * only a later resend. */
void K_RollbackNoteInput(uint32_t tic, uint8_t slot, const struct ticcmd_t *cmd);

/** This machine's running input tally and hash, for the packet to carry. */
void K_RollbackLiveInputs(uint32_t *count, uint32_t *hash);

/** Records how far PT_CLIENTCMD's own relabelling moved one arriving ticcmd:
  * faketic minus the tic the client actually tagged it with (realstart).
  * Called from the server's receive handler only -- a client never relabels
  * anything, and the histogram says so on its own if this is ever called
  * from one (every sample would land at zero). Also split by sender (this
  * server's own node or a remote one) and by whether a level is running. */
void K_RollbackNoteRelabel(int32_t delta, dboolean fromhost, dboolean inlevel);

/** NetUpdate has just made this machine's sample, realtics real tics after the
  * last one. One sample for several tics leaves the server a tic with none,
  * which it fills by repeating the one before (WORLDWIDE.md 8.85). Client side. */
void K_RollbackNoteSample(int32_t realtics);

/** The server has filed one player's sample: a tic later than it arrived,
  * because that slot was taken (shifted), and over a sample already filed there
  * (overwrote). Server side. */
void K_RollbackNoteFiling(int32_t player, dboolean shifted, dboolean overwrote);

/** SV_Maketic found no sample from a player for a tic, and repeated the one
  * before. Server side. */
void K_RollbackNoteRepeat(int32_t player);

/** Measures the stored correction against this client's confirmed world, and
  * applies it when asked to. Called from the tic loop once the confirmed world
  * is back and before it is advanced. Does nothing when none is pending. */
void K_RollbackApplyServerState(void);

/** How many tics the speculation runs ahead of the confirmed world. Zero when
  * the two-clock mode is off, which is the default. */
int32_t K_RollbackTwoClock(void);

/** True when this machine's own view is being covered by prediction -- either
  * the old rollback_loop or the current rollback_twoclock pivot, whichever of
  * the two (mutually exclusive) is actually running. The gentleman's delay
  * (server) and mindelay (client) should get out of the way while this is
  * true: rollback is already paying for the round trip, so a fixed delay on
  * top of it is a cost paid twice. */
dboolean K_RollbackPays(void);

/** True when this machine runs tics that are not the confirmed ones: the
  * two-clock speculation, or the old loop. Such a machine records no replay
  * (WORLDWIDE.md 8.96). */
dboolean K_RollbackPredicting(void);

/** True while speculative tics are being run. Sound, the snapshot keeper and
  * anything else that must not happen twice sit these out: a speculation is
  * thrown away and rebuilt every pass, so its side effects would repeat. */
dboolean K_RollbackSpeculating(void);

/** Counts a netxcmd refused because it was raised inside a speculation. */
void K_RollbackNoteSuppressedXCmd(void);

/** Puts the confirmed world back, undoing the last pass's speculation. Called
  * before the authoritative tic loop, so that loop starts where the server
  * left it. Does nothing when there is no speculation standing. */
void K_RollbackUnspeculate(void);

/** Saves the confirmed world and runs the speculation forward from it. Called
  * after the authoritative tic loop, so what the player sees and acts in is
  * ahead of what the server has confirmed. */
void K_RollbackSpeculate(void);

/** How many tics the last K_RollbackSpeculate ran: what is drawn moved on this
  * pass when it is above zero (WORLDWIDE.md 8.102). */
int32_t K_RollbackSpeculatedLastPass(void);

/** True while a correction is re-running tics. Sound and other outside-the-world
  * effects should sit those out: the tic already happened once. */
dboolean K_RollbackReplaying(void);

/** The same answer, for gameplay code that changes what a tic does when it is
  * off the timeline -- the gamedata guards. Also marks the tic so a speculation
  * that ran it is not kept as if it were the real one (rollback_keepspec). */
dboolean K_RollbackOffTimeline(void);

/** Whether a sound or a chat line started now should stay silent: a replay,
  * or, inside a tic, one this machine has already run once and so already heard
  * (WORLDWIDE.md 8.73, 8.108). Outside a tic -- a menu, the console -- never. */
dboolean K_RollbackSoundsSilenced(void);

/** The tic loop runs G_Ticker between these: what a tic starts is judged by
  * K_RollbackSoundsSilenced, what the menus start between tics is not. */
void K_RollbackTicRunning(dboolean running);

/** K_RollbackSoundsSilenced for a chat line, and in WORLDWIDE mode a console
  * line for each one a tic writes: on which tic, against the horizon and the
  * frontier, and how many were held back before it (WORLDWIDE.md 8.109). */
dboolean K_RollbackChatSilenced(void);

struct thinker_t;

/** The karts' bodies' reference counts (WORLDWIDE.md 8.111). In a PARANOIA
  * build, P_SetTarget reports every change of an MT_PLAYER's count with its
  * caller's file and line, the loader its claims of a player's body, and
  * P_RemoveMobj and P_RemoveThinkerDelayed a body's removal and its end; a
  * count going below zero prints the body's ledger, site by site, and what
  * still points at it. */
void K_RollbackRefTrace(struct mobj_t *mo, int32_t delta, const char *file, int32_t line);
void K_RollbackRefNegative(struct mobj_t *mo, const char *file, int32_t line);
void K_RollbackRefRemoved(struct mobj_t *mo);
void K_RollbackRefFreed(struct thinker_t *th);

/** Around every load of the network archive -- a rollback restore, the join's,
  * a resend's: a load frees every object and brings the objects back, so the
  * bodies followed start again, and a collision pointer left holding a freed
  * object is checked against what lives at its address after (8.111). */
void K_RollbackRefLoadBegin(void);
void K_RollbackRefLoadEnd(dboolean loaded);

// rollback_keepspec -- track A (WORLDWIDE.md 8.60, 8.73): leave the speculation
// standing across a pass, and rebuild it only when the tics the server confirms
// are not the ones it ran. Off by default; while off, nothing below does
// anything and every pass runs as before.
//
// K_RollbackKeepArm, at the head of TryRunTics in place of K_RollbackUnspeculate:
// leaves the world where it is and hands the netcode the frontier's clock.
// K_RollbackKeepDecide, after GetPackets, with the tic the authoritative loop
// would stop at and whether any tic up to it carries netxcmds: true if the
// speculation stands; false once the world is back at the frontier.
// K_RollbackKeepConsistancy, the checksum a kept tic left behind it, for the
// netcode's consistancy[]. K_RollbackKeepCommit moves the frontier and puts the
// clock back on the speculation's head.
dboolean K_RollbackKeepArm(void);
dboolean K_RollbackKeepArmed(void);
dboolean K_RollbackKeepDecide(tic_t upto, dboolean textcmds);
int16_t K_RollbackKeepConsistancy(tic_t tic);
void K_RollbackKeepCommit(void);

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

// What a pass of TryRunTics spends its time on, step by step, for
// rollback_twoclock's report (WORLDWIDE.md 8.60). Each call adds the time since
// *since to its step and moves *since to now. Counted in a level only.
typedef enum
{
	ROLLBACK_STEP_NET,          // NetUpdate and GetPackets
	ROLLBACK_STEP_CORRECTION,   // applying a state correction
	ROLLBACK_STEP_CONFIRMED,    // the authoritative tic loop, and what surrounds it
	ROLLBACK_NUMSTEPS
} rollbackstep_t;

void K_RollbackNoteStep(rollbackstep_t step, precise_t *since);

/** The authoritative loop of one pass ran this many tics. */
void K_RollbackNoteConfirmedTics(int32_t tics);

/** One iteration of the main loop: the work it did before any sleep, whether it
  * ran the tic loop, whether it drew a frame, and whether it was told to skip
  * the next one for running long. Counted in a level only. */
void K_RollbackNoteFrame(precise_t work, dboolean ranloop, dboolean drew, dboolean skipnext);

// rollback_objprofile: the objects' thinker list timed by object type
// (WORLDWIDE.md 8.66). P_RunThinkers reads the flag and times nothing while
// it is off.
extern dboolean g_rollbackobjprofile;
void K_RollbackNoteObjectThink(int32_t type, precise_t spent);
void K_RollbackNoteObjectTic(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ROLLBACK__
