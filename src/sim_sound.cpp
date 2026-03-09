/*
 *  sim_sound.cpp – SDL2 notification sound (simulator only)
 *
 *  Generates a short sine-wave "ding" using SDL2 audio.
 *  No external audio files needed.
 */

#ifdef SIMULATOR

#include "sound.h"
#include <SDL.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE  44100
#define DING_FREQ    880.0    /* A5 note */
#define DING_DURATION_MS 200
#define CLICK_FREQ   1200.0
#define CLICK_DURATION_MS 50

/* ── Audio state ─────────────────────────────────────────────── */
static SDL_AudioDeviceID audio_dev = 0;
static bool audio_ok = false;

/* Pre-generated ding and click buffers */
#define DING_SAMPLES  ((SAMPLE_RATE * DING_DURATION_MS) / 1000)
#define CLICK_SAMPLES ((SAMPLE_RATE * CLICK_DURATION_MS) / 1000)

static int16_t ding_buf[DING_SAMPLES];
static int16_t click_buf[CLICK_SAMPLES];

static void generate_tone(int16_t *buf, int num_samples, double freq) {
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / SAMPLE_RATE;
        /* Envelope: quick attack, exponential decay */
        double env = 1.0;
        double progress = (double)i / num_samples;
        if (progress < 0.02) {
            env = progress / 0.02;           /* 2% attack */
        } else {
            env = exp(-4.0 * (progress - 0.02));  /* decay */
        }
        double sample = sin(2.0 * M_PI * freq * t) * env * 0.6;
        /* Add a harmonic for a richer sound */
        sample += sin(2.0 * M_PI * freq * 2.0 * t) * env * 0.15;
        buf[i] = (int16_t)(sample * 32000);
    }
}

void sound_init(int buzzer_pin) {
    (void)buzzer_pin;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return;
    }

    SDL_AudioSpec want = {};
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = NULL;  /* use SDL_QueueAudio */

    SDL_AudioSpec have;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }

    /* Pre-generate tone buffers */
    generate_tone(ding_buf, DING_SAMPLES, DING_FREQ);
    generate_tone(click_buf, CLICK_SAMPLES, CLICK_FREQ);

    /* Unpause the audio device */
    SDL_PauseAudioDevice(audio_dev, 0);
    audio_ok = true;
    printf("Sound: SDL2 audio initialised\n");
}

void sound_play_ding(void) {
    if (!audio_ok) return;
    SDL_ClearQueuedAudio(audio_dev);
    SDL_QueueAudio(audio_dev, ding_buf, sizeof(ding_buf));
}

void sound_play_click(void) {
    if (!audio_ok) return;
    SDL_ClearQueuedAudio(audio_dev);
    SDL_QueueAudio(audio_dev, click_buf, sizeof(click_buf));
}

void sound_play_rtttl(const char *rtttl) {
    (void)rtttl;
    /* RTTTL not implemented in simulator – play a ding instead */
    sound_play_ding();
}

void sound_loop(void) {
    /* Nothing needed – SDL handles playback asynchronously */
}

#endif /* SIMULATOR */
