// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
// Copyright (C) 1996 by id Software, Inc.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  p_saveg.h
/// \brief Savegame I/O, archiving, persistence

#ifndef __P_SAVEG__
#define __P_SAVEG__

#ifdef __cplusplus
extern "C" {
#endif

// 1024 bytes is plenty for a savegame
// ...but we'll be accomodating of a heavily replaced Round Queue.
#define SAVEGAMESIZE (2048)

// For netgames
#define NETSAVEGAMESIZE (768*1024)

// Persistent storage/archiving.
// These are the load / save game routines.

// Local Play
void P_SaveGame(savebuffer_t *save);
boolean P_LoadGame(savebuffer_t *save);
void P_GetBackupCupData(savebuffer_t *save);

// Online
// `local` says the snapshot stays on this machine, as a rollback snapshot
// does. It keeps the per-viewport visibility flags, which are stripped from
// anything sent to another machine.
void P_SaveNetGame(savebuffer_t *save, dboolean resending, dboolean local);
// `local` says this restore is a rollback putting back a state this machine
// took itself, so decoration local to the machine can be left in place.
dboolean P_LoadNetGame(savebuffer_t *save, dboolean reloading, dboolean local);

// True while a rollback is putting back a state this machine took itself, as
// opposed to a gamestate arriving from the server. Anything that is a property
// of this machine rather than of the world -- a sound in flight, precipitation,
// an interpolation origin -- should be left alone when this is true.
dboolean P_LocalRestoreInProgress(void);

// How many gamestates have been loaded from elsewhere -- a join or a resend,
// never a rollback's own restore. A speculation left standing across a pass
// (rollback_keepspec) checks it did not change underneath it.
uint32_t P_NetLoadCount(void);

// Names the archive block that a byte offset of a P_SaveNetGame buffer falls
// in. Diagnostic aid for comparing two snapshots of the same state.
const char *P_LocateSnapshotBlock(const uint8_t *buffer, size_t length, size_t offset);

// Reads an offset inside the players block as a player and a distance into that
// player's record, for the archive written last.
dboolean P_LocatePlayerField(size_t offset, uint8_t *player, size_t *into);

// Names the field that such a distance lands in, for the part of the record
// whose layout does not depend on what the player has attached to them. NULL
// past that point.
const char *P_NamePlayerField(const uint8_t *buffer, size_t length, uint8_t player, size_t into);

// How long each step of the last P_LoadNetGame took, in microseconds. The
// restore is the expensive half of a rollback, so it says where its time goes.
#define P_LOADPROFILE_MAX 24

struct loadstep_t
{
	const char *name;
	uint32_t us;

	// A hash of the player structures as this step left them, so a step that
	// writes over what an earlier one restored can be named instead of
	// guessed at. Only filled while P_ProfileWatchPlayers is on.
	uint32_t playerhash;
};

// Turns the per-step capture of the player structures on and off. Off by
// default: it copies every player at every step of a restore, which is not
// something a game should pay for.
void P_ProfileWatchPlayers(dboolean on);

// The player structures as the given step left them, or NULL if that step was
// not captured. A hash says a step changed something; this says what, which
// matters because the steps that rebuild objects legitimately rewrite every
// pointer a player holds.
const uint8_t *P_GetProfilePlayers(size_t step);

size_t P_GetLoadProfile(const loadstep_t **steps);

// Where a local save's time goes, step by step, summed over every local save
// since the last P_ResetSaveProfile (WORLDWIDE.md 8.81). The load has had its
// steps timed since 8.34; the save, which rollback_keepspec runs once a kept
// pass and once per tic of a rebuild, had only its total -- about 4 ms on
// Opulence, some 40% of a driven pass (8.78). A raw snapshot (track B2) can
// replace some of these steps and not others (Lua, ACS); this says which ones
// are worth it.
#define P_SAVEPROFILE_MAX 24

struct savestep_t
{
	const char *name;
	uint64_t us;      // summed over every save counted
	uint64_t bytes;   // what the step wrote, summed likewise
};

// The steps, and how many local saves they sum.
size_t P_GetSaveProfile(const savestep_t **steps, uint32_t *saves);
void P_ResetSaveProfile(void);

// Raw local snapshots (rollback_rawsnap, WORLDWIDE.md 8.83, 8.88). A raw
// snapshot is three parts: this archive without what the level pools hold
// (P_SaveNetGameRaw), the pools themselves (Z_LevelPoolSnapshot), and the
// pointers into the pools held outside them (P_SaveRawHeads). Restored in the
// other order -- pools, heads, then P_LoadNetGameRaw -- after checking that
// every part fits this level (Z_LevelPoolRestore checks the pools,
// P_RawHeadsFit the heads). P_LoadNetGameRaw rebuilds every reference count
// from zero (choice E2, 8.83).
void P_SaveNetGameRaw(savebuffer_t *save);
dboolean P_LoadNetGameRaw(savebuffer_t *save);

// Called just before the pools are put back: which thinkers are alive now, so
// P_LoadNetGameRaw can make Lua forget the ones the restore takes away.
void P_NoteRawLiving(void);

size_t P_RawHeadsSize(void);
size_t P_SaveRawHeads(uint8_t *dst, size_t capacity);
dboolean P_RawHeadsFit(const uint8_t *src, size_t length);
dboolean P_RestoreRawHeads(const uint8_t *src, size_t length);

// Verify mode: while on, each P_LoadNetGameRaw compares the counts the raw copy
// brought back -- the live game's own -- with the ones it rebuilt.
#define RAWCOUNT_SHOWN 8

typedef struct rawcountmiss_s
{
	int32_t list;       // thinklistnum_t
	int32_t mobjtype;   // -1 when not an object
	uint32_t mobjnum;
	int32_t was, now;   // the live count, the recount
} rawcountmiss_t;

typedef struct rawcountcheck_s
{
	uint32_t thinkers;
	uint32_t mismatched;
	dboolean listschanged; // the lists moved during the load: nothing compared past that
	uint32_t shown;
	rawcountmiss_t miss[RAWCOUNT_SHOWN];
} rawcountcheck_t;

void P_RawCountCheck(dboolean on);
const rawcountcheck_t *P_GetRawCountCheck(void);

// Archives one mobj on its own, so the same object can be compared before and
// after a state restore. Diagnostic aid, see p_saveg.cpp.
size_t P_ArchiveMobjForDiagnostics(uint8_t *buffer, size_t size, const mobj_t *mobj);

mobj_t *P_FindNewPosition(UINT32 oldposition);

struct savedata_bot_s
{
	boolean valid;
	UINT16 skin;
	UINT8 difficulty;
	boolean rival;
	boolean foe;
	UINT32 score;
};

struct savedata_t
{
	UINT32 score;
	SINT8 lives;
	UINT16 totalring;

	UINT16 skin;
	UINT16 skincolor;
	INT32 followerskin;
	UINT16 followercolor;

	struct savedata_bot_s bots[MAXPLAYERS];
};

extern savedata_t savedata;

struct savedata_cup_t
{
	cupheader_t *cup;
	UINT8 difficulty;
	boolean encore;
};

extern savedata_cup_t cupsavedata;

struct savebuffer_t
{
	UINT8 *buffer;
	UINT8 *p;
	UINT8 *end;
	size_t size;
};

boolean P_SaveBufferZAlloc(savebuffer_t *save, size_t alloc_size, INT32 tag, void *user);
#define P_SaveBufferAlloc(a,b) P_SaveBufferZAlloc(a, b, PU_STATIC, NULL)
boolean P_SaveBufferFromExisting(savebuffer_t *save, UINT8 *existing_buffer, size_t existing_size);
boolean P_SaveBufferFromLump(savebuffer_t *save, lumpnum_t lump);
boolean P_SaveBufferFromFile(savebuffer_t *save, char const *name);
void P_SaveBufferFree(savebuffer_t *save);
size_t P_SaveBufferRemaining(const savebuffer_t *save);

boolean TypeIsNetSynced(mobjtype_t type);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
