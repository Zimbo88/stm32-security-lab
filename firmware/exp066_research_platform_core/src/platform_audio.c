#include "platform_audio.h"

/*
 * EXP071 deliberately leaves audio disabled by default. The repository board
 * evidence does not define a speaker, buzzer, or timer-capable audio pin.
 */

uint8_t platform_audio_available(void)
{
    return 0U;
}

uint8_t platform_audio_start_retro(void)
{
    return 0U;
}

void platform_audio_stop(void)
{
}
