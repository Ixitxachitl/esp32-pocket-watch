#pragma once
/*
 *  sound.h – Cross-platform notification sound
 *
 *  Simulator: Generates sine-wave ding via SDL2 audio.
 *  ESP32:     Drives a passive buzzer via LEDC PWM on a configurable GPIO.
 *             If no buzzer is connected, calls are harmless no-ops.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the sound subsystem.
   ESP32:     pass the GPIO pin connected to a passive buzzer (-1 to disable).
   Simulator: pin is ignored; SDL2 audio is used.                            */
void sound_init(int buzzer_pin);

/* Play a short notification ding (~200ms, 880 Hz). Non-blocking. */
void sound_play_ding(void);

/* Play a short click/tap sound (~50ms, 1200 Hz). Non-blocking. */
void sound_play_click(void);

/* Play an RTTTL melody string. Blocking – plays entire tune. */
void sound_play_rtttl(const char *rtttl);

/* Call from loop() – handles sound timing/stop (ESP32 only, no-op on sim). */
void sound_loop(void);

#ifdef __cplusplus
}
#endif
