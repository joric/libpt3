// PT3 (ZX Spectrum ProTracker 3 format) music player
// uses portions of AY_Emul, zxssk and ayemu
// plain C version by Joric, 2019

#include "data/music_data.h"

#include <stdint.h>
#include <string.h>		// memset
#include <stdio.h>		// file
#include <stdbool.h>	// bool
#include <time.h>		// clock

#define SAMPLE_RATE 44100

#include <stdint.h>
#include <string.h>

#pragma pack(1)
typedef struct {
	char MusicName[0x63];
	uint8_t TonTableId;
	uint8_t Delay;
	uint8_t NumberOfPositions;
	uint8_t LoopPosition;
	uint16_t PatternsPointer;
	uint8_t SamplesPointers_w[32 * 2];		// WORD array
	uint8_t OrnamentsPointers_w[16 * 2];	// WORD array
	uint8_t PositionList[1];				// open array
} PT3Header;
#pragma pack()

typedef struct {
	unsigned Address_In_Pattern, OrnamentPointer, SamplePointer, Ton;
	int Current_Amplitude_Sliding, Current_Noise_Sliding, Current_Envelope_Sliding, Ton_Slide_Count, Current_OnOff, OnOff_Delay, OffOn_Delay, Ton_Slide_Delay, Current_Ton_Sliding, Ton_Accumulator, Ton_Slide_Step, Ton_Delta;
	int8_t Note_Skip_Counter;
	uint8_t Loop_Ornament_Position, Ornament_Length, Position_In_Ornament, Loop_Sample_Position, Sample_Length, Position_In_Sample, Volume, Number_Of_Notes_To_Skip, Note, Slide_To_Note, Amplitude;
	uint8_t Envelope_Enabled, Enabled, SimpleGliss;
} PT3_Channel;

typedef struct {
	int Cur_Env_Slide, Env_Slide_Add;
	uint8_t Env_Base_lo, Env_Base_hi;
	uint8_t Noise_Base, Delay, AddToNoise, DelayCounter, CurrentPosition;
	int8_t Cur_Env_Delay, Env_Delay;
} PT3_Module;

typedef struct {
	int TS;
} PlConst;

typedef struct {
	unsigned a, aa, ip;
} PatCh;

typedef struct {
	PatCh ch[3];
} PatPtr;

typedef struct {
	PT3Header *header;
	uint8_t *module;
	PT3_Module mod;
	PT3_Channel ch[3];
	PlConst plconst;
} ChipDef;

typedef struct {
	uint32_t time, loop, tick;	// whole, loop and current time (in frames)
	uint8_t regs[2][16];
	uint8_t version;
	uint8_t tsMode;
	uint8_t *body;
	unsigned mod_size;
	uint8_t cur_delay;
	ChipDef chip[2];
	int AddToEnv;
	uint8_t TempMixer;
} PT3Player;


uint8_t PT3Player_fastInitPattern(PT3Player * player, PatPtr * dst, uint32_t i) {
	if (i > 84 * 3 || (i % 3) || player->chip[0].header->PatternsPointer + 2 * i + 8 >= player->mod_size)
		return 0;

	dst->ch[0].ip = player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 0] + 0x100 * player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 1];
	dst->ch[1].ip = player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 2] + 0x100 * player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 3];
	dst->ch[2].ip = player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 4] + 0x100 * player->chip[0].module[player->chip[0].header->PatternsPointer + 2 * i + 5];
	return 1;
}

uint8_t PT3Player_fastSimulatePattern(PT3Player * player, PatPtr * pat) {
	for (int ch = 0; ch < 3; ch++) {
		if (--pat->ch[ch].a == 0) {
			if (player->chip[0].module[pat->ch[ch].ip] == 0) {
				return 0;
			}
			int j = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c8 = 0;
			for (;;) {
				if (pat->ch[ch].ip >= player->mod_size) {
					return 0;
				}
				uint8_t cc = player->chip[0].module[pat->ch[ch].ip];
				if (cc == 0xD0 || cc == 0xC0 || (0x50 <= cc && cc <= 0xAF)) {
					pat->ch[ch].a = pat->ch[ch].aa;
					pat->ch[ch].ip++;
					break;
				} else if (cc == 0x10 || (0xF0 <= cc && cc <= 0xFF)) {
					pat->ch[ch].ip++;
				} else if (0xB2 <= cc && cc <= 0xBF) {
					pat->ch[ch].ip += 2;
				} else if (cc == 0xB1) {
					pat->ch[ch].ip++;
					pat->ch[ch].aa = player->chip[0].module[pat->ch[ch].ip];
				} else if (0x11 <= cc && cc <= 0x1F) {
					pat->ch[ch].ip += 3;
				} else if (cc == 1) {
					j++;
					c1 = j;
				} else if (cc == 2) {
					j++;
					c2 = j;
				} else if (cc == 3) {
					j++;
					c3 = j;
				} else if (cc == 4) {
					j++;
					c4 = j;
				} else if (cc == 5) {
					j++;
					c5 = j;
				} else if (cc == 8) {
					j++;
					c8 = j;
				} else if (cc == 9) {
					j++;
				}
				pat->ch[ch].ip++;
			}
			while (j > 0) {
				if (j == c1 || j == c8)
					pat->ch[ch].ip += 3;
				else if (j == c2)
					pat->ch[ch].ip += 5;
				else if (j == c3 || j == c4)
					pat->ch[ch].ip++;
				else if (j == c5)
					pat->ch[ch].ip += 2;
				else {
					if (pat->ch[ch].ip >= player->mod_size) {
						return 0;
					}
					player->cur_delay = player->chip[0].module[pat->ch[ch].ip];
					pat->ch[ch].ip++;
				}
				j--;
			}
		}
	}
	return 1;
}


uint8_t PT3Player_GetTime(PT3Player * player) {
	player->time = 0;
	player->loop = 0;
	player->cur_delay = player->chip[0].header->Delay;

	unsigned TS = (player->chip[0].header->MusicName[13] == '7' || player->chip[0].header->MusicName[13] == '9') ? player->chip[0].header->MusicName[98] : 0x20;
	PatPtr pat[2];
	for (int i = 0; i < 2; i++) {
		pat[i].ch[0].aa = pat[i].ch[1].aa = pat[i].ch[2].aa = 1;
	}
	int DLCatcher = 256 * 256;	//max 256 patterns 256 lines per pattern
	for (int i = 0; i < player->chip[0].header->NumberOfPositions; i++) {
		if (i == player->chip[0].header->LoopPosition)
			player->loop = player->time;
		if (!PT3Player_fastInitPattern(player, &pat[0], player->chip[0].header->PositionList[i])) {
			return 0;
		}
		pat[0].ch[0].a = pat[0].ch[1].a = pat[0].ch[2].a = 1;
		if (TS != 0x20) {
			PT3Player_fastInitPattern(player, &pat[1], TS * 3 - 3 - player->chip[0].header->PositionList[i]);
			pat[1].ch[0].a = pat[1].ch[1].a = pat[1].ch[2].a = 1;
		}

		for (;;) {
			if (!PT3Player_fastSimulatePattern(player, &pat[0]))
				break;
			if (TS != 0x20 && !PT3Player_fastSimulatePattern(player, &pat[1]))
				break;
			player->time += player->cur_delay;
			if (--DLCatcher == 0)
				return 0;
		}
		if (pat[0].ch[0].ip >= player->mod_size || pat[0].ch[1].ip >= player->mod_size || pat[0].ch[2].ip >= player->mod_size) {
			return 0;
		}
		if (TS != 0x20) {
			if (pat[1].ch[0].ip >= player->mod_size || pat[1].ch[1].ip >= player->mod_size || pat[1].ch[2].ip >= player->mod_size) {
				return 0;
			}
		}
	}
	return 1;
}


uint8_t PT3Player_Init(PT3Player * player, uint8_t * data, uint32_t data_size) {
	// InitForAllTypes

	player->body = data;
	player->mod_size = data_size;

	memset(&player->chip, 0, sizeof(player->chip));
	memset(&player->regs, 0, sizeof(player->regs));

	// PrepareItem / CaseTrModules

	player->chip[0].header = (PT3Header *) player->body;
	player->chip[1].header = (PT3Header *) player->body;
	player->chip[0].module = player->body;
	player->chip[1].module = player->body;

	if (!PT3Player_GetTime(player))
		return 0;
	player->tick = 0;

	uint8_t v = player->chip[0].header->MusicName[13];
	player->version = ('0' <= v && v <= '9') ? v - '0' : 6;
	player->chip[0].plconst.TS = player->chip[1].plconst.TS = 0x20;
	int TS = player->chip[0].header->MusicName[98];
	player->tsMode = (TS != 0x20);
	if (player->tsMode) {
		player->chip[1].plconst.TS = TS;
	} else if (player->mod_size > 400 && !memcmp(player->body + player->mod_size - 4, "02TS", 4)) {	// try load Vortex II '02TS'
		unsigned sz1 = player->body[player->mod_size - 12] + 0x100 * player->body[player->mod_size - 11];
		unsigned sz2 = player->body[player->mod_size - 6] + 0x100 * player->body[player->mod_size - 5];
		if (sz1 + sz2 < player->mod_size && sz1 > 200 && sz2 > 200) {
			player->tsMode = 1;
			player->chip[1].module = player->body + sz1;
			player->chip[1].header = (PT3Header *) player->chip[1].module;
		}
	}
	// InitTrackerModule code

	for (int cnum = 0; cnum < 2; cnum++) {
		player->chip[cnum].mod.Delay = player->chip[cnum].header->Delay;
		player->chip[cnum].mod.DelayCounter = 1;
		int b = player->chip[cnum].plconst.TS;
		uint8_t i = player->chip[cnum].header->PositionList[0];
		if (b != 0x20)
			i = (uint8_t) (3 * b - 3 - i);
		for (int chan = 0; chan < 3; chan++) {
			player->chip[cnum].ch[chan].Address_In_Pattern = player->chip[cnum].module[player->chip[cnum].header->PatternsPointer + 2 * (i + chan)] + player->chip[cnum].module[player->chip[cnum].header->PatternsPointer + 2 * (i + chan) + 1] * 0x100;
			player->chip[cnum].ch[chan].SamplePointer = player->chip[cnum].header->SamplesPointers_w[2] + player->chip[cnum].header->SamplesPointers_w[3] * 0x100;
			player->chip[cnum].ch[chan].OrnamentPointer = player->chip[cnum].header->OrnamentsPointers_w[0] + player->chip[cnum].header->OrnamentsPointers_w[1] * 0x100;
			player->chip[cnum].ch[chan].Loop_Ornament_Position = player->chip[cnum].module[player->chip[cnum].ch[chan].OrnamentPointer++];
			player->chip[cnum].ch[chan].Ornament_Length = player->chip[cnum].module[player->chip[cnum].ch[chan].OrnamentPointer++];
			player->chip[cnum].ch[chan].Loop_Sample_Position = player->chip[cnum].module[player->chip[cnum].ch[chan].SamplePointer++];
			player->chip[cnum].ch[chan].Sample_Length = player->chip[cnum].module[player->chip[cnum].ch[chan].SamplePointer++];
			player->chip[cnum].ch[chan].Volume = 15;
			player->chip[cnum].ch[chan].Note_Skip_Counter = 1;
		}
	}
	return 1;
}

const int PT3NoteTable_PT_33_34r[] = {
	0x0C21, 0x0B73, 0x0ACE, 0x0A33, 0x09A0, 0x0916, 0x0893, 0x0818, 0x07A4, 0x0736, 0x06CE, 0x066D,
	0x0610, 0x05B9, 0x0567, 0x0519, 0x04D0, 0x048B, 0x0449, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0336,
	0x0308, 0x02DC, 0x02B3, 0x028C, 0x0268, 0x0245, 0x0224, 0x0206, 0x01E9, 0x01CD, 0x01B3, 0x019B,
	0x0184, 0x016E, 0x0159, 0x0146, 0x0134, 0x0122, 0x0112, 0x0103, 0x00F4, 0x00E6, 0x00D9, 0x00CD,
	0x00C2, 0x00B7, 0x00AC, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0081, 0x007A, 0x0073, 0x006C, 0x0066,
	0x0061, 0x005B, 0x0056, 0x0051, 0x004D, 0x0048, 0x0044, 0x0040, 0x003D, 0x0039, 0x0036, 0x0033,
	0x0030, 0x002D, 0x002B, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E, 0x001C, 0x001B, 0x0019,
	0x0018, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C
};

const int PT3NoteTable_PT_34_35[] = {
	0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF, 0x066D,
	0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337,
	0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4, 0x019B,
	0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103, 0x00F5, 0x00E7, 0x00DA, 0x00CE,
	0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067,
	0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036, 0x0033,
	0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B, 0x001A,
	0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C
};

const int PT3NoteTable_ST[] = {
	0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960, 0x08E0, 0x0858, 0x07E0,
	0x077C, 0x0708, 0x06B0, 0x0640, 0x05EC, 0x0594, 0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03FD,
	0x03BE, 0x0384, 0x0358, 0x0320, 0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8,
	0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165, 0x0151, 0x013E, 0x012C, 0x011C, 0x010A, 0x00FC,
	0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F, 0x0096, 0x008E, 0x0085, 0x007E,
	0x0077, 0x0070, 0x006B, 0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0042, 0x003F,
	0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F,
	0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F
};

const int PT3NoteTable_ASM_34r[] = {
	0x0D3E, 0x0C80, 0x0BCC, 0x0B22, 0x0A82, 0x09EC, 0x095C, 0x08D6, 0x0858, 0x07E0, 0x076E, 0x0704,
	0x069F, 0x0640, 0x05E6, 0x0591, 0x0541, 0x04F6, 0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382,
	0x034F, 0x0320, 0x02F3, 0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216, 0x01F8, 0x01DC, 0x01C1,
	0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D, 0x012C, 0x011B, 0x010B, 0x00FC, 0x00EE, 0x00E0,
	0x00D4, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F, 0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070,
	0x006A, 0x0064, 0x005E, 0x0059, 0x0054, 0x0050, 0x004B, 0x0047, 0x0043, 0x003F, 0x003C, 0x0038,
	0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E, 0x001D,
	0x001B, 0x001A, 0x0019, 0x0018, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E
};

const int PT3NoteTable_ASM_34_35[] = {
	0x0D10, 0x0C55, 0x0BA4, 0x0AFC, 0x0A5F, 0x09CA, 0x093D, 0x08B8, 0x083B, 0x07C5, 0x0755, 0x06EC,
	0x0688, 0x062A, 0x05D2, 0x057E, 0x052F, 0x04E5, 0x049E, 0x045C, 0x041D, 0x03E2, 0x03AB, 0x0376,
	0x0344, 0x0315, 0x02E9, 0x02BF, 0x0298, 0x0272, 0x024F, 0x022E, 0x020F, 0x01F1, 0x01D5, 0x01BB,
	0x01A2, 0x018B, 0x0174, 0x0160, 0x014C, 0x0139, 0x0128, 0x0117, 0x0107, 0x00F9, 0x00EB, 0x00DD,
	0x00D1, 0x00C5, 0x00BA, 0x00B0, 0x00A6, 0x009D, 0x0094, 0x008C, 0x0084, 0x007C, 0x0075, 0x006F,
	0x0069, 0x0063, 0x005D, 0x0058, 0x0053, 0x004E, 0x004A, 0x0046, 0x0042, 0x003E, 0x003B, 0x0037,
	0x0034, 0x0031, 0x002F, 0x002C, 0x0029, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F, 0x001D, 0x001C,
	0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0014, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D
};

const int PT3NoteTable_REAL_34r[] = {
	0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF,
	0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367,
	0x0337, 0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4,
	0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0113, 0x0103, 0x00F5, 0x00E7, 0x00DA,
	0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D,
	0x0067, 0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036,
	0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B,
	0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D
};

const int PT3NoteTable_REAL_34_35[] = {
	0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4, 0x0737, 0x06CF,
	0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367,
	0x0337, 0x0308, 0x02DD, 0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4,
	0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103, 0x00F5, 0x00E7, 0x00DA,
	0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D,
	0x0067, 0x0061, 0x005C, 0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036,
	0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F, 0x001D, 0x001B,
	0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D
};


const uint8_t PT3VolumeTable_33_34[16][16] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02},
	{0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03},
	{0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04},
	{0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
	{0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
	{0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
	{0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
	{0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09},
	{0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A},
	{0x00, 0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0B},
	{0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C},
	{0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D},
	{0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}
};

const uint8_t PT3VolumeTable_35[16][16] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
	{0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02},
	{0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03},
	{0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04},
	{0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
	{0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
	{0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
	{0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
	{0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09},
	{0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x09, 0x0A},
	{0x00, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B},
	{0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C},
	{0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D},
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}
};


int PT3Player_GetNoteFreq(PT3Player * player, int cnum, int j) {
	switch (player->chip[cnum].header->TonTableId) {
		case 0:
			return (player->version <= 3)
				? PT3NoteTable_PT_33_34r[j]
				: PT3NoteTable_PT_34_35[j];
		case 1:
			return PT3NoteTable_ST[j];
		case 2:
			return (player->version <= 3)
				? PT3NoteTable_ASM_34r[j]
				: PT3NoteTable_ASM_34_35[j];
		default:
			return (player->version <= 3)
				? PT3NoteTable_REAL_34r[j]
				: PT3NoteTable_REAL_34_35[j];
	}
}

void PT3Player_PatternInterpreter(PT3Player * player, int cnum, PT3_Channel * chan) {
	int PrNote = chan->Note;
	int PrSliding = chan->Current_Ton_Sliding;
	uint8_t counter = 0;
	uint8_t f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f8 = 0, f9 = 0;
	for (;;) {
		uint8_t cc = player->chip[cnum].module[chan->Address_In_Pattern];
		if (0xF0 <= cc && cc <= 0xFF) {
			uint8_t c1 = cc - 0xF0;
			chan->OrnamentPointer = player->chip[cnum].header->OrnamentsPointers_w[2 * c1] + 0x100 * player->chip[cnum].header->OrnamentsPointers_w[2 * c1 + 1];
			chan->Loop_Ornament_Position = player->chip[cnum].module[chan->OrnamentPointer++];
			chan->Ornament_Length = player->chip[cnum].module[chan->OrnamentPointer++];
			chan->Address_In_Pattern++;
			uint8_t c2 = player->chip[cnum].module[chan->Address_In_Pattern] / 2;
			chan->SamplePointer = player->chip[cnum].header->SamplesPointers_w[2 * c2] + 0x100 * player->chip[cnum].header->SamplesPointers_w[2 * c2 + 1];
			chan->Loop_Sample_Position = player->chip[cnum].module[chan->SamplePointer++];
			chan->Sample_Length = player->chip[cnum].module[chan->SamplePointer++];
			chan->Envelope_Enabled = 0;
			chan->Position_In_Ornament = 0;
		} else if (0xD1 <= cc && cc <= 0xEF) {
			uint8_t c2 = cc - 0xD0;
			chan->SamplePointer = player->chip[cnum].header->SamplesPointers_w[2 * c2] + 0x100 * player->chip[cnum].header->SamplesPointers_w[2 * c2 + 1];
			chan->Loop_Sample_Position = player->chip[cnum].module[chan->SamplePointer++];
			chan->Sample_Length = player->chip[cnum].module[chan->SamplePointer++];
		} else if (cc == 0xD0) {
			chan->Address_In_Pattern++;
			break;
		} else if (0xC1 <= cc && cc <= 0xCF) {
			chan->Volume = cc - 0xC0;
		} else if (cc == 0xC0) {
			chan->Position_In_Sample = 0;
			chan->Current_Amplitude_Sliding = 0;
			chan->Current_Noise_Sliding = 0;
			chan->Current_Envelope_Sliding = 0;
			chan->Position_In_Ornament = 0;
			chan->Ton_Slide_Count = 0;
			chan->Current_Ton_Sliding = 0;
			chan->Ton_Accumulator = 0;
			chan->Current_OnOff = 0;
			chan->Enabled = 0;
			chan->Address_In_Pattern++;
			break;
		} else if (0xB2 <= cc && cc <= 0xBF) {
			chan->Envelope_Enabled = 1;
			player->regs[cnum][13] = cc - 0xB1;
			player->chip[cnum].mod.Env_Base_hi = player->chip[cnum].module[++chan->Address_In_Pattern];
			player->chip[cnum].mod.Env_Base_lo = player->chip[cnum].module[++chan->Address_In_Pattern];
			chan->Position_In_Ornament = 0;
			player->chip[cnum].mod.Cur_Env_Slide = 0;
			player->chip[cnum].mod.Cur_Env_Delay = 0;
		} else if (cc == 0xB1) {
			chan->Number_Of_Notes_To_Skip = player->chip[cnum].module[++chan->Address_In_Pattern];
		} else if (cc == 0xB0) {
			chan->Envelope_Enabled = 0;
			chan->Position_In_Ornament = 0;
		} else if (0x50 <= cc && cc <= 0xAF) {
			chan->Note = cc - 0x50;
			chan->Position_In_Sample = 0;
			chan->Current_Amplitude_Sliding = 0;
			chan->Current_Noise_Sliding = 0;
			chan->Current_Envelope_Sliding = 0;
			chan->Position_In_Ornament = 0;
			chan->Ton_Slide_Count = 0;
			chan->Current_Ton_Sliding = 0;
			chan->Ton_Accumulator = 0;
			chan->Current_OnOff = 0;
			chan->Enabled = 1;
			chan->Address_In_Pattern++;
			break;
		} else if (0x40 <= cc && cc <= 0x4F) {
			uint8_t c1 = cc - 0x40;
			chan->OrnamentPointer = player->chip[cnum].header->OrnamentsPointers_w[2 * c1] + 0x100 * player->chip[cnum].header->OrnamentsPointers_w[2 * c1 + 1];
			chan->Loop_Ornament_Position = player->chip[cnum].module[chan->OrnamentPointer++];
			chan->Ornament_Length = player->chip[cnum].module[chan->OrnamentPointer++];
			chan->Position_In_Ornament = 0;
		} else if (0x20 <= cc && cc <= 0x3F) {
			player->chip[cnum].mod.Noise_Base = cc - 0x20;
		} else if (0x10 <= cc && cc <= 0x1F) {
			chan->Envelope_Enabled = (cc != 0x10);
			if (chan->Envelope_Enabled) {
				player->regs[cnum][13] = cc - 0x10;
				player->chip[cnum].mod.Env_Base_hi = player->chip[cnum].module[++chan->Address_In_Pattern];
				player->chip[cnum].mod.Env_Base_lo = player->chip[cnum].module[++chan->Address_In_Pattern];
				player->chip[cnum].mod.Cur_Env_Slide = 0;
				player->chip[cnum].mod.Cur_Env_Delay = 0;
			}
			uint8_t c2 = player->chip[cnum].module[++chan->Address_In_Pattern] / 2;
			chan->SamplePointer = player->chip[cnum].header->SamplesPointers_w[2 * c2] + 0x100 * player->chip[cnum].header->SamplesPointers_w[2 * c2 + 1];
			chan->Loop_Sample_Position = player->chip[cnum].module[chan->SamplePointer++];
			chan->Sample_Length = player->chip[cnum].module[chan->SamplePointer++];
			chan->Position_In_Ornament = 0;
		} else if (cc == 0x09) {
			f9 = ++counter;
		} else if (cc == 0x08) {
			f8 = ++counter;
		} else if (cc == 0x05) {
			f5 = ++counter;
		} else if (cc == 0x04) {
			f4 = ++counter;
		} else if (cc == 0x03) {
			f3 = ++counter;
		} else if (cc == 0x02) {
			f2 = ++counter;
		} else if (cc == 0x01) {
			f1 = ++counter;
		}

		chan->Address_In_Pattern++;
	}

	while (counter > 0) {
		if (counter == f1) {
			chan->Ton_Slide_Delay = player->chip[cnum].module[chan->Address_In_Pattern++];
			chan->Ton_Slide_Count = chan->Ton_Slide_Delay;
			chan->Ton_Slide_Step = (int16_t) (player->chip[cnum].module[chan->Address_In_Pattern] + 0x100 * player->chip[cnum].module[chan->Address_In_Pattern + 1]);
			chan->Address_In_Pattern += 2;
			chan->SimpleGliss = 1;
			chan->Current_OnOff = 0;
			if (chan->Ton_Slide_Count == 0 && player->version >= 7)
				chan->Ton_Slide_Count++;
		} else if (counter == f2) {
			chan->SimpleGliss = 0;
			chan->Current_OnOff = 0;
			chan->Ton_Slide_Delay = player->chip[cnum].module[chan->Address_In_Pattern];
			chan->Ton_Slide_Count = chan->Ton_Slide_Delay;
			chan->Address_In_Pattern += 3;
			uint16_t step = player->chip[cnum].module[chan->Address_In_Pattern] + 0x100 * player->chip[cnum].module[chan->Address_In_Pattern + 1];
			chan->Address_In_Pattern += 2;
			int16_t signed_step = step;
			chan->Ton_Slide_Step = (signed_step < 0) ? -signed_step : signed_step;
			chan->Ton_Delta = PT3Player_GetNoteFreq(player, cnum, chan->Note) - PT3Player_GetNoteFreq(player, cnum, PrNote);
			chan->Slide_To_Note = chan->Note;
			chan->Note = PrNote;
			if (player->version >= 6)
				chan->Current_Ton_Sliding = PrSliding;
			if (chan->Ton_Delta - chan->Current_Ton_Sliding < 0)
				chan->Ton_Slide_Step = -chan->Ton_Slide_Step;
		} else if (counter == f3) {
			chan->Position_In_Sample = player->chip[cnum].module[chan->Address_In_Pattern++];
		} else if (counter == f4) {
			chan->Position_In_Ornament = player->chip[cnum].module[chan->Address_In_Pattern++];
		} else if (counter == f5) {
			chan->OnOff_Delay = player->chip[cnum].module[chan->Address_In_Pattern++];
			chan->OffOn_Delay = player->chip[cnum].module[chan->Address_In_Pattern++];
			chan->Current_OnOff = chan->OnOff_Delay;
			chan->Ton_Slide_Count = 0;
			chan->Current_Ton_Sliding = 0;
		} else if (counter == f8) {
			player->chip[cnum].mod.Env_Delay = player->chip[cnum].module[chan->Address_In_Pattern++];
			player->chip[cnum].mod.Cur_Env_Delay = player->chip[cnum].mod.Env_Delay;
			player->chip[cnum].mod.Env_Slide_Add = player->chip[cnum].module[chan->Address_In_Pattern] + 0x100 * player->chip[cnum].module[chan->Address_In_Pattern + 1];
			chan->Address_In_Pattern += 2;
		} else if (counter == f9) {
			uint8_t b = player->chip[cnum].module[chan->Address_In_Pattern++];
			player->chip[cnum].mod.Delay = b;
			if (player->tsMode && player->chip[1].plconst.TS != 0x20) {
				player->chip[0].mod.Delay = b;
				player->chip[0].mod.DelayCounter = b;
				player->chip[1].mod.Delay = b;
			}
		}
		counter--;
	}
	chan->Note_Skip_Counter = chan->Number_Of_Notes_To_Skip;
}


void PT3Player_ChangeRegisters(PT3Player * player, int cnum, PT3_Channel * chan) {
	if (chan->Enabled) {
		unsigned c1 = chan->SamplePointer + chan->Position_In_Sample * 4;
		uint8_t b0 = player->chip[cnum].module[c1], b1 = player->chip[cnum].module[c1 + 1];
		chan->Ton = player->chip[cnum].module[c1 + 2] + 0x100 * player->chip[cnum].module[c1 + 3];
		chan->Ton += chan->Ton_Accumulator;
		if (b1 & 0x40)
			chan->Ton_Accumulator = chan->Ton;
		uint8_t j = chan->Note + player->chip[cnum].module[chan->OrnamentPointer + chan->Position_In_Ornament];
		if ((int8_t) j < 0)
			j = 0;
		else if (j > 95)
			j = 95;
		int w = PT3Player_GetNoteFreq(player, cnum, j);

		chan->Ton = (chan->Ton + chan->Current_Ton_Sliding + w) & 0xFFF;
		if (chan->Ton_Slide_Count > 0) {
			if (!--chan->Ton_Slide_Count) {
				chan->Current_Ton_Sliding += chan->Ton_Slide_Step;
				chan->Ton_Slide_Count = chan->Ton_Slide_Delay;
				if (!chan->SimpleGliss) {
					if ((chan->Ton_Slide_Step < 0 && chan->Current_Ton_Sliding <= chan->Ton_Delta) || (chan->Ton_Slide_Step >= 0 && chan->Current_Ton_Sliding >= chan->Ton_Delta)) {
						chan->Note = chan->Slide_To_Note;
						chan->Ton_Slide_Count = 0;
						chan->Current_Ton_Sliding = 0;
					}
				}
			}
		}
		chan->Amplitude = b1 & 0x0F;
		if (b0 & 0x80) {
			if (b0 & 0x40) {
				if (chan->Current_Amplitude_Sliding < 15) {
					chan->Current_Amplitude_Sliding++;
				}
			} else if (chan->Current_Amplitude_Sliding > -15) {
				chan->Current_Amplitude_Sliding--;
			}
		}
		chan->Amplitude += chan->Current_Amplitude_Sliding;
		if ((int8_t) chan->Amplitude < 0)
			chan->Amplitude = 0;
		else if (chan->Amplitude > 15)
			chan->Amplitude = 15;
		if (player->version <= 4)
			chan->Amplitude = PT3VolumeTable_33_34[chan->Volume][chan->Amplitude];
		else
			chan->Amplitude = PT3VolumeTable_35[chan->Volume][chan->Amplitude];

		if (!(b0 & 1) && chan->Envelope_Enabled)
			chan->Amplitude |= 0x10;
		if (b1 & 0x80) {
			uint8_t j = (b0 & 0x20)
				? ((b0 >> 1) | 0xF0) + chan->Current_Envelope_Sliding : ((b0 >> 1) & 0x0F) + chan->Current_Envelope_Sliding;
			if (b1 & 0x20)
				chan->Current_Envelope_Sliding = j;
			player->AddToEnv += j;
		} else {
			player->chip[cnum].mod.AddToNoise = (b0 >> 1) + chan->Current_Noise_Sliding;
			if (b1 & 0x20)
				chan->Current_Noise_Sliding = player->chip[cnum].mod.AddToNoise;
		}
		player->TempMixer |= (b1 >> 1) & 0x48;
		if (++chan->Position_In_Sample >= chan->Sample_Length)
			chan->Position_In_Sample = chan->Loop_Sample_Position;
		if (++chan->Position_In_Ornament >= chan->Ornament_Length)
			chan->Position_In_Ornament = chan->Loop_Ornament_Position;
	} else {
		chan->Amplitude = 0;
	}
	player->TempMixer >>= 1;
	if (chan->Current_OnOff > 0) {
		if (!--chan->Current_OnOff) {
			chan->Enabled = !chan->Enabled;
			chan->Current_OnOff = chan->Enabled ? chan->OnOff_Delay : chan->OffOn_Delay;
		}
	}
}


void PT3Player_GetRegisters(PT3Player * player, int cnum) {
	player->regs[cnum][13] = 0xFF;

	if (!--player->chip[cnum].mod.DelayCounter) {
		for (int ch = 0; ch < 3; ch++) {
			if (!--player->chip[cnum].ch[ch].Note_Skip_Counter) {
				if (ch == 0 && player->chip[cnum].module[player->chip[cnum].ch[ch].Address_In_Pattern] == 0) {
					if (++player->chip[cnum].mod.CurrentPosition == player->chip[cnum].header->NumberOfPositions)
						player->chip[cnum].mod.CurrentPosition = player->chip[cnum].header->LoopPosition;
					uint8_t i = player->chip[cnum].header->PositionList[player->chip[cnum].mod.CurrentPosition];
					int b = player->chip[cnum].plconst.TS;
					if (b != 0x20)
						i = (uint8_t) (3 * b - 3 - i);
					for (int chan = 0; chan < 3; chan++) {
						player->chip[cnum].ch[chan].Address_In_Pattern = player->chip[cnum].module[player->chip[cnum].header->PatternsPointer + 2 * (i + chan)] + player->chip[cnum].module[player->chip[cnum].header->PatternsPointer + 2 * (i + chan) + 1] * 0x100;
					}
					player->chip[cnum].mod.Noise_Base = 0;
				}
				PT3Player_PatternInterpreter(player, cnum, &player->chip[cnum].ch[ch]);
			}
		}
		player->chip[cnum].mod.DelayCounter = player->chip[cnum].mod.Delay;
	}
	player->AddToEnv = 0;
	player->TempMixer = 0;

	for (int ch = 0; ch < 3; ch++)
		PT3Player_ChangeRegisters(player, cnum, &player->chip[cnum].ch[ch]);

	player->regs[cnum][7] = player->TempMixer;
	player->regs[cnum][6] = (player->chip[cnum].mod.Noise_Base + player->chip[cnum].mod.AddToNoise) & 0x1F;

	for (int ch = 0; ch < 3; ch++) {
		player->regs[cnum][2 * ch + 0] = player->chip[cnum].ch[ch].Ton & 0xFF;
		player->regs[cnum][2 * ch + 1] = (player->chip[cnum].ch[ch].Ton >> 8) & 0x0F;
		player->regs[cnum][ch + 8] = player->chip[cnum].ch[ch].Amplitude;
	}
	unsigned env = player->chip[cnum].mod.Env_Base_hi * 0x100 + player->chip[cnum].mod.Env_Base_lo + player->AddToEnv + player->chip[cnum].mod.Cur_Env_Slide;
	player->regs[cnum][11] = env & 0xFF;
	player->regs[cnum][12] = (env >> 8) & 0xFF;

	if (player->chip[cnum].mod.Cur_Env_Delay > 0) {
		if (!--player->chip[cnum].mod.Cur_Env_Delay) {
			player->chip[cnum].mod.Cur_Env_Delay = player->chip[cnum].mod.Env_Delay;
			player->chip[cnum].mod.Cur_Env_Slide += player->chip[cnum].mod.Env_Slide_Add;
		}
	}
}

void PT3Player_Step(PT3Player * player) {
	PT3Player_GetRegisters(player, 0);
	if (player->tsMode)
		PT3Player_GetRegisters(player, 1);
	player->tick++;
}

int main() {

	PT3Player static_player;
	int total_samples;
	int current_sample = 0;
	int playerFreq = 50;
	int samples_per_frame = 0;
	int rate = SAMPLE_RATE;

	PT3Player *player = &static_player;
	PT3Player_Init(player, (uint8_t *)music_data, music_data_size);
	samples_per_frame = rate / playerFreq;
	total_samples = samples_per_frame * player->time;

	int frame = 0;
	int loop = player->loop;

	printf("const unsigned char frame_data[][14] = {\n");

	clock_t t = clock();

	for(;;) {
		int pos = frame;
		PT3Player_Step(player);
		if (player->tick <= pos || player->time <= pos)
			break;
		printf("{"); for (int i = 0; i < 14; i++) printf("%d,", player->regs[0][i]); printf("},\n");
		frame++;
	}

	t = clock() - t;

	printf("};\n");

	double time_taken = ((double)t)/CLOCKS_PER_SEC;

	int frames = frame;

	fprintf(stderr, "zxssk, pt3 reader, ts: %d, frames: %d, loop: %d\n",
		player->tsMode, frames, loop);
	fprintf(stderr, "%f seconds\n", time_taken);
}


