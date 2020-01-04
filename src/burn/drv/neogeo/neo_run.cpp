#include "neogeo.h"
#include "cd_interface.h"
#include "burn_ym2610.h"
#include "bitswap.h"
#include "neocdlist.h"

// #undef USE_SPEEDHACKS

// #define LOG_IRQ
// #define LOG_DRAW

//#define CYCLE_LOG

#define NEO_HREFRESH (15625.0)
#define NEO_VREFRESH (NEO_HREFRESH / 264.0)
#define NEO_CDVREFRESH (6041957.0 / (264 * 384))

// If defined, enable emulation of the watchdog timer (timing doesn't match real hardware 100%)
#define EMULATE_WATCHDOG

// If defined, reset the Z80 when switching between the Z80 BIOS/cartridge ROM
//#define Z80_RESET_ON_BANKSWITCH

// If defined, overclock CDZ
#define OVERCLOCK_CDZ

// If defined, adjust the Z80 speed along with the 68000 when overclocking
#define Z80_SPEED_ADJUST

// If defined, use kludges to better align raster effects in some games (e.g. mosyougi)
#define RASTER_KLUDGE

// If defined, use the bAllowRasters variable to enable/disable raster effects
// #define RASTERS_OPTIONAL

#if defined Z80_SPEED_ADJUST
 static INT32 nZ80Clockspeed;
#else
 static const INT32 nZ80Clockspeed = 4000000;
#endif

#if defined RASTER_KLUDGE
 static UINT16 nScanlineOffset;
#else
 // 0xF8 is correct as verified on MVS hardware
 static const UINT16 nScanlineOffset = 0xF8;
#endif

#if defined RASTERS_OPTIONAL
 static bool bAllowRasters = false;
#endif

// The number of cartridge slots on the emulated MVS hardware (can be 1, 2, 4, or 6)
UINT8 nNeoNumSlots = 1;

UINT32 nNeoActiveSlot = 0;

UINT8 NeoButton1[32] = { 0, };
UINT8 NeoButton2[8]  = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoButton3[8]  = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoButton4[8]  = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoJoy1[8]	 = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoJoy2[8]     = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoJoy3[8]     = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT8 NeoJoy4[8]     = { 0, 0, 0, 0, 0, 0, 0, 0 };
UINT16 NeoAxis[2]	 = { 0, 0 };
UINT8 NeoInput[32]   = { 0, };
UINT8 NeoDiag[2]	 = { 0, 0 };
UINT8 NeoDebugDip[2] = { 0, 0 };
UINT8 NeoReset = 0, NeoSystem = 0;

static UINT8 OldDebugDip[2] = { 0, 0 };

// Which 68K BIOS to use
INT32 nBIOS;

#if defined CYCLE_LOG
// for debugging -dink (will be removed later)
static INT32 cycderp[16384+1];
static INT32 derpframe;
#endif

// Joyports are multiplexed
static INT32 nJoyport0[8] = { 0, };
static INT32 nJoyport1[8] = { 0, };

// Ports always mirror the joystick, except when other controllers are hooked up:
//
//         input no.       corresponds to
//
// Joyport0: 0x00		 : trackball X in irrmaze / p1 paddle in popbounc
//			 0x01		 : trackball Y in irrmaze
//			 0x09		 : mahjong controller
//			 0x12		 : mahjong controller (BIOS controls)
//			 0x1B		 : Always has p1 joystick & buttons
//			 0x24		 : mahjong controller
//			 0x20 & 0x21 : selected by irrmaze instead of 0x00 & 0x01 when you lose a life
//						   (activates air-jets)
// Joyport1: 0x00		 : p2 paddle in popbounc
//			 0x1B		 : Always has p2 joystick & buttons

// ----------------------------------------------------------------------------
// Variables that need to be included in savestates

static INT32 nCyclesExtra[2];
static INT32 nPrevBurnCPUSpeedAdjust;

bool bNeoEnableGraphics;
bool bNeoEnableSprites;
bool bNeoEnableText;

static INT32 nNeoCDSpriteSlot;	// for "blank sprites / text" during transfer (needs to be separate from bNeoEnableSprite/Text!
static INT32 nNeoCDTextSlot;	// ""

UINT32 nNeo68KROMBank;

static INT32 nIRQAcknowledge;

static INT32 nIRQControl;
static bool bSRAMWritable;

static bool b68KBoardROMBankedIn;
static bool bZ80BoardROMBankedIn;
static INT32 nZ80Bank0, nZ80Bank1, nZ80Bank2, nZ80Bank3;

static UINT8* NeoGraphicsRAMBank;
static UINT16 NeoGraphicsRAMPointer;
static INT32 nNeoGraphicsModulo;

INT32 nNeoSpriteFrame;

static INT32 nSpriteFrameSpeed;
static INT32 nSpriteFrameTimer;

static UINT8 nSoundLatch;
static UINT8 nSoundReply;
static UINT32 nSoundStatus;

#if 1 && defined USE_SPEEDHACKS
static INT32 nSoundPrevReply;
#endif

INT32 s1945pmode = 0;
INT32 fatfury2mode = 0; // fatfury2 protection active (fatfury2, ssideki)
INT32 vlinermode = 0;

static INT32 nInputSelect;
static UINT8* NeoInputBank;
static UINT32 nAnalogAxis[2] = { 0, 0 };

static UINT32 nuPD4990ATicks;

static UINT32 nIRQOffset;

#define NO_IRQ_PENDING (0x7FFFFFFF)
static INT32 nIRQCycles;

#if defined EMULATE_WATCHDOG
static INT32 nNeoWatchdog;
#endif

bool bDisableNeoWatchdog = false;

static INT32 bNeoCDIRQEnabled = false;
static INT32 nNeoCDIRQVector;

INT32 nNeoScreenWidth;

UINT8 nLEDLatch, nLED[3];

// ----------------------------------------------------------------------------

static bool bMemoryCardInserted, bMemoryCardWritable;

NEO_CALLBACK NeoCallback[MAX_SLOT] = { { NULL, NULL, NULL, NULL, NULL }, };
NEO_CALLBACK* NeoCallbackActive = &NeoCallback[0];

static INT32 nCyclesTotal[2];
static INT32 nCyclesSegment;
static INT32 nCyclesVBlank;
static INT32 nCycles68KSync;

UINT8 *Neo68KROM[MAX_SLOT] = { NULL, }, *Neo68KROMActive = NULL;
UINT8 *NeoVector[MAX_SLOT] = { NULL, }, *NeoVectorActive = NULL;
UINT8 *Neo68KFix[MAX_SLOT] = { NULL, };
UINT8 *NeoZ80ROM[MAX_SLOT] = { NULL, }, *NeoZ80ROMActive = NULL;

static UINT8 *AllRAM = NULL, *RAMEnd = NULL, *AllROM = NULL, *ROMEnd = NULL;

UINT8 *NeoSpriteRAM, *NeoTextRAM;

UINT8 *Neo68KBIOS, *NeoZ80BIOS;
static UINT8 *Neo68KRAM, *NeoZ80RAM, *NeoNVRAM, *NeoNVRAM2, *NeoMemoryCard;

static UINT32 nSpriteSize[MAX_SLOT] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static UINT32 nCodeSize[MAX_SLOT] = { 0, 0, 0, 0, 0, 0, 0, 0 };

UINT8* NeoGraphicsRAM;

UINT8* YM2610ADPCMAROM[MAX_SLOT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
UINT8* YM2610ADPCMBROM[MAX_SLOT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static INT32 nYM2610ADPCMASize[MAX_SLOT] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static INT32 nYM2610ADPCMBSize[MAX_SLOT] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static bool bIRQEnabled;
static INT32 nVBLankIRQ; // init setting
static INT32 nScanlineIRQ; // init setting

static bool bRenderImage;

static bool bRenderLineByLine;

static bool bForcePartialRender;
static bool bForceUpdateOnStatusRead;

static INT32 nNeoControlConfig;

static INT32 nNeoSystemType;
static bool bZ80BIOS;

static INT32 nNeoCDCyclesIRQ = 0, nNeoCDCyclesIRQPeriod = 0;

#ifdef BUILD_A68K
static bool bUseAsm68KCoreOldValue = false;
#endif

// NeoGeo CD-ROM Stuff
static INT32 nLC8951Register = 0;
static INT32 LC8951RegistersR[16];
static INT32 LC8951RegistersW[16];

static INT32 nActiveTransferArea;
static INT32 nSpriteTransferBank;
static INT32 nADPCMTransferBank;

static UINT8 nTransferWriteEnable; // not used

static bool NeoCDOBJBankUpdate[4];

static bool bNeoCDCommsClock, bNeoCDCommsSend;

static UINT8 NeoCDCommsCommandFIFO[10] = { 0, };
static UINT8 NeoCDCommsStatusFIFO[10]  = { 0, };

static INT32 NeoCDCommsWordCount = 0;

static INT32 NeoCDAssyStatus  = 0;

static INT32 NeoCDSectorMin = 0;
static INT32 NeoCDSectorSec = 0;
static INT32 NeoCDSectorFrm = 0;
static INT32 NeoCDSectorLBA = 0;

static char NeoCDSectorData[2352];

static bool bNeoCDLoadSector = false;

static INT32 NeoCDDMAAddress1 = 0;
static INT32 NeoCDDMAAddress2 = 0;
static INT32 NeoCDDMAValue1   = 0;
static INT32 NeoCDDMAValue2   = 0;
static INT32 NeoCDDMACount    = 0;

static INT32 NeoCDDMAMode     = 0;
static INT32 NeoCDVectorSwitch = 0; // 1 ROM(ram), 0 BIOS

static INT32 nNeoCDMode = 0;
static INT32 nff0002 = 0;
static INT32 nff0004 = 0;


bool IsNeoGeoCD() {
	//return (nNeoSystemType & NEO_SYS_CD);
	return ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOCD);
}

// This function is called once to determine how much memory is needed (RAMEnd-(UINT8 *)0),
// then a second time after the memory is allocated to set up all the pointers.
static INT32 RAMIndex()
{
	UINT8* Next = AllRAM;

	NeoPalSrc[0]		= Next; Next += 0x002000;		// Palette RAM Bank 0
	NeoPalSrc[1]		= Next; Next += 0x002000;		// Palette RAM Bank 1

	NeoGraphicsRAM		= Next; Next += 0x020000;		// Graphics controller RAM (2 64KB banks)

	if (nNeoSystemType & NEO_SYS_CART) {
		Neo68KRAM		= Next; Next += 0x010000;		// 68K work RAM
		NeoZ80RAM		= Next; Next += 0x000800;		// Z80 RAM

		NeoNVRAM		= Next; Next += 0x010000;		// Battery backed SRAM
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) == HARDWARE_SNK_GAMBLING) {
		NeoNVRAM2	= Next; Next += 0x002000;			// Extra SRAM for vliner/jockeygp
	}
	NeoMemoryCard		= Next; Next += 0x020000;		// Memory card

	if (nNeoSystemType & NEO_SYS_CD) {
		NeoSpriteRAM	= Next; Next += nSpriteSize[0];
		NeoTextRAM		= Next; Next += nNeoTextROMSize[0];
	}

	RAMEnd				= Next;

	return 0;
}

// This function is called once to determine how much memory is needed (ROMEnd-(UINT8*)0),
// then a second time after the memory is allocated to set up all the pointers.
static INT32 ROMIndex()
{
	UINT8* Next = AllROM;

	NeoZ80BIOS				= Next; Next += 0x020000;				// Z80 boardROM
	NeoZoomROM				= Next; Next += 0x020000;				// Y Zoom table
	NeoTextROMBIOS			= Next; Next += 0x020000;

	if (nNeoSystemType & NEO_SYS_CART) {
		Neo68KBIOS		= Next; Next += 0x080000;				// 68K boardROM
	} else {
		Neo68KROM[0]		= Next; Next += nCodeSize[0];
		NeoVector[0]		= Next; Next += 0x000400;				// Copy of 68K cartridge ROM with boardROM vector table
		Neo68KBIOS			= Next; Next += 0x080000;				// 68K boardROM

		NeoZ80ROM[0]		= Next; Next += 0x080000;
		NeoSpriteROM[0]		= Next; Next += nSpriteSize[0];
		NeoTextROM[0]		= Next; Next += nNeoTextROMSize[0];
		YM2610ADPCMAROM[0]	= Next; Next += nYM2610ADPCMASize[0];
		YM2610ADPCMBROM[0]	= Next; Next += nYM2610ADPCMBSize[0];
	}

	ROMEnd					= Next;

	return 0;
}

// -----------------------------------------------------------------------------
// ROM loading

static void NeoSetSystemType()
{
	// Neo CD
	if (nNeoSystemType & NEO_SYS_CD) {
		return;
	}
	// Dedicated JAMMA PCB
	if (nNeoSystemType & NEO_SYS_PCB) {
		return;
	}

	// See if we're emulating MVS or AES hardware
	if (nBIOS == -1 || nBIOS == 15 || nBIOS == 16 || nBIOS == 17 || ((NeoSystem & 0x74) == 0x20)) {
		nNeoSystemType = NEO_SYS_CART | NEO_SYS_AES;
		return;
	}

	nNeoSystemType = NEO_SYS_CART | NEO_SYS_MVS;
}

static INT32 NeoLoad68KBIOS(INT32 nNewBIOS)
{
	// Neo CD
	if (nNeoSystemType & NEO_SYS_CD) {
		return 0;
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) == HARDWARE_SNK_TRACKBALL) {
		nNewBIOS = 34;
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_DEDICATED_PCB) {
		nNewBIOS = 35;
	}

	// The most recent MVS models doesn't have a Z80 BIOS
	bZ80BIOS = (nNewBIOS != 0) ? true : false;

	// Check if we need to load a new BIOS
	if (nNewBIOS == nBIOS) {
		return 0;
	}

	nBIOS = nNewBIOS;

	// Load the BIOS ROMs
	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_MVS) {
			// Load the BIOS ROMs
			BurnLoadRom(Neo68KBIOS, 0x00000 + nBIOS, 1);
	} else {
		if (nBIOS >= 0) {
			BurnLoadRom(Neo68KBIOS, 0x00080 + nBIOS, 1);
		} else {
			BurnLoadRom(Neo68KBIOS, 0x00080 +     0, 1);
		}
	}

	if (!strcmp(BurnDrvGetTextA(DRV_NAME), "kf2k3pcb") || !strcmp(BurnDrvGetTextA(DRV_NAME), "k2k3pcbd")) kf2k3pcb_bios_decode();

	NeoUpdateVector();

	return 0;
}

static INT32 FindType(const char* pName)
{
	INT32 i = 0;

	while (pName[i] && pName[i] != '-' && pName[i] != '_') {
		i++;
	}

	return i + 1;
}

static INT32 FindROMs(UINT32 nType, INT32* pOffset, INT32* pNum)
{
	INT32 nOffset = -1;
	INT32 nNum = -1;

	struct BurnRomInfo ri;
	ri.nType = 0;
	ri.nLen = 0;

	// Invalidate indices
	if (pOffset) {
		*pOffset = -1;
	}
	if (pNum) {
		*pNum = 0;
	}

	do {
		if (BurnDrvGetRomInfo(&ri, ++nOffset)) {
			return 1;
		}
	} while ((ri.nType & 7) != nType && nOffset < 0x80);

	if (nOffset >= 0x7F) {
		return 1;
	}

	do {
		if (BurnDrvGetRomInfo(&ri, nOffset + ++nNum)) {
			break;
		}
	} while ((ri.nType & 7) == nType && nOffset < 0x80);

	if (pOffset) {
		*pOffset = nOffset;
	}
	if (pNum) {
		*pNum = nNum >= 0 ? nNum : 0;
	}

	return 0;
}

static INT32 LoadRoms()
{
	NeoGameInfo info;
	NeoGameInfo* pInfo = &info;

	{
		struct BurnRomInfo ri;

		ri.nType = 0;
		ri.nLen = 0;

		// Find 'P' ROMs
		FindROMs(1, &pInfo->nCodeOffset, &pInfo->nCodeNum);
		// Find 'S' ROM
		FindROMs(2, &pInfo->nTextOffset, NULL);
		// Find 'C' ROMs
		FindROMs(3, &pInfo->nSpriteOffset, &pInfo->nSpriteNum);
		// Find 'M' ROM
		FindROMs(4, &pInfo->nSoundOffset, NULL);
		// Find 'V' ROMs
		FindROMs(5, &pInfo->nADPCMOffset, &pInfo->nADPCMANum);
		FindROMs(6, NULL, &pInfo->nADPCMBNum);

		if (pInfo->nADPCMBNum < 0) {
			pInfo->nADPCMBNum = 0;
		}

#if 1 && defined FBNEO_DEBUG
		bprintf(PRINT_IMPORTANT, _T("  - P: %i (%i);"), pInfo->nCodeOffset, pInfo->nCodeNum);
		if (pInfo->nTextOffset >= 0) {
			bprintf(PRINT_IMPORTANT, _T(" S: %i;"), pInfo->nTextOffset);
		} else {
			bprintf(PRINT_IMPORTANT, _T(" S: unused;"));
		}
		bprintf(PRINT_IMPORTANT, _T(" C: %i (%i); M: %i"), pInfo->nSpriteOffset, pInfo->nSpriteNum, pInfo->nSoundOffset);
		if (pInfo->nADPCMOffset >= 0) {
			bprintf(PRINT_IMPORTANT, _T(" V: %i (%i, %i)"), pInfo->nADPCMOffset, pInfo->nADPCMANum, pInfo->nADPCMBNum);
		} else {
			bprintf(PRINT_IMPORTANT, _T(" V: unused"));
		}
		bprintf(PRINT_IMPORTANT,_T("\n"));
#endif

		nCodeSize[nNeoActiveSlot] = 0;
		for (INT32 i = 0; i < pInfo->nCodeNum; i++) {
			BurnDrvGetRomInfo(&ri, pInfo->nCodeOffset + i);
			nCodeSize[nNeoActiveSlot] += ri.nLen;
		}
		nCodeSize[nNeoActiveSlot] = (nCodeSize[nNeoActiveSlot] + 0x0FFFFF) & ~0x0FFFFF;

		nSpriteSize[nNeoActiveSlot] = 0;

		if (BurnDrvGetHardwareCode() & HARDWARE_SNK_SWAPC) {
			BurnDrvGetRomInfo(&ri, pInfo->nSpriteOffset);
			// for viewpoin, aof, ssideki
			if (pInfo->nSpriteNum == 2) {
				nSpriteSize[nNeoActiveSlot] = 0x600000;
			}
			// for kotm2
			if (pInfo->nSpriteNum == 4) {
				BurnDrvGetRomInfo(&ri, pInfo->nSpriteOffset + 2);
				if (ri.nLen == 0x080000) {
					nSpriteSize[nNeoActiveSlot] = 0x600000;
				}
			}
		}

		if (nSpriteSize[nNeoActiveSlot] == 0) {

			// Compute correct size taking gaps into account (kizuna)
			for (INT32 i = 0; i < pInfo->nSpriteNum - 2; i += 2) {
				BurnDrvGetRomInfo(&ri, pInfo->nSpriteOffset + i);
				if (ri.nLen > nSpriteSize[nNeoActiveSlot]) {
					nSpriteSize[nNeoActiveSlot] = ri.nLen;
				}
			}
			nSpriteSize[nNeoActiveSlot] *= pInfo->nSpriteNum - 2;

			if (!strcmp("kof97oro", BurnDrvGetTextA(DRV_NAME))) nSpriteSize[nNeoActiveSlot] = 0x2400000;
			if (!strcmp("neon", BurnDrvGetTextA(DRV_NAME))) nSpriteSize[nNeoActiveSlot] = 0x80000;

			// The final 2 ROMs may have a different size
			BurnDrvGetRomInfo(&ri, pInfo->nSpriteOffset + pInfo->nSpriteNum - 2);
			nSpriteSize[nNeoActiveSlot] += ri.nLen * 2;
		}

		{
			UINT32 nSize = nSpriteSize[nNeoActiveSlot];
//			if (nSize > 0x4000000) {
//				nSize = 0x4000000;
//			}

			for (nNeoTileMask[nNeoActiveSlot] = 1; nNeoTileMask[nNeoActiveSlot] < nSize; nNeoTileMask[nNeoActiveSlot] <<= 1) { }
			nNeoTileMask[nNeoActiveSlot] = (nNeoTileMask[nNeoActiveSlot] >> 7) - 1;
			nNeoMaxTile[nNeoActiveSlot] = nSize >> 7;
		}

		if (nNeoTextROMSize[nNeoActiveSlot] == 0) {
			if (pInfo->nTextOffset > 0) {
				BurnDrvGetRomInfo(&ri, pInfo->nTextOffset);
				nNeoTextROMSize[nNeoActiveSlot] = ri.nLen;
			} else {
				nNeoTextROMSize[nNeoActiveSlot] = 0x080000;
			}
		}

		nYM2610ADPCMASize[nNeoActiveSlot] = nYM2610ADPCMBSize[nNeoActiveSlot] = 0;
		if (pInfo->nADPCMOffset >= 0)	{
			char* pName;
			BurnDrvGetRomInfo(&ri, pInfo->nADPCMOffset);
			BurnDrvGetRomName(&pName, pInfo->nADPCMOffset, 0);
			nYM2610ADPCMASize[nNeoActiveSlot] = ri.nLen;

			if (pInfo->nADPCMANum > 1) {
				BurnDrvGetRomInfo(&ri, pInfo->nADPCMOffset + pInfo->nADPCMANum - 1);
				BurnDrvGetRomName(&pName, pInfo->nADPCMOffset + pInfo->nADPCMANum - 1, 0);
				if (pInfo->nADPCMBNum == 0) {
					nYM2610ADPCMASize[nNeoActiveSlot] *= pName[FindType(pName) + 1] - '1';
				} else {
					nYM2610ADPCMASize[nNeoActiveSlot] *= pName[FindType(pName) + 2] - '1';
				}
				nYM2610ADPCMASize[nNeoActiveSlot] += ri.nLen;
			}

			if (pInfo->nADPCMBNum) {
				BurnDrvGetRomInfo(&ri, pInfo->nADPCMOffset + pInfo->nADPCMANum);
				nYM2610ADPCMBSize[nNeoActiveSlot] = ri.nLen * (pInfo->nADPCMBNum - 1);
				BurnDrvGetRomInfo(&ri, pInfo->nADPCMOffset + pInfo->nADPCMANum + pInfo->nADPCMBNum - 1);
				nYM2610ADPCMBSize[nNeoActiveSlot] += ri.nLen;
			}
		}
	}

	if (!strcmp("kof2k4se", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x800000;
	if (!strcmp("cphd", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x4000000;
	if (!strcmp("kf2k4pls", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x800000;
	if (!strcmp("svcboot", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x400000;
	if (!strcmp("svcplus", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x400000;
	if (!strcmp("svcplusa", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x400000;
	if (!strcmp("svcsplus", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] += 0x400000;
	if (!strcmp("pbobblenb", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x380000;
	if (!strcmp("alpham2p", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x200000;
	if (!strcmp("burningfp", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x180000;
	if (!strcmp("burningfpa", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x200000;
	if (!strcmp("gpilotsp", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x180000;
	if (!strcmp("lresortp", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x200000;
	if (!strcmp("kotm2p", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x300000;
	if (!strcmp("viewpoinp", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x400000;
	if (!strcmp("sbp", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x800000;
	if (!strcmp("lasthope", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x600000;
	if (!strcmp("mslug5w", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x10002f0;
	if (!strcmp("kof2k2omg", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x1000000;
	if (!strcmp("kof2k2omg9b", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x1000000;
	if (!strcmp("kof2k2omg9", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x1000000;
	if (!strcmp("kof98pfe", BurnDrvGetTextA(DRV_NAME))) nYM2610ADPCMASize[nNeoActiveSlot] = 0x1000000;

//	bprintf(PRINT_NORMAL, _T("%x\n"), nYM2610ADPCMASize[nNeoActiveSlot]);

	// The kof2k3 PCB has 96MB of graphics ROM, however the last 16MB are unused, and the protection/decryption hardware does not see them
//	if (nSpriteSize[nNeoActiveSlot] > 0x4000000) {
//		nSpriteSize[nNeoActiveSlot] = 0x5000000;
//	}

	NeoSpriteROM[nNeoActiveSlot] = (UINT8*)BurnMalloc(nSpriteSize[nNeoActiveSlot] < (nNeoTileMask[nNeoActiveSlot] << 7) ? ((nNeoTileMask[nNeoActiveSlot] + 1) << 7) : nSpriteSize[nNeoActiveSlot]);
	if (NeoSpriteROM[nNeoActiveSlot] == NULL) {
		return 1;
	}

/*	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_DEDICATED_PCB) {
		BurnSetProgressRange(1.0 / ((double)nSpriteSize[nNeoActiveSlot] / 0x800000 / 12));
	} else if (BurnDrvGetHardwareCode() & (HARDWARE_SNK_CMC42 | HARDWARE_SNK_CMC50)) {
		BurnSetProgressRange(1.0 / ((double)nSpriteSize[nNeoActiveSlot] / 0x800000 /  9));
	} else {
		BurnSetProgressRange(1.0 / ((double)nSpriteSize[nNeoActiveSlot] / 0x800000 /  3));
	}*/

	if (BurnDrvGetHardwareCode() & (HARDWARE_SNK_CMC42 | HARDWARE_SNK_CMC50)) {
		double fRange = (double)pInfo->nSpriteNum / 4.0;
		if (fRange < 1.5) {
			fRange = 1.5;
		}
		BurnSetProgressRange(1.0 / fRange);
	} else {
		BurnSetProgressRange(1.0 / pInfo->nSpriteNum);
	}

	// Load sprite data
	NeoLoadSprites(pInfo->nSpriteOffset, pInfo->nSpriteNum, NeoSpriteROM[nNeoActiveSlot], nSpriteSize[nNeoActiveSlot]);

	NeoTextROM[nNeoActiveSlot] = (UINT8*)BurnMalloc(nNeoTextROMSize[nNeoActiveSlot]);
	if (NeoTextROM[nNeoActiveSlot] == NULL) {
		return 1;
	}

	// Load Text layer tiledata
	{
		if (pInfo->nTextOffset != -1) {
			// Load S ROM data
			BurnLoadRom(NeoTextROM[nNeoActiveSlot], pInfo->nTextOffset, 1);
		} else {
			// Extract data from the end of C ROMS
			BurnUpdateProgress(0.0, _T("Decrypting text layer graphics...")/*, BST_DECRYPT_TXT*/, 0);
			NeoCMCExtractSData(NeoSpriteROM[nNeoActiveSlot], NeoTextROM[nNeoActiveSlot], nSpriteSize[nNeoActiveSlot], nNeoTextROMSize[nNeoActiveSlot]);

			if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_DEDICATED_PCB) {
				for (INT32 i = 0; i < nNeoTextROMSize[nNeoActiveSlot]; i++) {
					NeoTextROM[nNeoActiveSlot][i] = BITSWAP08(NeoTextROM[nNeoActiveSlot][i] ^ 0xd2, 4, 0, 7, 2, 5, 1, 6, 3);
				}
			}
		}
	}

	Neo68KROM[nNeoActiveSlot] = (UINT8*)BurnMalloc(nCodeSize[nNeoActiveSlot]);	// 68K cartridge ROM
	if (Neo68KROM[nNeoActiveSlot] == NULL) {
		return 1;
	}
	Neo68KROMActive = Neo68KROM[nNeoActiveSlot];
	Neo68KFix[nNeoActiveSlot] = Neo68KROM[nNeoActiveSlot];

	// Load the roms into memory
	if (BurnDrvGetHardwareCode() & HARDWARE_SNK_SMA_PROTECTION) {
		BurnLoadRom(Neo68KROMActive + 0x0C0000, 0, 1);
		NeoLoadCode(pInfo->nCodeOffset + 1, pInfo->nCodeNum - 1, Neo68KROMActive + 0x100000);
	} else {
		NeoLoadCode(pInfo->nCodeOffset, pInfo->nCodeNum, Neo68KROMActive);
	}

	NeoZ80ROM[nNeoActiveSlot] = (UINT8*)BurnMalloc(0x080000);	// Z80 cartridge ROM
	if (NeoZ80ROM[nNeoActiveSlot] == NULL) {
		return 1;
	}
	NeoZ80ROMActive = NeoZ80ROM[nNeoActiveSlot];

	BurnLoadRom(NeoZ80ROMActive, pInfo->nSoundOffset, 1);
	if (BurnDrvGetHardwareCode() & HARDWARE_SNK_ENCRYPTED_M1) {
		neogeo_cmc50_m1_decrypt();
	}

	if (NeoCallbackActive && NeoCallbackActive->pInitialise) {
		NeoCallbackActive->pInitialise();
	}

	// Decode text data
	BurnUpdateProgress(0.0, _T("Preprocessing text layer graphics...")/*, BST_PROCESS_TXT*/, 0);
	NeoDecodeText(0, nNeoTextROMSize[nNeoActiveSlot], NeoTextROM[nNeoActiveSlot], NeoTextROM[nNeoActiveSlot]);

	// Decode sprite data
	NeoDecodeSprites(NeoSpriteROM[nNeoActiveSlot], nSpriteSize[nNeoActiveSlot]);

	if (pInfo->nADPCMANum) {
		char* pName;
		struct BurnRomInfo ri;
		UINT8* pADPCMData;

		YM2610ADPCMAROM[nNeoActiveSlot]	= (UINT8*)BurnMalloc(nYM2610ADPCMASize[nNeoActiveSlot]);
		if (YM2610ADPCMAROM[nNeoActiveSlot] == NULL) {
			return 1;
		}

		ri.nType = 0;
		ri.nLen = 0;
		BurnDrvGetRomInfo(&ri, pInfo->nADPCMOffset);
		BurnDrvGetRomName(&pName, pInfo->nADPCMOffset, 0);

		pADPCMData = YM2610ADPCMAROM[nNeoActiveSlot];

		if (strcmp(BurnDrvGetTextA(DRV_NAME), "sbp") != 0) { // not for sbp!
			// pbobblen needs this (V ROMs are v3 & v4), note aof/wh1/wh1h/kotm2 (V ROMs are v2 & v4)
			if (pInfo->nADPCMANum == 2 && pName[FindType(pName) + 1] == '3') {
				pADPCMData += ri.nLen * 2;
			}
		}
		if (!strcmp(BurnDrvGetTextA(DRV_NAME), "pbobblenb")) {
			pADPCMData = YM2610ADPCMAROM[nNeoActiveSlot] + 0x200000;
 		}

		NeoLoadADPCM(pInfo->nADPCMOffset, pInfo->nADPCMANum, pADPCMData);

		if (BurnDrvGetHardwareCode() & HARDWARE_SNK_SWAPV) {
			for (INT32 i = 0; i < 0x00200000; i++) {
				UINT8 n = YM2610ADPCMAROM[nNeoActiveSlot][i];
				YM2610ADPCMAROM[nNeoActiveSlot][i] = YM2610ADPCMAROM[nNeoActiveSlot][0x00200000 + i];
				YM2610ADPCMAROM[nNeoActiveSlot][0x00200000 + i] = n;
			}
		}
	}

	if (pInfo->nADPCMBNum) {
		YM2610ADPCMBROM[nNeoActiveSlot]	= (UINT8*)BurnMalloc(nYM2610ADPCMBSize[nNeoActiveSlot]);
		if (YM2610ADPCMBROM[nNeoActiveSlot] == NULL) {
			return 1;
		}

		NeoLoadADPCM(pInfo->nADPCMOffset + pInfo->nADPCMANum, pInfo->nADPCMBNum, YM2610ADPCMBROM[nNeoActiveSlot]);
	} else {
		YM2610ADPCMBROM[nNeoActiveSlot] = YM2610ADPCMAROM[nNeoActiveSlot];
		nYM2610ADPCMBSize[nNeoActiveSlot] = nYM2610ADPCMASize[nNeoActiveSlot];
	}

	return 0;
}

// ----------------------------------------------------------------------------
// Bankswitch / memory map functions

static void NeoZ80SetBank0(INT32 nBank)
{
	nBank &= 0x0F;
	if (nBank != nZ80Bank0) {
		UINT8* nStartAddress = NeoZ80ROMActive + (nBank << 14);
		ZetMapArea(0x8000, 0xBFFF, 0, nStartAddress);
		ZetMapArea(0x8000, 0xBFFF, 2, nStartAddress);

		nZ80Bank0 = nBank;
	}

	return;
}

static void NeoZ80SetBank1(INT32 nBank)
{
	nBank &= 0x1F;
	if (nBank != nZ80Bank1) {
		UINT8* nStartAddress = NeoZ80ROMActive + (nBank << 13);
		ZetMapArea(0xC000, 0xDFFF, 0, nStartAddress);
		ZetMapArea(0xC000, 0xDFFF, 2, nStartAddress);

		nZ80Bank1 = nBank;
	}

	return;
}

static void NeoZ80SetBank2(INT32 nBank)
{
	nBank &= 0x3F;
	if (nBank != nZ80Bank2) {
		UINT8* nStartAddress = NeoZ80ROMActive + (nBank << 12);
		ZetMapArea(0xE000, 0xEFFF, 0, nStartAddress);
		ZetMapArea(0xE000, 0xEFFF, 2, nStartAddress);

		nZ80Bank2 = nBank;
	}

	return;
}

static void NeoZ80SetBank3(INT32 nBank)
{
	nBank &= 0x7F;
	if (nBank != nZ80Bank3) {
		UINT8* nStartAddress = NeoZ80ROMActive + (nBank << 11);
		ZetMapArea(0xF000, 0xF7FF, 0, nStartAddress);
		ZetMapArea(0xF000, 0xF7FF, 2, nStartAddress);

		nZ80Bank3 = nBank;
	}

	return;
}

static void NeoZ80MapROM(bool bMapBoardROM)
{
	if (nNeoSystemType & NEO_SYS_CART) {
		if (bMapBoardROM && bZ80BIOS) {
			// Bank in the Z80 boardROM
			ZetMapArea(0x0000, 0x7FFF, 0, NeoZ80BIOS);
			ZetMapArea(0x0000, 0x7FFF, 2, NeoZ80BIOS);
		} else {
			// Bank in the Z80 cartridge ROM
			ZetMapArea(0x0000, 0x7FFF, 0, NeoZ80ROMActive);
			ZetMapArea(0x0000, 0x7FFF, 2, NeoZ80ROMActive);
		}
	}
}

static void MapVectorTable(bool bMapBoardROM)
{
	if (nNeoSystemType & NEO_SYS_CD) {
		NeoCDVectorSwitch = (!bMapBoardROM && Neo68KROMActive);
		return;
	}

	if (!bMapBoardROM && Neo68KROMActive) {
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x000000, 0x0003FF, MAP_ROM);
	} else {
		SekMapMemory(NeoVectorActive, 0x000000, 0x0003FF, MAP_ROM);
	}
}

inline static void MapPalette(INT32 nBank)
{
	if (nNeoPaletteBank != nBank) {
		nNeoPaletteBank = nBank;
		SekMapMemory(NeoPalSrc[nBank], 0x400000, 0x401FFF, MAP_ROM);

		NeoSetPalette();
	}
}

static void Bankswitch(UINT32 nBank)
{
	nBank = 0x100000 + ((nBank & 7) << 20);
	if (nBank >= nCodeSize[nNeoActiveSlot]) {
		nBank = 0x100000;
	}

	if (nBank != nNeo68KROMBank) {
//		bprintf(PRINT_NORMAL, "Bankswitched main ROM, new address is 0x%08X.\n", nBank);
		nNeo68KROMBank = nBank;
		SekMapMemory(Neo68KROMActive + nNeo68KROMBank, 0x200000, 0x2FFFFF, MAP_ROM);
	}
}

void NeoMapBank()
{
	SekMapMemory(Neo68KROMActive + nNeo68KROMBank, 0x200000, 0x2FFFFF, MAP_ROM);
}

void NeoMap68KFix()
{
	if ((nNeoSystemType & NEO_SYS_CART) && (nCodeSize[nNeoActiveSlot] > 0x100000)) {

		SekMapMemory(Neo68KFix[nNeoActiveSlot] + 0x0400, 0x000400, 0x0FFFFF, MAP_ROM);

		if (Neo68KROM[nNeoActiveSlot]) {
			memcpy(NeoVector[nNeoActiveSlot] + 0x80, Neo68KFix[nNeoActiveSlot] + 0x80, 0x0380);
		}
	}

	MapVectorTable(b68KBoardROMBankedIn);
}

void NeoUpdateVector()
{
	// Create copy of 68K with BIOS vector table
	for (INT32 i = 0; i < MAX_SLOT; i++) {
		if (NeoVector[i]) {
			memcpy(NeoVector[i] + 0x00, Neo68KBIOS, 0x0080);
			if (Neo68KROM[i]) {
				memcpy(NeoVector[i] + 0x80, Neo68KFix[i] + 0x80, 0x0380);
			}
		}
	}
}

// ----------------------------------------------------------------------------
// 68K bankswitch for most games without SMA/PVC protection

static void __fastcall neogeoWriteByteBankswitch(UINT32 sekAddress, UINT8 byteValue)
{
	if (sekAddress >= 0x2FFFF0) {

//		bprintf(PRINT_NORMAL, _T("  - Bankswitch: 0x%06X -> 0x%02X\n"), sekAddress, byteValue);

		Bankswitch(byteValue);
		return;
	}
}

static void __fastcall neogeoWriteWordBankswitch(UINT32 sekAddress, UINT16 wordValue)
{
	if (sekAddress >= 0x2FFFF0) {

//		bprintf(PRINT_NORMAL, _T("  - Bankswitch: 0x%06X -> 0x%04X\n"), sekAddress, wordValue);

		Bankswitch(wordValue);
		return;
	}
}

// ----------------------------------------------------------------------------
// CPU synchronisation

static inline void neogeoSynchroniseZ80(INT32 nExtraCycles)
{
#if defined Z80_SPEED_ADJUST
	INT32 nCycles = SekTotalCycles() / 3 + nExtraCycles;
#else
	INT32 nCycles = ((INT64)SekTotalCycles() * nCyclesTotal[1] / nCyclesTotal[0]) + nExtraCycles;
#endif

	if (nCycles <= ZetTotalCycles()) {
		return;
	}

	nCycles68KSync = nCycles - nExtraCycles;

	BurnTimerUpdate(nCycles);
}

// Callbacks for the FM chip

static void neogeoFMIRQHandler(INT32, INT32 nStatus)
{
//	bprintf(PRINT_NORMAL, _T("    YM2610 IRQ status: 0x%02X (%6i cycles)\n"), nStatus, ZetTotalCycles());

	if (nStatus & 1) {
		ZetSetIRQLine(0xFF, CPU_IRQSTATUS_ACK);
	} else {
		ZetSetIRQLine(0,    CPU_IRQSTATUS_NONE);
	}
}

// ----------------------------------------------------------------------------

static UINT8 __fastcall neogeoReadByteGambling(UINT32 sekAddress)
{
	switch (sekAddress) {
		case 0x280001: {
			return 0xff - NeoInput[3];
		}

		case 0x2c0001: {
			return 0x03;
		}
	}

//	bprintf(PRINT_NORMAL, _T("Read Byte 0x%08X.\n"), sekAddress);

	return 0xff;
}

static UINT16 __fastcall neogeoReadWordGambling(UINT32 sekAddress)
{
	switch (sekAddress) {
		case 0x280000: {
			return 0xff - NeoInput[3];
		}

		case 0x2c0000: {
			return 0x0003;
		}
	}

//	bprintf(PRINT_NORMAL, _T("Read Word 0x%08X.\n"), sekAddress);

	return 0xffff;
}

static UINT8 __fastcall vliner_timing(UINT32 sekAddress)
{
	switch (sekAddress) {
		case 0x320000: {
			INT32 nReply = nSoundReply;

#if 1 && defined USE_SPEEDHACKS
			// nSoundStatus: &1 = sound latch read, &2 = response written
			if (nSoundStatus != 3) {
				neogeoSynchroniseZ80(0x0100);
			}
#endif

			if (nSoundStatus & 1) {
//				bprintf(PRINT_NORMAL, _T("  - Sound reply read (0x%02X).\n"),  nSoundReply);

				return nReply;
			} else {
//				bprintf(PRINT_NORMAL, _T("  - Sound reply read while sound pending (0x%02X).\n"),  nSoundReply);

				return nReply & 0x7F;
			}
		}

		case 0x320001: {
//			if (!bAESBIOS) {
			if (nBIOS != 14 && nBIOS != 16 && nBIOS != 17) {
				UINT8 nuPD4990AOutput = uPD4990ARead(SekTotalCycles() - nuPD4990ATicks);
				nuPD4990ATicks = SekTotalCycles();
				return 0x3F | (nuPD4990AOutput << 6);
			}

			return (0x3f) & 0xE7;
		}
	}

//	bprintf(PRINT_NORMAL, _T("Read Byte 0x%08X.\n"), sekAddress);
	return 0xff;
}

// -----------------------------------------------------------------------------

static void NeoMapActiveCartridge()
{
	if (!(nNeoSystemType & NEO_SYS_CART)) {
		nNeoActiveSlot = 0;
		return;
	}

	neogeoSynchroniseZ80(0);

	if (NeoCallbackActive && NeoCallbackActive->pRemoveHandlers) {
		NeoCallbackActive->pRemoveHandlers();
	}

	NeoVectorActive = NeoVector[nNeoActiveSlot];

	if (Neo68KROM[nNeoActiveSlot] == NULL) {

		// If an empty slot is selected, only the vector table is valid

		Neo68KROMActive = NULL;

		SekMapHandler(0,	0x000000, 0x0FFFFF, MAP_RAM);
		SekMapHandler(0,	0x200000, 0x2FFFFF, MAP_RAM);

		b68KBoardROMBankedIn = true;
		MapVectorTable(b68KBoardROMBankedIn);

		NeoCallbackActive = NULL;

		ZetSetBUSREQLine(1);

		NeoSetSpriteSlot(nNeoActiveSlot);
		NeoSetTextSlot(nNeoActiveSlot);

		return;
	}

	Neo68KROMActive = Neo68KROM[nNeoActiveSlot];
	NeoZ80ROMActive = NeoZ80ROM[nNeoActiveSlot];
	NeoCallbackActive = &NeoCallback[nNeoActiveSlot];

	SekSetReadWordHandler(6, NULL);
	SekSetWriteWordHandler(6, NULL);
	SekSetReadByteHandler(6, NULL);
	SekSetWriteByteHandler(6, NULL);
	SekSetReadWordHandler(7, NULL);
	SekSetWriteWordHandler(7, NULL);
	SekSetReadByteHandler(7, NULL);
	SekSetWriteByteHandler(7, NULL);

	SekMapHandler(0,			  	0x000000, 0x0FFFFF, MAP_WRITE);

	if (nCodeSize[nNeoActiveSlot] <= 0x080000) {
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x000000, 0x07FFFF, MAP_ROM);
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x080000, 0x0FFFFF, MAP_ROM);
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x200000, 0x27FFFF, MAP_ROM);
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x280000, 0x2FFFFF, MAP_ROM);
	}

	if (nCodeSize[nNeoActiveSlot] <= 0x100000) {
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x000000, 0x0FFFFF, MAP_ROM);
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x200000, 0x2FFFFF, MAP_ROM);
	}

	if (nCodeSize[nNeoActiveSlot] > 0x100000) {
		SekMapMemory(Neo68KFix[nNeoActiveSlot], 0x000000, 0x0FFFFF, MAP_ROM);

		SekMapHandler(4,			  0x200000, 0x2FFFFF, MAP_WRITE);

		SekSetWriteWordHandler(4, neogeoWriteWordBankswitch);
		SekSetWriteByteHandler(4, neogeoWriteByteBankswitch);
	}

	if (NeoCallbackActive->pInstallHandlers) {
		NeoCallbackActive->pInstallHandlers();
	}

	if (nCodeSize[nNeoActiveSlot] > 0x100000) {
		nNeo68KROMBank = 0x100000;

		if (NeoCallbackActive->pBankswitch) {
			NeoCallbackActive->pBankswitch();
		} else {
			if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) != HARDWARE_SNK_GAMBLING) {
				NeoMapBank();
			}
		}
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) == HARDWARE_SNK_GAMBLING) {
			SekMapMemory(NeoNVRAM2,	0x200000, 0x201FFF, MAP_RAM);	// 68K RAM

			SekMapHandler(6,	0x202000, 0x2FFFFF, MAP_READ);
			SekSetReadByteHandler(6,     neogeoReadByteGambling);
			SekSetReadWordHandler(6,     neogeoReadWordGambling);

			if (vlinermode) {
				SekMapHandler(7,	0x320000, 0x320001, MAP_READ);
				SekSetReadByteHandler(7,     vliner_timing);
			}
		}

	if (NeoZ80ROMActive) {
		ZetSetBUSREQLine(0);

		nZ80Bank0 = nZ80Bank1 = nZ80Bank2 = nZ80Bank3 = -1;
		NeoZ80SetBank0(0x02);
		NeoZ80SetBank1(0x06);
		NeoZ80SetBank2(0x0E);
		NeoZ80SetBank3(0x1E);
	} else {
		ZetSetBUSREQLine(1);
	}

	MapVectorTable(b68KBoardROMBankedIn);
	NeoZ80MapROM(bZ80BoardROMBankedIn);

	NeoSetSpriteSlot(nNeoActiveSlot);
	NeoSetTextSlot(nNeoActiveSlot);

	// the text rendering code will get confused if we don't do this
	memset(NeoGraphicsRAM + 0xEA00, 0, 0x0200);

	BurnYM2610MapADPCMROM(YM2610ADPCMAROM[nNeoActiveSlot], nYM2610ADPCMASize[nNeoActiveSlot], YM2610ADPCMBROM[nNeoActiveSlot], nYM2610ADPCMBSize[nNeoActiveSlot]);

	return;
}

// -----------------------------------------------------------------------------
// Savestate support

INT32 NeoScan(INT32 nAction, INT32* pnMin)
{
	INT32 nOldBIOS = nBIOS;
	struct BurnArea ba;

	if (pnMin) {												// Return minimum compatible version
		*pnMin =  0x029713;
	}

	// Make sure we have the correct value for nBIOS
	if (nAction & ACB_DRIVER_DATA) {
		SCAN_VAR(nBIOS);
		// Update the system type immediately
		NeoSetSystemType();
	}

	if (nAction & ACB_MEMORY_ROM) {

		if (nNeoSystemType & NEO_SYS_CART) {
			ba.Data		= Neo68KBIOS;
			ba.nLen		= 0x80000;//(nNeoSystemType & NEO_SYS_PCB) ? 0x00080000 : 0x00020000;
			ba.nAddress = 0;
			ba.szName	= "68K BIOS";
			BurnAcb(&ba);

			ba.Data		= Neo68KROMActive;
			ba.nLen		= nCodeSize[nNeoActiveSlot];
			ba.nAddress = 0;
			ba.szName	= "68K ROM";
			BurnAcb(&ba);

			ba.Data		= NeoZ80BIOS;
			ba.nLen		= 0x00020000;
			ba.nAddress = 0;
			ba.szName	= "Z80 BIOS";
			BurnAcb(&ba);

			ba.Data		= NeoZ80ROMActive;
			ba.nLen		= 0x00080000;
			ba.nAddress = 0;
			ba.szName	= "Z80 ROM";
			BurnAcb(&ba);

			ba.Data		= YM2610ADPCMAROM[nNeoActiveSlot];
			ba.nLen		= nYM2610ADPCMASize[nNeoActiveSlot];
			ba.nAddress = 0;
			ba.szName	= "ADPCM ROM";
			BurnAcb(&ba);

			ba.Data		= YM2610ADPCMBROM[nNeoActiveSlot];
			ba.nLen		= nYM2610ADPCMBSize[nNeoActiveSlot];
			ba.nAddress = 0;
			ba.szName	= "Delta-T ROM";
			BurnAcb(&ba);
		} else {
			ba.Data		= Neo68KBIOS;
			ba.nLen		= 0x00080000;
			ba.nAddress = 0;
			ba.szName	= "68K BIOS";
			BurnAcb(&ba);
		}
	}

	if (nAction & ACB_MEMCARD) {
		if ((nNeoSystemType & NEO_SYS_CART) && !(nNeoSystemType & NEO_SYS_PCB)) {	// Scan memory card

			if (pnMin && (nAction & ACB_TYPEMASK) == ACB_MEMCARD) {					// Return minimum compatible version
				*pnMin = 0x029713;
			}

			ba.Data		= NeoMemoryCard;
			ba.nLen		= 0x020000;
			ba.nAddress = 0;
			ba.szName	= "Memory card";

			if ((nAction & ACB_TYPEMASK) == ACB_MEMCARD) {
				if (nAction & ACB_WRITE) {
					bMemoryCardInserted = true;
				}
				if (nAction & ACB_READ) {
					bMemoryCardInserted = false;

					// If a card is inserted, determine the size
					if (*((UINT16*)NeoMemoryCard) != 0x8000) {
						INT32 nSize = (NeoMemoryCard[21] << 8) | NeoMemoryCard[23];
						if (nSize >= 0x1000) {
							ba.nLen = nSize;
						}
					}
				}
			}

			BurnAcb(&ba);
		}
	}

	if ((nNeoSystemType & NEO_SYS_CD) && (nAction & ACB_NVRAM)) {				// Scan non-volatile memory (built-in memory card)

		if (pnMin && (nAction & ACB_TYPEMASK) == ACB_NVRAM) {					// Return minimum compatible version
			*pnMin = 0x029713;
		}

		ba.Data		= NeoMemoryCard;
		ba.nLen		= 0x004000;
		ba.nAddress = 0;
		ba.szName	= "Memory card";
		BurnAcb(&ba);
	}

	if ((nNeoSystemType & NEO_SYS_MVS) && (nAction & ACB_NVRAM)) {				// Scan non-volatile memory

		if (pnMin && (nAction & ACB_TYPEMASK) == ACB_NVRAM) {					// Return minimum compatible version
			*pnMin = 0x029713;
		}

		ba.Data		= NeoNVRAM;
		ba.nLen		= 0x00010000;
		ba.nAddress = 0;
		ba.szName	= "NVRAM";
		BurnAcb(&ba);
	}

	if (((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) == HARDWARE_SNK_GAMBLING) && (nAction & ACB_NVRAM)) {
		ba.Data		= NeoNVRAM2;
		ba.nLen		= 0x00002000;
		ba.nAddress = 0;
		ba.szName	= "Extra NVRAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_MEMORY_RAM) {								// Scan RAM

		if (nNeoSystemType & NEO_SYS_CART) {
			ba.Data		= Neo68KRAM;
			ba.nLen		= 0x00010000;
			ba.nAddress = 0;
			ba.szName	= "68K RAM";
			BurnAcb(&ba);

    		ba.Data		= NeoZ80RAM;
			ba.nLen		= 0x00000800;
			ba.nAddress = 0;
			ba.szName	= "Z80 RAM";
			BurnAcb(&ba);
		}

		if (nNeoSystemType & NEO_SYS_CD) {
			ba.Data		= Neo68KROMActive;
			ba.nLen		= nCodeSize[0];
			ba.nAddress = 0;
			ba.szName	= "68K program RAM";
			BurnAcb(&ba);

			ba.Data		= NeoZ80ROMActive;
			ba.nLen		= 0x00080000;
			ba.nAddress = 0;
			ba.szName	= "Z80 program RAM";
			BurnAcb(&ba);

			ba.Data		= NeoVector[0];
			ba.nLen		= 0x00000400;
			ba.nAddress = 0;
			ba.szName	= "68K vector RAM";
			BurnAcb(&ba);
		}

    	ba.Data		= NeoPalSrc[0];
		ba.nLen		= 0x000002000;
		ba.nAddress = 0;
		ba.szName	= "Palette 0";
		BurnAcb(&ba);
    	ba.Data		= NeoPalSrc[1];
		ba.nLen		= 0x000002000;
		ba.nAddress = 0;
		ba.szName	= "Palette 1";
		BurnAcb(&ba);

    	ba.Data		= NeoGraphicsRAM;
		ba.nLen		= 0x00020000;
		ba.nAddress = 0;
		ba.szName	= "Graphics RAM";
		BurnAcb(&ba);

		if (nNeoSystemType & NEO_SYS_CD) {
			ba.Data		= NeoSpriteRAM;
			ba.nLen		= nSpriteSize[0];
			ba.nAddress = 0;
			ba.szName	= "Sprite layer tile RAM";
			BurnAcb(&ba);

			ba.Data		= NeoTextRAM;
			ba.nLen		= nNeoTextROMSize[0];
			ba.nAddress = 0;
			ba.szName	= "Text layer tile RAM";
			BurnAcb(&ba);

			ba.Data		= YM2610ADPCMBROM[0];
			ba.nLen		= nYM2610ADPCMBSize[0];
			ba.nAddress = 0;
			ba.szName	= "ADPCM sample RAM";
			BurnAcb(&ba);
		}

		if (NeoCallbackActive && NeoCallbackActive->pScan) {
			NeoCallbackActive->pScan(nAction, pnMin);
		}
	}

	if (nAction & ACB_DRIVER_DATA) {						// Scan driver data

		SekScan(nAction);									// Scan 68000 state
		ZetScan(nAction);									// Scan Z80 state

		ZetOpen(0);
		BurnYM2610Scan(nAction, pnMin);
		ZetClose();

		if (nNeoSystemType & NEO_SYS_MVS) {
			uPD4990AScan(nAction, pnMin);
		}

		if (NeoCallbackActive && NeoCallbackActive->pScan) {
			NeoCallbackActive->pScan(nAction, pnMin);
		}

		SCAN_VAR(nCyclesExtra);

		SCAN_VAR(bNeoEnableGraphics);
		SCAN_VAR(bNeoEnableSprites);
		SCAN_VAR(bNeoEnableText);
		SCAN_VAR(bNeoDarkenPalette);

		SCAN_VAR(nIRQAcknowledge);

		SCAN_VAR(nIRQControl);	SCAN_VAR(nIRQOffset); SCAN_VAR(nIRQCycles);
		SCAN_VAR(bSRAMWritable);

#if defined EMULATE_WATCHDOG
		SCAN_VAR(nNeoWatchdog);
#endif

		SCAN_VAR(b68KBoardROMBankedIn);
		if (nNeoSystemType & NEO_SYS_CART) {
			SCAN_VAR(bBIOSTextROMEnabled);
			SCAN_VAR(nZ80Bank0); SCAN_VAR(nZ80Bank1); SCAN_VAR(nZ80Bank2); SCAN_VAR(nZ80Bank3);
			SCAN_VAR(nNeo68KROMBank);
		}

		// -- June 17-19, 2014; savestate crash fix - dink
		SCAN_OFF(NeoGraphicsRAMBank, NeoGraphicsRAM, nAction);
		SCAN_VAR(NeoGraphicsRAMPointer);
		SCAN_VAR(nNeoGraphicsModulo);
		// -- end
		SCAN_VAR(nNeoSpriteFrame); SCAN_VAR(nSpriteFrameSpeed); SCAN_VAR(nSpriteFrameTimer);

		SCAN_VAR(nNeoPaletteBank);

		SCAN_VAR(nSoundLatch);
		SCAN_VAR(nSoundReply);
		SCAN_VAR(nSoundStatus);

#if 1 && defined USE_SPEEDHACKS
		SCAN_VAR(nSoundPrevReply);
#endif

		SCAN_VAR(nInputSelect);

		SCAN_OFF(NeoInputBank, NeoInput, nAction);

		SCAN_VAR(nAnalogAxis);

		SCAN_VAR(nuPD4990ATicks);

		SCAN_OFF(Neo68KFix[nNeoActiveSlot], Neo68KROM[nNeoActiveSlot], nAction);

		SCAN_VAR(nLEDLatch);
		SCAN_VAR(nLED);

//			BurnGameFeedback(sizeof(nLED), nLED);

		if (nNeoSystemType & NEO_SYS_CD) {
			SCAN_VAR(bNeoCDIRQEnabled);
			SCAN_VAR(nNeoCDIRQVector);

			SCAN_VAR(nLC8951Register);
			SCAN_VAR(LC8951RegistersR);
			SCAN_VAR(LC8951RegistersW);

			SCAN_VAR(nActiveTransferArea);
			SCAN_VAR(nSpriteTransferBank);
			SCAN_VAR(nADPCMTransferBank);

			SCAN_VAR(nTransferWriteEnable);

			SCAN_VAR(NeoCDOBJBankUpdate);

			SCAN_VAR(bNeoCDCommsClock);
			SCAN_VAR(bNeoCDCommsSend);

			SCAN_VAR(NeoCDCommsCommandFIFO);
			SCAN_VAR(NeoCDCommsStatusFIFO);

			SCAN_VAR(NeoCDCommsWordCount);

			SCAN_VAR(NeoCDAssyStatus);

			SCAN_VAR(NeoCDSectorMin);
			SCAN_VAR(NeoCDSectorSec);
			SCAN_VAR(NeoCDSectorFrm);
			SCAN_VAR(NeoCDSectorLBA);

			SCAN_VAR(NeoCDSectorData);

			SCAN_VAR(bNeoCDLoadSector);

			SCAN_VAR(NeoCDDMAAddress1);
			SCAN_VAR(NeoCDDMAAddress2);
			SCAN_VAR(NeoCDDMAValue1);
			SCAN_VAR(NeoCDDMAValue2);
			SCAN_VAR(NeoCDDMACount);

			SCAN_VAR(NeoCDDMAMode);

			SCAN_VAR(NeoCDVectorSwitch);

			SCAN_VAR(nNeoCDCyclesIRQ);
			SCAN_VAR(nNeoCDMode);
			SCAN_VAR(nff0002);
			SCAN_VAR(nff0004);

			SCAN_VAR(nNeoCDTextSlot);
			SCAN_VAR(nNeoCDSpriteSlot);

			CDEmuScan(nAction, pnMin);

#if defined CYCLE_LOG
			SCAN_VAR(derpframe);
#else
			INT32 dframe = 0; // keep compatibility with CYCLE_LOG on or off
			SCAN_VAR(dframe);
#endif
		}

		if (nAction & ACB_WRITE) {
			INT32 nNewBIOS = nBIOS;
			INT32 nBank;

			if (nNeoSystemType & NEO_SYS_CD) {
				NeoSetSpriteSlot(nNeoCDSpriteSlot);
				NeoSetTextSlot(nNeoCDTextSlot);

				for (INT32 i = 0; i < 4; i++) {
					NeoCDOBJBankUpdate[i] = 1;
					if (NeoCDOBJBankUpdate[i]) {
						NeoDecodeSpritesCD(NeoSpriteRAM + (i << 20), NeoSpriteROM[0] + (i << 20), 0x100000);
						NeoUpdateSprites((i << 20), 0x100000);
					}
				}
				NeoUpdateText(0, 0x020000, NeoTextRAM, NeoTextROM[0]);
			} else {
				SekOpen(0);
				NeoMap68KFix();
				SekClose();
			}

			if (nNeoSystemType & NEO_SYS_CART) {
				ZetOpen(0);
				NeoZ80MapROM(bZ80BoardROMBankedIn);

				nBank = nZ80Bank0; nZ80Bank0 = -1;
				NeoZ80SetBank0(nBank);
				nBank = nZ80Bank1; nZ80Bank1 = -1;
				NeoZ80SetBank1(nBank);
				nBank = nZ80Bank2; nZ80Bank2 = -1;
				NeoZ80SetBank2(nBank);
				nBank = nZ80Bank3; nZ80Bank3 = -1;
				NeoZ80SetBank3(nBank);
				ZetClose();

				if (NeoCallbackActive && NeoCallbackActive->pBankswitch) {
					NeoCallbackActive->pBankswitch();
				} else {
					if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) != HARDWARE_SNK_GAMBLING && fatfury2mode == 0) {
						SekOpen(0);
						SekMapMemory(Neo68KROMActive + nNeo68KROMBank, 0x200000, 0x2FFFFF, MAP_ROM);
						SekClose();
					}
				}
			}

			nBank = nNeoPaletteBank; nNeoPaletteBank = -1;
			SekOpen(0);
			MapPalette(nBank);
			SekClose();

			NeoRecalcPalette = 1;

			if (nNeoSystemType & NEO_SYS_CART) {
				nBIOS = nOldBIOS;
				NeoLoad68KBIOS(nNewBIOS);

				NeoSetSystemType();
			}

			nPrevBurnCPUSpeedAdjust = -1;
		}
	}

	return 0;
}

// -----------------------------------------------------------------------------
// Z80 handlers

static UINT8 __fastcall neogeoZ80In(UINT16 nAddress)
{
	switch (nAddress & 0xFF) {
		case 0x00:									// Read sound command
//			bprintf(PRINT_NORMAL, _T("  - Sound command received (0x%02X).\n"), nSoundLatch);
			nSoundStatus = 1;
#if 1 && defined USE_SPEEDHACKS
			nSoundPrevReply = -1;
#endif
			return nSoundLatch;

		case 0x04:
			return BurnYM2610Read(0);
		case 0x05:
			return BurnYM2610Read(1);
		case 0x06:
			return BurnYM2610Read(2);

		case 0x08:
//			bprintf(PRINT_NORMAL, "  - Z80 bank 3 -> 0x%02X\n", nAddress >> 8);
			NeoZ80SetBank3(nAddress >> 8);
			break;
		case 0x09:
//			bprintf(PRINT_NORMAL, "  - Z80 bank 2 -> 0x%02X\n", nAddress >> 8);
			NeoZ80SetBank2(nAddress >> 8);
			break;
		case 0x0A:
//			bprintf(PRINT_NORMAL, "  - Z80 bank 1 -> 0x%02X\n", nAddress >> 8);
			NeoZ80SetBank1(nAddress >> 8);
			break;
		case 0x0B:
//			bprintf(PRINT_NORMAL, "  - Z80 bank 0 -> 0x%02X\n", nAddress >> 8);
			NeoZ80SetBank0(nAddress >> 8);
			break;

//		default: {
//			bprintf(PRINT_NORMAL, _T)"  - Z80 read port 0x%04X.\n"), nAddress);
//		}
	}

	return 0;
}

static UINT8 __fastcall neogeoZ80InCD(UINT16 nAddress)
{
	switch (nAddress & 0xFF) {
		case 0x00:									// Read sound command
//			bprintf(PRINT_NORMAL, _T("  - Sound command received (0x%02X).\n"), nSoundLatch);
			nSoundStatus = 1;
#if 1 && defined USE_SPEEDHACKS
			nSoundPrevReply = -1;
#endif
			return nSoundLatch;

		case 0x04:
			return BurnYM2610Read(0);
		case 0x05:
			return BurnYM2610Read(1);
		case 0x06:
			return BurnYM2610Read(2);

		// banskswitch on MVS/AES
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			break;

		default: {
//			bprintf(PRINT_NORMAL, _T("  - Z80 read port 0x%04X.\n"), nAddress);
		}
	}

	return 0;
}

static void __fastcall neogeoZ80Out(UINT16 nAddress, UINT8 nValue)
{
	switch (nAddress & 0x0FF) {
		case 0x00:									// Clear sound command
			nSoundLatch = 0;
			break;

		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			BurnYM2610Write(nAddress & 3, nValue);
			break;

		case 0x08:
        case 0x18:
            // sound nmi enable/disable bit.  causes too many problems, so we ignore it.
			// (breaks sound in irrmaze, sonicwi3 (cd and aes/mvs version), and possibly others
			break;

		case 0x0C:									// Write reply to sound commands
//			bprintf(PRINT_NORMAL, _T("  - Sound reply sent (0x%02X).\n"), nValue);
			nSoundReply = nValue;

#if 1 && defined USE_SPEEDHACKS
			if (nSoundPrevReply != nValue) {
				nSoundPrevReply = nValue;

				// s1945p replies a 0x00, then an 0xFF;
				// the 68K loops until it has read both
				if (nSoundReply == 0) {
					nSoundStatus &= ~2;
				} else {
					nSoundStatus |=  2;
				}
			} else {
				nSoundStatus |= 2;
			}

			if (ZetTotalCycles() > nCycles68KSync) {

				//				bprintf(PRINT_NORMAL, _T("    %i\n"), ZetTotalCycles());
				BurnTimerUpdateEnd();
				//				bprintf(PRINT_NORMAL, _T("    %i - %i\n"), ZetTotalCycles(), nCycles68KSync);
			}
#endif

			break;

		case 0x80: // NOP
		case 0xc0:
		case 0xc1:
		case 0xc2:
			break;

		default: {
//			bprintf(PRINT_NORMAL, _T("  - Z80 port 0x%04X -> 0x%02X.\n"), nAddress, nValue);
		}
	}
}

// -----------------------------------------------------------------------------
// 68K handlers

static INT32 __fastcall NeoCDIRQCallback(INT32 nIRQ)
{
	switch (nIRQ) {
		case 1:
			return (0x68 / 4); // irq 1 w/irq 2 vector
		case 3:
			return (0x64 / 4); // irq 3 w/irq 1 vector
		case 2:
			return (nNeoCDIRQVector);
	}

	return M68K_INT_ACK_AUTOVECTOR;
}

static inline INT32 NeoConvertIRQPosition(INT32 nOffset)
{
	UINT64 nNewPosition = ((UINT64)nOffset * nBurnCPUSpeedAdjust) >> 7;

	return (nNewPosition < NO_IRQ_PENDING) ? nNewPosition : NO_IRQ_PENDING;
}

static inline void NeoIRQUpdate(UINT16 wordValue)
{
	nIRQAcknowledge |= (wordValue & 7);

//	bprintf(PRINT_NORMAL, _T("  - IRQ Ack -> %02X (at line %3i).\n"), nIRQAcknowledge, SekCurrentScanline());

	if ((nIRQAcknowledge & 7) == 7) {
		SekSetIRQLine(7, CPU_IRQSTATUS_NONE);
	} else {
		if ((nIRQAcknowledge & 1) == 0) {
			SekSetIRQLine(3, CPU_IRQSTATUS_ACK);
		}
		if ((nIRQAcknowledge & 2) == 0) {
			SekSetIRQLine(nScanlineIRQ, CPU_IRQSTATUS_ACK);
		}
		if ((nIRQAcknowledge & 4) == 0) {
			SekSetIRQLine(nVBLankIRQ, CPU_IRQSTATUS_ACK);
		}
	}
}

static inline void NeoCDIRQUpdate(UINT8 byteValue)
{
	nIRQAcknowledge |= (byteValue & 0x38);

//	bprintf(PRINT_NORMAL, _T("  - IRQ Ack -> %02X (CD, at line %3i).\n"), nIRQAcknowledge, SekCurrentScanline());

	if ((nIRQAcknowledge & 0x3F) == 0x3F) {
		SekSetIRQLine(7, CPU_IRQSTATUS_NONE);
	} else {
		if ((nIRQAcknowledge & 0x07) != 7) {
			NeoIRQUpdate(0);
			return;
		}

		//if (!bNeoCDIRQEnabled) return;

		if ((nIRQAcknowledge & 0x08) == 0) {
			nNeoCDIRQVector = 0x5c / 4;
			SekSetIRQLine(2, CPU_IRQSTATUS_ACK);
			return;
		}
		if ((nIRQAcknowledge & 0x10) == 0) {
			nNeoCDIRQVector = 0x58 / 4;
			SekSetIRQLine(2, CPU_IRQSTATUS_ACK);
			return;
		}
		if ((nIRQAcknowledge & 0x20) == 0) {
			nNeoCDIRQVector = 0x54 / 4;
			SekSetIRQLine(2, CPU_IRQSTATUS_ACK);
			return;
		}
	}
}

static inline void SendSoundCommand(const UINT8 nCommand)
{
//	bprintf(PRINT_NORMAL, _T("  - Sound command sent (0x%02X).\n"), nCommand);

	neogeoSynchroniseZ80(0);

	nSoundStatus &= ~1;
	nSoundLatch = nCommand;

	ZetNmi();

#if 1 && defined USE_SPEEDHACKS
	// notes: value too high, and breaks nam1975 voice after coin up
	// stikers 1945p: goes really slow
	// pulstar: music/bonus count noise at end of level
	neogeoSynchroniseZ80(0x24);
#endif
}

static UINT8 ReadInput1(INT32 nOffset)
{
	switch (nOffset) {
		case 0x00:
//			bprintf(PRINT_NORMAL, _T(" -- bank %d inputP0[0x%02X] read (%i).\n"), 0, nInputSelect, SekTotalCycles());
			return ~NeoInputBank[nJoyport0[nInputSelect & 0x07]];

		case 0x01:
//			bprintf(PRINT_NORMAL, _T("  - 0x%06X read (byte).\n"), sekAddress);
			if (nNeoSystemType & NEO_SYS_MVS) {
				return ~NeoInputBank[4];
			}
			return ~0;

		case 0x81:
//			bprintf(PRINT_NORMAL, _T("  - 0x%06X read (byte).\n"), sekAddress);
			if (nNeoSystemType & NEO_SYS_MVS) {
				return ~NeoInputBank[5];
			}
			return ~0;
	}

	return ~0;
}

static UINT8 ReadInput2(INT32 nOffset)
{
	if ((nOffset & 1) == 0) {
//		bprintf(PRINT_NORMAL, _T(" -- bank %d inputP1[0x%02X] read.\n"), 0, nInputSelect);
		return ~NeoInputBank[nJoyport1[(nInputSelect >> 3) & 0x07]];
	}

	return ~0;
}

static UINT8 ReadInput3(INT32 nOffset)
{
	if ((nOffset & 1) == 0) {
//		bprintf(PRINT_NORMAL, " -- input 2 read.\n");
		return ~NeoInputBank[2];
	}

	return ~0;
}

static UINT8 __fastcall neogeoReadByte(UINT32 sekAddress)
{
	if (sekAddress >= 0x200000 && sekAddress <= 0x2fffff)
		return ~0; // data from open bus should be read here

	switch (sekAddress & 0xFE0000) {
		case 0x300000:
			return ReadInput1(sekAddress & 0xFF);

		case 0x320000: {
			if ((sekAddress & 1) == 0) {
				INT32 nReply = nSoundReply;

#if 1 && defined USE_SPEEDHACKS
				// nSoundStatus: &1 = sound latch read, &2 = response written
				if (nSoundStatus != 3) {
					neogeoSynchroniseZ80((s1945pmode) ? 0x60 : 0x0100);
				}
#else
				neogeoSynchroniseZ80(0);
#endif

				if ((nSoundStatus & 1) == 0) {
//					bprintf(PRINT_NORMAL, _T("  - Sound reply read while sound pending (0x%02X).\n"),  nSoundReply);

					return nReply & 0x7F;
				}

//				bprintf(PRINT_NORMAL, _T("  - Sound reply read (0x%02X).\n"),  nSoundReply);
				return nReply;
			}

			if (nNeoSystemType & NEO_SYS_MVS) {
				UINT8 nuPD4990AOutput = uPD4990ARead(SekTotalCycles() - nuPD4990ATicks);
				nuPD4990ATicks = SekTotalCycles();
				return (~NeoInputBank[3] & 0x3F) | (nuPD4990AOutput << 6);
			}

			return (~NeoInputBank[3] & 0x7F) & 0xE7;
		}

		case 0x340000:
			return ReadInput2(sekAddress & 0xFF);

		case 0x380000:
			return ReadInput3(sekAddress & 0xFF);

		default:
			bprintf(PRINT_NORMAL, _T("  - 0x%08X read (byte, PC: %08X)\n"), sekAddress, SekGetPC(-1));
	}

	return ~0;
}

static UINT16 __fastcall neogeoReadWord(UINT32 sekAddress)
{
	if (sekAddress >= 0x200000 && sekAddress <= 0x2fffff)
		return ~0; // data from open bus should be read here

	switch (sekAddress & 0xFE0000) {
		case 0x300000:
			return (ReadInput1(sekAddress & 0xFE) << 8) | ReadInput1((sekAddress & 0xFE) | 1);

		case 0x320000:
			SEK_DEF_READ_WORD(0, sekAddress);

		case 0x340000:
			return (ReadInput2(sekAddress & 0xFE) << 8) | ReadInput2((sekAddress & 0xFE) | 1);

		case 0x380000:
			return (ReadInput3(sekAddress & 0xFE) << 8) | ReadInput3((sekAddress & 0xFE) | 1);

		default:
			bprintf(PRINT_NORMAL, _T("  - 0x%08X read (word, PC: %08X)\n"), sekAddress, SekGetPC(-1));
	}

	return ~0;
}

static void WriteIO1(INT32 nOffset, UINT8 byteValue)
{
	switch (nOffset) {
		case 0x01:											// Select the input returned at 0x300000
//			bprintf(PRINT_NORMAL, _T("  - InputP0/P1 0x%02X selected (%i).\n"), byteValue, SekTotalCycles());
			nInputSelect = byteValue;
			break;

		case 0x21:											// Select the active cartridge slot
//			bprintf(PRINT_NORMAL, _T("  - Cartridge slot 0x%02X activated.\n"), byteValue);
			nNeoActiveSlot = byteValue;
			if (nNeoActiveSlot >= nNeoNumSlots) {
				nNeoActiveSlot = 0;
			}

			NeoMapActiveCartridge();
			break;

		case 0x31:											// Send latched output to LEDs (bits 5/4 - numeric displays, bit 3 - marquee lights, one per slot)
//			bprintf(PRINT_NORMAL, _T("  - 0x%06X -> 0x%02X.\n"), sekAddress, byteValue);
//			if (byteValue != 255) {
//				bprintf(PRINT_NORMAL, _T("  - LED %02X -> %02X\n"), ~byteValue & 255, nLEDLatch & 255);
//			}

/*			if ((byteValue & 0x20) == 0) {
				nLED[0] = nLEDLatch;
			}
			if ((byteValue & 0x10) == 0) {
				nLED[1] = nLEDLatch;
			}
			if ((byteValue & 0x08) == 0) {
				nLED[2] = nLEDLatch;
			}

			BurnGameFeedback(sizeof(nLED), nLED);*/

			break;

		case 0x41:											// Latch LED output
			// for numeric displays - digits displayed    = ~bytevalue
			// for start buttons    - highlighted marquee = ~bytevalue
//			bprintf(PRINT_NORMAL, _T("  - LED output -> 0x%02X.\n"), byteValue);
			nLEDLatch = ~byteValue;
			break;

		case 0x51:											// Send command to RTC
//			bprintf(PRINT_NORMAL, _T("  - RTC -> 0x%02X.\n"), byteValue);
			if (nNeoSystemType & NEO_SYS_MVS) {
				uPD4990AWrite(byteValue & 2, byteValue & 4, byteValue & 1);
			}
			break;

		case 0x61:											// Coin lockout chute 1 & input bank select
//			bprintf(PRINT_NORMAL, _T("  - %sInput bank 0 selected (0x%02X).\n"), byteValue ? _T("Chute 1 coin lockout -> High / ") : _T(""), byteValue);
			NeoInputBank = NeoInput + 0;
			break;
		case 0x63:											// Coin lockout chute 2
//			bprintf(PRINT_NORMAL, _T("  - Chute 2 coin lockout -> High (0x%02X).\n"), byteValue);
			break;

		case 0x65:											// Coin counter chute 1 -> High
			break;
//			bprintf(PRINT_NORMAL, _T("  - Chute 1 coin counter -> High (0x%02X).\n"), byteValue);
		case 0x67:											// Coin counter chute 2 -> High
//			bprintf(PRINT_NORMAL, _T("  - Chute 2 coin counter -> High (0x%02X).\n"), byteValue);
			break;

		case 0xD1:											// Send command to RTC
//			bprintf(PRINT_NORMAL, _T("  - RTC -> 0x%02X.\n"), byteValue);
			if (nNeoSystemType & NEO_SYS_MVS) {
				uPD4990AWrite(byteValue & 2, byteValue & 4, byteValue & 1);
			}
			break;

		case 0xE1:
//			bprintf(PRINT_NORMAL, _T("  - Chute 2 coin lockout -> Low / Input bank 1 selected (0x%02X).\n"), byteValue);
			NeoInputBank = NeoInput + 8;
			break;
		case 0xE3:
//			bprintf(PRINT_NORMAL, _T("  - Chute 2 coin lockout -> Low (0x%02X).\n"), byteValue);
			break;

		case 0xE5:											// Coin counter chute 1 -> Low
//			bprintf(PRINT_NORMAL, _T("  - Chute 1 coin counter -> Low (0x%02X).\n"), byteValue);
			break;
		case 0xE7:											// Coin counter chute 2 -> Low
//			bprintf(PRINT_NORMAL, _T("  - Chute 2 coin counter -> Low (0x%02X).\n"), byteValue);
			break;
	}

	return;
}

static void WriteIO2(INT32 nOffset, UINT8 byteValue)
{
	switch (nOffset) {
		case 0x01:
		case 0x09:
		case 0x11:
		case 0x19: // Screen Brightness
			if (nNeoSystemType & NEO_SYS_CART) {
				NeoRecalcPalette = 1;
				bNeoDarkenPalette = (nOffset == 0x11) ? 1 : 0;
				//bprintf(PRINT_NORMAL, _T("  - Darken Palette %X (0x%02X, at scanline %i).\n"), bNeoDarkenPalette, byteValue, SekCurrentScanline());
			}
			break;

		case 0x03:											// Select BIOS vector table
			if (nNeoSystemType & NEO_SYS_CART) {
				if (!b68KBoardROMBankedIn) {
					MapVectorTable(true);
					b68KBoardROMBankedIn = true;
				}
//				bprintf(PRINT_NORMAL, _T("  - BIOS vector table banked in (0x%02X).\n"), byteValue);
			}
			break;

		case 0x0B:											// Select BIOS text ROM
//			bprintf(PRINT_NORMAL, _T("  - BIOS text/Z80 ROM banked in (0x%02X).\n"), byteValue);

			bBIOSTextROMEnabled = !(nNeoSystemType & (NEO_SYS_PCB | NEO_SYS_AES));

			if (bZ80BIOS) {
				if (!bZ80BoardROMBankedIn) {
					bZ80BoardROMBankedIn = true;
					NeoZ80MapROM(true);
				}

#if defined Z80_RESET_ON_BANKSWITCH
				nSoundStatus |= 1;
				ZetReset();
#endif
			}
			break;

		case 0x0D:											// Write-protect SRAM
			bSRAMWritable = false;
//			bprintf(PRINT_NORMAL, _T("  - SRAM write-protected (0x%02X).\n"), byteValue);
			break;

		case 0x0F:											// Select palette bank 1
//			bprintf(PRINT_NORMAL, _T("  - Palette 1 banked in (0x%02X).\n"), byteValue);
			MapPalette(1);
			break;

		case 0x13:											// Select game vector table
			if (nNeoSystemType & NEO_SYS_CART) {
				if (b68KBoardROMBankedIn) {
					MapVectorTable(false);
					b68KBoardROMBankedIn = false;
				}
//				bprintf(PRINT_NORMAL, _T("  - ROM vector table banked in (0x%02X).\n"), byteValue);
			}
			break;

		case 0x1B:											// Select game text ROM
//			bprintf(PRINT_NORMAL, _T("  - Cartridge text/Z80 ROM banked in (0x%02X).\n"), byteValue);

			bBIOSTextROMEnabled = false;

			if (bZ80BIOS) {
				if (bZ80BoardROMBankedIn) {
					bZ80BoardROMBankedIn = false;
					NeoZ80MapROM(false);
				}

#if defined Z80_RESET_ON_BANKSWITCH
				nSoundStatus |= 1;
				ZetReset();
#endif
			}
			break;

		case 0x1D:											// Write-enable SRAM
			bSRAMWritable = true;
//			bprintf(PRINT_NORMAL, _T("  - SRAM writable (0x%02X).\n"), byteValue);
			break;

		case 0x1F:											// Select palette bank 0
//			bprintf(PRINT_NORMAL, _T("  - Palette 0 banked in (0x%02X).\n"), byteValue);
			MapPalette(0);
			break;
	}

	return;
}

static void __fastcall neogeoWriteByte(UINT32 sekAddress, UINT8 byteValue)
{
	switch (sekAddress & 0xFF0000) {
		case 0x300000:
			if ((sekAddress & 1) == 1) {
//				bprintf(PRINT_NORMAL, "  - Watchdog timer reset (%02X, at scanline %i)\n", byteValue, SekCurrentScanline());

#if defined EMULATE_WATCHDOG
				nNeoWatchdog = -SekTotalCycles();
#endif

			}
			return;

		case 0x320000:
			if ((sekAddress & 1) == 0) {
				SendSoundCommand(byteValue);
			}
			return;

		case 0x380000:
			WriteIO1(sekAddress & 0xFF, byteValue);
			return;

		case 0x3A0000:
			WriteIO2(sekAddress & 0x1F, byteValue);
			return;
	}

//	bprintf(PRINT_NORMAL, _T("  - Attempt to write byte 0x%06X ->   0x%02X\n"), sekAddress, byteValue);

	return;
}

void __fastcall neogeoWriteWord(UINT32 sekAddress, UINT16 wordValue)
{
//	neogeoWriteByte(sekAddress | 1, wordValue);

	SEK_DEF_WRITE_WORD(0, sekAddress, wordValue);

//	bprintf(PRINT_NORMAL, _T("  - Attempt to write word 0x%06X -> 0x%04X\n"), sekAddress, wordValue);
}

// ----------------------------------------------------------------------------
// Video controller reads

static UINT16 __fastcall neogeoReadWordVideo(UINT32 sekAddress)
{
	switch (sekAddress & 6) {
		case 0x00:
		case 0x02:
//			bprintf(PRINT_NORMAL, "  - Graphics RAM read (Bank %i, address 0x%04X).\n", NeoGraphicsRAMPointer > 0xFFFF ? 1 : 0, NeoGraphicsRAMPointer & 0xFFFF);
			return *((UINT16*)(NeoGraphicsRAMBank + NeoGraphicsRAMPointer));

		case 0x04:
//			bprintf(PRINT_NORMAL, "  - Graphics RAM modulo read.\n");
			return (UINT16)(nNeoGraphicsModulo >> 1);

		case 0x06:										// Display status
//			bprintf(PRINT_NORMAL, "  - Display status read, line: %3i, anim: %i\n", SekCurrentScanline(), nNeoSpriteFrame);

#if 1 && !defined USE_SPEEDHACKS
			bForcePartialRender |= bForceUpdateOnStatusRead;
#endif

			return ((SekCurrentScanline() + nScanlineOffset) << 7) | 0 | (nNeoSpriteFrame & 7);
	}

	return 0;
}

static UINT8 __fastcall neogeoReadByteVideo(UINT32 sekAddress)
{
	if (sekAddress & 1) {
		return 0x0FF;
	} else {
		return neogeoReadWordVideo(sekAddress) >> 8;
	}
}

static void __fastcall neogeoWriteWordVideo(UINT32 sekAddress, UINT16 wordValue)
{
//	if (sekAddress >= 0x3C0010)
//	bprintf(PRINT_NORMAL, _T("  - Attempt to write word 0x%06X -> 0x%04X\n"), sekAddress, wordValue);

	switch (sekAddress & 14) {
		case 0x00: {
			NeoGraphicsRAMPointer = wordValue << 1;
			NeoGraphicsRAMBank = NeoGraphicsRAM;

			if (wordValue & 0x8000) {
				NeoGraphicsRAMBank += 0x00010000;
			}
			break;
		}
		case 0x02: {
			*((UINT16*)(NeoGraphicsRAMBank + NeoGraphicsRAMPointer)) = wordValue;
			NeoGraphicsRAMPointer += nNeoGraphicsModulo;

#if 0
			if ((NeoGraphicsRAMBank == NeoGraphicsRAM) && NeoGraphicsRAMPointer >= 0xC000 && NeoGraphicsRAMPointer < 0xE000) {
				bprintf(PRINT_NORMAL, _T("VRAM bank 0 + 0x%04X -> %04X\n"), NeoGraphicsRAMPointer, wordValue);
			}
			if ((NeoGraphicsRAMBank != NeoGraphicsRAM) && NeoGraphicsRAMPointer >= 0x0C00) {
				bprintf(PRINT_NORMAL, _T("VRAM bank 1 + 0x%04X -> %04X\n"), NeoGraphicsRAMPointer, wordValue);
			}
#endif

#if 1 && defined USE_SPEEDHACKS
			bForcePartialRender |= bForceUpdateOnStatusRead;
#endif
			break;
		}
		case 0x04: {
			nNeoGraphicsModulo = ((INT16)wordValue) << 1;
			break;
		}

		case 0x06: {
			nSpriteFrameSpeed = (wordValue >> 8);

			if ((nIRQControl & 0x10) == 0 && wordValue & 0x10) {

#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("  - IRQ enabled  (at line %3i, IRQControl: 0x%02X).\n"), SekCurrentScanline(), wordValue & 0xFF);
#endif

				if (nIRQCycles < nCyclesSegment) {
					SekRunAdjust(nIRQCycles - nCyclesSegment);
				}
			}

#if 0 || defined LOG_IRQ
			if (nIRQControl & 0x10 && (wordValue & 0x10) == 0) {
				bprintf(PRINT_NORMAL, _T("  - IRQ disabled (at line %3i, IRQControl: 0x%02X).\n"), SekCurrentScanline(), wordValue & 0xFF);
			}
#endif

			nIRQControl = wordValue;
//			bprintf(PRINT_NORMAL, _T("  - Autoanim speed -> 0x%02X\n"), wordValue >> 8);
//			bprintf(PRINT_NORMAL, _T("  - IRQ control register -> 0x%02X (at line %3i)\n"), wordValue & 0xFF, SekCurrentScanline());
			break;
		}

		case 0x08: {
//			bprintf(PRINT_NORMAL, "0x%06X -> 0x%04X\n", sekAddress, wordValue);
			// Bit 15 seems to be ignored
			nIRQOffset = (nIRQOffset & 0x0000FFFF) | ((wordValue & 0x7FFF) << 16);
			break;
		}
		case 0x0A: {
//			bprintf(PRINT_NORMAL, "0x%06X -> 0x%04X\n", sekAddress, wordValue);
			nIRQOffset = (nIRQOffset & 0xFFFF0000) | wordValue;

#if 0 || defined LOG_IRQ
			bprintf(PRINT_NORMAL, _T("  - IRQ offs -> 0x%08X (at line %3i, IRQControl: 0x%02X).\n"), nIRQOffset, SekCurrentScanline(), nIRQControl);
#endif

			if (nIRQControl & 0x20) {
				// when implimented incorrectly:
				//   turfmast: pixels glitch in the water
				//   ssideki2: turf glitches like maddness

				// note: not sure about the "+ 8", but it's needed for turfmast (otherwise flickery pixels)

				nIRQCycles = SekTotalCycles() + NeoConvertIRQPosition(nIRQOffset + 8);

#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("    IRQ Line -> %3i (at line %3i, relative).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

				if (nIRQCycles < 0) {
					nIRQCycles = NO_IRQ_PENDING;
				}
				if (nIRQCycles < nCyclesSegment) {
					SekRunAdjust(nIRQCycles - nCyclesSegment);
				}
			}

			break;
		}

		case 0x0C: {
			NeoIRQUpdate(wordValue);
			break;
		}
	}
}

static void __fastcall neogeoWriteByteVideo(UINT32 sekAddress, UINT8 byteValue)
{
//	bprintf(PRINT_NORMAL, _T("  - Attempt to write byte 0x%06X ->   0x%02X\n"), sekAddress, byteValue);

#if 1
	if ((sekAddress & 1) == 0) {
		neogeoWriteWordVideo(sekAddress, byteValue);
	}
#else
	if (sekAddress & 1) {
		neogeoWriteWordVideo(sekAddress, byteValue);
	} else {
		neogeoWriteWordVideo(sekAddress, (byteValue << 8));
	}
#endif

#if 1 && defined USE_SPEEDHACKS
	bForcePartialRender |= bForceUpdateOnStatusRead;
#endif
}

// ----------------------------------------------------------------------------
// Backup RAM on MVS hardware

static void __fastcall neogeoWriteByteSRAM(UINT32 sekAddress, UINT8 byteValue)
{
	sekAddress &= 0xFFFF;

	if (bSRAMWritable) {
		NeoNVRAM[sekAddress ^ 1] = byteValue;
	}
}

static void __fastcall neogeoWriteWordSRAM(UINT32 sekAddress, UINT16 wordValue)
{
	sekAddress &= 0xFFFF;

	if (bSRAMWritable) {
		*((UINT16*)(NeoNVRAM + sekAddress)) = BURN_ENDIAN_SWAP_INT16(wordValue);
	}
}

// ----------------------------------------------------------------------------

static UINT8 __fastcall neogeoReadByteMemoryCard(UINT32 sekAddress)
{
//	if (sekAddress < 0x800100)
//	bprintf(PRINT_NORMAL, _T("  - Memcard 0x%04X read (PC: 0x%06X).\n"), sekAddress & 0x7FFF, SekGetPC(-1));

	if (bMemoryCardInserted) {
		if ((NeoSystem & 0x40) || (sekAddress & 1)) {
			return NeoMemoryCard[sekAddress & 0x01FFFF];
		}
	}

	return 0xFF;
}

static void __fastcall neogeoWriteByteMemoryCard(UINT32 sekAddress, UINT8 byteValue)
{
//	if (sekAddress < 0x800100)
//	bprintf(PRINT_NORMAL, _T("  - Memcard 0x%04X -> 0x%02X (PC: 0x%06X).\n"), sekAddress & 0x7FFF, byteValue, SekGetPC(-1));

	if (bMemoryCardInserted && bMemoryCardWritable) {
		if ((NeoSystem & 0x40) || (sekAddress & 1)) {
			NeoMemoryCard[sekAddress & 0x01FFFF] = byteValue;
		}
	}
}

static UINT8 __fastcall neoCDReadByteMemoryCard(UINT32 sekAddress)
{
	sekAddress &= 0x01FFFF;
	if (sekAddress < 0x4000 && sekAddress & 1) {
		return NeoMemoryCard[sekAddress];
	}

	return 0xFF;
}

static void __fastcall neoCDWriteByteMemoryCard(UINT32 sekAddress, UINT8 byteValue)
{
	sekAddress &= 0x01FFFF;
	if (sekAddress < 0x4000 && sekAddress & 1) {
		NeoMemoryCard[sekAddress] = byteValue;
	}
}

// ----------------------------------------------------------------------------
// NeoCD CD-ROM controller handlers

#define CD_FRAMES_MINUTE (60 * 75)
#define CD_FRAMES_SECOND (     75)
#define CD_FRAMES_PREGAP ( 2 * 75)

static void NeoCDLBAToMSF(const INT32 LBA)
{
	NeoCDSectorMin = (LBA + CD_FRAMES_PREGAP)                    / CD_FRAMES_MINUTE;
	NeoCDSectorSec = (LBA + CD_FRAMES_PREGAP) % CD_FRAMES_MINUTE / CD_FRAMES_SECOND;
	NeoCDSectorFrm = (LBA + CD_FRAMES_PREGAP) % CD_FRAMES_SECOND;
}

static void NeoCDCommsWrite(UINT8 data)
{
	if (NeoCDCommsWordCount >= 0 && NeoCDCommsWordCount < 10) {
		NeoCDCommsCommandFIFO[NeoCDCommsWordCount] = data & 0x0F;
	}
}

static UINT8 NeoCDCommsread()
{
	UINT8 ret = 0;

	if (NeoCDCommsWordCount >= 0 && NeoCDCommsWordCount < 10) {
		ret = NeoCDCommsStatusFIFO[NeoCDCommsWordCount] & 0x0F;
	}

	if (bNeoCDCommsClock) {
		ret |= 0x10;
	}

	return ret;
}

static void NeoCDCommsReset()
{
	bNeoCDCommsSend  = false;
	bNeoCDCommsClock = true;

	memset(NeoCDCommsCommandFIFO, 0, sizeof(NeoCDCommsCommandFIFO));
	memset(NeoCDCommsStatusFIFO,  0, sizeof(NeoCDCommsStatusFIFO));

	NeoCDCommsWordCount = 0;

	NeoCDAssyStatus = 9;

	memset(NeoCDSectorData, 0, sizeof(NeoCDSectorData));

	bNeoCDLoadSector = false;

	nNeoCDMode = 0;
	NeoCDSectorLBA = 0;
}

static void LC8951UpdateHeader()
{
	NeoCDLBAToMSF(NeoCDSectorLBA);

	if (LC8951RegistersW[11] & 1) {

		// HEAD registers have sub-header

		LC8951RegistersR[4] = 0;													// HEAD0
		LC8951RegistersR[5] = 0;													// HEAD1
		LC8951RegistersR[6] = 0;													// HEAD2
		LC8951RegistersR[7] = 0;													// HEAD3

	} else {

		// HEAD registers have header
		LC8951RegistersR[4] = NeoCDSectorData[12];	// HEAD0
		LC8951RegistersR[5] = NeoCDSectorData[13];	// HEAD1
		LC8951RegistersR[6] = NeoCDSectorData[14];	// HEAD2
		LC8951RegistersR[7] = NeoCDSectorData[15];	// HEAD3
	}
}

static char* LC8951InitTransfer()
{
	if (!LC8951RegistersW[6]) {
		bprintf(PRINT_ERROR, _T("    LC8951 DTTRG status invalid\n"));
		return NULL;
	}
	if (!(LC8951RegistersW[1] & 0x02)) {
		bprintf(PRINT_ERROR, _T("    LC8951 DOUTEN status invalid\n"));
		return NULL;
	}
	if (((LC8951RegistersW[5] << 8) | LC8951RegistersW[4]) + (NeoCDDMACount << 1) > 2352) {
		if (SekReadWord(0x108) == 0x044) { // aof bonus round fix (aof bug 1/2)
			bprintf(0, _T("NeoGeoCD: aof-bonus round patch. dmacnt %X\n"), NeoCDDMACount);
			SekWriteLong(0x10FEFC, 0x800);
			NeoCDDMACount = 0x400;
		} else {
			bprintf(PRINT_ERROR, _T("    DMA transfer exceeds current sector in LC8951 external buffer\n"));
			return NULL;
		}
	}

	return NeoCDSectorData + 12 + ((LC8951RegistersW[5] << 8) | LC8951RegistersW[4]);
}

static void LC8951EndTransfer()
{
	LC8951RegistersW[6]  = 0x00; 												// reset DTTRG

	LC8951RegistersR[1] |= 0x48;												//   set DTEI & DTBSY
	if (LC8951RegistersW[1] & 0x40) {

		// trigger DTE interrupt

		// the Neo Geo CD doesn't use the DTE interrupt
		// nIRQAcknowledge &= ~0x20;
		// NeoCDIRQUpdate(0);

	}
}

static void LC8951Reset()
{
	nLC8951Register = 0;
	memset(LC8951RegistersR, 0, sizeof(LC8951RegistersR));
	memset(LC8951RegistersW, 0, sizeof(LC8951RegistersW));

	bNeoCDLoadSector = false;

	LC8951RegistersR[0x01] = 0xFF;
	LC8951RegistersR[0x0F] = 0x80;

	memset(NeoCDSectorData, 0, sizeof(NeoCDSectorData));
	LC8951UpdateHeader();
}

static void NeoCDProcessCommand()
{
	memset(NeoCDCommsStatusFIFO,  0, sizeof(NeoCDCommsStatusFIFO));

	if (NeoCDCommsCommandFIFO[0]) {
		NeoCDCommsStatusFIFO[1] = 15;
	}

	switch (NeoCDCommsCommandFIFO[0]) {
		case 0:
			break;
		case 1:
//								bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			CDEmuStop();

			NeoCDAssyStatus = 0x0E;
			bNeoCDLoadSector = false;
			break;
		case 2:
//								bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			NeoCDCommsStatusFIFO[1] = NeoCDCommsCommandFIFO[3];
			switch (NeoCDCommsCommandFIFO[3]) {

				case 0: {
					UINT8* ChannelData = CDEmuReadQChannel();

					NeoCDCommsStatusFIFO[2] = ChannelData[1] >> 4;
					NeoCDCommsStatusFIFO[3] = ChannelData[1] & 0x0F;

					NeoCDCommsStatusFIFO[4] = ChannelData[2] >> 4;
					NeoCDCommsStatusFIFO[5] = ChannelData[2] & 0x0F;

					NeoCDCommsStatusFIFO[6] = ChannelData[3] >> 4;
					NeoCDCommsStatusFIFO[7] = ChannelData[3] & 0x0F;

					NeoCDCommsStatusFIFO[8] = ChannelData[7] >> 4;

// bprintf(PRINT_ERROR, _T("    %02i %02i:%02i:%02i %02i:%02i:%02i %02i\n"), ChannelData[0], ChannelData[1], ChannelData[2], ChannelData[3], ChannelData[4], ChannelData[5], ChannelData[6], ChannelData[7]);

					break;
				}
				case 1: {
					UINT8* ChannelData = CDEmuReadQChannel();

					NeoCDCommsStatusFIFO[2] = ChannelData[4] >> 4;
					NeoCDCommsStatusFIFO[3] = ChannelData[4] & 0x0F;

					NeoCDCommsStatusFIFO[4] = ChannelData[5] >> 4;
					NeoCDCommsStatusFIFO[5] = ChannelData[5] & 0x0F;

					NeoCDCommsStatusFIFO[6] = ChannelData[6] >> 4;
					NeoCDCommsStatusFIFO[7] = ChannelData[6] & 0x0F;

					NeoCDCommsStatusFIFO[8] = ChannelData[7] >> 4;

					break;
				}
				case 2: {
					UINT8* ChannelData = CDEmuReadQChannel();

					NeoCDCommsStatusFIFO[2] = ChannelData[0] >> 4;
					NeoCDCommsStatusFIFO[3] = ChannelData[0] & 0x0F;

					UINT8* TOCEntry = CDEmuReadTOC(CDEmuTOC_FIRSTINDEX);
					NeoCDCommsStatusFIFO[4] = TOCEntry[0] >> 4;
					NeoCDCommsStatusFIFO[5] = TOCEntry[0] & 0x0F;

					NeoCDCommsStatusFIFO[8] = ChannelData[7] >> 4;

					break;
				}
				case 3: {
					UINT8* TOCEntry = CDEmuReadTOC(CDEmuTOC_LASTMSF);

					NeoCDCommsStatusFIFO[2] = TOCEntry[0] >> 4;
					NeoCDCommsStatusFIFO[3] = TOCEntry[0] & 0x0F;

					NeoCDCommsStatusFIFO[4] = TOCEntry[1] >> 4;
					NeoCDCommsStatusFIFO[5] = TOCEntry[1] & 0x0F;

					NeoCDCommsStatusFIFO[6] = TOCEntry[2] >> 4;
					NeoCDCommsStatusFIFO[7] = TOCEntry[2] & 0x0F;

					break;
				}
				case 4: {
					UINT8* TOCEntry = CDEmuReadTOC(CDEmuTOC_FIRSTLAST);

					NeoCDCommsStatusFIFO[2] = TOCEntry[0] > 4;
					NeoCDCommsStatusFIFO[3] = TOCEntry[0] & 0x0F;

					NeoCDCommsStatusFIFO[4] = TOCEntry[1] >> 4;
					NeoCDCommsStatusFIFO[5] = TOCEntry[1] & 0x0F;

					break;
				}
				case 5:	{
					INT32 NeoCDTrack = (NeoCDCommsCommandFIFO[4] << 4) | NeoCDCommsCommandFIFO[5];

					UINT8* TOCEntry = CDEmuReadTOC(NeoCDTrack);

					NeoCDCommsStatusFIFO[2] = TOCEntry[0] >> 4;
					NeoCDCommsStatusFIFO[3] = TOCEntry[0] & 0x0F;

					NeoCDCommsStatusFIFO[4] = TOCEntry[1] >> 4;
					NeoCDCommsStatusFIFO[5] = TOCEntry[1] & 0x0F;

					NeoCDCommsStatusFIFO[6] = TOCEntry[2] >> 4;
					NeoCDCommsStatusFIFO[7] = TOCEntry[2] & 0x0F;

					// bit 3 of the 1st minutes digit indicates a data track
					if (TOCEntry[3] & 4) {
						NeoCDCommsStatusFIFO[6] |= 8;
					}

					NeoCDCommsStatusFIFO[8] = NeoCDTrack & 0x0F;

					break;
				}

				case 6: {
					UINT8* ChannelData = CDEmuReadQChannel();

					NeoCDCommsStatusFIFO[8] = ChannelData[7] >> 4;

					break;
				}

				case 7: {
					NeoCDCommsStatusFIFO[2] = 0;
					NeoCDCommsStatusFIFO[3] = 2; // must be 02, 0E, 0F, or 05

					NeoCDCommsStatusFIFO[4] = 0;
					NeoCDCommsStatusFIFO[5] = 0;

					NeoCDCommsStatusFIFO[6] = 0;
					NeoCDCommsStatusFIFO[7] = 0;
					break;
				}
			 }
			break;

		case 3: { // play (audio) / start read (data)
			NeoCDSectorLBA  = NeoCDCommsCommandFIFO[2] * (10 * CD_FRAMES_MINUTE);
			NeoCDSectorLBA += NeoCDCommsCommandFIFO[3] * ( 1 * CD_FRAMES_MINUTE);
			NeoCDSectorLBA += NeoCDCommsCommandFIFO[4] * (10 * CD_FRAMES_SECOND);
			NeoCDSectorLBA += NeoCDCommsCommandFIFO[5] * ( 1 * CD_FRAMES_SECOND);
			NeoCDSectorLBA += NeoCDCommsCommandFIFO[6] * (10                   );
			NeoCDSectorLBA += NeoCDCommsCommandFIFO[7] * ( 1                   );

			CDEmuPlay((NeoCDCommsCommandFIFO[2] * 16) + NeoCDCommsCommandFIFO[3], (NeoCDCommsCommandFIFO[4] * 16) + NeoCDCommsCommandFIFO[5], (NeoCDCommsCommandFIFO[6] * 16) + NeoCDCommsCommandFIFO[7]);

			// aof has a bug that happens before the bonus stage. (aof bug 2/2)
			// depending on the buttons pressed during the motorcycle cut-scene
			// (or just randomness) it will:
			// make bad dma request (99.9% of the time)
			// load bad values into the lc8951 (buttons pressed)
			// Here we make a little fix to the registers just incase... -dink july.1.2019

			if (CDEmuGetStatus() != playing) { // its not audio
				bprintf(0, _T("         - data read\n"));
				LC8951RegistersW[10] |= 4; // data mode
				CDEmuStartRead();
			} else {
				LC8951RegistersW[10] &= ~4; // audio mode
			}

			NeoCDAssyStatus = 1;
			bNeoCDLoadSector = true;
			break;

#if 0
			if (LC8951RegistersW[10] & 4) {

				if (CDEmuGetStatus() == playing) {
					//bprintf(PRINT_ERROR, _T("*** Switching CD mode to CD-ROM while in audio mode!(PC: 0x%06X)\n"), SekGetPC(-1));
				}

				NeoCDSectorLBA  = NeoCDCommsCommandFIFO[2] * (10 * CD_FRAMES_MINUTE);
				NeoCDSectorLBA += NeoCDCommsCommandFIFO[3] * ( 1 * CD_FRAMES_MINUTE);
				NeoCDSectorLBA += NeoCDCommsCommandFIFO[4] * (10 * CD_FRAMES_SECOND);
				NeoCDSectorLBA += NeoCDCommsCommandFIFO[5] * ( 1 * CD_FRAMES_SECOND);
				NeoCDSectorLBA += NeoCDCommsCommandFIFO[6] * (10                   );
				NeoCDSectorLBA += NeoCDCommsCommandFIFO[7] * ( 1                   );

				CDEmuStartRead();
//				LC8951RegistersR[1] |= 0x20;
			} else {

				if (CDEmuGetStatus() == reading) {
					//bprintf(PRINT_ERROR, _T("*** Switching CD mode to audio while in CD-ROM mode!(PC: 0x%06X)\n"), SekGetPC(-1));
				}

				CDEmuPlay((NeoCDCommsCommandFIFO[2] * 16) + NeoCDCommsCommandFIFO[3], (NeoCDCommsCommandFIFO[4] * 16) + NeoCDCommsCommandFIFO[5], (NeoCDCommsCommandFIFO[6] * 16) + NeoCDCommsCommandFIFO[7]);
			}

			NeoCDAssyStatus = 1;
			bNeoCDLoadSector = true;

			break;
#endif
		}
		case 4:
//			bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			CDEmuPause();
			break;
		case 5:
//			bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
//			NeoCDAssyStatus = 9;
//			bNeoCDLoadSector = false;
			break;

		case 6:
//			bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			NeoCDAssyStatus = 4;
			bNeoCDLoadSector = false;
			CDEmuPause();
			break;
		case 7:
//			bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			NeoCDAssyStatus = 1;
			bNeoCDLoadSector = true;
			CDEmuResume();
			break;

		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
//			bprintf(PRINT_ERROR, _T("    CD comms received command %i\n"), NeoCDCommsCommandFIFO[0]);
			NeoCDAssyStatus = 9;
			bNeoCDLoadSector = false;
			break;
	}
}

// For Double Dragon (Early release) - see notes a few pages below..
static INT32 CheckDMASourceForBlankVectorTable(INT32 dmadest, INT32 dmasrc)
{
	if (dmadest == 0) {
		INT32 vectnotok = 1;
		for (INT32 i = 0; i < 0x40; i++) {
			if (SekReadWord(dmasrc + (i<<1)) != 0x0000)
				vectnotok = 0;
		}

		return vectnotok;
	} else {
		return 0;
	}
}

static void NeoCDDoDMA()
{

	// The LC8953 chip has a programmable DMA controller, which is not properly emulated.
	// Since the software only uses it in a limited way, we can apply a simple heuristic
	// to determnine the requested operation.

	// Additionally, we don't know how many cycles DMA operations take.
	// Here, only bus access is used to get a rough approximation --
	// each read/write takes a single cycle, setup and everything else is ignored.

//	bprintf(PRINT_IMPORTANT, _T("  - DMA controller transfer started (PC: 0x%06X)\n"), SekGetPC(-1));

	switch (NeoCDDMAMode) {

		case 0xCFFD: {
//			bprintf(PRINT_NORMAL, _T("    adr : 0x%08X - 0x%08X <- address, skip odd bytes\n"), NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 8);

			//  - DMA controller 0x7E -> 0xCFFD (PC: 0xC07CE2)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC07CE8)
			//  - DMA controller program[02] -> 0xE8DA (PC: 0xC07CEE)
			//  - DMA controller program[04] -> 0x92DA (PC: 0xC07CF4)
			//  - DMA controller program[06] -> 0x92DB (PC: 0xC07CFA)
			//  - DMA controller program[08] -> 0x96DB (PC: 0xC07D00)
			//  - DMA controller program[10] -> 0x96F6 (PC: 0xC07D06)
			//  - DMA controller program[12] -> 0x2E02 (PC: 0xC07D0C)
			//  - DMA controller program[14] -> 0xFDFF (PC: 0xC07D12)

			//SekIdle(NeoCDDMACount * 4);

			while (NeoCDDMACount--) {
				SekWriteWord(NeoCDDMAAddress1 + 0, NeoCDDMAAddress1 >> 24);
				SekWriteWord(NeoCDDMAAddress1 + 2, NeoCDDMAAddress1 >> 16);
				SekWriteWord(NeoCDDMAAddress1 + 4, NeoCDDMAAddress1 >>  8);
				SekWriteWord(NeoCDDMAAddress1 + 6, NeoCDDMAAddress1 >>  0);
				NeoCDDMAAddress1 += 8;
			}

			break;
		}

		case 0xE2DD: {
//			bprintf(PRINT_NORMAL, _T("    copy: 0x%08X - 0x%08X <- 0x%08X - 0x%08X, skip odd bytes\n"), NeoCDDMAAddress2, NeoCDDMAAddress2 + NeoCDDMACount * 2, NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 4);

			//  - DMA controller 0x7E -> 0xE2DD (PC: 0xC0A190)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0A192)
			//  - DMA controller program[02] -> 0x82BE (PC: 0xC0A194)
			//  - DMA controller program[04] -> 0x93DA (PC: 0xC0A196)
			//  - DMA controller program[06] -> 0xBE93 (PC: 0xC0A198)
			//  - DMA controller program[08] -> 0xDABE (PC: 0xC0A19A)
			//  - DMA controller program[10] -> 0xF62D (PC: 0xC0A19C)
			//  - DMA controller program[12] -> 0x02FD (PC: 0xC0A19E)
			//  - DMA controller program[14] -> 0xFFFF (PC: 0xC0A1A0)

			//SekIdle(NeoCDDMACount * 1);

			while (NeoCDDMACount--) {
				SekWriteWord(NeoCDDMAAddress2 + 0, SekReadByte(NeoCDDMAAddress1 + 0));
				SekWriteWord(NeoCDDMAAddress2 + 2, SekReadByte(NeoCDDMAAddress1 + 1));
				NeoCDDMAAddress1 += 2;
				NeoCDDMAAddress2 += 4;
			}

			break;
		}

		case 0xFC2D: {
//			bprintf(PRINT_NORMAL, _T("    copy: 0x%08X - 0x%08X <- LC8951 external buffer, skip odd bytes\n"), NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 4);

			//  - DMA controller 0x7E -> 0xFC2D (PC: 0xC0A190)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0A192)
			//  - DMA controller program[02] -> 0x8492 (PC: 0xC0A194)
			//  - DMA controller program[04] -> 0xDA92 (PC: 0xC0A196)
			//  - DMA controller program[06] -> 0xDAF6 (PC: 0xC0A198)
			//  - DMA controller program[08] -> 0x2A02 (PC: 0xC0A19A)
			//  - DMA controller program[10] -> 0xFDFF (PC: 0xC0A19C)
			//  - DMA controller program[12] -> 0x48E7 (PC: 0xC0A19E)
			//  - DMA controller program[14] -> 0xFFFE (PC: 0xC0A1A0)

			char* data = LC8951InitTransfer();
			if (data == NULL) {
				break;
			}

			//SekIdle(NeoCDDMACount * 4);

			while (NeoCDDMACount--) {
				SekWriteByte(NeoCDDMAAddress1 + 0, data[0]);
				SekWriteByte(NeoCDDMAAddress1 + 2, data[1]);
				NeoCDDMAAddress1 += 4;
				data += 2;
			}

			LC8951EndTransfer();

			break;
		}

		case 0xFE3D:

			//  - DMA controller 0x7E -> 0xFE3D (PC: 0xC0A190)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0A192)
			//  - DMA controller program[02] -> 0x82BF (PC: 0xC0A194)
			//  - DMA controller program[04] -> 0x93BF (PC: 0xC0A196)
			//  - DMA controller program[06] -> 0xF629 (PC: 0xC0A198)
			//  - DMA controller program[08] -> 0x02FD (PC: 0xC0A19A)
			//  - DMA controller program[10] -> 0xFFFF (PC: 0xC0A19C)
			//  - DMA controller program[12] -> 0xF17D (PC: 0xC0A19E)
			//  - DMA controller program[14] -> 0xFCF5 (PC: 0xC0A1A0)

		case 0xFE6D: {
//			bprintf(PRINT_NORMAL, _T("    copy: 0x%08X - 0x%08X <- 0x%08X - 0x%08X\n"), NeoCDDMAAddress2, NeoCDDMAAddress2 + NeoCDDMACount * 2, NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 2);

			//  - DMA controller 0x7E -> 0xFE6D (PC: 0xC0FD7A)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0FD7C)
			//  - DMA controller program[02] -> 0x82BF (PC: 0xC0FD7E)
			//  - DMA controller program[04] -> 0xF693 (PC: 0xC0FD80)
			//  - DMA controller program[06] -> 0xBF29 (PC: 0xC0FD82)
			//  - DMA controller program[08] -> 0x02FD (PC: 0xC0FD84)
			//  - DMA controller program[10] -> 0xFFFF (PC: 0xC0FD86)
			//  - DMA controller program[12] -> 0xC515 (PC: 0xC0FD88)
			//  - DMA controller program[14] -> 0xFCF5 (PC: 0xC0FD8A)

			// Double Dragon clears the vector table before loading in the
			// actual vectors.  Let's just ignore the clearing part
			// so the game doesn't freeze while loading.

			INT32 OkWriteVect = !CheckDMASourceForBlankVectorTable(NeoCDDMAAddress2, NeoCDDMAAddress1);

			if (!OkWriteVect) {
				bprintf(0, _T("(DMA) Inhibit blank vector table write into 68k ram-vectspace\n"));
			}

			//SekIdle(NeoCDDMACount * 1);

			while (NeoCDDMACount--) {
				if (OkWriteVect || NeoCDDMAAddress2 >= 0x80) {
					SekWriteWord(NeoCDDMAAddress2, SekReadWord(NeoCDDMAAddress1));
				}

				NeoCDDMAAddress1 += 2;
				NeoCDDMAAddress2 += 2;
			}

			break;
		}

		case 0xFEF5: {
//			bprintf(PRINT_NORMAL, _T("    adr : 0x%08X - 0x%08X <- address\n"), NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 4);

			//  - DMA controller 0x7E -> 0xFEF5 (PC: 0xC07CE2)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC07CE8)
			//  - DMA controller program[02] -> 0x92E8 (PC: 0xC07CEE)
			//  - DMA controller program[04] -> 0xBE96 (PC: 0xC07CF4)
			//  - DMA controller program[06] -> 0xF629 (PC: 0xC07CFA)
			//  - DMA controller program[08] -> 0x02FD (PC: 0xC07D00)
			//  - DMA controller program[10] -> 0xFFFF (PC: 0xC07D06)
			//  - DMA controller program[12] -> 0xFC3D (PC: 0xC07D0C)
			//  - DMA controller program[14] -> 0xFCF5 (PC: 0xC07D12)

			//SekIdle(NeoCDDMACount * 2);

			while (NeoCDDMACount--) {
				SekWriteWord(NeoCDDMAAddress1 + 0, NeoCDDMAAddress1 >> 16);
				SekWriteWord(NeoCDDMAAddress1 + 2, NeoCDDMAAddress1 >>  0);
				NeoCDDMAAddress1 += 4;
			}

			break;
		}

		case 0xFFC5: {
//			bprintf(PRINT_NORMAL, _T("    copy: 0x%08X - 0x%08X <- LC8951 external buffer\n"), NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 2);

			//  - DMA controller 0x7E -> 0xFFC5 (PC: 0xC0A190)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0A192)
			//  - DMA controller program[02] -> 0xA6F6 (PC: 0xC0A194)
			//  - DMA controller program[04] -> 0x2602 (PC: 0xC0A196)
			//  - DMA controller program[06] -> 0xFDFF (PC: 0xC0A198)
			//  - DMA controller program[08] -> 0xFC2D (PC: 0xC0A19A)
			//  - DMA controller program[10] -> 0xFCF5 (PC: 0xC0A19C)
			//  - DMA controller program[12] -> 0x8492 (PC: 0xC0A19E)
			//  - DMA controller program[14] -> 0xDA92 (PC: 0xC0A1A0)

			char* data = LC8951InitTransfer();
			if (data == NULL) {
				break;
			}

			//SekIdle(NeoCDDMACount * 4);

			while (NeoCDDMACount--) {
				SekWriteByte(NeoCDDMAAddress1 + 0, data[0]);
				SekWriteByte(NeoCDDMAAddress1 + 1, data[1]);
				NeoCDDMAAddress1 += 2;
				data += 2;
			}

			LC8951EndTransfer();

			break;
		}

		case 0xFFCD:

			//  - DMA controller 0x7E -> 0xFFCD (PC: 0xC0A190)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC0A192)
			//  - DMA controller program[02] -> 0x92F6 (PC: 0xC0A194)
			//  - DMA controller program[04] -> 0x2602 (PC: 0xC0A196)
			//  - DMA controller program[06] -> 0xFDFF (PC: 0xC0A198)
			//  - DMA controller program[08] -> 0x7006 (PC: 0xC0A19A)
			//  - DMA controller program[10] -> 0x6100 (PC: 0xC0A19C)
			//  - DMA controller program[12] -> 0x2412 (PC: 0xC0A19E)
			//  - DMA controller program[14] -> 0x13FC (PC: 0xC0A1A0)

		case 0xFFDD: {
//			bprintf(PRINT_NORMAL, _T("    Fill: 0x%08X - 0x%08X <- 0x%04X\n"), NeoCDDMAAddress1, NeoCDDMAAddress1 + NeoCDDMACount * 2, NeoCDDMAValue1);

			//  - DMA controller 0x7E -> 0xFFDD (PC: 0xC07CE2)
			//  - DMA controller program[00] -> 0xFCF5 (PC: 0xC07CE8)
			//  - DMA controller program[02] -> 0x92F6 (PC: 0xC07CEE)
			//  - DMA controller program[04] -> 0x2602 (PC: 0xC07CF4)
			//  - DMA controller program[06] -> 0xFDFF (PC: 0xC07CFA)
			//  - DMA controller program[08] -> 0xFFFF (PC: 0xC07D00)
			//  - DMA controller program[10] -> 0xFCF5 (PC: 0xC07D06)
			//  - DMA controller program[12] -> 0x8AF0 (PC: 0xC07D0C)
			//  - DMA controller program[14] -> 0x1609 (PC: 0xC07D12)

			//SekIdle(NeoCDDMACount * 1);

			while (NeoCDDMACount--) {
				SekWriteWord(NeoCDDMAAddress1, NeoCDDMAValue1);
				NeoCDDMAAddress1 += 2;
			}

			break;
		}
		default: {
			bprintf(PRINT_ERROR, _T("    Unknown transfer type 0x%04X (PC: 0x%06X)\n"), NeoCDDMAMode, SekGetPC(-1));
			bprintf(PRINT_NORMAL, _T("    ??? : 0x%08X  0x%08X 0x%04X 0x%04X 0x%08X\n"), NeoCDDMAAddress1, NeoCDDMAAddress2, NeoCDDMAValue1, NeoCDDMAValue2, NeoCDDMACount);
		}
	}
}

static void NeoCDCommsControl(UINT8 clock, UINT8 send)
{
	if (clock && !bNeoCDCommsClock) {
		NeoCDCommsWordCount++;
		if (NeoCDCommsWordCount >= 10) {
			NeoCDCommsWordCount = 0;

			if (send) {

				// command receive complete

				if (NeoCDCommsCommandFIFO[0]) {
					INT32  sum = 0;

//					bprintf(PRINT_NORMAL, _T("  - CD mechanism command receive completed : 0x"));
					for (INT32 i = 0; i < 9; i++) {
//						bprintf(PRINT_NORMAL, _T("%X"), NeoCDCommsCommandFIFO[i]);
						sum += NeoCDCommsCommandFIFO[i];
					}
					sum = ~(sum + 5) & 0x0F;
//					bprintf(PRINT_NORMAL, _T(" (CS 0x%X, %s)\n"), NeoCDCommsCommandFIFO[9], (sum == NeoCDCommsCommandFIFO[9]) ? _T("OK") : _T("NG"));
					if (sum == NeoCDCommsCommandFIFO[9]) {

						NeoCDProcessCommand();

						if (NeoCDCommsCommandFIFO[0]) {

							if (NeoCDAssyStatus == 1) {
								if (CDEmuGetStatus() == idle) {
									NeoCDAssyStatus = 0x0E;
									bNeoCDLoadSector = false;
								}
							}

							NeoCDCommsStatusFIFO[0] = NeoCDAssyStatus;

							// compute checksum

							sum = 0;

							for (INT32 i = 0; i < 9; i++) {
								sum += NeoCDCommsStatusFIFO[i];
							}
							NeoCDCommsStatusFIFO[9] = ~(sum + 5) & 0x0F;
						}
					}
				}
			} else {

				// status send complete

//				if (NeoCDCommsStatusFIFO[0] || NeoCDCommsStatusFIFO[1]) {
//					INT32  sum = 0;
//
//					bprintf(PRINT_NORMAL, _T("  - CD mechanism status send completed : 0x"));
//					for (INT32 i = 0; i < 9; i++) {
//						bprintf(PRINT_NORMAL, _T("%X"), NeoCDCommsStatusFIFO[i]);
//						sum += NeoCDCommsStatusFIFO[i];
//					}
//					sum = ~(sum + 5) & 0x0F;
//					bprintf(PRINT_NORMAL, _T(" (CS 0x%X, %s)\n"), NeoCDCommsStatusFIFO[9], (sum == NeoCDCommsStatusFIFO[9]) ? _T("OK") : _T("NG"));
//				}

//				if (NeoCDAssyStatus == 0xE) {
//					NeoCDAssyStatus = 9;
//				}
			}

		}
		bNeoCDCommsSend = send;
	}
	bNeoCDCommsClock = clock;
}

static void NeoCDReadSector()
{
	if ((nff0002 & 0x0500)) {
		if (NeoCDAssyStatus == 1 && bNeoCDLoadSector) {
			NeoCDSectorLBA++;
			NeoCDSectorLBA = CDEmuLoadSector(NeoCDSectorLBA, NeoCDSectorData) - 1;

			if (LC8951RegistersW[10] & 0x80) {
				LC8951UpdateHeader();

				LC8951RegistersR[12] = 0x80;										// STAT0
				LC8951RegistersR[13] = 0;											// STAT1
				LC8951RegistersR[14] = 0x10;										// STAT2
				LC8951RegistersR[15] = 0;											// STAT3

				//bprintf(PRINT_IMPORTANT, _T("    Sector %08i (%02i:%02i:%02i) read\n"), NeoCDSectorLBA, NeoCDSectorMin, NeoCDSectorSec, NeoCDSectorFrm);

				if (NeoCDSectorData[(12 + 4) + 64] == 'g' && !strncmp(NeoCDSectorData + 12 + 4, "Copyright by SNK", 16)) {
					//bprintf(0, _T("\n    simulated CDZ protection error\n"));
					//bprintf(PRINT_ERROR, _T("    %.70hs\n"), NeoCDSectorData + sectoffs + 4);

					NeoCDSectorData[(12 + 4) + 64] = 'f';

					// LC8951RegistersR[12] = 0x00;									// STAT0
				}

				nIRQAcknowledge &= ~0x20;
				NeoCDIRQUpdate(0);

				LC8951RegistersR[1] &= ~0x20;

//				bprintf(PRINT_IMPORTANT, _T("    DECI interrupt triggered\n"));
			}
		}

		bNeoCDLoadSector = true;
	}
}

static UINT8 __fastcall neogeoReadByteCDROM(UINT32 sekAddress)
{
//	bprintf(PRINT_NORMAL, _T("  - CDROM: 0x%06X read (byte, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));

	switch (sekAddress & 0xFFFF) {

		case 0x0017:
			return nNeoCDMode;

		// LC8951 registers
		case 0x0101:
//			bprintf(PRINT_NORMAL, _T("  - LC8951 register read (PC: 0x%06X)\n"), SekGetPC(-1));
			return nLC8951Register;
		case 0x0103: {
//			bprintf(PRINT_NORMAL, _T("  - LC8951 register 0x%X read (PC: 0x%06X)\n"), nLC8951Register, SekGetPC(-1));

			INT32 reg = LC8951RegistersR[nLC8951Register];

			switch (nLC8951Register) {
				case 0x03:														// DBCH
					LC8951RegistersR[3] &=  0x0F;
					LC8951RegistersR[3] |=  (LC8951RegistersR[1] & 0x40) ? 0x00 : 0xF0;
					break;
				case 0x0D:														// STAT3
					LC8951RegistersR[1] |= 0x20;								// reset DECI
					// bprintf(PRINT_ERROR, _T("  - DECI (PC: 0x%06X)\n"), SekGetPC(-1));
					break;
			}

			nLC8951Register = (nLC8951Register + 1) & 0x0F;
			return reg;
		}

		// CD mechanism communication
		case 0x0161:
			return NeoCDCommsread();

		default: {
//			bprintf(PRINT_NORMAL, _T("  - NGCD port 0x%06X read (byte, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));
		}
	}

	return ~0;
}

static UINT16 __fastcall neogeoReadWordCDROM(UINT32 sekAddress)
{
//	bprintf(PRINT_NORMAL, _T("  - CDROM: 0x%06X read (word, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));

	switch (sekAddress & 0xFFFF) {
		case 0x0004:
			//bprintf(PRINT_IMPORTANT, _T("  - NGCD VBL (read) Interrupt mask -> 0x%04X (PC: 0x%06X)\n"), nff0004, SekGetPC(-1));

			return nff0004;
			break;

		case 0x011C:
			return ~((0x10 | (NeoSystem & 3)) << 8);
	}

//	bprintf(PRINT_NORMAL, _T("  - NGCD port 0x%06X read (word, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));

	return ~0;
}

static void __fastcall neogeoWriteByteCDROM(UINT32 sekAddress, UINT8 byteValue)
{
//	bprintf(PRINT_NORMAL, _T("  - Neo Geo CD: 0x%06X -> 0x%02X (PC: 0x%06X)\n"), sekAddress, byteValue, SekGetPC(-1));

	switch (sekAddress & 0xFFFF) {
		case 0x000F:
			NeoCDIRQUpdate(byteValue);
			break;

		case 0x0017:
			nNeoCDMode = byteValue;
			break;

		case 0x0061:
			if (byteValue & 0x40) {
				NeoCDDoDMA();
			} else
				if (byteValue == 0) {
					NeoCDDMAAddress1 = 0;
					NeoCDDMAAddress2 = 0;
					NeoCDDMAValue1 = 0;
					NeoCDDMAValue2 = 0;
					NeoCDDMACount = 0;
				}
			break;

		// LC8951 registers
		case 0x0101:
			nLC8951Register = byteValue & 0x0F;
//			bprintf(PRINT_NORMAL, _T("  - LC8951 register -> 0x%02X (PC: 0x%06X)\n"), nLC8951Register, SekGetPC(-1));
			break;
		case 0x0103:
//			bprintf(PRINT_NORMAL, _T("  - LC8951 register 0x%X -> 0x%02X (PC: 0x%06X)\n"), nLC8951Register, byteValue, SekGetPC(-1));
			switch (nLC8951Register) {
				case 3:															// DBCH
					LC8951RegistersW[ 3]  = byteValue & 0x0F;
					break;
				case 6:															// DTTRG
					LC8951RegistersW[ 6]  = ~0x00;
					LC8951RegistersR[ 1] &= ~0x08;
					break;
				case 7:															// DTACK
					LC8951RegistersW[ 7]  = ~0x00;
					LC8951RegistersR[ 1] &= ~0x40;
					break;
//				case 10:
//					LC8951RegistersW[nLC8951Register] = byteValue;
//					bprintf(PRINT_NORMAL, _T("  - CTRL0 -> %02X (PC: 0x%06X)\n"), LC8951RegistersW[nLC8951Register], byteValue, SekGetPC(-1));
//					break;
				case 11:
					LC8951RegistersW[11]  = byteValue;							// CTRL1
					LC8951UpdateHeader();
					break;
				case 15:
					LC8951Reset();
					break;
				default:
					LC8951RegistersW[nLC8951Register] = byteValue;
			}
			nLC8951Register = (nLC8951Register + 1) & 0x0F;
			break;

		case 0x0105:
//			bprintf(PRINT_NORMAL, _T("  - NGCD 0xE00000 area -> 0x%02X (PC: 0x%06X)\n"), byteValue, SekGetPC(-1));
			nActiveTransferArea = byteValue;
			break;

		case 0x0111:
			bNeoEnableSprites = (byteValue == 0);
			break;

		case 0x0115:
			bNeoEnableText = (byteValue == 0);
			break;

		case 0x0119:
			bNeoEnableGraphics = (byteValue != 0);
			break;

		case 0x0121:
//			bprintf(PRINT_NORMAL, _T("  - NGCD OBJ BUSREQ -> 1 (PC: 0x%06X)\n"), SekGetPC(-1));
			NeoSetSpriteSlot(1);
			nNeoCDSpriteSlot = 1;
			memset(NeoCDOBJBankUpdate, 0, sizeof(NeoCDOBJBankUpdate));
			break;
		case 0x0123:
//			bprintf(PRINT_NORMAL, _T("  - NGCD PCM BUSREQ -> 1 (PC: 0x%06X) %x\n"), SekGetPC(-1), byteValue);
			break;
		case 0x0127:
//			bprintf(PRINT_NORMAL, _T("  - NGCD Z80 BUSREQ -> 1 (PC: 0x%06X)\n"), SekGetPC(-1));
			neogeoSynchroniseZ80(0);
			ZetSetBUSREQLine(1);
			break;
		case 0x0129:
//			bprintf(PRINT_NORMAL, _T("  - NGCD FIX BUSREQ -> 1 (PC: 0x%06X)\n"), SekGetPC(-1));
			NeoSetTextSlot(1);
			nNeoCDTextSlot = 1;
			break;

		case 0x0141:
//			bprintf(PRINT_NORMAL, _T("  - NGCD OBJ BUSREQ -> 0 (PC: 0x%06X)\n"), SekGetPC(-1));
			NeoSetSpriteSlot(0);
			nNeoCDSpriteSlot = 0;
			for (INT32 i = 0; i < 4; i++) {
				if (NeoCDOBJBankUpdate[i]) {
					NeoDecodeSpritesCD(NeoSpriteRAM + (i << 20), NeoSpriteROM[0] + (i << 20), 0x100000);
					NeoUpdateSprites((i << 20), 0x100000);
				}
			}

			break;
		case 0x0143:
//			bprintf(PRINT_NORMAL, _T("  - NGCD PCM BUSREQ -> 0 (PC: 0x%06X)\n"), SekGetPC(-1));
			break;
		case 0x0147:
//			bprintf(PRINT_NORMAL, _T("  - NGCD Z80 BUSREQ -> 0 (PC: 0x%06X)\n"), SekGetPC(-1));
			neogeoSynchroniseZ80(0);
			ZetSetBUSREQLine(0);
			break;
		case 0x0149:
//			bprintf(PRINT_NORMAL, _T("  - NGCD FIX BUSREQ -> 0 (PC: 0x%06X)\n"), SekGetPC(-1));
			NeoSetTextSlot(0);
			nNeoCDTextSlot = 0;
			NeoUpdateText(0, 0x020000, NeoTextRAM, NeoTextROM[0]);
			break;

		// CD mechanism communication
		case 0x0163:
			NeoCDCommsWrite(byteValue);
			break;
		case 0x0165:
			NeoCDCommsControl(byteValue & 1, byteValue & 2);
			break;

		case 0x016D:
//			bprintf(PRINT_ERROR, _T("  - NGCD port 0x%06X -> 0x%02X (PC: 0x%06X)\n"), sekAddress, byteValue, SekGetPC(-1));

			MapVectorTable(!(byteValue == 0xFF));
			break;

		case 0x016F:
//			bprintf(PRINT_IMPORTANT, _T("  - NGCD 0xE00000 area write access %s (0x%02X, PC: 0x%06X)\n"), byteValue ? _T("enabled") : _T("disabled"), byteValue, SekGetPC(-1));

			nTransferWriteEnable = byteValue;
			break;

		case 0x0181:
			bNeoCDIRQEnabled = (byteValue != 0);
			break;

		case 0x0183: {
			if (byteValue == 0) {
				//bprintf(PRINT_IMPORTANT, _T("  - NGCD Z80 in-reset (PC: 0x%06X, 68k Cyc: %d, Z80 Cyc: %d, Frame: %d)\n"), SekGetPC(-1), SekTotalCycles(), ZetTotalCycles(), nCurrentFrame);
				BurnYM2610Reset();
				ZetSetRESETLine(1);
			} else {
				//bprintf(PRINT_IMPORTANT, _T("  - NGCD Z80 out-reset (PC: 0x%06X, 68k Cyc: %d, Z80 Cyc: %d, Frame: %d)\n"), SekGetPC(-1), SekTotalCycles(), ZetTotalCycles(), nCurrentFrame);
				ZetSetRESETLine(0);
			}
			break;
		}
		case 0x01A1:
			nSpriteTransferBank = (byteValue & 3) << 20;
			break;
		case 0x01A3:
			nADPCMTransferBank  = (byteValue & 1) << 19;
			break;

		default: {
//			bprintf(PRINT_NORMAL, _T("  - NGCD port 0x%06X -> 0x%02X (PC: 0x%06X)\n"), sekAddress, byteValue, SekGetPC(-1));
		}
	}
}

static void __fastcall neogeoWriteWordCDROM(UINT32 sekAddress, UINT16 wordValue)
{
//	bprintf(PRINT_NORMAL, _T("  - NGCD port 0x%06X -> 0x%04X (PC: 0x%06X)\n"), sekAddress, wordValue, SekGetPC(-1));

	switch (sekAddress & 0xFFFE) {
		case 0x0002:
//			bprintf(PRINT_IMPORTANT, _T("  - NGCD Interrupt mask -> 0x%04X (PC: 0x%06X)\n"), wordValue, SekGetPC(-1));
			if ((wordValue & 0x0500) && !(nff0002 & 0x0500)) {
				//bprintf(0, _T("cd period: old %d new %d\n"), nNeoCDCyclesIRQ);
				//nNeoCDCyclesIRQ = nNeoCDCyclesIRQPeriod;
			}

			nff0002 = wordValue;

// LC8951RegistersR[1] |= 0x20;
			break;

		case 0x0004:
			//bprintf(PRINT_IMPORTANT, _T("  - NGCD (write) VBL Interrupt mask -> 0x%04X (PC: 0x%06X)\n"), wordValue, SekGetPC(-1));

			nff0004 = wordValue;
			break;

		case 0x000E:
			NeoCDIRQUpdate(wordValue);
			break;

		// DMA controller
		case 0x0064:
			NeoCDDMAAddress1 &= 0x0000FFFF;
			NeoCDDMAAddress1 |= wordValue << 16;
			break;
		case 0x0066:
			NeoCDDMAAddress1 &= 0xFFFF0000;
			NeoCDDMAAddress1 |= wordValue;
			break;
		case 0x0068:
			NeoCDDMAAddress2 &= 0x0000FFFF;
			NeoCDDMAAddress2 |= wordValue << 16;
			break;
		case 0x006A:
			NeoCDDMAAddress2 &= 0xFFFF0000;
			NeoCDDMAAddress2 |= wordValue;
			break;
		case 0x006C:
			NeoCDDMAValue1 = wordValue;
			break;
		case 0x006E:
			NeoCDDMAValue2 = wordValue;
			break;
		case 0x0070:
			NeoCDDMACount &= 0x0000FFFF;
			NeoCDDMACount |= wordValue << 16;
			break;
		case 0x0072:
			NeoCDDMACount &= 0xFFFF0000;
			NeoCDDMACount |= wordValue;
			break;

		case 0x007E:
			NeoCDDMAMode = wordValue;
//			bprintf(PRINT_NORMAL, _T("  - DMA controller 0x%2X -> 0x%04X (PC: 0x%06X)\n"), sekAddress & 0xFF, wordValue, SekGetPC(-1));
			break;

		// upload DMA controller program

		case 0x0080:
		case 0x0082:
		case 0x0084:
		case 0x0086:
		case 0x0088:
		case 0x008A:
		case 0x008C:
		case 0x008E:
//			bprintf(PRINT_NORMAL, _T("  - DMA controller program[%02i] -> 0x%04X (PC: 0x%06X)\n"), sekAddress & 0x0F, wordValue, SekGetPC(-1));
			break;

		default: {
//			bprintf(PRINT_NORMAL, _T("  - NGCD port 0x%06X -> 0x%04X (PC: 0x%06X)\n"), sekAddress, wordValue, SekGetPC(-1));
		}
	}

}

// Reads from / writes to the transfer area

static UINT8 __fastcall neogeoReadByteTransfer(UINT32 sekAddress)
{
//	if ((sekAddress & 0x0FFFFF) < 16)
//		printf(PRINT_NORMAL, _T("  - NGCD port 0x%06X read (byte, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));

	sekAddress ^= 1;

	switch (nActiveTransferArea) {
		case 0:							// Sprites
			return NeoSpriteRAM[nSpriteTransferBank + (sekAddress & 0x0FFFFF)];
			break;
		case 1:							// ADPCM
			return YM2610ADPCMAROM[nNeoActiveSlot][nADPCMTransferBank + ((sekAddress & 0x0FFFFF) >> 1)];
			break;
		case 4:							// Z80
			if ((sekAddress & 0xfffff) >= 0x20000) break;
			return NeoZ80ROMActive[(sekAddress & 0x1FFFF) >> 1];
			break;
		case 5:							// Text
			return NeoTextRAM[(sekAddress & 0x3FFFF) >> 1];
			break;
	}

	return ~0;
}

static UINT16 __fastcall neogeoReadWordTransfer(UINT32 sekAddress)
{
//	if ((sekAddress & 0x0FFFFF) < 16)
//		bprintf(PRINT_NORMAL, _T("  - Transfer: 0x%06X read (word, PC: 0x%06X)\n"), sekAddress, SekGetPC(-1));

	switch (nActiveTransferArea) {
		case 0:							// Sprites
			return *((UINT16*)(NeoSpriteRAM + nSpriteTransferBank + (sekAddress & 0xFFFFF)));
			break;
		case 1:							// ADPCM
			return 0xFF00 | YM2610ADPCMAROM[nNeoActiveSlot][nADPCMTransferBank + ((sekAddress & 0x0FFFFF) >> 1)];
			break;
		case 4:							// Z80
			if ((sekAddress & 0xfffff) >= 0x20000) break;
			return 0xFF00 | NeoZ80ROMActive[(sekAddress & 0x1FFFF) >> 1];
			break;
		case 5:							// Text
			return 0xFF00 | NeoTextRAM[(sekAddress & 0x3FFFF) >> 1];
			break;
	}

	return ~0;
}

static void __fastcall neogeoWriteByteTransfer(UINT32 sekAddress, UINT8 byteValue)
{
//	if ((sekAddress & 0x0FFFFF) < 16)
//		bprintf(PRINT_NORMAL, _T("  - Transfer: 0x%06X -> 0x%02X (PC: 0x%06X)\n"), sekAddress, byteValue, SekGetPC(-1));

	if (!nTransferWriteEnable) {
//		return;
	}

	sekAddress ^= 1;

	switch (nActiveTransferArea) {
		case 0:							// Sprites
			NeoSpriteRAM[nSpriteTransferBank + (sekAddress & 0x0FFFFF)] = byteValue;
			NeoCDOBJBankUpdate[nSpriteTransferBank >> 20] = true;
			break;
		case 1:							// ADPCM
			YM2610ADPCMAROM[nNeoActiveSlot][nADPCMTransferBank + ((sekAddress & 0x0FFFFF) >> 1)] = byteValue;
			break;
		case 4:							// Z80
			if ((sekAddress & 0xfffff) >= 0x20000) break;
			NeoZ80ROMActive[(sekAddress & 0x1FFFF) >> 1] = byteValue;
			break;
		case 5:							// Text
			NeoTextRAM[(sekAddress & 0x3FFFF) >> 1] = byteValue;
//			NeoUpdateTextOne((sekAddress & 0x3FFFF) >> 1, byteValue);
			break;
	}
}

static void __fastcall neogeoWriteWordTransfer(UINT32 sekAddress, UINT16 wordValue)
{
//	if ((sekAddress & 0x0FFFFF) < 16)
//		bprintf(PRINT_NORMAL, _T("  - Transfer: 0x%06X -> 0x%04X (PC: 0x%06X)\n"), sekAddress, wordValue, SekGetPC(-1));

	if (!nTransferWriteEnable) {
//		return;
	}

	switch (nActiveTransferArea) {
		case 0:							// Sprites
			*((UINT16*)(NeoSpriteRAM + nSpriteTransferBank + (sekAddress & 0xFFFFF))) = wordValue;
			NeoCDOBJBankUpdate[nSpriteTransferBank >> 20] = true;
			break;
		case 1:							// ADPCM
			YM2610ADPCMAROM[nNeoActiveSlot][nADPCMTransferBank + ((sekAddress & 0x0FFFFF) >> 1)] = wordValue;
			break;
		case 4:							// Z80
			if ((sekAddress & 0xfffff) >= 0x20000) break;
			NeoZ80ROMActive[(sekAddress & 0x1FFFF) >> 1] = wordValue;
			break;
		case 5:							// Text
			NeoTextRAM[(sekAddress & 0x3FFFF) >> 1] = wordValue;
//			NeoUpdateTextOne((sekAddress & 0x3FFFF) >> 1, wordValue);
			break;
	}
}

static UINT16 __fastcall neogeoCDReadWord68KProgram(UINT32 sekAddress)
{
	if (sekAddress < 0x80 && NeoCDVectorSwitch == 0) {
		return *((UINT16*)(NeoVectorActive + sekAddress));
	}

	return *((UINT16*)(Neo68KROMActive + sekAddress));
}

static UINT8 __fastcall neogeoCDReadByte68KProgram(UINT32 sekAddress)
{
	if (sekAddress < 0x80 && NeoCDVectorSwitch == 0) {
		return NeoVectorActive[sekAddress ^ 1];
	}

	return Neo68KROMActive[sekAddress ^ 1];
}

// ----------------------------------------------------------------------------

static INT32 neogeoReset()
{
	if (nNeoSystemType & NEO_SYS_CART) {
		NeoLoad68KBIOS(NeoSystem & 0x3f);

		if (nBIOS == -1 || nBIOS == 33) {
			// Write system type & region code into BIOS ROM
			*((UINT16*)(Neo68KBIOS + 0x000400)) = BURN_ENDIAN_SWAP_INT16(((NeoSystem & 4) << 13) | (NeoSystem & 0x03));
		}

#if 1 && defined FBNEO_DEBUG
		if (((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_SNK_DEDICATED_PCB) && (BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) != HARDWARE_SNK_TRACKBALL) {
			switch (NeoSystem & 0x3f) {
				case 0x00: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Asia/Europe ver. 6 (1 slot) BIOS\n")); break; }
				case 0x01: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Asia/Europe ver. 5 (1 slot) BIOS\n")); break; }
				case 0x02: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Asia/Europe ver. 3 (4 slot) BIOS\n")); break; }
				case 0x03: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS USA ver. 5 (2 slot) BIOS\n")); break; }
				case 0x04: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS USA ver. 5 (4 slot) BIOS\n")); break; }
				case 0x05: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS USA ver. 5 (6 slot) BIOS\n")); break; }
				case 0x06: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS USA (U4) BIOS\n")); break; }
				case 0x07: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS USA (U3) BIOS\n")); break; }
				case 0x08: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Japan ver. 6 (? slot) BIOS\n")); break; }
				case 0x09: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Japan ver. 5 (? slot) BIOS\n")); break; }
				case 0x0a: { bprintf(PRINT_IMPORTANT, _T("Emulating using MVS Japan ver. 3 (4 slot) BIOS\n")); break; }
				case 0x0b: { bprintf(PRINT_IMPORTANT, _T("Emulating using NEO-MVH MV1C BIOS (Asia)\n")); break; }
				case 0x0c: { bprintf(PRINT_IMPORTANT, _T("Emulating using NEO-MVH MV1C BIOS (Japan)\n")); break; }
				case 0x0d: { bprintf(PRINT_IMPORTANT, _T("MVS Japan (J3)\n")); break; }
				case 0x0e: { bprintf(PRINT_IMPORTANT, _T("MVS Japan (J3, alt)\n")); break; }
				case 0x0f: { bprintf(PRINT_IMPORTANT, _T("Emulating using AES Japan BIOS\n")); break; }
				case 0x10: { bprintf(PRINT_IMPORTANT, _T("Emulating using AES Asia BIOS\n")); break; }
				case 0x11: { bprintf(PRINT_IMPORTANT, _T("Emulating using Development Kit BIOS\n")); break; }
				case 0x12: { bprintf(PRINT_IMPORTANT, _T("Emulating using Deck ver. 6 (Git Ver 1.3) BIOS\n")); break; }
				case 0x13: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 3.3 BIOS\n")); break; }
				case 0x14: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 3.2 BIOS\n")); break; }
				case 0x15: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 3.1 BIOS\n")); break; }
				case 0x16: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 3.0 BIOS\n")); break; }
				case 0x17: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 2.3 BIOS\n")); break; }
				case 0x18: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 2.3 (alt) BIOS\n")); break; }
				case 0x19: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 2.2 BIOS\n")); break; }
				case 0x1a: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 2.1 BIOS\n")); break; }
				case 0x1b: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 2.0 BIOS\n")); break; }
				case 0x1c: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 1.3 BIOS\n")); break; }
				case 0x1d: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 1.2 BIOS\n")); break; }
				case 0x1e: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 1.2 (alt) BIOS\n")); break; }
				case 0x1f: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 1.1 BIOS\n")); break; }
				case 0x20: { bprintf(PRINT_IMPORTANT, _T("Emulating using Universe BIOS ver. 1.0 BIOS\n")); break; }
				case 0x21: { bprintf(PRINT_IMPORTANT, _T("Emulating using NeoOpen BIOS v0.1 beta BIOS\n")); break; }
			}
		}

		if ((BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK) == HARDWARE_SNK_TRACKBALL) {
			bprintf(PRINT_IMPORTANT, _T("Emulating using custom Trackball BIOS\n"));
		}

		if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_DEDICATED_PCB) {
			bprintf(PRINT_IMPORTANT, _T("Emulating using custom PCB BIOS\n"));
		}
#endif

		OldDebugDip[0] = NeoDebugDip[0] = 0;
		OldDebugDip[1] = NeoDebugDip[1] = 0;
	}

#if 1 && defined FBNEO_DEBUG
	if (nNeoSystemType & NEO_SYS_CD) {
		bprintf(PRINT_IMPORTANT, _T("  - Emulating Neo CD system.\n"));
	}
#endif

	NeoSetSystemType();

	bSRAMWritable = false;

	bNeoEnableGraphics = true;
	bNeoEnableSprites = true;
	bNeoEnableText = true;

	bBIOSTextROMEnabled = false;
	bZ80BoardROMBankedIn = false;
	b68KBoardROMBankedIn = true;

	nNeoPaletteBank = -1;

	nSpriteFrameSpeed = 4;
	nSpriteFrameTimer = 0;
	nNeoSpriteFrame = 0;

	nIRQAcknowledge = ~0;
	bIRQEnabled = false;
	nIRQOffset = 0;
	nIRQControl = 0;
	nIRQCycles = NO_IRQ_PENDING;

	nSoundLatch = 0x00;
	nSoundReply = 0x00;
	nSoundStatus = 1;

#if 1 && defined USE_SPEEDHACKS
	nSoundPrevReply = -1;
#endif

	NeoGraphicsRAMBank = NeoGraphicsRAM;
	NeoGraphicsRAMPointer = 0;
	nNeoGraphicsModulo = 0;

	nNeo68KROMBank = 0;

	nAnalogAxis[0] = nAnalogAxis[1] = 0;

	nuPD4990ATicks = 0;
	nCycles68KSync = 0;

	nInputSelect = 0;
	NeoInputBank = NeoInput;

	nCyclesExtra[0] = nCyclesExtra[1] = 0;

	{
		SekOpen(0);
		ZetOpen(0);

		if (nNeoSystemType & NEO_SYS_MVS) {
			for (INT32 a = 0xD00000; a < 0xE00000; a += 0x010000) {
				SekMapMemory(NeoNVRAM, a, a + 0xFFFF, MAP_RAM);			// 68K RAM
			}
			SekMapHandler(1,			0xD00000, 0xDFFFFF, MAP_WRITE);	//
		} else {
			SekMapHandler(0,			0xD00000, 0xDFFFFF, MAP_RAM);	// AES/NeoCD don't have the SRAM
		}

		if (nNeoSystemType & NEO_SYS_CART) {
			NeoMapActiveCartridge();
		}

 		if (nNeoSystemType & NEO_SYS_PCB) {
			if (BurnDrvGetHardwareCode() & HARDWARE_SNK_KOF2K3) {
				SekMapMemory(Neo68KBIOS, 0xC00000, 0xC7FFFF, MAP_ROM);
				SekMapMemory(Neo68KBIOS, 0xC80000, 0xCFFFFF, MAP_ROM);
			} else {
				for (INT32 a = 0xC00000; a < 0xD00000; a += 0x020000) {
					SekMapMemory(Neo68KBIOS + (NeoSystem & 0x03) * 0x020000, a, a + 0x01FFFF, MAP_ROM);
				}
			}
		}

		// Set by a switch on the PCB
		if (!strcmp(BurnDrvGetTextA(DRV_NAME), "svcpcb") || !strcmp(BurnDrvGetTextA(DRV_NAME), "svcpcba") || !strcmp(BurnDrvGetTextA(DRV_NAME), "svcpcbnd") || !strcmp(BurnDrvGetTextA(DRV_NAME), "ms5pcb") || !strcmp(BurnDrvGetTextA(DRV_NAME), "ms5pcbnd")) {
			SekMapMemory(Neo68KBIOS + 0x20000 * (~NeoSystem & 1), 0xc00000, 0xc1ffff, MAP_ROM);
		}

		MapVectorTable(true);

		if (nNeoSystemType & NEO_SYS_CD) {
			bNeoEnableSprites = false;
			bNeoEnableText = false;

			nActiveTransferArea = -1;
			nSpriteTransferBank = -1;
			nADPCMTransferBank  = -1;

			bNeoCDIRQEnabled = false;
			nNeoCDIRQVector = 0;
			nNeoCDCyclesIRQ = 0;
			nff0002 = 0;
			nff0004 = 0;

			NeoCDDMAAddress1 = 0;
			NeoCDDMAAddress2 = 0;
			NeoCDDMAValue1 = 0;
			NeoCDDMAValue2 = 0;
			NeoCDDMACount = 0;
			NeoCDDMAMode = 0;

			NeoCDVectorSwitch = 0;
			nNeoCDMode = 0;

			NeoCDCommsReset();

			nTransferWriteEnable = 0;

			NeoSetTextSlot(0);
			NeoSetSpriteSlot(0);

			nNeoCDSpriteSlot = 0;
			nNeoCDTextSlot = 0;

			memset(NeoCDOBJBankUpdate, 0, sizeof(NeoCDOBJBankUpdate));

			LC8951Reset();

			CDEmuStop();
		}

		ZetSetBUSREQLine(0);
		ZetSetRESETLine(0);

		SekReset();
		ZetReset();

		MapPalette(0);

		ZetClose();
		SekClose();
	}

	ZetOpen(0);
	BurnYM2610Reset();
	ZetClose();

#if defined EMULATE_WATCHDOG
	nNeoWatchdog = 0;
#endif

	HiscoreReset();

#if defined CYCLE_LOG
	derpframe = 0;
	bprintf(0, _T("reset! derpframe = %d  ncextra:%d\n"), derpframe, nCyclesExtra[0]);
#endif

	return 0;
}

#ifdef BUILD_A68K
static void SwitchToMusashi()
{
	if (bBurnUseASMCPUEmulation) {
#if 1 && defined FBNEO_DEBUG
		bprintf(PRINT_NORMAL, _T("Switching to Musashi 68000 core\n"));
#endif
		bUseAsm68KCoreOldValue = bBurnUseASMCPUEmulation;
		bBurnUseASMCPUEmulation = false;
	}
}
#endif

static INT32 NeoInitCommon()
{
	BurnSetRefreshRate((nNeoSystemType & NEO_SYS_CD) ? NEO_CDVREFRESH : NEO_VREFRESH);
	INT32 nNeoScreenHeight; // not used
	BurnDrvGetFullSize(&nNeoScreenWidth, &nNeoScreenHeight);

	if (nNeoSystemType & NEO_SYS_CART) {
		nVBLankIRQ   = 1;
		nScanlineIRQ = 2;
	} else {
		nVBLankIRQ   = 1;
		nScanlineIRQ = 3;
	}

	// Allocate all memory is needed for RAM
	{
		INT32 nLen;

		RAMIndex();													// Get amount of memory needed
		nLen = RAMEnd - (UINT8*)0;
		if ((AllRAM = (UINT8*)BurnMalloc(nLen)) == NULL) {		// Allocate memory
			return 1;
		}
		memset(AllRAM, 0, nLen);									// Initialise memory
		RAMIndex();													// Index the allocated memory
	}

#ifdef BUILD_A68K
	if (nNeoSystemType & NEO_SYS_CD) {
		SwitchToMusashi();
	}
#endif

	SekInit(0, 0x68000);											// Allocate 68000
    SekOpen(0);

	ZetInit(0);
	ZetOpen(0);

	{
		if (nNeoSystemType & NEO_SYS_CD) {
			SekSetIrqCallback(NeoCDIRQCallback);
		}

		SekSetCyclesScanline((INT32)(12000000.0 / NEO_HREFRESH));

		// Map 68000 memory:

		if (nNeoSystemType & NEO_SYS_CART) {

			for (INT32 a = 0x100000; a < 0x200000; a += 0x010000) {
				SekMapMemory(Neo68KRAM, a, a + 0xFFFF, MAP_RAM);				// 68K RAM
			}

			if (!(nNeoSystemType & NEO_SYS_PCB)) {
//				for (INT32 a = 0xC00000; a < 0xD00000; a += 0x020000) {
//					SekMapMemory(Neo68KBIOS, a, a + 0x01FFFF, MAP_ROM);		// MVS/AES BIOS ROM
//				}
				SekMapMemory(Neo68KBIOS,	0xC00000, 0xC7FFFF, MAP_ROM);	// BIOS ROM
			}

		} else {
			SekMapMemory(Neo68KFix[0],		0x000000, 0x1FFFFF, MAP_RAM);	// Main 68K RAM
			SekMapMemory(Neo68KBIOS,		0xC00000, 0xC7FFFF, MAP_ROM);	// NeoCD BIOS ROM
			SekMapMemory(Neo68KBIOS,		0xC80000, 0xCFFFFF, MAP_ROM);	//
		}

		SekSetReadWordHandler(0, neogeoReadWord);
		SekSetReadByteHandler(0, neogeoReadByte);
		SekSetWriteWordHandler(0, neogeoWriteWord);
		SekSetWriteByteHandler(0, neogeoWriteByte);

		SekSetWriteWordHandler(1, neogeoWriteWordSRAM);
		SekSetWriteByteHandler(1, neogeoWriteByteSRAM);

		if (!(nNeoSystemType & NEO_SYS_PCB)) {
			SekMapHandler(2,			0x800000, 0xBFFFFF, MAP_ROM);		// Memory card
			SekMapHandler(2,			0x800000, 0xBFFFFF, MAP_WRITE);		//

			SekSetReadByteHandler(2, neogeoReadByteMemoryCard);
			SekSetWriteByteHandler(2, neogeoWriteByteMemoryCard);
		}

		SekMapHandler(3,	0x400000, 0x7FFFFF, MAP_WRITE);					// Palette

		SekSetWriteWordHandler(3, NeoPalWriteWord);
		SekSetWriteByteHandler(3, NeoPalWriteByte);

		// Set up mirrors
		for (INT32 a = 0x420000; a < 0x800000; a += 0x2000) {
			SekMapMemory(NeoPalSrc[0], a, a + 0x1FFF, MAP_ROM);
		}

		SekMapHandler(5,	0x3C0000, 0x3DFFFF, MAP_RAM);					// Read Video Controller
		SekSetReadWordHandler(5, neogeoReadWordVideo);
		SekSetReadByteHandler(5, neogeoReadByteVideo);
		SekSetWriteWordHandler(5, neogeoWriteWordVideo);
		SekSetWriteByteHandler(5, neogeoWriteByteVideo);

		if (nNeoSystemType & NEO_SYS_CD) {
			SekMapHandler(4,	0x000000, 0x0003FF, MAP_ROM);

			SekSetReadWordHandler(4, neogeoCDReadWord68KProgram);           // Vectors
			SekSetReadByteHandler(4, neogeoCDReadByte68KProgram);

			SekMapHandler(6,	0xE00000, 0xEFFFFF, MAP_RAM);

			SekSetReadWordHandler(6, neogeoReadWordTransfer);
			SekSetReadByteHandler(6, neogeoReadByteTransfer);
			SekSetWriteWordHandler(6, neogeoWriteWordTransfer);
			SekSetWriteByteHandler(6, neogeoWriteByteTransfer);

			SekMapHandler(7,	0xF00000, 0xFFFFFF, MAP_RAM);

			SekSetReadWordHandler(7, neogeoReadWordCDROM);
			SekSetReadByteHandler(7, neogeoReadByteCDROM);
			SekSetWriteWordHandler(7, neogeoWriteWordCDROM);
			SekSetWriteByteHandler(7, neogeoWriteByteCDROM);

			SekSetReadByteHandler(2, neoCDReadByteMemoryCard);
			SekSetWriteByteHandler(2, neoCDWriteByteMemoryCard);

		}
	}

	{
		// Z80 setup

		if (nNeoSystemType & NEO_SYS_CART) {
			// Work RAM
			ZetMapArea(0xF800, 0xFFFF, 0, NeoZ80RAM);
			ZetMapArea(0xF800, 0xFFFF, 1, NeoZ80RAM);
			ZetMapArea(0xF800, 0xFFFF, 2, NeoZ80RAM);


			ZetSetInHandler(neogeoZ80In);
			ZetSetOutHandler(neogeoZ80Out);
		}

		if (nNeoSystemType & NEO_SYS_CD) {
			// Main Z80 RAM
			ZetMapArea(0x0000, 0xFFFF, 0, NeoZ80ROMActive);
			ZetMapArea(0x0000, 0xFFFF, 1, NeoZ80ROMActive);
			ZetMapArea(0x0000, 0xFFFF, 2, NeoZ80ROMActive);


			ZetSetInHandler(neogeoZ80InCD);
			ZetSetOutHandler(neogeoZ80Out);
		}
	}

	ZetClose();
	SekClose();

#if defined USE_SPEEDHACKS
	bRenderLineByLine = false;
#else
	bRenderLineByLine = false;
#endif

#if defined RASTER_KLUDGE
	nScanlineOffset = 0xF8;									// correct as verified on MVS hardware
#endif

	// These games rely on reading the line counter for synchronising raster effects
	if (!strcmp(BurnDrvGetTextA(DRV_NAME), "mosyougi")) {
		bRenderLineByLine = true;

#if defined RASTER_KLUDGE
		nScanlineOffset = 0xFB;
#endif

	}
	if (!strcmp(BurnDrvGetTextA(DRV_NAME), "neodrift")) {
		bRenderLineByLine = true;
	}
	if (!strcmp(BurnDrvGetTextA(DRV_NAME), "zedblade")) {
		bRenderLineByLine = true;
	}

	//if (!strcmp(BurnDrvGetTextA(DRV_NAME), "neocdz")) {
	//	bRenderLineByLine = true;
	//}

	nNeoControlConfig = BurnDrvGetHardwareCode() & HARDWARE_SNK_CONTROLMASK;

	// Hook up standard joysticks to all ports
	for (INT32 i = 0; i < 8; i++) {
		nJoyport0[i] = 0;
		nJoyport1[i] = 1;
	}

	if (nNeoSystemType & NEO_SYS_CD) {
		nJoyport0[4] = 16;
		nJoyport1[4] = 17;
	} else {
		// Override for special controllers
		switch (nNeoControlConfig) {
			case HARDWARE_SNK_PADDLE:								// Two Paddles + joysticks
				nJoyport0[0] = 6;
				nJoyport1[0] = 7;
				break;
			case HARDWARE_SNK_TRACKBALL:							// Trackball controller
				nJoyport0[0] = 6;
				nJoyport0[1] = 7;
				break;
			case HARDWARE_SNK_MAHJONG:								// Mahjong controller
				nJoyport0[1] = 16;
				nJoyport0[2] = 17;
				nJoyport0[4] = 18;
				break;
		}
	}

#if defined Z80_SPEED_ADJUST
	nZ80Clockspeed = 4000000;
#endif

	if (nNeoSystemType & NEO_SYS_CART) {
		BurnYM2610Init(8000000, YM2610ADPCMAROM[0], &nYM2610ADPCMASize[0], YM2610ADPCMBROM[0], &nYM2610ADPCMBSize[0], &neogeoFMIRQHandler, 0);
	} else {
		BurnYM2610Init(8000000, YM2610ADPCMBROM[0], &nYM2610ADPCMBSize[0], YM2610ADPCMBROM[0], &nYM2610ADPCMBSize[0], &neogeoFMIRQHandler, 0);
	}

	BurnYM2610SetRoute(BURN_SND_YM2610_YM2610_ROUTE_1, 1.00, BURN_SND_ROUTE_LEFT);
	BurnYM2610SetRoute(BURN_SND_YM2610_YM2610_ROUTE_2, 1.00, BURN_SND_ROUTE_RIGHT);
	BurnYM2610SetRoute(BURN_SND_YM2610_AY8910_ROUTE, 0.20, BURN_SND_ROUTE_BOTH);

	BurnTimerAttachZet(nZ80Clockspeed);

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_MVS) {
		for (nNeoActiveSlot = 0; nNeoActiveSlot < MAX_SLOT; nNeoActiveSlot++) {
			if (nBurnDrvSelect[nNeoActiveSlot] < nBurnDrvCount) {
//				NeoInitText(nNeoActiveSlot);
//				NeoInitSprites(nNeoActiveSlot);
			}
		}

		NeoInitText(-1);

	} else {
		NeoInitText(0);
		NeoInitText(-1);
		NeoInitSprites(0);
	}

	if (nNeoSystemType & NEO_SYS_CART) {
		ZetOpen(0);
		nZ80Bank0 = nZ80Bank1 = nZ80Bank2 = nZ80Bank3 = -1;
		NeoZ80SetBank0(0x02);
		NeoZ80SetBank1(0x06);
		NeoZ80SetBank2(0x0E);
		NeoZ80SetBank3(0x1E);

		NeoZ80MapROM(false);
		ZetClose();
	}

	NeoInitPalette();

	uPD4990AInit(12000000);
	nPrevBurnCPUSpeedAdjust = -1;

	if (nNeoSystemType & NEO_SYS_CD) {
		bMemoryCardInserted = true;
		bMemoryCardWritable = true;
	} else {
		bMemoryCardInserted = false;
		bMemoryCardWritable = false;
	}

	nNeoActiveSlot = 0;
	neogeoReset();							// Reset machine

	return 0;
}

static bool recursing = false;

INT32 NeoInit()
{
	if (recursing) {
		if (LoadRoms()) {
			return 1;
		}
		return 0;
	}

	recursing = true;

	nNeoSystemType = NEO_SYS_CART;

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_DEDICATED_PCB) {
		nNeoSystemType |= NEO_SYS_MVS | NEO_SYS_PCB;
	}

	nNeoActiveSlot = 0;

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_MVS) {
		UINT32 nDriver = nBurnDrvActive;

//		memset(NeoCallback, 0, sizeof(NeoCallback));

		for (nNeoActiveSlot = 0; nNeoActiveSlot < MAX_SLOT; nNeoActiveSlot++) {

			if (nBurnDrvSelect[nNeoActiveSlot] < nBurnDrvCount) {

				nBurnDrvActive = nBurnDrvSelect[nNeoActiveSlot];
				NeoCallbackActive = &NeoCallback[nNeoActiveSlot];

				if (BurnDrvCartridgeSetup(CART_INIT_START)) {
					return 1;
				}

				NeoInitText(nNeoActiveSlot);
				NeoInitSprites(nNeoActiveSlot);
			}
		}

		for (nNeoNumSlots = 5; nNeoNumSlots > 0 && nBurnDrvSelect[nNeoNumSlots] >= nBurnDrvCount; nNeoNumSlots--) { }

		switch (nNeoNumSlots) {
			case 0:
				nNeoNumSlots = 1;
				break;
			case 1:
				nNeoNumSlots = 2;
				break;
			case 2:
			case 3:
				nNeoNumSlots = 4;
				break;
			case 4:
			case 5:
				nNeoNumSlots = 6;
				break;
		}

		nBurnDrvActive = nDriver;

		if (BurnDrvCartridgeSetup(CART_INIT_END)) {
			return 1;
		}

	} else {
		if (LoadRoms()) {
			return 1;
		}
	}

	recursing = false;

	for (nNeoActiveSlot = 0; nNeoActiveSlot < nNeoNumSlots; nNeoActiveSlot++) {
		NeoVector[nNeoActiveSlot] = (UINT8*)BurnMalloc(0x0400);
		if (NeoVector[nNeoActiveSlot] == NULL) {
			return 1;
		}
		memset(NeoVector[nNeoActiveSlot], 0, 0x0400);
	}

	// Allocate all memory needed for ROM
	{
		INT32 nLen;

		ROMIndex();													// Get amount of memory needed
		nLen = ROMEnd - (UINT8*)0;
		if ((AllROM = (UINT8*)BurnMalloc(nLen)) == NULL) {		// Allocate memory
			return 1;
		}
		memset(AllROM, 0, nLen);									// Initialise memory
		ROMIndex();													// Index the allocated memory
	}

	if (nNeoSystemType & NEO_SYS_PCB) {
		BurnLoadRom(Neo68KBIOS, 0x00080 +     35, 1);
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_MVS) {
		BurnLoadRom(NeoZ80BIOS,		0x00000 + 36, 1);
		BurnLoadRom(NeoTextROMBIOS,	0x00000 + 37, 1);
		BurnLoadRom(NeoZoomROM,		0x00000 + 38, 1);
	} else {

		// Still load the Z80 BIOS & text layer data for AES systems, since it might be switched to MVS later

		if (nNeoSystemType & NEO_SYS_PCB) {
			bZ80BIOS = false;
			BurnLoadRom(NeoTextROMBIOS,	0x00080 + 37, 1);
			BurnLoadRom(NeoZoomROM,		0x00080 + 38, 1);
		} else {
			BurnLoadRom(NeoZ80BIOS,		0x00080 + 36, 1);
			BurnLoadRom(NeoTextROMBIOS,	0x00080 + 37, 1);
			BurnLoadRom(NeoZoomROM,		0x00080 + 38, 1);
		}
	}
	BurnUpdateProgress(0.0, _T("Preprocessing text layer graphics...")/*, BST_PROCESS_TXT*/, 0);
	NeoDecodeTextBIOS(0, 0x020000, NeoTextROMBIOS);

	nBIOS = 9999;
	if (NeoLoad68KBIOS(NeoSystem & 0x3f)) {
		return 1;
	}

	return NeoInitCommon();
}

INT32 NeoCDInit()
{
	recursing = false;

	nNeoSystemType = NEO_SYS_CD;

	nCodeSize[0]		 = 0x200000;
	nSpriteSize[0]		 = 0x400000;
	nNeoTextROMSize[0]	 = 0x020000;
	nYM2610ADPCMASize[0] = 0;
	nYM2610ADPCMBSize[0] = 0x100000;

	// Allocate all memory is needed for ROM
	{
		INT32 nLen;

		ROMIndex();													// Get amount of memory needed
		nLen = ROMEnd - (UINT8*)0;
		if ((AllROM = (UINT8*)BurnMalloc(nLen)) == NULL) {		// Allocate memory
			return 1;
		}
		memset(AllROM, 0, nLen);									// Initialise memory
		ROMIndex();													// Index the allocated memory
	}

	Neo68KROMActive = Neo68KROM[0];
	NeoVectorActive = NeoVector[0];
	NeoZ80ROMActive = NeoZ80ROM[0];

	Neo68KFix[0] = Neo68KROM[0];

	BurnLoadRom(Neo68KBIOS,	0, 1);
	BurnLoadRom(NeoZoomROM,	1, 1);

	// Create copy of 68K with BIOS vector table
	memcpy(NeoVectorActive + 0x00, Neo68KBIOS, 0x0100);

	// Create a default program for the Z80
	NeoZ80ROMActive[0] = 0xC3;	// JP 0
	NeoZ80ROMActive[1] = 0x00;	//
	NeoZ80ROMActive[2] = 0x00;	//

	bZ80BIOS = false;

	for (nNeoTileMask[0] = 1; nNeoTileMask[0] < nSpriteSize[0]; nNeoTileMask[0] <<= 1) { }
	nNeoTileMask[0] = (nNeoTileMask[0] >> 7) - 1;
	nNeoMaxTile[0] = nSpriteSize[0] >> 7;

	return NeoInitCommon();
}

INT32 NeoExit()
{
	if (recursing) {
		return 0;
	}

	recursing = true;

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_MVS) {
		UINT32 nDriver = nBurnDrvActive;

		for (nNeoActiveSlot = 0; nNeoActiveSlot < MAX_SLOT; nNeoActiveSlot++) {

			if (nBurnDrvSelect[nNeoActiveSlot] < nBurnDrvCount) {

				nBurnDrvActive = nBurnDrvSelect[nNeoActiveSlot];

				if (BurnDrvCartridgeSetup(CART_EXIT)) {
					return 1;
				}
			}
		}

		nBurnDrvActive = nDriver;
	}

	uPD4990AExit();

	NeoExitPalette();

	BurnYM2610Exit();

	ZetExit();									// Deallocate Z80
	SekExit();									// Deallocate 68000

	if (nNeoSystemType & NEO_SYS_CART) {

		// Deallocate all used memory

		for (nNeoActiveSlot = 0; nNeoActiveSlot < MAX_SLOT; nNeoActiveSlot++) {

			NeoExitSprites(nNeoActiveSlot);
			NeoExitText(nNeoActiveSlot);

			BurnFree(NeoTextROM[nNeoActiveSlot]);						// Text ROM
			nNeoTextROMSize[nNeoActiveSlot] = 0;

			BurnFree(NeoSpriteROM[nNeoActiveSlot]);						// Sprite ROM
			BurnFree(Neo68KROM[nNeoActiveSlot]);						// 68K ROM
			BurnFree(NeoVector[nNeoActiveSlot]);						// 68K vectors
			BurnFree(NeoZ80ROM[nNeoActiveSlot]);						// Z80 ROM
			BurnFree(YM2610ADPCMAROM[nNeoActiveSlot]);
			BurnFree(YM2610ADPCMBROM[nNeoActiveSlot]);
		}
	}

	if (nNeoSystemType & NEO_SYS_CD) {
		NeoExitSprites(0);
		NeoExitText(0);
	}

	BurnFree(AllROM);								// Misc ROM
	BurnFree(AllRAM);								// Misc RAM

	memset(NeoCallback, 0, sizeof(NeoCallback));
	NeoCallbackActive = &NeoCallback[0];

	nNeoTextROMSize[0] = 0;

	nBIOS = 9999;

	nNeoActiveSlot = 0;
	NeoVectorActive = NULL;
	Neo68KROMActive = NULL;
	NeoZ80ROMActive = NULL;

	nCodeSize[0] = 0;

#ifdef BUILD_A68K
	// Switch back CPU core if needed
	if (nNeoSystemType & NEO_SYS_CD) {
		if (bUseAsm68KCoreOldValue) {
#if 1 && defined FBNEO_DEBUG
			bprintf(PRINT_NORMAL, _T("Switching back to A68K core\n"));
#endif
			bUseAsm68KCoreOldValue = false;
			bBurnUseASMCPUEmulation = true;
		}
	}
#endif

	recursing = false;

	bDisableNeoWatchdog = false;

	// release the NeoGeo CD information object if needed
	NeoCDInfo_Exit();

	s1945pmode = 0;
	fatfury2mode = 0;
	vlinermode = 0;

	nNeoSystemType = 0;

	return 0;
}

INT32 NeoRender()
{
	NeoUpdatePalette();							// Update the palette
	NeoClearScreen();

	if (bNeoEnableGraphics) {
		nSliceStart = 0x10; nSliceEnd = 0xF0;
		nSliceSize = nSliceEnd - nSliceStart;

#if 0 || defined LOG_DRAW
		bprintf(PRINT_NORMAL, _T(" -- Drawing slice: %3i - %3i.\n"), nSliceStart, nSliceEnd);
#endif

		if (bNeoEnableSprites) NeoRenderSprites();					// Render sprites
		if (bNeoEnableText)    NeoRenderText();						// Render text layer
	}

	return 0;
}

inline static void NeoClearOpposites(UINT8* nJoystickInputs)
{
	if ((*nJoystickInputs & 0x03) == 0x03) {
		*nJoystickInputs &= ~0x03;
	}
	if ((*nJoystickInputs & 0x0C) == 0x0C) {
		*nJoystickInputs &= ~0x0C;
	}
}

static void NeoStandardInputs(INT32 nBank)
{
	if (nBank) {
		NeoInput[ 8] = 0x00;					   					// Player 1
		NeoInput[ 9] = 0x00;				   						// Player 2
		NeoInput[10] = 0x00;				   						// Buttons
		NeoInput[11] = 0x00;				   						//
		for (INT32 i = 0; i < 8; i++) {
			NeoInput[ 8] |= (NeoJoy3[i] & 1) << i;
			NeoInput[ 9] |= (NeoJoy4[i] & 1) << i;
			NeoInput[10] |= (NeoButton3[i] & 1) << i;
			NeoInput[11] |= (NeoButton4[i] & 1) << i;
		}
		NeoClearOpposites(&NeoInput[ 8]);
		NeoClearOpposites(&NeoInput[ 9]);

		if (NeoDiag[1]) {
			NeoInput[13] |= 0x80;
		}
	} else {
		NeoInput[ 0] = 0x00;					   					// Player 1
		NeoInput[ 1] = 0x00;					   					// Player 2
		NeoInput[ 2] = 0x00;					   					// Buttons
		NeoInput[ 3] = 0x00;					   					//
		for (INT32 i = 0; i < 8; i++) {
			NeoInput[ 0] |= (NeoJoy1[i] & 1) << i;
			NeoInput[ 1] |= (NeoJoy2[i] & 1) << i;
			NeoInput[ 2] |= (NeoButton1[i] & 1) << i;
			NeoInput[ 3] |= (NeoButton2[i] & 1) << i;
		}
		NeoClearOpposites(&NeoInput[ 0]);
		NeoClearOpposites(&NeoInput[ 1]);

		if (NeoDiag[0]) {
			NeoInput[ 5] |= 0x80;
		}
	}
}

static INT32 NeoSekRun(const INT32 nCycles)
{
	INT32 nCyclesExecutedTotal = 0;

	if (!(nNeoSystemType & NEO_SYS_CD))
		return SekRun(nCycles);

	while (nCyclesExecutedTotal < nCycles) {

		INT32 nIRQCyc = 0;

		if (nNeoCDCyclesIRQ <= 0) {
			nNeoCDCyclesIRQ += nNeoCDCyclesIRQPeriod;

			if ((nff0002 & 0x0500) && bNeoCDIRQEnabled) {
				NeoCDReadSector();

				if (nff0002 & 0x0050) {
					nIRQCyc += SekRun(4); // Allow cpu to ack Decoder irq
				}
			}

			if (nff0002 & 0x0050 && bNeoCDIRQEnabled) {
				// Trigger CD mechanism communication interrupt
				nIRQAcknowledge &= ~0x10;
				NeoCDIRQUpdate(0);
			}
		}

		INT32 nCDCyclesSegment = (nNeoCDCyclesIRQ < (nCycles - nCyclesExecutedTotal)) ? nNeoCDCyclesIRQ : (nCycles - nCyclesExecutedTotal);
		INT32 nCyclesExecuted = SekRun(nCDCyclesSegment);
		nCyclesExecuted += nIRQCyc;
		nIRQCyc = 0;

		nCyclesExecutedTotal += nCyclesExecuted;
		nNeoCDCyclesIRQ      -= nCyclesExecuted;
	}

	return nCyclesExecutedTotal;
}

INT32 NeoFrame()
{
	//bprintf(0, _T("%X,"), SekReadWord(0x108)); // show game-id

	if (NeoReset) {							   						// Reset machine
		if (nNeoSystemType & NEO_SYS_CART) {
			memset(Neo68KRAM, 0, 0x010000);
		}
		if (nNeoSystemType & NEO_SYS_CD) {
			memset(Neo68KROM[0], 0, nCodeSize[0]);
		}
		neogeoReset();
	}

	NeoInput[ 5] &= 0x1F;											// Clear ports
	NeoInput[13]  = 0x00;											//

	switch (nNeoControlConfig) {
		case HARDWARE_SNK_PADDLE: {									// Two Paddles + joysticks

			NeoStandardInputs(0);

			// Handle analog controls
			nAnalogAxis[0] -= NeoAxis[0];
			nAnalogAxis[1] -= NeoAxis[1];
			NeoInput[6] = (nAnalogAxis[0] >> 8) & 0xFF;
			NeoInput[7] = (nAnalogAxis[1] >> 8) & 0xFF;

			break;
		}

		case HARDWARE_SNK_TRACKBALL: {								// Trackball controller
			NeoInput[1] = 0x00;				   						// Buttons
			NeoInput[2] = 0x00;					   					//
			NeoInput[3] = 0x00;					   					//
			for (INT32 i = 0; i < 8; i++) {
				NeoInput[1] |= (NeoJoy2[i] & 1) << i;
				NeoInput[2] |= (NeoButton1[i] & 1) << i;
				NeoInput[3] |= (NeoButton2[i] & 1) << i;
			}
			// Handle analog controls
			nAnalogAxis[0] += NeoAxis[0];
			nAnalogAxis[1] += NeoAxis[1];
			NeoInput[6] = (nAnalogAxis[0] >> 8) & 0xFF;
			NeoInput[7] = (nAnalogAxis[1] >> 8) & 0xFF;

			if (NeoDiag[0]) {
				NeoInput[5] |= 0x80;
			}

			break;
		}

		case HARDWARE_SNK_4_JOYSTICKS: {							// Four joystick controllers

			NeoStandardInputs(0);
			NeoStandardInputs(1);

			break;
		}

		case HARDWARE_SNK_MAHJONG: {								// Mahjong controller

			NeoStandardInputs(0);

			NeoInput[16] = 0x00;
			NeoInput[17] = 0x00;
			NeoInput[18] = 0x00;
			for (INT32 i = 0; i < 7; i++) {
				NeoInput[16] |= (NeoButton1[i +  8] & 1) << i;
				NeoInput[17] |= (NeoButton1[i + 16] & 1) << i;
				NeoInput[18] |= (NeoButton1[i + 24] & 1) << i;
			}

			break;
		}

		case HARDWARE_SNK_GAMBLING: {								// Gambling configuration

			NeoStandardInputs(0);

/*			NeoInput[16] = 0x00;
			for (INT32 i = 0; i < 7; i++) {
				NeoInput[16] |= (NeoButton1[i + 16] & 1) << i;
			}*/

			break;
		}

		default: {													// Two joystick controllers
			NeoStandardInputs(0);
		}
	}

	if (nNeoSystemType & NEO_SYS_CART) {

		bMemoryCardWritable = (NeoSystem & 0x80);

		if (bMemoryCardInserted) {
			NeoInput[2] |= 0x30;									// JEIDA connector pen /CD1, /READY
			if (bMemoryCardWritable) {
				NeoInput[2] |= 0x40;								// JEIDA connector pen /WP
			}
		}

		if (nNeoSystemType & NEO_SYS_AES) {							// Report the type of hardware
			NeoInput[2] |= 0x80;
		}

		if (nNeoControlConfig != HARDWARE_SNK_GAMBLING) {
			if (nNeoSystemType & NEO_SYS_MVS) {						// Report the appropriate number of slots on MVS hardware
				switch (nNeoNumSlots) {
					case 4:
						NeoInput[ 5] &= ~0x40;
						NeoInput[ 3] |=  0x20;
						break;
					case 6:
						NeoInput[ 5] &= ~0x40;
						NeoInput[ 3] &= ~0x20;
						break;
					default:

						// Default to 1/2 slot hardware

						NeoInput[ 5] |=  0x40;
						NeoInput[ 3] |=  0x20;
				}
			}
		}

		if (OldDebugDip[0] != NeoDebugDip[0]) {
			SekOpen(0);
			SekWriteByte(SekReadLong(0x010E) + 0, NeoDebugDip[0]);
			SekClose();
			OldDebugDip[0] = NeoDebugDip[0];
		}
		if (OldDebugDip[1] != NeoDebugDip[1]) {
			SekOpen(0);
			SekWriteByte(SekReadLong(0x010E) + 1, NeoDebugDip[1]);
			SekClose();
			OldDebugDip[1] = NeoDebugDip[1];
		}
	} else {
		NeoInput[2] |= 0x70;

		NeoInput[16] = 0x00;
		NeoInput[17] = 0x00;

		if (NeoDiag[0]) {
			NeoInput[16] = (UINT8)~0xDA;
		}
	}

	if (nPrevBurnCPUSpeedAdjust != nBurnCPUSpeedAdjust) {
		bprintf(0, _T("\n---init cycles etc ---\n"));
		// 68K CPU clock is 12MHz, modified by nBurnCPUSpeedAdjust
		nCyclesTotal[0] = (INT32)(12000000.0 * nBurnCPUSpeedAdjust / (256.0 * NEO_VREFRESH));
#if defined Z80_SPEED_ADJUST
		// Z80 CPU clock always 68K / 3
		nCyclesTotal[1] = nCyclesTotal[0] / 3;
		nZ80Clockspeed = (INT32)(4000000.0 * nBurnCPUSpeedAdjust / 256);
		BurnTimerAttachZet(nZ80Clockspeed);
#else
		// Z80 CPU clock is always 4MHz
		nCyclesTotal[1] = 4000000.0 / NEO_VREFRESH;
#endif
		// 68K cycles executed each scanline
		SekOpen(0);
		SekSetCyclesScanline((INT32)(12000000.0 * nBurnCPUSpeedAdjust / (256.0 * NEO_HREFRESH)));
		SekClose();

		// uPD499A ticks per second (same as 68K clock)
		uPD499ASetTicks((INT32)(12000000.0 * nBurnCPUSpeedAdjust / 256));

#if defined OVERCLOCK_CDZ
		nNeoCDCyclesIRQPeriod = (INT32)(12000000.0 * nBurnCPUSpeedAdjust / (256.0 * 225.0));
#else
		nNeoCDCyclesIRQPeriod = (INT32)(12000000.0 * nBurnCPUSpeedAdjust / (256.0 * 150.0));
#endif

		nPrevBurnCPUSpeedAdjust = nBurnCPUSpeedAdjust;

		bprintf(0, _T("\n68k cycles total: %d\n"), nCyclesTotal[0]);
		bprintf(0, _T("\nz80 cycles total: %d / %d\n"), nCyclesTotal[1], nZ80Clockspeed);
	}

#if defined EMULATE_WATCHDOG
	// If the watchdog isn't reset every 8 frames, reset the system
	// This can't be 100% accurate, as the 68000 instruction timings are not 100%
	if ((nNeoSystemType & NEO_SYS_CART) && nNeoWatchdog > nCyclesTotal[0] * 8) {
		if (bDisableNeoWatchdog == false) {
#if 1 && defined FBNEO_DEBUG
			SekOpen(0);
			bprintf(PRINT_IMPORTANT, _T(" ** Watchdog triggered system reset (PC: 0x%06X)\n"), SekGetPC(-1));
			SekClose();
#endif
			neogeoReset();
		}
	}
#endif
//bprintf(PRINT_NORMAL, _T("***\n"));

	bRenderImage = false;
	bForceUpdateOnStatusRead = false;

	if (pBurnDraw) {
		NeoUpdatePalette();											// Update the palette
		NeoClearScreen();
	}
	nSliceEnd = 0x10;

	SekNewFrame();
	ZetNewFrame();

	SekOpen(0);
	ZetOpen(0);

	// Compensate for extra cycles executed
	SekIdle(nCyclesExtra[0]);
	ZetIdle(nCyclesExtra[1]);

	nuPD4990ATicks = nCyclesExtra[0];

	// Run 68000
	nCyclesSegment = nSekCyclesScanline * 24;
	while (SekTotalCycles() < nCyclesSegment) {

		if ((nIRQControl & 0x10) && (nIRQCycles < NO_IRQ_PENDING) && (SekTotalCycles() >= nIRQCycles)) {

			nIRQAcknowledge &= ~2;
			SekSetIRQLine(nScanlineIRQ, CPU_IRQSTATUS_ACK);

#if 0 || defined LOG_IRQ
			bprintf(PRINT_NORMAL, _T("  - IRQ triggered (line %3i + %3i cycles).\n"), SekCurrentScanline(), SekTotalCycles() - SekCurrentScanline() * nSekCyclesScanline);
#endif

			if (nIRQControl & 0x80) {
				nIRQCycles += NeoConvertIRQPosition(nIRQOffset + 1);

#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("  - IRQ Line -> %3i (at line %3i, autoload).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

			}
		}

		if (nCyclesSegment < nIRQCycles || SekTotalCycles() >= nIRQCycles) {
			NeoSekRun(nCyclesSegment - SekTotalCycles());
		} else {
			NeoSekRun(nIRQCycles - SekTotalCycles());
		}
	}

	// NeoCDZ: Automatically enable raster/line-rendering for gameID's that need it.
	if (IsNeoGeoCD()) {
		INT32 nGameID = SekReadWord(0x108);
		INT32 bRenderMode = 0;

		switch (nGameID) {
			case 0x0050: bRenderMode = 1; break; // ninjacommando (cd)
			case 0x0061: bRenderMode = 1; break; // ssideki2 (cd)
			case 0x0200: bRenderMode = 1; break; // turfmasters (cd)
		}
		bRenderLineByLine = (!bNeoCDIRQEnabled) && bRenderMode;
	}

	bRenderImage = pBurnDraw != NULL && bNeoEnableGraphics;
	bForceUpdateOnStatusRead = bRenderImage && bRenderLineByLine;
	bForcePartialRender = false;

	// Display starts here

	nCyclesVBlank = nSekCyclesScanline * 248;
	if (bRenderLineByLine) {
		INT32 nLastIRQ = nIRQCycles - 1;
		while (SekTotalCycles() < nCyclesVBlank) {

			if ((nIRQControl & 0x10) && (nIRQCycles < NO_IRQ_PENDING) && (nLastIRQ < nIRQCycles) && (SekTotalCycles() >= nIRQCycles)) {
				nLastIRQ = nIRQCycles;
				nIRQAcknowledge &= ~2;
				SekSetIRQLine(nScanlineIRQ, CPU_IRQSTATUS_ACK);
#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("  - IRQ triggered (line %3i + %3i cycles).\n"), SekCurrentScanline(), SekTotalCycles() - SekCurrentScanline() * nSekCyclesScanline);
#endif

				if (nIRQControl & 0x80) {
					nIRQCycles += NeoConvertIRQPosition(nIRQOffset + 1);
#if 0 || defined LOG_IRQ
					bprintf(PRINT_NORMAL, _T("  - IRQ Line -> %3i (at line %3i, autoload).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

				}

				bForcePartialRender = bRenderImage;
				if (bForcePartialRender) {
					nSliceStart = nSliceEnd;
					nSliceEnd = SekCurrentScanline() - 5;
				}
			} else {
				if (bForcePartialRender) {
					nSliceStart = nSliceEnd;
					nSliceEnd = SekCurrentScanline() - 6;
				}
			}

			if (bForcePartialRender) {

				if (nSliceEnd > 240) {
					nSliceEnd = 240;
				}
				nSliceSize = nSliceEnd - nSliceStart;
				if (nSliceSize > 0) {

#if 0 || defined LOG_DRAW
					bprintf(PRINT_NORMAL, _T(" -- Drawing slice: %3i - %3i.\n"), nSliceStart, nSliceEnd);
#endif

					if (bNeoEnableSprites) NeoRenderSprites();		// Render sprites
				}
			}

			bForcePartialRender = false;

			if (SekTotalCycles() >= nCyclesSegment) {
				nCyclesSegment += nSekCyclesScanline;
			}
			if (nCyclesSegment < nIRQCycles || SekTotalCycles() >= nIRQCycles) {
				NeoSekRun(nCyclesSegment - SekTotalCycles());
			} else {
				NeoSekRun(nIRQCycles - SekTotalCycles());
			}
		}
	} else {
		nCyclesSegment = nCyclesVBlank;
		while (SekTotalCycles() < nCyclesVBlank) {

			if ((nIRQControl & 0x10) && (nIRQCycles < NO_IRQ_PENDING) && (SekTotalCycles() >= nIRQCycles)) {
				nIRQAcknowledge &= ~2;
				SekSetIRQLine(nScanlineIRQ, CPU_IRQSTATUS_ACK);

#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("  - IRQ triggered (line %3i + %3i cycles).\n"), SekCurrentScanline(), SekTotalCycles() - SekCurrentScanline() * nSekCyclesScanline);
#endif

				if (nIRQControl & 0x80) {
					nIRQCycles += NeoConvertIRQPosition(nIRQOffset + 1);

#if 0 || defined LOG_IRQ
					bprintf(PRINT_NORMAL, _T("  - IRQ Line -> %3i (at line %3i, autoload).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

				}

				bForcePartialRender = bRenderImage;
			}

#if defined RASTERS_OPTIONAL
			if (bAllowRasters) {
#endif
				if (bForcePartialRender) {

					nSliceStart = nSliceEnd;
					nSliceEnd = SekCurrentScanline() - 5;

					if (nSliceEnd > 240) {
						nSliceEnd = 240;
					}
					nSliceSize = nSliceEnd - nSliceStart;
					if (nSliceSize > 0) {

#if 0 || defined LOG_DRAW
						bprintf(PRINT_NORMAL, _T(" -- Drawing slice: %3i - %3i.\n"), nSliceStart, nSliceEnd);
#endif

						if (bNeoEnableSprites) NeoRenderSprites();		// Render sprites
					}
				}

#if defined RASTERS_OPTIONAL
			}
#endif

			bForcePartialRender = false;

			if (nCyclesSegment < nIRQCycles || SekTotalCycles() >= nIRQCycles) {
				NeoSekRun(nCyclesSegment - SekTotalCycles());
			} else {
				NeoSekRun(nIRQCycles - SekTotalCycles());
			}
		}
	}

	if (bRenderImage) {
		if (nSliceEnd < 240) {
			nSliceStart = nSliceEnd;
			nSliceEnd = 240;
			nSliceSize = nSliceEnd - nSliceStart;

#if 0 || defined LOG_DRAW
			bprintf(PRINT_NORMAL, _T(" -- Drawing slice: %3i - %3i.\n"), nSliceStart, nSliceEnd);
#endif

			if (bNeoEnableSprites) NeoRenderSprites();				// Render sprites
		}
		if (bNeoEnableText) NeoRenderText();						// Render text layer
	}

	if ( ((nNeoSystemType & NEO_SYS_CD) && (nff0004 & 0x0030) == 0x0030) || (~nNeoSystemType & NEO_SYS_CD) ) {
#if 0 || defined LOG_IRQ
		bprintf(PRINT_NORMAL, _T("  - VBLank.\n"));
#endif
		nIRQAcknowledge &= ~4;
		SekSetIRQLine(nVBLankIRQ, CPU_IRQSTATUS_ACK);
	}

	// set IRQ scanline at line 248
	if (nIRQControl & 0x40) {
		if (NeoConvertIRQPosition(nIRQOffset) < NO_IRQ_PENDING) {
			nIRQCycles = nCyclesSegment + NeoConvertIRQPosition(nIRQOffset);
		}

#if 0 || defined LOG_IRQ
		bprintf(PRINT_NORMAL, _T("  - IRQ Line -> %3i (at line %3i, VBlank).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

	}

	nCyclesSegment = nCyclesTotal[0];
	while (SekTotalCycles() < nCyclesSegment) {

		if ((nIRQControl & 0x10) && (nIRQCycles < NO_IRQ_PENDING) && (SekTotalCycles() >= nIRQCycles)) {
			nIRQAcknowledge &= ~2;
			SekSetIRQLine(nScanlineIRQ, CPU_IRQSTATUS_ACK);

#if 0 || defined LOG_IRQ
			bprintf(PRINT_NORMAL, _T("  - IRQ triggered (line %3i + %3i cycles).\n"), SekCurrentScanline(), SekTotalCycles() - SekCurrentScanline() * nSekCyclesScanline);
#endif

			if (nIRQControl & 0x80) {
				nIRQCycles += NeoConvertIRQPosition(nIRQOffset + 1);

#if 0 || defined LOG_IRQ
				bprintf(PRINT_NORMAL, _T("  - IRQ Line -> %3i (at line %3i, autoload).\n"), nIRQCycles / nSekCyclesScanline, SekCurrentScanline());
#endif

			}
		}

		if (nCyclesSegment < nIRQCycles || SekTotalCycles() >= nIRQCycles) {
			NeoSekRun(nCyclesSegment - SekTotalCycles());
		} else {
			NeoSekRun(nIRQCycles - SekTotalCycles());
		}
	}

	if (nIRQCycles < NO_IRQ_PENDING) {
		nIRQCycles -= nCyclesTotal[0];
		if (nIRQCycles < 0) {
			nIRQCycles = NO_IRQ_PENDING;

#if 0 || defined LOG_IRQ
		} else {
			bprintf(PRINT_NORMAL, _T("  - IRQ Line past screen end (IRQControl: %02X, line -> %3i).\n"), nIRQControl, nIRQCycles / nSekCyclesScanline);
#endif

		}
	}
//	bprintf(PRINT_NORMAL, " -- IRQControl: %02X, nIRQCycles / nSekCyclesScanline: %3i.\n", nIRQControl, nIRQCycles / nSekCyclesScanline);

	// Update the sound until the end of the frame

	nCycles68KSync = SekTotalCycles();
	BurnTimerEndFrame(nCyclesTotal[1]);
	if (pBurnSoundOut) {
		BurnYM2610Update(pBurnSoundOut, nBurnSoundLen);
	}

	// Update the uPD4990 until the end of the frame
	uPD4990AUpdate(SekTotalCycles() - nuPD4990ATicks);

#if defined EMULATE_WATCHDOG
	// Handle the watchdog
	nNeoWatchdog += SekTotalCycles();
#endif

	// Remember extra cycles executed
	nCyclesExtra[0] = SekTotalCycles() - nCyclesTotal[0];
	nCyclesExtra[1] = ZetTotalCycles() - nCyclesTotal[1];

#if defined CYCLE_LOG
	extern int counter;
	if (counter == 0) {
		if (derpframe < 16384) {
			//bprintf(0, _T("r:%d - %d.   "), derpframe, nCyclesExtra[0]);
			cycderp[derpframe++] = nCyclesExtra[0];
		}
	} else {
		if (derpframe == 0) {
			memset(&cycderp, 0, sizeof(cycderp));
			BurnDumpLoad("cycderp_r.dat", (UINT8*)&cycderp, sizeof(cycderp));
		}
		if (counter == 2) BurnDump("cycderp.dat", (UINT8*)&cycderp, sizeof(cycderp));
		if (derpframe < 16384) {
			//bprintf(0, _T("p:%d - %d %d.   "), derpframe, nCyclesExtra[0], cycderp[derpframe]);
			if (cycderp[derpframe] != nCyclesExtra[0])
				bprintf(0, _T("CYCLE OFFSET @ frame %d   is %X   shouldbe %X\n"), derpframe, nCyclesExtra[0], cycderp[derpframe]);
			derpframe++;
		}
	}
#endif
//	bprintf(PRINT_NORMAL, _T("    Z80 PC 0x%04X\n"), Doze.pc);
//	bprintf(PRINT_NORMAL, _T("  - %i\n"), SekTotalCycles());

	ZetClose();
	SekClose();

	if ((nIRQControl & 8) == 0) {
		if (++nSpriteFrameTimer > nSpriteFrameSpeed) {
			nSpriteFrameTimer = 0;
			nNeoSpriteFrame++;
		}
	}

	if (pBurnSoundOut) {
		if ((nNeoSystemType & NEO_SYS_CD) && !(LC8951RegistersW[10] & 4))
			CDEmuGetSoundBuffer(pBurnSoundOut, nBurnSoundLen);
	}

	return 0;
}
