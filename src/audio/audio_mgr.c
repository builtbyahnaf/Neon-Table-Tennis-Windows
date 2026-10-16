/*
 * =============================================================================
 * audio_mgr.c  —  Neon Table Tennis  |  Audio Implementation
 * =============================================================================
 *
 * IMPLEMENTATION DETAILS
 * ----------------------
 * Handles procedural sound asset loading and playback with dynamic pitch modulation.
 * The audio subsystem is designed to fail gracefully: if hardware is missing, or
 * if optional asset files cannot be located on disk, all operations degrade
 * to safe no-ops without terminating execution.
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
 * =============================================================================
 */

#include "audio_mgr.h"
#include "math_3d.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

/* Raylib version compatibility fallback: map IsSoundReady to IsSoundValid */
#ifndef IsSoundReady
    #define IsSoundReady(s) IsSoundValid(s)
#endif

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
 * If audio hardware fails to open, or if sound files are not found, execution
 * proceeds smoothly with warning logs.
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

    /* Load game sound effect assets from disk */
    a->hit    = LoadSound("assets/audio/paddle.wav");
    a->bounce = LoadSound("assets/audio/bounce.wav");
    a->ui     = LoadSound("assets/audio/ui.wav");
    a->smash  = LoadSound("assets/audio/smash.wav");

    /* Log status for asset diagnosis */
    if (!IsSoundValid(a->hit)) {
        TraceLog(LOG_INFO, "[AudioMgr] 'assets/audio/paddle.wav' not found — hit sounds disabled.");
    }
    if (!IsSoundValid(a->bounce)) {
        TraceLog(LOG_INFO, "[AudioMgr] 'assets/audio/bounce.wav' not found — bounce sounds disabled.");
    }
    if (!IsSoundValid(a->ui)) {
        TraceLog(LOG_INFO, "[AudioMgr] 'assets/audio/ui.wav' not found — UI clicks disabled.");
    }
    if (!IsSoundValid(a->smash)) {
        TraceLog(LOG_INFO, "[AudioMgr] 'assets/audio/smash.wav' not found — smash sounds disabled.");
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
