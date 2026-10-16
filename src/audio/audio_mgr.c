/*
 * =============================================================================
 * audio_mgr.c  —  Neon Table Tennis  |  Audio Implementation
 * =============================================================================
 *
 * IMPLEMENTATION DETAILS
 * ----------------------
 * Handles procedural sound asset loading and playback with dynamic pitch modulation.
 * The audio subsystem is designed to fail gracefully: if hardware is missing, or
 * if optional asset files cannot be located on disk, the system procedurally
 * synthesizes retro arcade audio waveforms directly in memory via Raylib Wave buffers.
 *
 * PITCH MODULATION ALGORITHM
 * --------------------------
 * When a paddle strikes the ball, the sound pitch is modulated by the ball's
 * linear velocity magnitude at impact:
 *
 *   speed = |velocity|  (m/s)
 *   pitch = clamp(speed / 9.0f, 0.75f, 1.45f)
 *
 * This provides acoustic feedback of the shot's kinetic energy:
 *   - Soft touch or drop shot: deeper pitch (~0.75x)
 *   - Normal rally return: standard pitch (~1.0x)
 *   - Charged smash: high-frequency, crisp acoustic attack (~1.45x)
 *
 * PROCEDURAL AUDIO SYNTHESIZER
 * ----------------------------
 * When optional WAV files are absent from disk, `SynthSound` generates 16-bit
 * 44.1 kHz PCM audio on-the-fly:
 *   - Type 0 (Hit): Damped 280 Hz tone with exponential decay
 *   - Type 1 (Bounce): Damped 170 Hz wood tap
 *   - Type 2 (UI): High 620 Hz blip
 *   - Type 3 (Smash): 380 Hz downward pitch sweep + transient noise burst
 *
 * =============================================================================
 */

#include "audio_mgr.h"
#include "math_3d.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

/* Raylib version compatibility fallback: map IsSoundReady to IsSoundValid */
#ifndef IsSoundReady
    #define IsSoundReady(s) IsSoundValid(s)
#endif

/* ---------------------------------------------------------------------------
 * Procedural Sound Synthesizer Fallback
 * --------------------------------------------------------------------------- */

/*
 * SynthSound
 * ----------
 * Generates an in-memory procedural audio waveform and converts it into a Sound.
 *
 * @param type  0 = hit, 1 = bounce, 2 = UI click, 3 = power smash.
 * @return      Valid Sound handle.
 */
static Sound SynthSound(int type)
{
    unsigned int sampleRate = 44100;
    float duration = 0.08f;
    float baseFreq = 280.0f;

    if (type == 1) {
        duration = 0.05f;
        baseFreq = 170.0f;
    } else if (type == 2) {
        duration = 0.035f;
        baseFreq = 620.0f;
    } else if (type == 3) {
        duration = 0.16f;
        baseFreq = 380.0f;
    }

    unsigned int frameCount = (unsigned int)((float)sampleRate * duration);
    short *samples = (short *)calloc(frameCount, sizeof(short));
    if (samples == NULL) {
        return (Sound){ 0 };
    }

    const float twoPi = 6.2831853f;

    for (unsigned int i = 0; i < frameCount; i++) {
        float t        = (float)i / (float)sampleRate;
        float progress = (float)i / (float)frameCount;
        float envelope = expf(-progress * 6.5f);
        float freq     = baseFreq;

        if (type == 3) {
            /* Downward pitch sweep for power smash */
            freq = baseFreq * (1.0f - progress * 0.45f);
        }

        float sample = sinf(twoPi * freq * t);

        if (type == 3) {
            /* White noise burst on attack for metallic punch */
            float noise = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            sample = sample * 0.70f + noise * 0.30f * expf(-progress * 14.0f);
        }

        samples[i] = (short)(sample * envelope * 22000.0f);
    }

    Wave wave = {
        .frameCount = frameCount,
        .sampleRate = sampleRate,
        .sampleSize = 16,
        .channels   = 1,
        .data       = samples
    };

    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

/* ---------------------------------------------------------------------------
 * Internal Playback Helper
 * --------------------------------------------------------------------------- */

/*
 * Play
 * ----
 * Validates audio device readiness and sound buffer validity before applying
 * volume and pitch parameters and triggering playback.
 *
 * @param a      Pointer to AudioMgr subsystem state.
 * @param s      Raylib Sound handle to play.
 * @param pitch  Playback frequency multiplier (> 0.0f).
 */
static void Play(AudioMgr *a, Sound s, float pitch)
{
    if (a == NULL || !a->ready) {
        return;
    }

    if (IsSoundValid(s)) {
        SetSoundVolume(s, a->master);
        SetSoundPitch(s, pitch);
        PlaySound(s);
    }
}

/* ---------------------------------------------------------------------------
 * Public Subsystem Lifecycle
 * --------------------------------------------------------------------------- */

/*
 * AudioInit
 * ---------
 * Initializes Raylib audio hardware and loads all sound assets from disk.
 * If optional WAV files are absent, procedural waveforms are synthesized in-memory.
 */
void AudioInit(AudioMgr *a)
{
    if (a == NULL) {
        return;
    }

    memset(a, 0, sizeof(*a));
    a->master = 0.70f;

    /* Initialize Raylib audio device */
    InitAudioDevice();

    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "[AudioMgr] Audio device initialization failed — running in silent mode.");
        a->ready = false;
        return;
    }

    a->ready = true;

    /* 1. Paddle drive hit */
    a->hit = LoadSound("assets/audio/paddle.wav");
    if (!IsSoundValid(a->hit)) {
        TraceLog(LOG_INFO, "[AudioMgr] Generating procedural fallback for paddle.wav");
        a->hit = SynthSound(0);
    }

    /* 2. Table surface bounce */
    a->bounce = LoadSound("assets/audio/bounce.wav");
    if (!IsSoundValid(a->bounce)) {
        TraceLog(LOG_INFO, "[AudioMgr] Generating procedural fallback for bounce.wav");
        a->bounce = SynthSound(1);
    }

    /* 3. UI click */
    a->ui = LoadSound("assets/audio/ui.wav");
    if (!IsSoundValid(a->ui)) {
        TraceLog(LOG_INFO, "[AudioMgr] Generating procedural fallback for ui.wav");
        a->ui = SynthSound(2);
    }

    /* 4. Power smash */
    a->smash = LoadSound("assets/audio/smash.wav");
    if (!IsSoundValid(a->smash)) {
        TraceLog(LOG_INFO, "[AudioMgr] Generating procedural fallback for smash.wav");
        a->smash = SynthSound(3);
    }
}

/*
 * AudioFree
 * ---------
 * Safely unloads all sound effect buffers and shuts down the audio device.
 */
void AudioFree(AudioMgr *a)
{
    if (a == NULL) {
        return;
    }

    if (a->ready) {
        if (IsSoundValid(a->hit)) {
            UnloadSound(a->hit);
        }
        if (IsSoundValid(a->bounce)) {
            UnloadSound(a->bounce);
        }
        if (IsSoundValid(a->ui)) {
            UnloadSound(a->ui);
        }
        if (IsSoundValid(a->smash)) {
            UnloadSound(a->smash);
        }

        CloseAudioDevice();
    }

    memset(a, 0, sizeof(*a));
}

/* ---------------------------------------------------------------------------
 * Sound Triggers
 * --------------------------------------------------------------------------- */

/*
 * AudioPlayHit
 * ------------
 * Plays the paddle hit or smash sound, modulating pitch by speed.
 */
void AudioPlayHit(AudioMgr *a, bool smash, float speed)
{
    /* Dynamic pitch modulation formula: clamp(speed / 9.0f, 0.75f, 1.45f) */
    float pitch = ClampF(speed / 9.0f, 0.75f, 1.45f);
    Play(a, smash ? a->smash : a->hit, pitch);
}

/*
 * AudioPlayBounce
 * ---------------
 * Plays the table bounce sound effect at standard 1.0 pitch.
 */
void AudioPlayBounce(AudioMgr *a)
{
    Play(a, a->bounce, 1.0f);
}

/*
 * AudioPlayUI
 * -----------
 * Plays the UI navigation / button click sound effect.
 */
void AudioPlayUI(AudioMgr *a)
{
    Play(a, a->ui, 1.0f);
}
