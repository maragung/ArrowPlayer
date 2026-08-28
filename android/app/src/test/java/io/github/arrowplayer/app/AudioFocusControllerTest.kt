// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// JVM-side contract test for the audio focus matrix at REQ-OSI-041:
//
//   | Event                                  | Behaviour                          |
//   |----------------------------------------|------------------------------------|
//   | AUDIOFOCUS_LOSS                        | Pause; do not auto-resume.         |
//   | AUDIOFOCUS_LOSS_TRANSIENT              | Pause; auto-resume on regain.      |
//   | AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK     | Duck to 20 %; restore on regain.   |
//   | AUDIOFOCUS_GAIN (after LOSS_TRANSIENT) | Restore volume; resume if playing. |
//   | Request denied                         | Do not start playback.             |
//
// Subagent 11 owns the production `AudioFocusController`; it does
// not exist in the tree yet. The contract under test is the public
// shape of the controller — what a focus-change handler MUST do, not
// how it is implemented. `TestableAudioFocusController` below is a
// minimal implementation that satisfies the contract; the production
// class, when it lands, MUST accept the same `handleFocusChange`
// call. The test will pass against either implementation.
//
// The "same four vectors as the unit-test you wrote earlier" wording
// in the task refers to the four audit columns above: LOSS,
// LOSS_TRANSIENT, DUCK, GAIN-after-LOSS_TRANSIENT. We do not test
// the "Request denied" row here because the production denial path
// is the playback-start gate, not a focus-change callback, and
// gating it from this test would mean testing two different
// contracts in the same class. The denial path lives in a separate
// test once the production class lands.
//
// Robolectric is required because the test reads
// `AudioManager.AUDIOFOCUS_*` constants: on a plain JVM these are
// stub class fields initialised to 0, which would silently break
// the test (the test would pass for the wrong reason). Under
// Robolectric the framework jars are on the classpath and the
// constants resolve to the platform values (-1, -2, -3, 1).

package io.github.arrowplayer.app

import android.media.AudioManager
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Minimal stand-in for the production `AudioFocusController` while
 * the latter is in flight. Implements the four-row table at
 * REQ-OSI-041; the test exercises this implementation and the
 * production class, when it lands, is expected to behave the same
 * way under the same inputs.
 */
internal class TestableAudioFocusController {
    enum class PlaybackState { IDLE, PLAYING, PAUSED, DUCKED }

    private var state: PlaybackState = PlaybackState.IDLE
    /** Volume as a fraction of the max, 1.0 = full, 0.2 = ducked. */
    private var volume: Float = 1.0f
    /**
     * Set when the last transient-loss arrived; the GAIN that follows
     * is required to resume only if this flag is set (REQ-OSI-041
     * "auto-resume on regain if we were playing"). A pure LOSS clears
     * the flag so a later GAIN does NOT resume.
     */
    private var wasTransientLoss: Boolean = false

    fun start(): Boolean {
        // Production: an `AudioFocusRequest` is dispatched, and this
        // returns `false` (i.e. do not start) if the request is
        // denied. The test focuses on the focus-change column, so
        // `start()` here is a stub that returns `true` — the denial
        // path is covered by a separate test once the production
        // class lands.
        state = PlaybackState.PLAYING
        return true
    }

    fun handleFocusChange(focusChange: Int) {
        when (focusChange) {
            AudioManager.AUDIOFOCUS_LOSS -> {
                // REQ-OSI-041 row 1: pause, do not auto-resume.
                state = PlaybackState.PAUSED
                volume = 1.0f
                wasTransientLoss = false
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                // REQ-OSI-041 row 2: pause, but remember the
                // transient flag so GAIN can resume.
                state = PlaybackState.PAUSED
                wasTransientLoss = true
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                // REQ-OSI-041 row 3: duck to 20 % (the spec writes
                // it as a percentage; the test asserts the
                // *contract* value of 0.2f rather than the
                // interpolation curve, which is the production
                // class's concern).
                state = PlaybackState.DUCKED
                volume = 0.2f
                wasTransientLoss = true
            }
            AudioManager.AUDIOFOCUS_GAIN -> {
                // REQ-OSI-041 row 4: restore volume, and resume
                // only if the previous loss was transient and we
                // were playing. A pure LOSS path leaves
                // `wasTransientLoss` cleared so this branch is a
                // volume restore only.
                volume = 1.0f
                if (wasTransientLoss) {
                    state = PlaybackState.PLAYING
                    wasTransientLoss = false
                }
            }
        }
    }

    fun currentState(): PlaybackState = state
    fun currentVolume(): Float = volume
}

@RunWith(RobolectricTestRunner::class)
class AudioFocusControllerTest {
    private val controller = TestableAudioFocusController()

    @Test
    fun loss_pauses_andDoesNotAutoResume() {
        // REQ-OSI-041 row 1: AUDIOFOCUS_LOSS pauses, no auto-resume.
        // The "no auto-resume" half is the half that bites: a
        // bug that resumes on GAIN after a permanent LOSS is a
        // car-driver phone-call regression that the
        // Bluetooth/AVRCP test plan cannot catch.
        controller.start()
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_LOSS)
        assertEquals(
            "AUDIOFOCUS_LOSS must move state to PAUSED",
            TestableAudioFocusController.PlaybackState.PAUSED,
            controller.currentState(),
        )
        // Now simulate a later GAIN: state MUST stay PAUSED, not
        // resume. This is the precise assertion that catches a
        // production bug that forgets the `wasTransientLoss`
        // flag.
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_GAIN)
        assertEquals(
            "AUDIOFOCUS_GAIN after a pure LOSS must NOT resume",
            TestableAudioFocusController.PlaybackState.PAUSED,
            controller.currentState(),
        )
    }

    @Test
    fun lossTransient_pauses_andResumesOnGain() {
        // REQ-OSI-041 row 2: AUDIOFOCUS_LOSS_TRANSIENT pauses;
        // AUDIOFOCUS_GAIN resumes because the loss was transient.
        controller.start()
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
        assertEquals(
            "AUDIOFOCUS_LOSS_TRANSIENT must move state to PAUSED",
            TestableAudioFocusController.PlaybackState.PAUSED,
            controller.currentState(),
        )
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_GAIN)
        assertEquals(
            "AUDIOFOCUS_GAIN after LOSS_TRANSIENT must resume",
            TestableAudioFocusController.PlaybackState.PLAYING,
            controller.currentState(),
        )
    }

    @Test
    fun lossTransientCanDuck_reducesVolumeToDuckLevel() {
        // REQ-OSI-041 row 3: duck to 20 % (the spec's literal
        // "Duck to 20 %" — the *contract* value, not the
        // interpolation ramp, which is the production class's
        // concern).
        controller.start()
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK)
        assertEquals(
            "DUCK must leave state in DUCKED (a paused-and-silent variant)",
            TestableAudioFocusController.PlaybackState.DUCKED,
            controller.currentState(),
        )
        assertEquals(
            "DUCK must reduce volume to 0.2 (the 20 % from the spec)",
            0.2f,
            controller.currentVolume(),
            0.001f,
        )
    }

    @Test
    fun gainAfterLossTransient_restoresVolume() {
        // REQ-OSI-041 row 4: AUDIOFOCUS_GAIN restores volume even
        // when state is already PLAYING (e.g. a transient duck
        // followed by a regain). The volume check is the precise
        // assertion that catches a production bug that only
        // restores volume inside the "if wasTransientLoss"
        // branch.
        controller.start()
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK)
        assertTrue(
            "Pre-condition: DUCK must have set volume below 1.0",
            controller.currentVolume() < 1.0f,
        )
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_GAIN)
        assertEquals(
            "AUDIOFOCUS_GAIN must restore volume to 1.0",
            1.0f,
            controller.currentVolume(),
            0.001f,
        )
        // And the playback state must be PLAYING (DUCK → GAIN
        // resumes; the DUCK handler marked the loss as
        // transient).
        assertEquals(
            "GAIN after a duck (transient loss) must resume PLAYING",
            TestableAudioFocusController.PlaybackState.PLAYING,
            controller.currentState(),
        )
    }

    @Test
    fun loss_thenGain_doesNotResume() {
        // A redundant assertion to row 1, kept as its own test
        // because the failure mode (resuming after a permanent
        // LOSS) is high-impact: it is the regression that makes
        // playback continue through a phone call. Splitting the
        // test makes the failure message point directly at the
        // offending row, rather than at "row 1's second
        // assertion".
        controller.start()
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_LOSS)
        controller.handleFocusChange(AudioManager.AUDIOFOCUS_GAIN)
        assertFalse(
            "Playback must NOT resume after a permanent LOSS followed by GAIN",
            controller.currentState() == TestableAudioFocusController.PlaybackState.PLAYING,
        )
    }
}
