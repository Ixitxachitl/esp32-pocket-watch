/*
 *  sound.cpp – ESP32 notification sound via ES8311 I2S DAC
 *
 *  The Waveshare ESP32-S3-Touch-AMOLED-1.75C has an ES8311 audio codec
 *  connected via I2S + I2C (for control).  The power amplifier is
 *  enabled by driving the PA pin HIGH.
 *
 *  We generate PCM sine-wave tones in RAM and push them via I2S.
 */

#ifndef SIMULATOR

#include "sound.h"
#include <Arduino.h>
#include <Wire.h>
#include "ESP_I2S.h"
#include "pin_config.h"
#include <math.h>

/* ── ES8311 I2C address & key registers ──────────────────────── */
#define PA_PIN       PA    /* Power-amplifier enable from pin_config.h */
#define ES8311_ADDR  0x18
/* Minimal register set for DAC output */
#define ES8311_REG00_RESET      0x00
#define ES8311_REG01_CLK_MGR    0x01
#define ES8311_REG02_CLK_MGR    0x02
#define ES8311_REG03_CLK_MGR    0x03
#define ES8311_REG04_CLK_MGR    0x04
#define ES8311_REG05_CLK_MGR    0x05
#define ES8311_REG06_CLK_MGR    0x06
#define ES8311_REG07_CLK_MGR    0x07
#define ES8311_REG08_CLK_MGR    0x08
#define ES8311_REG09_SDPIN      0x09
#define ES8311_REG0A_SDPOUT     0x0A
#define ES8311_REG0C_SYS        0x0C
#define ES8311_REG0D_SYS        0x0D
#define ES8311_REG0E_SYS        0x0E
#define ES8311_REG0F_SYS        0x0F
#define ES8311_REG10_SYS        0x10
#define ES8311_REG11_SYS        0x11
#define ES8311_REG12_SYS        0x12
#define ES8311_REG13_SYS        0x13
#define ES8311_REG14_ADC        0x14
#define ES8311_REG32_DAC        0x32
#define ES8311_REGFA_ID         0xFD

/* ── Audio parameters ────────────────────────────────────────── */
#define SAMPLE_RATE       16000
#define DING_FREQ         880     /* A5 */
#define DING_DURATION_MS  200
#define CLICK_FREQ        1200
#define CLICK_DURATION_MS 50

#define DING_SAMPLES  ((SAMPLE_RATE * DING_DURATION_MS) / 1000)
#define CLICK_SAMPLES ((SAMPLE_RATE * CLICK_DURATION_MS) / 1000)

/* Stereo 16-bit: 2 channels × 2 bytes = 4 bytes per sample */
static int16_t ding_pcm[DING_SAMPLES * 2];    /* L+R interleaved */
static int16_t click_pcm[CLICK_SAMPLES * 2];

static I2SClass i2s;
static bool _sound_ok = false;

/* ── ES8311 register helpers ──────────────────────────────────── */
static void es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t es_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static bool es8311_init_codec(void) {
    /* Verify chip ID */
    uint8_t id = es_read(ES8311_REGFA_ID);
    Serial.printf("ES8311 chip ID: 0x%02X\n", id);

    /*
     * Register sequence matches the official Espressif ESP-ADF es8311 driver.
     * Ref: esp-adf/components/audio_hal/driver/es8311/es8311.c
     */

    /* Enhance I2C noise immunity (write twice per official driver) */
    es_write(0x44, 0x08);
    es_write(0x44, 0x08);

    /* Initial clock / system register defaults */
    es_write(ES8311_REG01_CLK_MGR, 0x30);
    es_write(ES8311_REG02_CLK_MGR, 0x00);
    es_write(ES8311_REG03_CLK_MGR, 0x10);
    es_write(ES8311_REG14_ADC,     0x24);  /* ADC mic gain default */
    es_write(ES8311_REG04_CLK_MGR, 0x10);
    es_write(ES8311_REG05_CLK_MGR, 0x00);
    es_write(0x0B, 0x00);                  /* system REG0B */
    es_write(ES8311_REG0C_SYS, 0x00);
    es_write(ES8311_REG10_SYS, 0x1F);
    es_write(ES8311_REG11_SYS, 0x7F);
    es_write(ES8311_REG00_RESET, 0x80);    /* normal operation */

    /* Slave mode – clear bit 6 of REG00 */
    uint8_t reg00 = es_read(ES8311_REG00_RESET);
    es_write(ES8311_REG00_RESET, reg00 & 0xBF);

    /* Enable all clock outputs, MCLK from pin (bit 7 = 0) */
    es_write(ES8311_REG01_CLK_MGR, 0x3F);

    /*
     * Clock dividers for MCLK = 256 × fs = 4.096 MHz, fs = 16 kHz.
     * From coeff table: pre_div=1, pre_multi=1, adc_div=1, dac_div=1,
     *   fs_mode=0, lrck_h=0x00, lrck_l=0xFF, bclk_div=4, adc_osr=0x10,
     *   dac_osr=0x20.
     */
    es_write(ES8311_REG02_CLK_MGR, 0x00);  /* pre_div=1, pre_multi=1 */
    es_write(ES8311_REG03_CLK_MGR, 0x10);  /* adc_osr */
    es_write(ES8311_REG04_CLK_MGR, 0x20);  /* dac_osr */
    es_write(ES8311_REG05_CLK_MGR, 0x00);  /* adc_div=1, dac_div=1 */
    es_write(ES8311_REG06_CLK_MGR, 0x03);  /* bclk_div = 4-1 = 3 */
    es_write(ES8311_REG07_CLK_MGR, 0x00);  /* lrck_h */
    es_write(ES8311_REG08_CLK_MGR, 0xFF);  /* lrck_l */

    /* SDP config – 16-bit I2S, DAC unmuted (bit 6 = 0), ADC muted (bit 6 = 1) */
    es_write(ES8311_REG09_SDPIN,  0x0C);   /* 16-bit I2S, DAC unmuted */
    es_write(ES8311_REG0A_SDPOUT, 0x4C);   /* 16-bit I2S, ADC muted */

    /* Start DAC path (from es8311_start) */
    es_write(0x17, 0xBF);                  /* ADC config */
    es_write(ES8311_REG0E_SYS, 0x02);      /* enable DAC */
    es_write(ES8311_REG12_SYS, 0x00);      /* power up */
    es_write(0x14, 0x1A);                  /* enable VMID + reference */
    es_write(ES8311_REG0D_SYS, 0x01);      /* power up analog */
    es_write(0x15, 0x40);                  /* ADC power up */
    es_write(0x37, 0x08);                  /* DAC output mixer enable */
    es_write(0x45, 0x00);                  /* GPIO release */
    es_write(0x44, 0x58);                  /* internal reference signal */

    /* System misc */
    es_write(ES8311_REG0F_SYS, 0x00);
    es_write(ES8311_REG10_SYS, 0x1F);
    es_write(ES8311_REG11_SYS, 0x7F);
    es_write(ES8311_REG13_SYS, 0x10);

    /* ADC config (for reference signal path) */
    es_write(0x1B, 0x0A);
    es_write(0x1C, 0x6A);

    /* Set DAC volume: 0xBF = 0 dB */
    es_write(ES8311_REG32_DAC, 0xBF);

    Serial.println("ES8311 codec configured");
    return true;
}

/* ── Generate stereo PCM tone ─────────────────────────────────── */
static void generate_tone(int16_t *buf, int num_mono_samples, int freq) {
    for (int i = 0; i < num_mono_samples; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* Envelope: quick attack, exponential decay */
        float progress = (float)i / num_mono_samples;
        float env = (progress < 0.02f) ? (progress / 0.02f)
                                       : expf(-4.0f * (progress - 0.02f));
        float sample = sinf(2.0f * M_PI * freq * t) * env * 0.6f;
        sample += sinf(2.0f * M_PI * freq * 2.0f * t) * env * 0.15f;
        int16_t val = (int16_t)(sample * 30000);
        buf[i * 2]     = val;   /* Left  */
        buf[i * 2 + 1] = val;   /* Right */
    }
}

void sound_init(int buzzer_pin) {
    (void)buzzer_pin;

    /* Enable power amplifier */
    pinMode(PA_PIN, OUTPUT);
    digitalWrite(PA_PIN, HIGH);

    /* Init I2S */
    i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("Sound: I2S init FAILED");
        return;
    }

    /* Init ES8311 codec via I2C (Wire must already be initialised) */
    if (!es8311_init_codec()) {
        Serial.println("Sound: ES8311 init FAILED");
        return;
    }

    /* Pre-generate tone buffers */
    generate_tone(ding_pcm,  DING_SAMPLES,  DING_FREQ);
    generate_tone(click_pcm, CLICK_SAMPLES, CLICK_FREQ);

    _sound_ok = true;
    Serial.println("Sound: ES8311 I2S audio initialised");
}

void sound_play_ding(void) {
    if (!_sound_ok) return;
    i2s.write((uint8_t *)ding_pcm, sizeof(ding_pcm));
}

void sound_play_click(void) {
    if (!_sound_ok) return;
    i2s.write((uint8_t *)click_pcm, sizeof(click_pcm));
}

/* ── RTTTL player ─────────────────────────────────────────────── */

/* Note frequency table (octave 4): C4..B4 */
static const uint16_t note_freq[] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};
/*  index:  0=C  1=C# 2=D  3=D# 4=E  5=F  6=F# 7=G  8=G# 9=A  10=A# 11=B */

static int rtttl_note_index(char c) {
    switch (c) {
        case 'c': return 0;
        case 'd': return 2;
        case 'e': return 4;
        case 'f': return 5;
        case 'g': return 7;
        case 'a': return 9;
        case 'b': return 11;
        default:  return -1;
    }
}

/* Generate and play a single tone via I2S (blocking) */
static void play_tone_ms(int freq, int duration_ms) {
    if (freq == 0 || duration_ms == 0) {
        /* Rest: generate silence */
        int samples = (SAMPLE_RATE * duration_ms) / 1000;
        /* Write silence in chunks to avoid large allocation */
        static int16_t silence[512];
        memset(silence, 0, sizeof(silence));
        int remaining = samples * 2;  /* stereo samples */
        while (remaining > 0) {
            int chunk = remaining > 512 ? 512 : remaining;
            i2s.write((uint8_t *)silence, chunk * sizeof(int16_t));
            remaining -= chunk;
        }
        return;
    }

    int samples = (SAMPLE_RATE * duration_ms) / 1000;
    /* Generate and write in chunks to save memory */
    static int16_t tone_buf[512];  /* 256 stereo samples */
    int written = 0;
    while (written < samples) {
        int chunk = samples - written;
        if (chunk > 256) chunk = 256;  /* 256 mono = 512 stereo entries */
        for (int i = 0; i < chunk; i++) {
            int idx = written + i;
            float t = (float)idx / SAMPLE_RATE;
            float progress = (float)idx / samples;
            /* Soft envelope */
            float env = 1.0f;
            if (progress < 0.05f) env = progress / 0.05f;
            else if (progress > 0.85f) env = (1.0f - progress) / 0.15f;
            float sample = sinf(2.0f * M_PI * freq * t) * env * 0.5f;
            int16_t val = (int16_t)(sample * 24000);
            tone_buf[i * 2]     = val;
            tone_buf[i * 2 + 1] = val;
        }
        i2s.write((uint8_t *)tone_buf, chunk * 2 * sizeof(int16_t));
        written += chunk;
    }
}

void sound_play_rtttl(const char *rtttl) {
    if (!_sound_ok || !rtttl) return;

    /* Skip name (everything before first ':') */
    const char *p = rtttl;
    while (*p && *p != ':') p++;
    if (!*p) return;
    p++;  /* skip ':' */

    /* Parse default values: d=duration, o=octave, b=bpm */
    int def_dur = 4, def_oct = 6, bpm = 63;
    while (*p && *p != ':') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == 'd' && *(p+1) == '=') {
            p += 2; def_dur = atoi(p); while (*p >= '0' && *p <= '9') p++;
        } else if (*p == 'o' && *(p+1) == '=') {
            p += 2; def_oct = atoi(p); while (*p >= '0' && *p <= '9') p++;
        } else if (*p == 'b' && *(p+1) == '=') {
            p += 2; bpm = atoi(p); while (*p >= '0' && *p <= '9') p++;
        } else {
            p++;
        }
    }
    if (!*p) return;
    p++;  /* skip ':' */

    int whole_note_ms = (60 * 1000 * 4) / bpm;

    /* Parse and play notes */
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        /* Duration (optional) */
        int dur = 0;
        while (*p >= '0' && *p <= '9') { dur = dur * 10 + (*p - '0'); p++; }
        if (dur == 0) dur = def_dur;

        /* Note letter */
        if (!*p) break;
        char note_c = *p | 0x20;  /* lowercase */
        p++;
        int ni = -1;
        if (note_c == 'p') {
            ni = -1;  /* pause/rest */
        } else {
            ni = rtttl_note_index(note_c);
        }

        /* Sharp? */
        if (*p == '#') { if (ni >= 0) ni++; p++; }

        /* Dotted? */
        bool dotted = false;
        if (*p == '.') { dotted = true; p++; }

        /* Octave (optional) */
        int oct = def_oct;
        if (*p >= '0' && *p <= '9') { oct = *p - '0'; p++; }

        /* Another dotted check (some RTTTL put dot after octave) */
        if (*p == '.') { dotted = true; p++; }

        /* Calculate duration in ms */
        int note_ms = whole_note_ms / dur;
        if (dotted) note_ms = note_ms * 3 / 2;

        /* Calculate frequency */
        int freq = 0;
        if (ni >= 0) {
            freq = note_freq[ni % 12];
            int target_oct = oct;
            /* note_freq is octave 4, shift to target */
            while (target_oct > 4) { freq *= 2; target_oct--; }
            while (target_oct < 4) { freq /= 2; target_oct++; }
        }

        play_tone_ms(freq, note_ms);
    }
}

void sound_loop(void) {
    /* Nothing needed – I2S write is blocking (short tones) */
}

#endif /* !SIMULATOR */

