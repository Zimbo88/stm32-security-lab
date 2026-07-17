#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdint.h>

uint8_t platform_audio_available(void);
uint8_t platform_audio_start_retro(void);
void platform_audio_stop(void);

#endif
