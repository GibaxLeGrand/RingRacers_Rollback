// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_clisrv.h
/// \brief high level networking stuff

#ifndef __D_CLISRV__
#define __D_CLISRV__

#include "d_ticcmd.h"
#include "d_net.h"
#include "d_netcmd.h"
#include "d_net.h"
#include "tables.h"
#include "d_player.h"
#include "mserv.h"

#include "k_pwrlv.h" // PWRLV_NUMTYPES
#include "p_saveg.h" // NETSAVEGAMESIZE

#ifdef __cplusplus
extern "C" {
#endif

/*
The 'packet version' is used to distinguish packet formats.
This version is independent of VERSION and SUBVERSION. Different
applications may follow different packet versions.
*/
#define PACKETVERSION 0

// Network play related stuff.
// There is a data struct that stores network
//  communication related stuff, and another
//  one that defines the actual packets to
//  be transmitted.

#define HU_MAXMSGLEN 223

#define MAXSERVERNAME 32
#define MAXSERVERCONTACT 1024

// Networking and tick handling related.
#define BACKUPTICS 512 // more than enough for most timeouts....
#define CLIENTBACKUPTICS 32
#define MAXTEXTCMD 512

// No. of tics your controls can be delayed by.

// TODO: Instead of storing a ton of extra cmds for gentlemens' delay,
// keep them in a linked-list, with timestamps to discard everything that's older than already sent.
// That will support any amount of lag, and be less wasteful for clients who don't use it.
// This just works as a quick implementation.
#define MAXGENTLEMENDELAY TICRATE

//
// Packet structure
//
typedef enum
{
	PT_NOTHING,       // To send a nop through the network. ^_~
	PT_SERVERCFG,     // Server config used in start game
	                  // (must stay 1 for backwards compatibility).
	                  // This is a positive response to a CLIENTJOIN request.
	PT_CLIENTCMD,     // Ticcmd of the client.
	PT_CLIENTMIS,     // Same as above with but saying resend from.
	PT_CLIENT2CMD,    // 2 cmds in the packet for splitscreen.
	PT_CLIENT2MIS,    // Same as above with but saying resend from
	PT_NODEKEEPALIVE, // Same but without ticcmd and consistancy
	PT_NODEKEEPALIVEMIS,
	PT_SERVERTICS,    // All cmds for the tic.
	PT_SERVERREFUSE,  // Server refuses joiner (reason inside).
	PT_SERVERSHUTDOWN,
	PT_CLIENTQUIT,    // Client closes the connection.

	PT_ASKINFO,       // Anyone can ask info of the server.
	PT_SERVERINFO,    // Send game & server info (gamespy).
	PT_PLAYERINFO,    // Send information for players in game (gamespy).
	PT_REQUESTFILE,   // Client requests a file transfer
	PT_ASKINFOVIAMS,  // Packet from the MS requesting info be sent to new client.
	                  // If this ID changes, update masterserver definition.

	PT_WILLRESENDGAMESTATE, // Hey Client, I am about to resend you the gamestate!
	PT_CANRECEIVEGAMESTATE, // Okay Server, I'm ready to receive it, you can go ahead.
	PT_RECEIVEDGAMESTATE,   // Thank you Server, I am ready to play again!

	PT_SENDINGLUAFILE, // Server telling a client Lua needs to open a file
	PT_ASKLUAFILE,     // Client telling the server they don't have the file
	PT_HASLUAFILE,     // Client telling the server they have the file

	// Add non-PT_CANFAIL packet types here to avoid breaking MS compatibility.

	// Kart-specific packets
	PT_CLIENT3CMD,    // 3P
	PT_CLIENT3MIS,
	PT_CLIENT4CMD,    // 4P
	PT_CLIENT4MIS,
	PT_BASICKEEPALIVE,// Keep the network alive during wipes, as tics aren't advanced and NetUpdate isn't called

	PT_CANFAIL,       // This is kind of a priority. Anything bigger than CANFAIL
	                  // allows HSendPacket(*, true, *, *) to return false.
	                  // In addition, this packet can't occupy all the available slots.

	PT_FILEFRAGMENT = PT_CANFAIL, // A part of a file.
	PT_FILEACK,
	PT_FILERECEIVED,

	PT_TEXTCMD,       // Extra text commands from the client.
	PT_TEXTCMD2,      // Splitscreen text commands.
	PT_TEXTCMD3,      // 3P
	PT_TEXTCMD4,      // 4P
	PT_CLIENTJOIN,    // Client wants to join; used in start game.
	PT_NODETIMEOUT,   // Packet sent to self if the connection times out.

	PT_TELLFILESNEEDED, // Client, to server: "what other files do I need starting from this number?"
	PT_MOREFILESNEEDED, // Server, to client: "you need these (+ more on top of those)"

	PT_LOGIN,         // Login attempt from the client.

	PT_PING,          // Packet sent to tell clients the other client's latency to server.

	PT_CLIENTKEY,		// "Here's my public key"
	PT_SERVERCHALLENGE,		// "Prove it"

	PT_CHALLENGEALL,	// Prove to the other clients you are who you say you are, sign this random bullshit!
	PT_RESPONSEALL,		// OK, here is my signature on that random bullshit
	PT_RESULTSALL,		// Here's what everyone responded to PT_CHALLENGEALL with, if this is wrong or you don't receive it disconnect

	PT_SAY,				// "Hey server, please send this chat message to everyone via XD_SAY"

	PT_REQMAPQUEUE,		// Client requesting a roundqueue operation

	PT_VOICE,           // Voice packet for either side

	PT_STATECORRECTION, // Server, to a client: where the karts actually are.
	                    // Breaks compatibility with stock servers by existing,
	                    // which is deliberate -- see statecorrection_pak.
	                    //
	                    // Deliberately at the end, and so above PT_CANFAIL:
	                    // droppable, and unable to occupy every send slot. A
	                    // correction that crowds out the file fragments of a
	                    // joining client is worse than a correction that is
	                    // missed, because the next one is four tics behind it
	                    // and says something more recent.

	NUMPACKETTYPE
} packettype_t;

typedef enum
{
	SIGN_OK,
	SIGN_BADTIME, // Timestamp differs by too much, suspect reuse of an old challenge.
	SIGN_BADIP // Asked to sign the wrong IP by an external host, suspect reuse of another server's challenge.
} shouldsign_t;

#ifdef PACKETDROP
void Command_Drop(void);
void Command_Droprate(void);
#endif
void Command_Numnodes(void);

#if defined(_MSC_VER)
#pragma pack(1)
#endif

// Client to server packet
struct clientcmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	UINT8 wantdelay;
	ticcmd_t cmd;
} ATTRPACK;

// Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client2cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	UINT8 wantdelay;
	ticcmd_t cmd, cmd2;
} ATTRPACK;

// 3P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client3cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	UINT8 wantdelay;
	ticcmd_t cmd, cmd2, cmd3;
} ATTRPACK;

// 4P Splitscreen packet
// WARNING: must have the same format of clientcmd_pak, for more easy use
struct client4cmd_pak
{
	UINT8 client_tic;
	UINT8 resendfrom;
	INT16 consistancy;
	UINT8 wantdelay;
	ticcmd_t cmd, cmd2, cmd3, cmd4;
} ATTRPACK;

#ifdef _MSC_VER
#pragma warning(disable :  4200)
#endif

// Server to client packet
// this packet is too large
struct servertics_pak
{
	UINT8 starttic;
	UINT8 numtics;
	UINT8 numslots; // "Slots filled": Highest player number in use plus one.
	ticcmd_t cmds[45]; // Normally [BACKUPTIC][MAXPLAYERS] but too large
} ATTRPACK;

struct serverconfig_pak
{
	UINT8 version; // Different versions don't work
	UINT8 subversion; // Contains build version

	// Server launch stuffs
	UINT8 serverplayer;
	UINT8 totalslotnum; // "Slots": highest player number in use plus one.

	tic_t gametic;
	UINT8 clientnode;
	UINT8 gamestate;

	UINT8 gametype;
	UINT8 modifiedgame;
	boolean dedicated;

	char server_context[8]; // Unique context id, generated at server startup.

	// Discord info (always defined for net compatibility)
	UINT8 maxplayer;
	boolean allownewplayer;
	boolean discordinvites;

	char server_name[MAXSERVERNAME];
	char server_contact[MAXSERVERCONTACT];
} ATTRPACK;

struct filetx_pak
{
	UINT8 fileid;
	UINT32 filesize;
	UINT8 iteration;
	UINT32 position;
	UINT16 size;
	UINT8 data[]; // Size is variable using hardware_MAXPACKETLENGTH
} ATTRPACK;

struct fileacksegment_t
{
	UINT32 start;
	UINT32 acks;
} ATTRPACK;

struct fileack_pak
{
	UINT8 fileid;
	UINT8 iteration;
	UINT8 numsegments;
	fileacksegment_t segments[];
} ATTRPACK;

#ifdef _MSC_VER
#pragma warning(default : 4200)
#endif

#define MAXAPPLICATION 16

struct player_config_t
{
	char name[MAXPLAYERNAME+1];
	UINT16 skin;
	UINT16 color;
	INT16 follower;
	UINT16 follower_color;
	UINT8 weapon_prefs;
	UINT8 min_delay;
	uint8_t key[PUBKEYLENGTH];
	UINT16 pwr[PWRLV_NUMTYPES];
} ATTRPACK;

struct clientconfig_pak
{
	UINT8 _255;/* see serverinfo_pak */
	UINT8 packetversion;
	char application[MAXAPPLICATION];
	UINT8 version; // Different versions don't work
	UINT8 subversion; // Contains build version
	UINT8 localplayers;	// number of splitscreen players
	UINT8 mode;
	char _names_outdated[MAXSPLITSCREENPLAYERS][MAXPLAYERNAME];
	UINT8 availabilities[MAXAVAILABILITY];
	uint8_t challengeResponse[MAXSPLITSCREENPLAYERS][SIGNATURELENGTH];
	player_config_t player_configs[MAXSPLITSCREENPLAYERS];
} ATTRPACK;

#define SV_SPEEDMASK 0x03		// used to send kartspeed
#define SV_WORLDWIDE 0x04		// the server runs WORLDWIDE mode (cv_worldwide). A stock client
								// reads kartvars through SV_SPEEDMASK and the flags above only,
								// so it never sees this bit.
#define SV_DEDICATED 0x40		// server is dedicated
#define SV_VOICEENABLED 0x80    // voice_mute is off/voice chat is enabled
#define SV_LOTSOFADDONS 0x20	// flag used to ask for full file list in d_netfil

// A WORLDWIDE client's PT_CLIENTJOIN is clientconfig_pak with these bytes after
// it. A stock server reads the packet as a clientconfig_pak and never checks
// its length (HandleConnect), so the extra bytes cost it nothing; a WORLDWIDE
// server reads them to tell a WORLDWIDE client from a stock one, and refuses
// the stock one while it runs WORLDWIDE mode. WORLDWIDE_PROTOCOL goes up when
// two WORLDWIDE builds can no longer play together.
#define WORLDWIDE_MAGIC "RRWW"
#define WORLDWIDE_PROTOCOL 1

struct clientworldwide_pak
{
	clientconfig_pak cfg;
	uint8_t magic[4];	// WORLDWIDE_MAGIC, without its terminator
	uint8_t protocol;	// WORLDWIDE_PROTOCOL
} ATTRPACK;

#define MAXFILENEEDED 915
#define MAX_MIRROR_LENGTH 256
// This packet is too large
struct serverinfo_pak
{
	/*
	In the old packet, 'version' is the first field. Now that field is set
	to 255 always, so older versions won't be confused with the new
	versions or vice-versa.
	*/
	UINT8 _255;
	UINT8 packetversion;
	char  application[MAXAPPLICATION];
	UINT8 version;
	UINT8 subversion;
	UINT8 commit[GIT_SHA_ABBREV];
	UINT8 numberofplayer;
	UINT8 maxplayer;
	UINT8 refusereason; // 0: joinable, 1: joins disabled, 2: full
	char gametypename[24];
	UINT8 modifiedgame;
	UINT8 cheatsenabled;
	UINT8 kartvars; // Previously isdedicated, now appropriated for our own nefarious purposes
	UINT8 fileneedednum;
	tic_t time;
	tic_t leveltime;
	char servername[MAXSERVERNAME];
	char maptitle[33];
	unsigned char mapmd5[16];
	UINT8 actnum;
	UINT8 iszone;
	char httpsource[MAX_MIRROR_LENGTH]; // HTTP URL to download from, always defined for compatibility
	INT16 avgpwrlv; // Kart avg power level
	UINT8 fileneeded[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct serverrefuse_pak
{
	char reason[255];
} ATTRPACK;

struct askinfo_pak
{
	UINT8 version;
	tic_t time; // used for ping evaluation
} ATTRPACK;

struct msaskinfo_pak
{
	char clientaddr[22];
	tic_t time; // used for ping evaluation
} ATTRPACK;

// Shorter player information for external use.
struct plrinfo
{
	UINT8 num;
	char name[MAXPLAYERNAME+1];
	UINT8 address[4]; // sending another string would run us up against MAXPACKETLENGTH
	UINT8 team;
	UINT8 deprecated_skin;
	UINT8 data; // Color is first four bits, hasflag, isit and issuper have one bit each, the last is unused.
	UINT32 score;
	UINT16 timeinserver; // In seconds.
} ATTRPACK;

struct filesneededconfig_pak
{
	INT32 first;
	UINT8 num;
	UINT8 more;
	UINT8 files[MAXFILENEEDED]; // is filled with writexxx (byteptr.h)
} ATTRPACK;

struct clientkey_pak
{
	uint8_t key[MAXSPLITSCREENPLAYERS][PUBKEYLENGTH];
} ATTRPACK;

struct serverchallenge_pak
{
	uint8_t secret[CHALLENGELENGTH];
} ATTRPACK;

struct challengeall_pak
{
	uint8_t secret[CHALLENGELENGTH];
} ATTRPACK;

struct responseall_pak
{
	uint8_t signature[MAXSPLITSCREENPLAYERS][SIGNATURELENGTH];
} ATTRPACK;

struct resultsall_pak
{
	uint8_t signature[MAXPLAYERS][SIGNATURELENGTH];
} ATTRPACK;

struct say_pak
{
	char message[HU_MAXMSGLEN + 1];
	UINT8 target;
	UINT8 flags;
	UINT8 source;
} ATTRPACK;

struct reqmapqueue_pak
{
	UINT16 newmapnum;
	UINT16 newgametype;
	UINT8 flags;
	UINT8 source;
} ATTRPACK;

struct netinfo_pak
{
	UINT32 pingtable[MAXPLAYERS+1];
	UINT32 packetloss[MAXPLAYERS+1];
	UINT32 delay[MAXPLAYERS+1];
} ATTRPACK;

// Sent by both sides. Contains Opus-encoded voice packet
// flags bitset map (left to right, low to high)
// | PPPPPTRR | -- P = Player num, T = Terminal, R = Reserved (0)
// Data following voice header is a single Opus frame
struct voice_pak
{
	UINT64 frame;
	UINT8 flags;
} ATTRPACK;

#define VOICE_PAK_FLAGS_PLAYERNUM_BITS 0x1F
#define VOICE_PAK_FLAGS_TERMINAL_BIT 0x20
#define VOICE_PAK_FLAGS_RESERVED0_BIT 0x40
#define VOICE_PAK_FLAGS_RESERVED1_BIT 0x80
#define VOICE_PAK_FLAGS_RESERVED_BITS (VOICE_PAK_FLAGS_RESERVED0_BIT | VOICE_PAK_FLAGS_RESERVED1_BIT)

// One kart's kinematics, as the authoritative server had them on a tic it has
// confirmed. Thirty-eight bytes.
struct statekart_pak
{
	uint8_t slot;      // player number, so a partial list is still readable
	uint8_t flags;     // reserved; zero for now
	int32_t x, y, z;   // fixed_t
	int32_t momx, momy, momz;
	uint32_t angle;    // angle_t
	int32_t hitlag;
	int16_t rings;
	int8_t itemtype;
	uint8_t itemamount;

	// The state, not the kinematics.
	//
	// A spike line showed a kart's momentum being re-derived wrong within four
	// tics of being handed the server's value, twenty tics running -- so the
	// divergence is a *state* this machine holds and the server does not, and
	// correcting the momentum eight times a second only treats the symptom.
	// These say which state. They are measured and printed, and deliberately not
	// applied: which field to carry is the question, and guessing at it is how
	// every reverted fix on this branch started.
	uint16_t spinouttimer;
	uint16_t nocontrol;
	uint16_t flashing;
	uint8_t spinouttype;
	uint8_t tumbleBounces;
	uint8_t wipeoutslow;
	uint8_t justbumped;
	int32_t offroad;
	int32_t speed;
} ATTRPACK;

// The light correction channel.
//
// Stock Ring Racers has exactly one way to correct a client: send it the whole
// savegame as a file transfer -- 120 KiB at the start of a race and 318 KiB
// three minutes in -- one client at a time, with a five second cooldown, and a
// load that costs about 11 ms on a machine that is drawing. That is a repair,
// not a correction, and a predicting client needs the opposite: something small
// enough to send constantly.
//
// Sixteen karts of statekart_pak is 608 bytes against 318 KiB. A factor of five
// hundred is what makes a correction every few tics affordable at all: at one
// every four tics that is under 6 KB/s a client, where the same cadence with
// whole snapshots would be 2.8 MB/s. The arithmetic is why this packet carries
// kinematics and not a state dump.
//
// It is also the best instrument this branch has had for the drift it is
// chasing. Every correction is a measurement of the gap between one client and
// the server, on a named tic, for every kart -- continuously, instead of only
// when the consistency checksum trips and the resend cooldown allows it. So the
// receiving side measures whether or not it applies anything.
struct statecorrection_pak
{
	uint32_t tic;       // the confirmed tic these describe

	// Damage outcomes this machine has resolved since the map started, and a
	// hash of which ones. Keyed by player slot and mobj type, never by mobjnum:
	// that is handed out afresh at every save, and a client taking a snapshot
	// every tic renumbers constantly, so a hash over mobjnums would differ
	// between two machines that agreed about everything.
	//
	// This field held a collision tally first, and that tally was wrong: it sat
	// in PIT_CheckThing, which tests every pair of objects that come *near* each
	// other, so it read twenty-seven million per race and its value depended on
	// who was near whom -- that is, on the divergence it was meant to date.
	// Circular, and therefore mute.
	//
	// Damage is the opposite kind of event: a few dozen per race, each one a
	// decision both machines must reach identically. The race that retired the
	// collision tally said as much -- of forty spikes, twenty-nine differed in
	// flashing, tumbleBounces or hitlag, and all three are written by the damage
	// path; one kart counted down a sixty-four tic flash on the server that the
	// client never started.
	//
	// Both sides sample at the same point: the server stamps the tic it is about
	// to run, and the receiver holds the packet until its own clock reaches that
	// tic. The client missed the events from before it joined, so it adopts
	// these two values once, on the first correction that lands on its own tic;
	// after that the two machines fold the same events in the same order, and
	// the pair is an equality test. rollback_damagelog on both machines dates
	// any parting to a tic and an object.
	uint32_t damages;
	uint32_t damagehash;

	// Every instrument on this branch has ruled out one cause after another --
	// the archive, the restore, tic determinism, the confirmed clock running
	// on a guess -- while positions still measurably drift and the game's own
	// Consistancy() agrees. What none of them checked: whether every machine
	// actually ran the SAME ticcmd for the SAME player on the SAME confirmed
	// tic on the FIRST delivery. K_RollbackNoteArrival already answers a
	// narrower question -- does a late RESEND of an already-run tic disagree --
	// and it has read zero every race, because a resend of an already-consumed
	// tic is rare on a clean local link. This is the general case: folded once
	// per player, every confirmed tic, from the exact ticcmd about to be run.
	uint32_t inputs;
	uint32_t inputhash;

	uint8_t numkarts;
	uint8_t reserved;
	statekart_pak kart[MAXPLAYERS];
} ATTRPACK;

//
// Network packet data
//
struct doomdata_t
{
	UINT32 checksum;
	UINT8 ack; // If not zero the node asks for acknowledgement, the receiver must resend the ack
	UINT8 ackreturn; // The return of the ack number

	UINT8 packettype;
#ifdef SIGNGAMETRAFFIC
	uint8_t signature[MAXSPLITSCREENPLAYERS][SIGNATURELENGTH];
#endif
	UINT8 reserved; // Padding
	union
	{
		clientcmd_pak clientpak;            //         147 bytes
		client2cmd_pak client2pak;          //         206 bytes
		client3cmd_pak client3pak;          //         264 bytes(?)
		client4cmd_pak client4pak;          //         324 bytes(?)
		servertics_pak serverpak;           //      132495 bytes (more around 360, no?)
		serverconfig_pak servercfg;         //         777 bytes
		UINT8 textcmd[MAXTEXTCMD+2];        //       66049 bytes (wut??? 64k??? More like 258 bytes...)
		char filetxpak[sizeof (filetx_pak)];//         139 bytes
		char fileack[sizeof (fileack_pak)];
		UINT8 filereceived;
		clientconfig_pak clientcfg;         //         650 bytes
		clientworldwide_pak clientww;       // clientcfg, and what a WORLDWIDE client adds
		uint8_t md5sum[16];
		serverinfo_pak serverinfo;          //        1024 bytes
		serverrefuse_pak serverrefuse;      //       65025 bytes (somehow I feel like those values are garbage...)
		askinfo_pak askinfo;                //          61 bytes
		msaskinfo_pak msaskinfo;            //          22 bytes
		plrinfo playerinfo[MSCOMPAT_MAXPLAYERS];//         576 bytes(?)
		INT32 filesneedednum;               //           4 bytes
		filesneededconfig_pak filesneededcfg; //       ??? bytes
		netinfo_pak netinfo;					// Don't believe their lies
		clientkey_pak clientkey;				// 32 bytes
		serverchallenge_pak serverchallenge;	// 256 bytes
		challengeall_pak challengeall;			// 256 bytes
		responseall_pak responseall;			// 256 bytes
		resultsall_pak resultsall;				// 1024 bytes. Also, you really shouldn't trust anything here.
		say_pak say;							// I don't care anymore.
		reqmapqueue_pak reqmapqueue;			// Formerly XD_REQMAPQUEUE
		voice_pak voice;                        // Unreliable voice data, variable length
		statecorrection_pak statecorrection;    //         910 bytes
	} u; // This is needed to pack diff packet types data together
} ATTRPACK;

#if defined(_MSC_VER)
#pragma pack()
#endif

#define MAXSERVERLIST (MAXNETNODES-1)
#define GTCALC_RACE 0
#define GTCALC_BATTLE 1
#define GTCALC_CUSTOM 2
struct serverelem_t
{
	SINT8 node;
	serverinfo_pak info;
	UINT8 cachedgtcalc;
};

extern serverelem_t serverlist[MAXSERVERLIST];
extern UINT32 serverlistcount, serverlistultimatecount;
extern boolean serverlistmode;
extern INT32 mapchangepending;

// Points inside doomcom
extern doomdata_t *netbuffer;
extern consvar_t cv_stunserver;
extern consvar_t cv_httpsource;
extern consvar_t cv_kicktime;

extern consvar_t cv_showjoinaddress;
extern consvar_t cv_playbackspeed;

#define BASEPACKETSIZE      offsetof(doomdata_t, u)
#define FILETXHEADER        offsetof(filetx_pak, data)
#define BASESERVERTICSSIZE  offsetof(doomdata_t, u.serverpak.cmds[0])

typedef enum
{
	KICK_MSG_PLAYER_QUIT = 0,	// Player intentionally left
	KICK_MSG_KICKED,			// Server kick message w/ no reason
	KICK_MSG_CUSTOM_KICK,		// Server kick message w/ reason
	KICK_MSG_VOTE_KICK,			// Vote kick message
	KICK_MSG_BANNED,			// Ban message w/ no reason
	KICK_MSG_CUSTOM_BAN,		// Ban message w/ custom reason
	KICK_MSG_TIMEOUT,			// Player's connection timed out
	KICK_MSG_PING_HIGH,			// Player hit the ping limit
	KICK_MSG_GRIEF,				// Player was detected by antigrief
	KICK_MSG_CON_FAIL,			// Player failed to resync game state
	KICK_MSG_SIGFAIL,			// Player failed signature check
	KICK_MSG__MAX				// Number of unique messages
} kickmsg_t;

typedef enum
{
	KR_KICK          = 1, //Kicked by server
	KR_PINGLIMIT     = 2, //Broke Ping Limit
	KR_SYNCH         = 3, //Synch Failure
	KR_TIMEOUT       = 4, //Connection Timeout
	KR_BAN           = 5, //Banned by server
	KR_LEAVE         = 6, //Quit the game
} kickreason_t;

/* the max number of name changes in some time period */
#define MAXNAMECHANGES (5)
#define NAMECHANGERATE (60*TICRATE)

extern boolean server;
extern boolean serverrunning;
#define client (!server)
extern boolean dedicated; // For dedicated server
extern boolean connectedtodedicated; // Client that is connected to a dedicated server.
extern UINT16 software_MAXPACKETLENGTH;
extern boolean acceptnewnode;
extern SINT8 servernode;
extern char connectedservername[MAXSERVERNAME];
extern char connectedservercontact[MAXSERVERCONTACT];
extern UINT32 ourIP;
extern uint8_t lastReceivedKey[MAXNETNODES][MAXSPLITSCREENPLAYERS][PUBKEYLENGTH];
extern uint8_t lastSentChallenge[MAXNETNODES][CHALLENGELENGTH];
extern uint8_t lastChallengeAll[CHALLENGELENGTH];
extern uint8_t lastReceivedSignature[MAXPLAYERS][SIGNATURELENGTH];
extern uint8_t knownWhenChallenged[MAXPLAYERS][PUBKEYLENGTH];
extern boolean expectChallenge;

// We give clients a chance to verify each other once per race.
// When is that challenge sent, and when should clients bail if they don't receive the responses?
#define CHALLENGEALL_START (TICRATE*5) // Server sends challenges here.
#define CHALLENGEALL_KICKUNRESPONSIVE (TICRATE*10) // Server kicks players that haven't submitted signatures here.
#define CHALLENGEALL_SENDRESULTS (TICRATE*15) // Server waits for kicks to process until here. (Failing players shouldn't be in-game when results are received, or clients get spooked.)
#define CHALLENGEALL_CLIENTCUTOFF (TICRATE*20) // If challenge process hasn't completed by now, clients who were in-game for CHALLENGEALL_START should leave.

void Command_Ping_f(void);
extern tic_t connectiontimeout;
extern tic_t jointimeout;
extern UINT16 pingmeasurecount;
extern UINT32 realpingtable[MAXPLAYERS];
extern UINT32 playerpingtable[MAXPLAYERS];
extern UINT32 playerpacketlosstable[MAXPLAYERS];
extern UINT32 playerdelaytable[MAXPLAYERS];
extern tic_t servermaxping;

extern boolean server_lagless;
extern consvar_t cv_mindelay;

extern consvar_t cv_netticbuffer, cv_allownewplayer, cv_maxconnections, cv_joindelay;
extern consvar_t cv_worldwide;
extern consvar_t cv_pingtimeout, cv_blamecfail;
extern consvar_t cv_maxsend, cv_noticedownload, cv_downloadspeed;

#ifdef VANILLAJOINNEXTROUND
extern consvar_t cv_joinnextround;
#endif

extern consvar_t cv_discordinvites;

extern consvar_t cv_allowguests;

extern consvar_t cv_gamestochat;

#ifdef DEVELOP
	extern consvar_t cv_badjoin;
	extern consvar_t cv_badtraffic;
	extern consvar_t cv_badresponse;
	extern consvar_t cv_noresponse;
	extern consvar_t cv_nochallenge;
	extern consvar_t cv_badresults;
	extern consvar_t cv_noresults;
	extern consvar_t cv_badtime;
	extern consvar_t cv_badip;
#endif

// Used in d_net, the only dependence
tic_t ExpandTics(INT32 low, tic_t basetic);
void D_ClientServerInit(void);

void GenerateChallenge(uint8_t *buf);
shouldsign_t ShouldSignChallenge(uint8_t *message);

// Initialise the other field
void RegisterNetXCmd(netxcmd_t id, void (*cmd_f)(const UINT8 **p, INT32 playernum));
void SendNetXCmdForPlayer(UINT8 playerid, netxcmd_t id, const void *param, size_t nparam);
#define SendNetXCmd(id, param, nparam) SendNetXCmdForPlayer(0, id, param, nparam) // Shortcut for P1
void SendKick(UINT8 playernum, UINT8 msg);

// Create any new ticcmds and broadcast to other players.
void NetKeepAlive(void);
void NetUpdate(void);
void NetVoiceUpdate(void);

void SV_StartSinglePlayerServer(INT32 dogametype, boolean donetgame);
boolean SV_SpawnServer(void);
void SV_StopServer(void);
void SV_ResetServer(void);

/*--------------------------------------------------
	boolean K_AddBotFromServer(UINT16 skin, UINT8 difficulty, botStyle_e style, UINT8 *newplayernum);

		Adds a new bot, using a server-sided packet sent to all clients.
		Using regular K_AddBot wherever possible is better, but this is kept
		as a back-up measure if this is the only option.

	Input Arguments:-
		skin - Skin number that the bot will use.
		difficulty - Difficulty level this bot will use.
		style - Bot style to spawn this bot with, see botStyle_e.
		newplayernum - Pointer to the last valid player slot number.
			Is a pointer so that this function can be called multiple times to add more than one bot.

	Return:-
		true if a bot can be added via a packet later, otherwise false.
--------------------------------------------------*/

boolean K_AddBotFromServer(UINT16 skin, UINT8 difficulty, botStyle_e style, UINT8 *p);

void CL_AddSplitscreenPlayer(void);
void CL_RemoveSplitscreenPlayer(UINT8 p);
void CL_Reset(void);
void CL_ClearPlayer(INT32 playernum);
void CL_RemovePlayer(INT32 playernum, kickreason_t reason);
void CL_QueryServerList(msg_server_t *list);
void CL_UpdateServerList(void);
void CL_TimeoutServerList(void);
// Is there a game running
boolean Playing(void);
boolean InADedicatedServer(void);

// Advance client-to-client pubkey verification flow
void UpdateChallenges(void);

// Broadcasts special packets to other players
//  to notify of game exit
void D_QuitNetGame(void);

//? How many ticks to run?
boolean TryRunTics(tic_t realtic);

// extra data for lmps
// these functions scare me. they contain magic.
/*boolean AddLmpExtradata(UINT8 **demo_p, INT32 playernum);
void ReadLmpExtraData(UINT8 **demo_pointer, INT32 playernum);*/

// translate a playername in a player number return -1 if not found and
// print a error message in the console
SINT8 nametonum(const char *name);

extern char motd[254], server_context[8];
extern UINT8 playernode[MAXPLAYERS];
/* consoleplayer of this player (splitscreen) */
extern UINT8 playerconsole[MAXPLAYERS];

INT32 D_NumPlayers(void);
INT32 D_NumPlayersInRace(void);
boolean D_IsPlayerHumanAndGaming(INT32 player_number);

void D_ResetTiccmds(void);
void D_ResetTiccmdAngle(UINT8 ss, angle_t angle);
ticcmd_t *D_LocalTiccmd(UINT8 ss);

/** The local input built `age` samples ago (0 is the newest, the one
  * D_LocalTiccmd returns), or NULL past the MAXGENTLEMENDELAY kept. One sample
  * is built and sent per pass, so this is also the send order. */
ticcmd_t *D_LocalTiccmdAge(uint8_t ss, int32_t age);

/** rollback_ontime (WORLDWIDE.md 8.130): if a real tic has gone by since the
  * last sample, makes one and sends it, as NetUpdate does, without reading the
  * network. For a long pass, between two of its tics. True when one was made.
  * Client side, in a level. */
dboolean CL_SampleOnTime(void);

/* Hash of the parts of the game state the netcode compares between
   machines. Exposed for the rollback netcode, which uses it to check a
   restored state against the one it was taken from. */
int16_t Consistancy(void);

/** Stores the current world's checksum as the given tic's. Used by the rollback
  * replay, which advances tics without going through the tic loop that does it. */
void D_RecordConsistancy(tic_t tic);

/** The first tic the server has not sent yet: every tic below it holds the
  * server's inputs for every player. */
tic_t D_NeededTic(void);

/** Console command: records what the consistency checksum was looking at, tic by
  * tic, so a synch failure can name a position, an item or an RNG seed instead of
  * being guessed at. */
void Command_RollbackBlame_f(void);

tic_t GetLag(int32_t node);
uint8_t GetFreeXCmdSize(uint8_t playerid);

void D_MD5PasswordPass(const UINT8 *buffer, size_t len, const char *salt, void *dest);

extern UINT8 hu_redownloadinggamestate;

extern UINT8 adminpassmd5[16];
extern boolean adminpasswordset;

extern boolean hu_stopped;

//
// SRB2Kart
//

void HandleSigfail(const char *string);

void DoSayPacket(SINT8 target, UINT8 flags, UINT8 source, char *message);
void DoSayPacketFromCommand(SINT8 target, size_t usedargs, UINT8 flags);
void DoVoicePacket(SINT8 target, UINT64 frame, const UINT8* opusdata, size_t len);
void SendServerNotice(SINT8 target, char *message);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
