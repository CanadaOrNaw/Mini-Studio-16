// ============================================================
// CardputerGroovebox - wavetable.cpp
// ============================================================
#include "wavetable.h"
#include "sampler.h"   // reuse WAV parsing helpers
#include "sd_io_arbiter.h"
#include <SD.h>
#include <math.h>
#include <new>
#include <string.h>

int16_t g_wavetables[NUM_WT_TOTAL][WT_SIZE];
char    g_wtNames[NUM_WT_TOTAL][WT_NAME_LEN];
uint8_t g_numWavetables = 0;

static void normalizeTable(float* buf) {
    float peak = 0.0001f;
    for (int i = 0; i < WT_SIZE; i++) {
        float a = fabsf(buf[i]);
        if (a > peak) peak = a;
    }
    float g = 0.98f / peak;
    for (int i = 0; i < WT_SIZE; i++) buf[i] *= g;
}

static void commitTable(int slot, const float* buf, const char* name) {
    for (int i = 0; i < WT_SIZE; i++)
        g_wavetables[slot][i] = (int16_t)(buf[i] * 32767.0f);
    strncpy(g_wtNames[slot], name, WT_NAME_LEN - 1);
    g_wtNames[slot][WT_NAME_LEN - 1] = 0;
}

void wavetableInitBuiltins() {
    float buf[WT_SIZE];
    float ph;

    // 0: ORGAN - harmonics 1,2,4,8
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        buf[i] = sinf(ph) + 0.5f * sinf(2*ph) + 0.35f * sinf(4*ph) + 0.2f * sinf(8*ph);
    }
    normalizeTable(buf); commitTable(0, buf, "ORGAN");

    // 1: SOFTSAW - band-limited saw (8 harmonics)
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        float s = 0;
        for (int h = 1; h <= 8; h++) s += sinf(h * ph) / (float)h;
        buf[i] = s;
    }
    normalizeTable(buf); commitTable(1, buf, "SOFTSAW");

    // 2: PWM25 - band-limited 25% pulse (odd/even mix via harmonic series)
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        float s = 0;
        for (int h = 1; h <= 10; h++)
            s += (2.0f / (h * 3.14159f)) * sinf(h * 3.14159f * 0.25f) * cosf(h * ph);
        buf[i] = s;
    }
    normalizeTable(buf); commitTable(2, buf, "PWM25");

    // 3: BELL - inharmonic partials
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        buf[i] = sinf(ph) + 0.6f * sinf(2.76f * ph) + 0.4f * sinf(5.4f * ph) + 0.25f * sinf(8.93f * ph);
    }
    normalizeTable(buf); commitTable(3, buf, "BELL");

    // 4: FORMANT - vowel-ish resonant bumps
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        buf[i] = sinf(ph) + 0.7f * sinf(3*ph) * (0.5f + 0.5f * sinf(ph))
                          + 0.5f * sinf(5*ph) * (0.5f + 0.5f * cosf(2*ph));
    }
    normalizeTable(buf); commitTable(4, buf, "FORMANT");

    // 5: METAL - high odd partials, gritty
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        buf[i] = sinf(ph) + 0.8f * sinf(7*ph) + 0.6f * sinf(11*ph) + 0.4f * sinf(13*ph);
    }
    normalizeTable(buf); commitTable(5, buf, "METAL");

    // 6: FOLDSIN - waveshaped (folded) sine, rich and warm
    for (int i = 0; i < WT_SIZE; i++) {
        ph = TWO_PI_F * (float)i / WT_SIZE;
        buf[i] = sinf(1.8f * sinf(ph) * 3.14159f * 0.5f);
    }
    normalizeTable(buf); commitTable(6, buf, "FOLDSIN");

    // 7: GRIT - fixed pseudo-random cycle (deterministic seed), lo-fi digital
    {
        uint32_t seed = 0xC0FFEE42;
        float last = 0;
        for (int i = 0; i < WT_SIZE; i++) {
            seed = seed * 1664525u + 1013904223u;
            float n = ((float)(seed >> 16) / 32768.0f) - 1.0f;
            last = last * 0.7f + n * 0.3f;   // slight smoothing
            buf[i] = last + 0.5f * sinf(TWO_PI_F * (float)i / WT_SIZE);
        }
        // remove DC
        float dc = 0; for (int i = 0; i < WT_SIZE; i++) dc += buf[i];
        dc /= WT_SIZE;  for (int i = 0; i < WT_SIZE; i++) buf[i] -= dc;
        normalizeTable(buf); commitTable(7, buf, "GRIT");
    }

    g_numWavetables = NUM_BUILTIN_WT;
}

void wavetableLoadUserFromSD() {
    File dir;
    { SdIoGuard guard; dir = SD.open(DIR_WAVETABLES); }
    if (!dir || !dir.isDirectory()) return;

    // temp buffer for decoded wav (single cycle files are small; cap 4096 frames)
    const int MAXF = 4096;
    int16_t* tmp = new (std::nothrow) int16_t[MAXF];
    if (!tmp) {
        SdIoGuard guard;
        dir.close();
        return;
    }

    File f;
    { SdIoGuard guard; f = dir.openNextFile(); }
    while (f && g_numWavetables < NUM_WT_TOTAL) {
        String nm = f.name();
        int slash = nm.lastIndexOf('/');
        if (slash >= 0) nm = nm.substring(slash + 1);
        if (!f.isDirectory() && (nm.endsWith(".wav") || nm.endsWith(".WAV"))) {
            uint32_t frames = 0, rate = 0;
            if (wavDecodeToMono16(f, tmp, MAXF, frames, rate) && frames >= 8) {
                // resample whole file (assumed single cycle) to WT_SIZE
                float buf[WT_SIZE];
                for (int i = 0; i < WT_SIZE; i++) {
                    float pos  = (float)i * (float)frames / (float)WT_SIZE;
                    uint32_t a = (uint32_t)pos;
                    float fr   = pos - a;
                    uint32_t b = (a + 1 < frames) ? a + 1 : 0;
                    buf[i] = ((float)tmp[a] * (1.0f - fr) + (float)tmp[b] * fr) / 32768.0f;
                }
                // remove DC + normalize
                float dc = 0; for (int i = 0; i < WT_SIZE; i++) dc += buf[i];
                dc /= WT_SIZE; for (int i = 0; i < WT_SIZE; i++) buf[i] -= dc;
                normalizeTable(buf);

                // strip extension for name
                int dot = nm.lastIndexOf('.');
                String base = (dot > 0) ? nm.substring(0, dot) : nm;
                commitTable(g_numWavetables, buf, base.c_str());
                g_numWavetables++;
            }
        }
        { SdIoGuard guard; f.close(); f = dir.openNextFile(); }
    }
    { SdIoGuard guard; dir.close(); }
    delete[] tmp;
}
