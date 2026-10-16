/*
 * =============================================================================
 * audio_mgr.h  —  Neon Table Tennis  |  Audio Management Subsystem
 * =============================================================================
 *
 * PURPOSE
 * -------
 * Declares the audio subsystem interface for Neon Table Tennis. Handles sound
 * effect playback, asset loading, pitch-based velocity modulation, and graceful
 * degradation when sound files or audio devices are missing.
 *
 * ARCHITECTURE & FALLBACK STRATEGY
 * --------------------------------
 *   1. Hardware Init:
 *      AudioInit() attempts to initialize Raylib's audio device via InitAudioDevice().
 *      If initialization fails, the manager marks `ready = false` and all subsequent
 *      play requests become safe no-ops. The game continues silently without crashing.
 *
 *   2. Asset Loading:
 *      Loads optional WAV files from "assets/audio/". If any individual file is missing
 *      or corrupt, Raylib handles it safely, and IsSoundReady() checks prevent crashes.
 *
 *   3. Dynamic Modulation:
 *      Hit sounds modulate playback pitch proportionally to ball speed to convey impact
 *      energy (high speed = higher pitch and punch).
 *
 * DEPENDENCIES
 * ------------
 *   raylib.h  — Sound structure, InitAudioDevice, PlaySound, etc.
 *   stdbool.h — bool type
 *
 * =============================================================================
 */

#ifndef NTT_AUDIO_MGR_H
#define NTT_AUDIO_MGR_H

#include "raylib.h"
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * AudioMgr
 * ---------------------------------------------------------------------------
 * Holds the loaded sound resources and runtime state of the audio engine.
 * Typically stored as a field within the top-level GameEngine struct.
 * --------------------------------------------------------------------------- */
typedef struct AudioMgr {
    Sound hit;      /* Regular drive impact sound (assets/audio/paddle.wav)     */
    Sound bounce;   /* Table surface bounce sound (assets/audio/bounce.wav)     */
    Sound ui;       /* UI button click / navigation sound (assets/audio/ui.wav) */
    Sound smash;    /* Power smash impact sound (assets/audio/smash.wav)        */
    bool  ready;    /* True if audio device was successfully initialized        */
    float master;   /* Master volume multiplier [0.0f, 1.0f] (defaults to 0.7f) */
} AudioMgr;

/* ---------------------------------------------------------------------------
 * Subsystem Lifecycle Functions
 * --------------------------------------------------------------------------- */

/*
 * AudioInit
 * ---------
 * Initializes the Raylib audio device and attempts to load all game sound effects.
 *
 * @param a  Pointer to the AudioMgr structure to initialize.
 *
 * Side Effects:
 *   - Calls InitAudioDevice().
 *   - Attempts file I/O for each sound effect asset.
 *   - Sets a->ready to true on success, or false if audio hardware is unavailable.
 */
void AudioInit(AudioMgr *a);

/*
 * AudioFree
 * ---------
 * Unloads all loaded sound effects and closes the audio hardware device.
 * Safe to call even if AudioInit partially or completely failed.
 *
 * @param a  Pointer to the AudioMgr structure to tear down and zero out.
 */
void AudioFree(AudioMgr *a);

/* ---------------------------------------------------------------------------
 * Sound Playback Triggers
 * --------------------------------------------------------------------------- */

/*
 * AudioPlayHit
 * ------------
 * Plays the paddle-hit sound effect with dynamic pitch modulation based on
 * the impact velocity.
 *
 * Mathematical formula for pitch modulation:
 *   pitch = clamp(speed / 9.0f, 0.75f, 1.45f)
 *   - Low speed (~6.7 m/s)  --> pitch ≈ 0.75 (deeper thud)
 *   - Baseline (~9.0 m/s)   --> pitch = 1.00 (nominal recording frequency)
 *   - High speed (~13.0 m/s)--> pitch ≈ 1.45 (sharp, energetic crack)
 *
 * @param a      Pointer to the active audio manager.
 * @param smash  If true, plays the smash sound; otherwise plays standard hit.
 * @param speed  Magnitude of the ball's velocity vector at impact (m/s).
 */
void AudioPlayHit(AudioMgr *a, bool smash, float speed);

/*
 * AudioPlayBounce
 * ---------------
 * Plays the table bounce sound effect at nominal volume and default pitch.
 *
 * @param a  Pointer to the active audio manager.
 */
void AudioPlayBounce(AudioMgr *a);

/*
 * AudioPlayUI
 * -----------
 * Plays the short menu button click sound effect.
 *
 * @param a  Pointer to the active audio manager.
 */
void AudioPlayUI(AudioMgr *a);

#endif /* NTT_AUDIO_MGR_H */
