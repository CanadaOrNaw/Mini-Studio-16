#include "../wavetable.h"

int16_t g_wavetables[NUM_WT_TOTAL][WT_SIZE] = {};
char g_wtNames[NUM_WT_TOTAL][WT_NAME_LEN] = {};
uint8_t g_numWavetables = NUM_BUILTIN_WT;
