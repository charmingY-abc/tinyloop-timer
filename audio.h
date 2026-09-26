#ifndef TINYLOOP_AUDIO_H
#define TINYLOOP_AUDIO_H
#include <stddef.h>
#include <stdint.h>

#define TL_AUDIO_RATE 22050U
#define TL_AUDIO_MAX_MS 12500U

typedef struct {
    unsigned char *wav;
    size_t capacity;
    size_t length;
} TLAudio;

int tl_audio_init(TLAudio *a);
void tl_audio_free(TLAudio *a);
int tl_audio_chime(TLAudio *a, uint32_t serial);
int tl_audio_music(TLAudio *a, uint32_t serial);
int tl_audio_rest_end(TLAudio *a);

#endif
