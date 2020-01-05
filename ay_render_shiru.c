// ported from https://github.com/ESPboy-edu/ESPboy_PT3Play
// Plain C version by Joric, 2019

#include "test/data_shiru.h"

#include <stdint.h>
#include <stdlib.h>	//free
#include <string.h>	//memset
#include <stdio.h>	//file
#include <stdbool.h>//bool
#include <time.h>

#define AY_CLOCK      1773400	//pitch
#define SAMPLE_RATE   44100		//quality of the sound, i2s DAC can't handle more than 44100 by some reason (not even 48000)
#define FRAME_RATE    50		//speed

typedef struct {
	int count;
	int state;
} toneStruct;

typedef struct {
	int count;
	int reg;
	int qcc;
	int state;
} noiseStruct;

typedef struct {
	int count;
	int dac;
	int up;
} envStruct;

typedef struct {
	toneStruct tone[3];
	noiseStruct noise;
	envStruct env;
	int reg[16];
	int dac[3];
	int out[3];
	int freqDiv;

	int vols[6][32];
} AYChipStruct;

//табличка громкости (реальная, (С)HackerKAY)

#define VDIV	3

int volTab[16] = {
	0 / VDIV,
	836 / VDIV,
	1212 / VDIV,
	1773 / VDIV,
	2619 / VDIV,
	3875 / VDIV,
	5397 / VDIV,
	8823 / VDIV,
	10392 / VDIV,
	16706 / VDIV,
	23339 / VDIV,
	29292 / VDIV,
	36969 / VDIV,
	46421 / VDIV,
	55195 / VDIV,
	65535 / VDIV
};

void ay_init(AYChipStruct * ay) {
	memset(ay, 0, sizeof(AYChipStruct));
	ay->noise.reg = 0x0ffff;
	ay->noise.qcc = 0;
	ay->noise.state = 0;
}

void ay_out(AYChipStruct * ay, int reg, int value) {
	if (reg > 13)
		return;

	if (reg==13 && value==255)
		return; // added by joric

	//записи в каждый из регистров сразу подрезает данные как надо
	//также, при записи R13 надо проводится сброс счётчика огибающей
	switch (reg) {
		case 1:
		case 3:
		case 5:
			value &= 15;
			break;
		case 8:
		case 9:
		case 10:
		case 6:
			value &= 31;
			break;
		case 13:
			value &= 15;
			ay->env.count = 0;
			if (value & 2) {
				ay->env.dac = 0;
				ay->env.up = 1;
			} else {
				ay->env.dac = 15;
				ay->env.up = 0;
			}
			break;
	}

	ay->reg[reg] = value;
}

inline void ay_tick(AYChipStruct * ay, int ticks) {
	int noise_di;
	int i, ta, tb, tc, na, nb, nc;

	ay->out[0] = 0;
	ay->out[1] = 0;
	ay->out[2] = 0;

	for (i = 0; i < ticks; ++i) {

		//делитель тактовой частоты
		ay->freqDiv ^= 1;

		//тональники
		if (ay->tone[0].count >= (ay->reg[0] | (ay->reg[1] << 8))) {
			ay->tone[0].count = 0;
			ay->tone[0].state ^= 1;
		}
		if (ay->tone[1].count >= (ay->reg[2] | (ay->reg[3] << 8))) {
			ay->tone[1].count = 0;
			ay->tone[1].state ^= 1;
		}
		if (ay->tone[2].count >= (ay->reg[4] | (ay->reg[5] << 8))) {
			ay->tone[2].count = 0;
			ay->tone[2].state ^= 1;
		}

		ay->tone[0].count++;
		ay->tone[1].count++;
		ay->tone[2].count++;


		if (ay->freqDiv) {

			//шум (реальный алгоритм, (C)HackerKAY)

			if (ay->noise.count == 0) {
				noise_di = (ay->noise.qcc ^ ((ay->noise.reg >> 13) & 1)) ^ 1;
				ay->noise.qcc = (ay->noise.reg >> 15) & 1;
				ay->noise.state = ay->noise.qcc;
				ay->noise.reg = (ay->noise.reg << 1) | noise_di;
			}

			ay->noise.count = (ay->noise.count + 1) & 31;
			if (ay->noise.count >= ay->reg[6])
				ay->noise.count = 0;

			//огибающая
			if (ay->env.count == 0) {
				switch (ay->reg[13]) {
					case 0:
					case 1:
					case 2:
					case 3:
					case 9:
						if (ay->env.dac > 0)
							ay->env.dac--;
						break;
					case 4:
					case 5:
					case 6:
					case 7:
					case 15:
						if (ay->env.up) {
							ay->env.dac++;
							if (ay->env.dac > 15) {
								ay->env.dac = 0;
								ay->env.up = 0;
							}
						}
						break;

					case 8:
						ay->env.dac--;
						if (ay->env.dac < 0)
							ay->env.dac = 15;
						break;

					case 10:
					case 14:
						if (!ay->env.up) {
							ay->env.dac--;
							if (ay->env.dac < 0) {
								ay->env.dac = 0;
								ay->env.up = 1;
							}
						} else {
							ay->env.dac++;
							if (ay->env.dac > 15) {
								ay->env.dac = 15;
								ay->env.up = 0;
							}

						}
						break;

					case 11:
						if (!ay->env.up) {
							ay->env.dac--;
							if (ay->env.dac < 0) {
								ay->env.dac = 15;
								ay->env.up = 1;
							}
						}
						break;

					case 12:
						ay->env.dac++;
						if (ay->env.dac > 15)
							ay->env.dac = 0;
						break;

					case 13:
						if (ay->env.dac < 15)
							ay->env.dac++;
						break;

				}
			}

			ay->env.count++;
			if (ay->env.count >= (ay->reg[11] | (ay->reg[12] << 8))) {
				ay->env.count = 0;
			}
		}

		//микшер
		ta = ay->tone[0].state | ((ay->reg[7] >> 0) & 1);
		tb = ay->tone[1].state | ((ay->reg[7] >> 1) & 1);
		tc = ay->tone[2].state | ((ay->reg[7] >> 2) & 1);
		na = ay->noise.state | ((ay->reg[7] >> 3) & 1);
		nb = ay->noise.state | ((ay->reg[7] >> 4) & 1);
		nc = ay->noise.state | ((ay->reg[7] >> 5) & 1);

		if (ay->reg[8] & 16) {
			ay->dac[0] = ay->env.dac;
		} else {
			if (ta & na)
				ay->dac[0] = ay->reg[8];
			else
				ay->dac[0] = 0;
		}

		if (ay->reg[9] & 16) {
			ay->dac[1] = ay->env.dac;
		} else {
			if (tb & nb)
				ay->dac[1] = ay->reg[9];
			else
				ay->dac[1] = 0;
		}

		if (ay->reg[10] & 16) {
			ay->dac[2] = ay->env.dac;
		} else {
			if (tc & nc)
				ay->dac[2] = ay->reg[10];
			else
				ay->dac[2] = 0;
		}

		ay->out[0] += volTab[ay->dac[0]];
		ay->out[1] += volTab[ay->dac[1]];
		ay->out[2] += volTab[ay->dac[2]];
	}

	ay->out[0] /= ticks;
	ay->out[1] /= ticks;
	ay->out[2] /= ticks;
}


/////////////////////////////

inline uint32_t ay_mix(AYChipStruct * ay) {

	ay_tick(ay, (AY_CLOCK / SAMPLE_RATE / 8));

	uint32_t out_l, out_r;

	out_l = ay->out[0] + ay->out[1] / 2;
	out_r = ay->out[2] + ay->out[1] / 2;

	if (out_l > 32767)
		out_l = 32767;
	if (out_r > 32767)
		out_r = 32767;

	return out_l | (out_r << 16);
}

int main() {

	AYChipStruct chip0;

	ay_init(&chip0);

	int samplerate = 44100;
	int channels = 2;
	int bits = 16;
	int playerFreq = 50;

	int frames = sizeof(frame_data) / sizeof(frame_data[0]);
	int samples = samplerate * frames / playerFreq;
	int samples_per_frame = samples / frames;

	fprintf(stderr, "shiru, ay render, frames: %d, writing wav...\n", frames);

	int bufsize = samples*4;
	uint32_t * buf = (uint32_t*)malloc(bufsize);
	uint32_t * out = buf;

	clock_t t = clock();

	int frame = 0;
	while (frame < frames) {

		for (int i = 0; i < 14; i++)
			ay_out(&chip0, i, frame_data[frame][i]);

		for (int i = 0; i < samples_per_frame; i++)
			*out++ = ay_mix(&chip0);

		frame++;
	}

	t = clock() - t;
	double time_taken = ((double)t) / CLOCKS_PER_SEC;
	fprintf(stderr, "%f seconds\n", time_taken);

	FILE *fp = fopen("out_shiru.wav", "wb");
	int hdr[] = { 1179011410, 36, 1163280727, 544501094, 16, 131073, 44100, 176400, 1048580, 1635017060, 0 };
	hdr[1] = samples * 4 + 15;
	hdr[10] = samples * 4;
	fwrite(hdr, 1, sizeof(hdr), fp);
	fwrite(buf, 1, bufsize, fp);
	free(buf);
	fclose(fp);
}

