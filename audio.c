#include "audio.h"
#include "timer_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
static void u16(unsigned char *p, unsigned x) { p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8); }
static void u32(unsigned char *p, unsigned x) {
    p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8);
    p[2] = (unsigned char)(x >> 16); p[3] = (unsigned char)(x >> 24);
}
static short *begin_wave(TLAudio *a, unsigned ms) {
    unsigned n = ms * TL_AUDIO_RATE / 1000U, bytes = n * 2U;
    unsigned char *p = a->wav;
    if (!p || (size_t)bytes + 44U > a->capacity) return NULL;
    memcpy(p, "RIFF", 4); u32(p + 4, bytes + 36); memcpy(p + 8, "WAVEfmt ", 8);
    u32(p + 16, 16); u16(p + 20, 1); u16(p + 22, 1);
    u32(p + 24, TL_AUDIO_RATE); u32(p + 28, TL_AUDIO_RATE * 2U);
    u16(p + 32, 2); u16(p + 34, 16);
    memcpy(p + 36, "data", 4); u32(p + 40, bytes);
    a->length = (size_t)bytes + 44U;
    return (short *)(p + 44);
}
static short sample(double v) {
    if (v > 0.7) v = 0.7;
    if (v < -0.7) v = -0.7;
    return (short)(v * 32767.0);
}
int tl_audio_init(TLAudio *a) {
    a->capacity = 44U + (size_t)TL_AUDIO_MAX_MS * TL_AUDIO_RATE / 1000U * 2U;
    a->wav = (unsigned char *)malloc(a->capacity);
    a->length = 0;
    return a->wav != NULL;
}
void tl_audio_free(TLAudio *a) { free(a->wav); a->wav = NULL; a->length = a->capacity = 0; }

int tl_audio_chime(TLAudio *a, uint32_t serial) {
    static const double hz[8] = {329.63, 392.00, 440.00, 493.88,
                                  523.25, 587.33, 659.25, 783.99};
    int notes[3], i;
    short *dst = begin_wave(a, TL_CHIME_MS);
    if (!dst) return 0;
    tl_chime_notes(serial, notes);
    for (i = 0; i < (int)(TL_CHIME_MS * TL_AUDIO_RATE / 1000U); ++i) {
        double t = (double)i / TL_AUDIO_RATE, v = 0.0;
        int slot = (int)(t / 0.3);
        if (slot < 3) {
            double local = t - slot * 0.3;
            if (local < 0.22) {
                double env = fmin(1.0, local / 0.012) * exp(-7.5 * local);
                double f = hz[notes[slot]];
                v = 0.21 * env * (sin(2 * PI * f * local) +
                                   0.22 * sin(2 * PI * f * 2 * local));
            }
        }
        dst[i] = sample(v);
    }
    return 1;
}

/* Four short, original pentatonic themes. Each is synthesized rather than
   shipped as a recording; no audio file, network, or codec is needed. */
int tl_audio_music(TLAudio *a, uint32_t serial) {
    static const int score[4][16] = {
        {0,2,4,5,4,2,1,0, 2,4,5,7,5,4,2,0},
        {4,5,7,5,4,2,0,1, 2,4,2,1,0,2,4,2},
        {7,5,4,2,4,5,4,2, 1,0,2,4,5,4,2,0},
        {0,1,2,4,5,7,5,4, 2,1,0,2,4,2,1,0}
    };
    static const double melody[8] = {261.63,293.66,329.63,392.00,
                                      440.00,523.25,587.33,659.25};
    static const double roots[4][4] = {
        {130.81,110.00,174.61,196.00},
        {174.61,130.81,196.00,110.00},
        {110.00,174.61,130.81,196.00},
        {130.81,196.00,110.00,174.61}
    };
    unsigned variant = serial % 4U;
    int i, n = (int)(TL_MUSIC_MS * TL_AUDIO_RATE / 1000U);
    short *dst = begin_wave(a, TL_MUSIC_MS);
    if (!dst) return 0;
    for (i = 0; i < n; ++i) {
        double t = (double)i / TL_AUDIO_RATE, local, f, env, chord, v;
        int beat = (int)(t / 0.75);
        if (beat > 15) { dst[i] = 0; continue; }
        local = t - beat * 0.75;
        f = melody[score[variant][beat]];
        env = fmin(1.0, local / 0.025) * exp(-2.0 * local);
        chord = roots[variant][beat / 4];
        v = 0.15 * env * (sin(2 * PI * f * local) + 0.16 * sin(4 * PI * f * local));
        v += 0.035 * (sin(2 * PI * chord * t) +
                      0.55 * sin(2 * PI * chord * 1.5 * t) +
                      0.4 * sin(2 * PI * chord * 2 * t));
        v *= fmin(1.0, t / 0.20) * fmin(1.0, (12.0 - t) / 0.75);
        dst[i] = sample(v);
    }
    return 1;
}

int tl_audio_rest_end(TLAudio *a) {
    int i, n = (int)(600U * TL_AUDIO_RATE / 1000U);
    short *dst = begin_wave(a, 600U);
    if (!dst) return 0;
    for (i = 0; i < n; ++i) {
        double t = (double)i / TL_AUDIO_RATE, local = fmod(t, 0.3);
        double f = t < 0.3 ? 392.0 : 523.25;
        double env = fmin(1.0, local / 0.012) * exp(-7.0 * local);
        dst[i] = sample(0.14 * env * sin(2 * PI * f * local));
    }
    return 1;
}
