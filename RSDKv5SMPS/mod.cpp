#include "Game.hpp"
#include "pch.h"
#include <string>
#include <unordered_map>

using namespace RSDK;

ManiaGlobalVariables* maniaGlobals = nullptr;

BOOL(*SMPS_LoadAndPlaySong)(short song);
BOOL(*SMPS_StopSong)();
BOOL(*SMPS_PauseSong)();
BOOL(*SMPS_ResumeSong)();
BOOL(*SMPS_SetSongTempo)(double multiplier);
BOOL(*SMPS_SetVolume)(double vol);

std::unordered_map<std::string, short> songmap;
int smpsChannel = -1;
bool isSpeedUp = false;
bool one_up = false;

#if _WINDOWS
#if defined(_M_IX86) || defined(__i386__)
#define JMPSIZE 5
static inline void WriteJump(void* writeaddress, void* funcaddress)
{
	uint8_t data[JMPSIZE]{ 0xE9 };
	*(int32_t*)(data + 1) = (uint32_t)funcaddress - ((uint32_t)writeaddress + JMPSIZE);
	DWORD origprot;
	VirtualProtect(writeaddress, JMPSIZE, PAGE_EXECUTE_WRITECOPY, &origprot);
	memcpy(writeaddress, data, JMPSIZE);
}
#elif defined(_M_AMD64) || defined(__amd64__)
#define JMPSIZE 14
static inline void WriteJump(void* writeaddress, void* funcaddress)
{
	uint8_t data[JMPSIZE]{ 0xFF, 0x25 };
	*(void**)(data + 6) = funcaddress;
	DWORD origprot;
	VirtualProtect(writeaddress, JMPSIZE, PAGE_EXECUTE_WRITECOPY, &origprot);
	memcpy(writeaddress, data, JMPSIZE);
}
#endif
#endif

template<typename TRet, typename... TArgs>
class Func
{
public:
	typedef TRet(*PointerType)(TArgs...);

	Func(intptr_t address)
	{
		pointer = (decltype(pointer))address;
	}

	Func(void* address)
	{
		pointer = (decltype(pointer))address;
	}

	~Func()
	{
		if (ishooked)
			Unhook();
	}

	TRet operator()(TArgs... args)
	{
		return pointer(args...);
	}

	PointerType operator&()
	{
		return pointer;
	}

	operator PointerType()
	{
		return pointer;
	}

	void Hook(PointerType hookfunc)
	{
		if (ishooked)
			throw new std::exception("Attempted to hook already hooked function!");
		memcpy(origdata, pointer, JMPSIZE);
		WriteJump(pointer, hookfunc);
		ishooked = true;
	}

	void Unhook()
	{
		if (!ishooked)
			throw new std::exception("Attempted to unhook function that wasn't hooked!");
		memcpy(pointer, origdata, JMPSIZE);
		ishooked = false;
	}

	TRet Original(TArgs... args)
	{
		if (ishooked)
		{
			uint8_t hookdata[JMPSIZE];
			memcpy(hookdata, pointer, JMPSIZE);
			memcpy(pointer, origdata, JMPSIZE);
			TRet retval = pointer(args...);
			memcpy(pointer, hookdata, JMPSIZE);
			return retval;
		}
		else
			return pointer(args...);
	}

private:
	PointerType pointer = nullptr;
	bool ishooked = false;
	uint8_t origdata[JMPSIZE]{};
};

template<typename... TArgs>
class Func<void, TArgs...>
{
public:
	typedef void (*PointerType)(TArgs...);

	Func(intptr_t address)
	{
		pointer = (decltype(pointer))address;
	}

	Func(void* address)
	{
		pointer = (decltype(pointer))address;
	}

	~Func()
	{
		if (ishooked)
			Unhook();
	}

	void operator()(TArgs... args)
	{
		pointer(args...);
	}

	PointerType operator&()
	{
		return pointer;
	}

	operator PointerType()
	{
		return pointer;
	}

	void Hook(PointerType hookfunc)
	{
		if (ishooked)
			throw new std::exception("Attempted to hook already hooked function!");
		memcpy(origdata, pointer, JMPSIZE);
		WriteJump(pointer, hookfunc);
		ishooked = true;
	}

	void Unhook()
	{
		if (!ishooked)
			throw new std::exception("Attempted to unhook function that wasn't hooked!");
		memcpy(pointer, origdata, JMPSIZE);
		ishooked = false;
	}

	void Original(TArgs... args)
	{
		if (ishooked)
		{
			uint8_t hookdata[JMPSIZE];
			memcpy(hookdata, pointer, JMPSIZE);
			memcpy(pointer, origdata, JMPSIZE);
			pointer(args...);
			memcpy(pointer, hookdata, JMPSIZE);
		}
		else
			pointer(args...);
	}

private:
	PointerType pointer = nullptr;
	bool ishooked = false;
	uint8_t origdata[JMPSIZE]{};
};

Func<int32, const char*, uint32, uint32, uint32, bool32>* PlayStream;
int32 PlayStream_r(const char* filename, uint32 channel, uint32 startPos, uint32 loopPoint, bool32 loadASync)
{
	auto iter = songmap.find(filename);
	if (iter != songmap.cend())
	{
		SMPS_LoadAndPlaySong(iter->second);
		SMPS_SetVolume(0.5);
		SMPS_SetSongTempo(maniaGlobals->vapeMode ? 0.75 : 1);
		SMPS_ResumeSong();
		isSpeedUp = false;
		one_up = !strcmp(filename, "1up.ogg");
		smpsChannel = channel;
		return PlayStream->Original("SMPSDummy.ogg", channel, 0, 1, loadASync);
	}
	smpsChannel = -1;
	return PlayStream->Original(filename, channel, startPos, loopPoint, loadASync);
}

Func<void, uint32>* StopChannel;
void StopChannel_r(uint32 channelID)
{
	if (smpsChannel == channelID)
	{
		SMPS_StopSong();
		smpsChannel = -1;
	}
	StopChannel->Original(channelID);
}

Func<void, uint32>* PauseChannel;
void PauseChannel_r(uint32 channelID)
{
	if (smpsChannel == channelID)
		SMPS_PauseSong();
	PauseChannel->Original(channelID);
}

Func<void, uint32>* ResumeChannel;
void ResumeChannel_r(uint32 channelID)
{
	if (smpsChannel == channelID)
		SMPS_ResumeSong();
	ResumeChannel->Original(channelID);
}

Func<void, uint8, float, float, float>* SetChannelAttributes;
void SetChannelAttributes_r(uint8 channelID, float volume, float pan, float speed)
{
	if (smpsChannel == channelID)
	{
		SMPS_SetVolume(0.5 * volume);
		SMPS_SetSongTempo(speed);
	}
	else
		SetChannelAttributes->Original(channelID, volume, pan, speed);
}

Func<void, uint8>* Music_PlayJingle;
void Music_PlayJingle_r(uint8 trackID)
{
	if (smpsChannel != -1)
	{
		switch (trackID)
		{
		case 2:
			isSpeedUp = true;
			SMPS_SetSongTempo(maniaGlobals->vapeMode ? 0.9375 : 1.25);
			return;
		case 13:
			auto iter = songmap.find("1up.ogg");
			if (iter != songmap.cend())
			{
				SMPS_LoadAndPlaySong(iter->second);
				SMPS_SetVolume(0.5);
				SMPS_SetSongTempo(maniaGlobals->vapeMode ? 0.75 : 1);
				SMPS_ResumeSong();
				isSpeedUp = false;
				one_up = true;
				return;
			}
		}
	}
	Music_PlayJingle->Original(trackID);
}

void SongStoppedCallback()
{
	if (!one_up)
	{
		StopChannel->Original(smpsChannel);
		smpsChannel = -1;
	}
	one_up = false;
}

#define StateMachine(name) void (*name)(void)
#define TABLE(var, ...)  var
#define STATIC(var, val) var

struct ObjectPlayer : GameObject::Static {
	TABLE(int32 sonicPhysicsTable[64],
		{ 0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x68000, -0x40000, 0x30000, 0x600,  0xC00,  0x300, 0x4000, 0x300, 0x38000, -0x20000,
		  0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x80000, -0x40000, 0x50000, 0x1800, 0x3000, 0xC00, 0x8000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x68000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x80000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000 });
	TABLE(int32 tailsPhysicsTable[64],
		{ 0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x68000, -0x40000, 0x30000, 0x600,  0xC00,  0x300, 0x4000, 0x300, 0x38000, -0x20000,
		  0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x80000, -0x40000, 0x50000, 0x1800, 0x3000, 0xC00, 0x8000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x68000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x80000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000 });
	TABLE(int32 knuxPhysicsTable[64],
		{ 0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x60000, -0x40000, 0x30000, 0x600,  0xC00,  0x300, 0x4000, 0x300, 0x30000, -0x20000,
		  0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x60000, -0x40000, 0x50000, 0x1800, 0x3000, 0xC00, 0x8000, 0x300, 0x30000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x60000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x30000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x60000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x8000, 0x300, 0x30000, -0x20000 });
	TABLE(int32 mightyPhysicsTable[64],
		{ 0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x68000, -0x40000, 0x30000, 0x600,  0xC00,  0x300, 0x4000, 0x300, 0x38000, -0x20000,
		  0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x80000, -0x40000, 0x50000, 0x1800, 0x3000, 0xC00, 0x8000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x68000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x80000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000 });
	TABLE(int32 rayPhysicsTable[64],
		{ 0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x68000, -0x40000, 0x30000, 0x600,  0xC00,  0x300, 0x4000, 0x300, 0x38000, -0x20000,
		  0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x80000, -0x40000, 0x50000, 0x1800, 0x3000, 0xC00, 0x8000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x68000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000,
		  0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x80000, -0x40000, 0x60000, 0xC00,  0x1800, 0x600, 0x4000, 0x300, 0x38000, -0x20000 });
	TABLE(color superPalette_Sonic[18], { 0x000080, 0x0038C0, 0x0068F0, 0x1888F0, 0x30A0F0, 0x68D0F0, 0xF0C001, 0xF0D028, 0xF0E040, 0xF0E860,
										  0xF0E898, 0xF0E8D0, 0xF0D898, 0xF0E0B0, 0xF0E8C0, 0xF0F0D8, 0xF0F0F0, 0xF0F0F8 });
	TABLE(color superPalette_Tails[18], { 0x800801, 0xB01801, 0xD05001, 0xE07808, 0xE89008, 0xF0A801, 0xF03830, 0xF06848, 0xF09860, 0xF0B868,
										  0xF0C870, 0xF0D870, 0xF03830, 0xF06848, 0xF09860, 0xF0B868, 0xF0C870, 0xF0D870 });
	TABLE(color superPalette_Knux[18], { 0x580818, 0x980130, 0xD00840, 0xE82858, 0xF06080, 0xF08088, 0xF05878, 0xF06090, 0xF080A0, 0xF098B0, 0xF0B0C8,
										 0xF0C0C8, 0xF05878, 0xF06090, 0xF080A0, 0xF098B0, 0xF0B0C8, 0xF0C0C8 });
	TABLE(color superPalette_Mighty[18], { 0x501010, 0x882020, 0xA83030, 0xC84040, 0xE06868, 0xF09098, 0x701010, 0xD84040, 0xF05858, 0xF07878,
										   0xF0B8B8, 0xF0E0E8, 0x701010, 0xD84040, 0xF05858, 0xF07878, 0xF0B8B8, 0xF0E0E8 });
	TABLE(color superPalette_Ray[18], { 0xA06800, 0xB88810, 0xD0A810, 0xE0C020, 0xE8D038, 0xF0E078, 0xE0A801, 0xF0C820, 0xF0E820, 0xF0F040, 0xF0F068,
										0xF0F0B8, 0xE0A801, 0xF0C820, 0xF0E820, 0xF0F040, 0xF0F068, 0xF0F0B8 });
	TABLE(color superPalette_Sonic_HCZ[18], { 0x200888, 0x3020C8, 0x3840F0, 0x4070F0, 0x4098F0, 0x40C0F0, 0x88C880, 0x68E090, 0x50F098, 0x68F0C0,
											  0x78F0C8, 0xA0F0D8, 0x60E898, 0x48F0A0, 0x58F0B0, 0x68F0C0, 0x90F0D0, 0xA0F0D8 });
	TABLE(color superPalette_Tails_HCZ[18], { 0x880808, 0xA03810, 0xA05848, 0xB07058, 0xC08068, 0xC89078, 0xCC6161, 0xDC8462, 0xD5978A, 0xDEA893,
											  0xE6B09D, 0xEABAA7, 0xCC6161, 0xDC8462, 0xD5978A, 0xDEA893, 0xE6B09D, 0xEABAA7 });
	TABLE(color superPalette_Knux_HCZ[18], { 0x181050, 0x301090, 0x5018A8, 0x8828C0, 0xA048C0, 0xB868C8, 0x746DA3, 0x7F65D0, 0x9768E0, 0xC070EF,
											 0xD086EB, 0xDE9CED, 0x746DA3, 0x7F65D0, 0x9768E0, 0xC070EF, 0xD086EB, 0xDE9CED });
	TABLE(color superPalette_Mighty_HCZ[18], { 0x381058, 0x502098, 0x7028B0, 0x8048C8, 0x7868C8, 0x8098D0, 0x401078, 0x9038C0, 0x9068C0, 0x9890E0,
											   0xA8C0D8, 0xC0E8F0, 0x401078, 0x9038C0, 0x9068C0, 0x9890E0, 0xA8C0D8, 0xC0E8F0 });
	TABLE(color superPalette_Ray_HCZ[18], { 0x406090, 0x488890, 0x68A880, 0x70C080, 0x68D080, 0x50E888, 0x80B088, 0x78D090, 0x68F080, 0x50F098,
											0x90F0C0, 0xA8F0E0, 0x80B088, 0x78D090, 0x68F080, 0x50F098, 0x90F0C0, 0xA8F0E0 });
	TABLE(color superPalette_Sonic_CPZ[18], { 0x4000D8, 0x5800E0, 0x6810E0, 0x8020E0, 0xA020E0, 0xC040E0, 0xE04880, 0xE06890, 0xE078A8, 0xE078D8,
											  0xE080E0, 0xE080E0, 0xE080B0, 0xE080B0, 0xE080C0, 0xE080C0, 0xE080E0, 0xE080E0 });
	TABLE(color superPalette_Tails_CPZ[18], { 0xC80180, 0xD00178, 0xE00180, 0xE81088, 0xE83098, 0xE84898, 0xF078F0, 0xF078F0, 0xF080F0, 0xF088F0,
											  0xF098F0, 0xF0A0F0, 0xF078F0, 0xF078F0, 0xF080F0, 0xF088F0, 0xF098F0, 0xF0A0F0 });
	TABLE(color superPalette_Knux_CPZ[18], { 0xA00180, 0xB00178, 0xC00190, 0xD001B0, 0xE001E0, 0xE820E8, 0xF078D8, 0xF078E8, 0xF088F0, 0xF098F0,
											 0xF0A8F0, 0xF0B0F0, 0xF078D8, 0xF078E8, 0xF088F0, 0xF098F0, 0xF0A8F0, 0xF0B0F0 });
	TABLE(color superPalette_Mighty_CPZ[18], { 0xA00180, 0xD80188, 0xE001A0, 0xE001B0, 0xE001D8, 0xE001E0, 0xB80180, 0xE001A8, 0xE001C8, 0xE001E0,
											   0xE040E0, 0xE078E0, 0xB80180, 0xE001A8, 0xE001C8, 0xE001E0, 0xE040E0, 0xE078E0 });
	TABLE(color superPalette_Ray_CPZ[18], { 0xE00180, 0xE00190, 0xE02898, 0xE048A8, 0xE060B8, 0xE078E0, 0xE02880, 0xE05888, 0xE08088, 0xE080A8,
											0xE080D8, 0xE080E0, 0xE02880, 0xE05888, 0xE08088, 0xE080A8, 0xE080D8, 0xE080E0 });
	bool32 cantSwap;
	int32 playerCount;
	uint16 upState;
	uint16 downState;
	uint16 leftState;
	uint16 rightState;
	uint16 jumpPressState;
	uint16 jumpHoldState;
	int32 nextLeaderPosID;
	int32 lastLeaderPosID;
	Vector2 leaderPositionBuffer[16];
	Vector2 targetLeaderPosition;
	int32 autoJumpTimer;
	int32 respawnTimer;
	int32 aiInputSwapTimer;
	bool32 disableP2KeyCheck;
	int32 rings;
	STATIC(int32 ringExtraLife, 100);
	int32 powerups;
	STATIC(int32 savedLives, 3);
	int32 savedScore;
	STATIC(int32 savedScore1UP, 50000);
	uint16 sonicFrames;
	uint16 superFrames;
	uint16 tailsFrames;
	uint16 tailsTailsFrames;
	uint16 knuxFrames;
	uint16 mightyFrames;
	uint16 rayFrames;
	uint16 sfxJump;
	uint16 sfxRoll;
	uint16 sfxCharge;
	uint16 sfxRelease;
	uint16 sfxPeelCharge;
	uint16 sfxPeelRelease;
	uint16 sfxDropdash;
	uint16 sfxLoseRings;
	uint16 sfxHurt;
	uint16 sfxPimPom;
	uint16 sfxSkidding;
	uint16 sfxGrab;
	uint16 sfxFlying;
	bool32 playingFlySfx;
	uint16 sfxTired;
	bool32 playingTiredSfx;
	uint16 sfxLand;
	uint16 sfxSlide;
	uint16 sfxOuttahere;
	uint16 sfxTransform2;
	uint16 sfxSwap;
	uint16 sfxSwapFail;
	uint16 sfxMightyDeflect;
	uint16 sfxMightyDrill;
	uint16 sfxMightyLand;
	uint16 sfxMightyUnspin;
	int32 raySwoopTimer;
	int32 rayDiveTimer;
	bool32 gotHit[PLAYER_COUNT];
	StateMachine(configureGhostCB);
	bool32(*canSuperCB)(bool32 isHUD);
	int32 superDashCooldown;
};

struct EntityPlayer : GameObject::Entity {
	StateMachine(state);
	StateMachine(nextAirState);
	StateMachine(nextGroundState);
	void* camera;
	Animator animator;
	Animator tailAnimator;
	int32 minJogVelocity;
	int32 minRunVelocity;
	int32 minDashVelocity;
	int32 unused; // the only used variable in the player struct, I cant find a ref to it anywhere so...
	int32 tailRotation;
	int32 tailDirection;
	uint16 aniFrames;
	uint16 tailFrames;
	uint16 animationReserve; // what anim to return to after SpringTwirl/SpringDiagonal has finished and the player is falling downwards
	uint16 playerID;
	Hitbox* outerbox;
	Hitbox* innerbox;
	int32 characterID;
	int32 rings;
	int32 ringExtraLife;
	int32 shield;
	int32 lives;
	int32 score;
	int32 score1UP;
	bool32 hyperRing;
	int32 timer;
	int32 outtaHereTimer;
	int32 abilityTimer;
	int32 spindashCharge;
	int32 abilityValue;
	int32 drownTimer;
	int32 invincibleTimer;
	int32 speedShoesTimer;
	int32 blinkTimer;
	int32 scrollDelay;
	int32 skidding;
	int32 pushing;
	int32 underwater;     // 0 = not in water, 1 = in palette water, else water entityID
	bool32 groundedStore; // prev frame's onGround value
	bool32 invertGravity;
	bool32 isChibi;
	bool32 isTransforming;
	int32 superState;
	int32 superRingLossTimer;
	int32 superBlendAmount;
	int32 superBlendState;
	bool32 sidekick;
	int32 scoreBonus;
	int32 jumpOffset;
	int32 collisionFlagH;
	int32 collisionFlagV;
	int32 topSpeed;
	int32 acceleration;
	int32 deceleration;
	int32 airAcceleration;
	int32 airDeceleration;
	int32 skidSpeed;
	int32 rollingFriction;
	int32 rollingDeceleration;
	int32 gravityStrength;
	int32 abilitySpeed;
	int32 jumpStrength;
	int32 jumpCap;
	int32 flailing;
	int32 sensorX[5];
	int32 sensorY;
	Vector2 moveLayerPosition;
	StateMachine(stateInputReplay);
	StateMachine(stateInput);
	int32 controllerID;
	int32 controlLock;
	bool32 up;
	bool32 down;
	bool32 left;
	bool32 right;
	bool32 jumpPress;
	bool32 jumpHold;
	bool32 applyJumpCap;
	int32 jumpAbilityState;
	StateMachine(stateAbility);
	StateMachine(statePeelout);
	int32 flyCarryTimer;
	Vector2 flyCarrySidekickPos;
	Vector2 flyCarryLeaderPos;
	uint8 deathType;
	bool32 forceRespawn;
	bool32 isGhost;
	int32 abilityValues[8];
	void* abilityPtrs[8];
	int32 uncurlTimer;
};

struct Player : EntityPlayer {

	struct Static : ObjectPlayer {
	};

	struct ModStatic {};

	void Update()
	{
		int oldshoestimer = speedShoesTimer;
		modTable->Super(classID, SUPER_UPDATE, nullptr);
		if (isSpeedUp && oldshoestimer != 0 && speedShoesTimer == 0)
			SMPS_SetSongTempo(maniaGlobals->vapeMode ? 0.75 : 1);
	}

	MOD_DECLARE(Player);
};

MOD_REGISTER_OBJECT(Player);

struct ObjectBSS_Setup : GameObject::Static {
	uint8 randomNumbers[4]; // used to calculate the map (& colours) for Random BSS mode
	int32 sphereCount;
	int32 pinkSphereCount;
	int32 rings;
	int32 ringPan;
	int32 ringCount;
	int32 ringID; // updated in BSS_Collected, but aside from that it goes pretty much unused it seems
	uint16 bgLayer;
	uint16 globeLayer;
	uint16 frustum1Layer;
	uint16 frustum2Layer;
	uint16 playFieldLayer;
	uint16 ringCountLayer;
	uint16 globeFrames;
	TABLE(int32 globeFrameTable[0xF], { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0 });
	TABLE(int32 globeDirTableL[0xF], { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1 });
	TABLE(int32 globeDirTableR[0xF], { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 });
	TABLE(int32 screenYTable[0x70],
		{ 280, 270, 260, 251, 243, 235, 228, 221, 215, 208, 202, 197, 191, 185, 180, 175, 170, 165, 160, 155, 151, 147, 143,
		  139, 135, 131, 127, 124, 121, 117, 114, 111, 108, 105, 103, 100, 97,  95,  92,  90,  88,  86,  83,  81,  79,  77,
		  76,  74,  72,  70,  69,  67,  66,  64,  63,  62,  60,  59,  58,  57,  56,  55,  54,  53,  52,  51,  50,  49,  48,
		  47,  47,  46,  45,  45,  44,  44,  43,  43,  42,  42,  41,  40,  40,  40,  40,  40,  40,  40,  40,  39,  39,  39,
		  39,  39,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38,  38 });
	TABLE(int32 divisorTable[0x70],
		{ 4096, 4032, 3968, 3904, 3840, 3776, 3712, 3648, 3584, 3520, 3456, 3392, 3328, 3264, 3200, 3136, 3072, 2995, 2920, 2847, 2775, 2706, 2639,
		  2572, 2508, 2446, 2384, 2324, 2266, 2210, 2154, 2100, 2048, 2012, 1976, 1940, 1906, 1872, 1838, 1806, 1774, 1742, 1711, 1680, 1650, 1621,
		  1592, 1564, 1536, 1509, 1482, 1455, 1429, 1404, 1379, 1354, 1330, 1307, 1283, 1260, 1238, 1216, 1194, 1173, 1152, 1134, 1116, 1099, 1082,
		  1065, 1048, 1032, 1016, 1000, 985,  969,  954,  939,  925,  910,  896,  892,  888,  884,  880,  875,  871,  867,  863,  859,  855,  851,
		  848,  844,  840,  836,  832,  830,  828,  826,  824,  822,  820,  818,  816,  814,  812,  810,  808,  806,  804,  802 });
	TABLE(int32 xMultiplierTable[0x70],
		{ 134, 131, 128, 125, 123, 121, 119, 117, 115, 114, 112, 111, 109, 108, 106, 104, 104, 102, 100, 98, 97, 96, 94, 93, 92, 90, 89, 88,
		  86,  84,  83,  82,  80,  80,  79,  78,  77,  76,  74,  73,  72,  71,  70,  70,  68,  68,  67,  66, 65, 64, 63, 62, 61, 60, 60, 59,
		  58,  57,  57,  56,  55,  54,  53,  53,  52,  51,  51,  50,  50,  49,  48,  48,  47,  47,  46,  46, 45, 45, 44, 44, 44, 43, 43, 43,
		  42,  42,  42,  41,  41,  41,  41,  40,  40,  40,  40,  39,  39,  39,  39,  39,  38,  38,  38,  38, 38, 37, 37, 37, 37, 37, 36, 36 });
	TABLE(int32 frameTable[0x80],
		{ 31, 31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30, 29, 29, 29, 29, 29, 28, 28, 28, 28, 27, 27, 27, 26, 26, 26, 26, 25, 25, 25,
		  24, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22, 21, 21, 21, 21, 20, 20, 20, 20, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17,
		  16, 16, 16, 15, 15, 14, 14, 14, 13, 13, 13, 12, 12, 12, 11, 11, 10, 10, 10, 10, 9,  9,  9,  9,  8,  8,  8,  8,  7,  7,  7,  7,
		  6,  6,  6,  6,  5,  5,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 });
	Vector2 offsetTable[0x100];
	int32 offsetRadiusTable[0x100];
	int32 frustumCount[2];
	int32 frustumOffset[2];
	int32 unused1;
	uint16 playField[0x400];            // Active Spheres & Collectables (0x400 == 0x20 * 0x20 == BSS_PLAYFIELD_W * BSS_PLAYFIELD_H)
	uint16 sphereChainTable[0x400];     // Currently chained spheres     (0x400 == 0x20 * 0x20 == BSS_PLAYFIELD_W * BSS_PLAYFIELD_H)
	uint16 sphereCollectedTable[0x400]; // Spheres to turn into rings    (0x400 == 0x20 * 0x20 == BSS_PLAYFIELD_W * BSS_PLAYFIELD_H)
	uint16 sfxBlueSphere;
	uint16 sfxSSExit;
	uint16 sfxBumper;
	uint16 sfxSpring;
	uint16 sfxRing;
	uint16 sfxLoseRings;
	uint16 sfxSSJettison;
	uint16 sfxEmerald;
	uint16 sfxEvent;
	uint16 sfxMedal;
	uint16 sfxMedalCaught;
	uint16 sfxTeleport;
};

struct EntityBSS_Setup : GameObject::Entity {
	StateMachine(state);
	int32 spinTimer;
	int32 speedupTimer;
	int32 speedupInterval;
	int32 timer;
	int32 spinState;
	int32 palettePage;
	int32 unused1;
	int32 xMultiplier;
	int32 divisor;
	int32 speedupLevel;
	int32 globeSpeed;
	bool32 playerWasBumped;
	int32 globeSpeedInc;
	bool32 disableBumpers;
	int32 globeTimer;
	int32 paletteLine;
	int32 offsetDir;
	int32 unused2;
	Vector2 offset;
	Vector2 playerPos;
	Vector2 lastSpherePos;
	int32 unused3;
	bool32 completedRingLoop;
	int32 paletteID;
	int32 stopMovement;
	Animator globeSpinAnimator;
	Animator shadowAnimator;
};

struct BSS_Setup : EntityBSS_Setup {

	struct Static : ObjectBSS_Setup {
	};

	struct ModStatic {};

	void Update()
	{
		int oldlvl = speedupLevel;
		sVars->Super(SUPER_UPDATE);
		if (smpsChannel != -1 && speedupLevel != oldlvl)
		{
			double tempo = 1 + (1 / ((-(speedupLevel - 32) * 2 + 8) / 2.0));
			if (maniaGlobals->vapeMode)
				tempo *= 0.75;
			SMPS_SetSongTempo(tempo);
		}
	}

	MOD_DECLARE(BSS_Setup);
};

MOD_REGISTER_OBJECT(BSS_Setup);

void InitModAPI(void)
{
	PlayStream = new Func<int32, const char*, uint32, uint32, uint32, bool32>(RSDKTable->PlayStream);
	PlayStream->Hook(PlayStream_r);
	StopChannel = new Func<void, uint32>(RSDKTable->StopChannel);
	StopChannel->Hook(StopChannel_r);
	PauseChannel = new Func<void, uint32>(RSDKTable->PauseChannel);
	PauseChannel->Hook(PauseChannel_r);
	ResumeChannel = new Func<void, uint32>(RSDKTable->ResumeChannel);
	ResumeChannel->Hook(ResumeChannel_r);
	SetChannelAttributes = new Func<void, uint8, float, float, float>(RSDKTable->SetChannelAttributes);
	SetChannelAttributes->Hook(SetChannelAttributes_r);
	Music_PlayJingle = new Func<void, uint8>(Mod::GetPublicFunction(nullptr, "Music_PlayJingle"));
	Music_PlayJingle->Hook(Music_PlayJingle_r);
}

#define ADD_PUBLIC_FUNC(func) Mod.AddPublicFunction(#func, (void *)(func))

extern "C" __declspec(dllexport) bool32 LinkModLogic(EngineInfo * info, const char* id)
{
#if !RETRO_REV01
	LinkGameLogicDLL(info);
#else
	LinkGameLogicDLL(*info);
#endif

	maniaGlobals = (ManiaGlobalVariables*)Mod::GetGlobals();

	char buf[_MAX_PATH];
	sprintf_s(buf, "mods\\%s\\SMPSPlay.dll", id);
	HMODULE handle = LoadLibraryA(buf);
	if (!handle)
	{
		Dev::Print(Dev::PRINT_ERROR, "SMPSPlay DLL failed to load!");
		return false;
	}

	((BOOL(*)())GetProcAddress(handle, "SMPS_InitializeDriver"))();
	((void(*)(void(*callback)()))GetProcAddress(handle, "SMPS_RegisterSongStoppedCallback"))(SongStoppedCallback);
	SMPS_LoadAndPlaySong = (decltype(SMPS_LoadAndPlaySong))GetProcAddress(handle, "SMPS_LoadAndPlaySong");
	SMPS_StopSong = (decltype(SMPS_StopSong))GetProcAddress(handle, "SMPS_StopSong");
	SMPS_PauseSong = (decltype(SMPS_PauseSong))GetProcAddress(handle, "SMPS_PauseSong");
	SMPS_ResumeSong = (decltype(SMPS_ResumeSong))GetProcAddress(handle, "SMPS_ResumeSong");
	SMPS_SetSongTempo = (decltype(SMPS_SetSongTempo))GetProcAddress(handle, "SMPS_SetSongTempo");
	SMPS_SetVolume = (decltype(SMPS_SetVolume))GetProcAddress(handle, "SMPS_SetVolume");
	SMPS_SetVolume(0.5);
	unsigned int trackCnt;
	auto tracks = ((const char** (*)(unsigned int& count))GetProcAddress(handle, "SMPS_GetSongNames"))(trackCnt);
	std::unordered_map<std::string, short> smpsmap;
	for (size_t i = 0; i < trackCnt; i++)
		smpsmap[tracks[i]] = (short)i;

	for (auto& str : Mod::Config::Get())
	{
		if (str.size > 5)
		{
			char* buf = new char[str.size + 1];
			str.CStr(buf);
			if (!memcmp(buf, "SMPS:", 5))
			{
				String val;
				Mod::Config::GetString(buf, &val, "");
				char* buf2 = new char[val.size + 1];
				val.CStr(buf2);
				auto iter = smpsmap.find(buf2);
				if (iter != smpsmap.cend())
					songmap[buf + 5] = iter->second;
				delete[] buf2;
			}
			delete[] buf;
		}
	}

	InitModAPI();

	return true;
}
