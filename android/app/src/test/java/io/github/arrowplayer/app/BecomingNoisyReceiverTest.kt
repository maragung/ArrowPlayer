// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// JVM-side contract test for the becoming-noisy path at REQ-OSI-042:
// the system broadcasts `ACTION_AUDIO_BECOMING_NOISY` when headphones
// are unplugged (or, in test, when the user switches to a Bluetooth
// output that the platform considers noisy). The receiver MUST pause
// playback — a missing pause is the precise failure mode that turns
// "I unplugged my headphones" into "everyone in the train now hears
// my music", which is the kind of regression that no user reports
// as a bug, only as a complaint.
//
// The test uses Robolectric's `Shadows.shadowOf(context)` API to
// dispatch `AUDIO_BECOMING_NOISY` synchronously — the shadow
// implementation runs `onReceive` on the calling thread, which is
// what makes the assertion deterministic. On a real device the
// broadcast is delivered on the main thread asynchronously, and
// `ShadowLooper.idleMainLooper()` would be needed to drain the
// queue; under Robolectric the synchronous dispatch is the
// documented contract.

package io.github.arrowplayer.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

/**
 * Test-double for the production `BecomingNoisyReceiver`. The
 * production class — when it lands — MUST do the same thing this
 * stub does on receipt of `AUDIO_BECOMING_NOISY`: pause whatever
 * playback is in flight. The shape of the contract is "register
 * a receiver, observe a flag flip on dispatch".
 */
internal class TestableBecomingNoisyReceiver(
    private val context: Context,
    private val onPause: () -> Unit,
) {
    private var registered: BroadcastReceiver? = null

    fun register() {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                if (intent.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) {
                    // REQ-OSI-042: pause playback. The
                    // production class is expected to call into
                    // the player; this stub calls the test's
                    // pause hook so the assertion has a
                    // surface.
                    onPause()
                }
            }
        }
        context.registerReceiver(
            receiver,
            android.content.IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY),
        )
        registered = receiver
    }

    fun unregister() {
        registered?.let { context.unregisterReceiver(it) }
        registered = null
    }
}

@RunWith(RobolectricTestRunner::class)
class BecomingNoisyReceiverTest {
    private val context: Context = RuntimeEnvironment.getApplication()

    @Test
    fun audioBecomingNoisy_pausesPlayback() {
        // REQ-OSI-042: the broadcast MUST pause playback. The
        // boolean is a stand-in for the real "is the player
        // playing?" — the production receiver checks a similar
        // gate before calling `player.pause()`, so the test
        // shape matches: a "did we pause?" boolean, observed
        // before and after the dispatch.
        var paused = false
        val receiver = TestableBecomingNoisyReceiver(context) { paused = true }

        receiver.register()
        try {
            // Dispatch the broadcast via the Robolectric shadow.
            // `shadowOf(context)` returns a `ShadowApplication`
            // that exposes the registered receivers and lets
            // tests fire intents synchronously. The dispatch
            // runs `onReceive` on the calling thread; no
            // `idleMainLooper` is needed.
            val intent = Intent(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
            shadowOf(context).sendBroadcast(intent)
        } finally {
            receiver.unregister()
        }

        assertEquals(
            "ACTION_AUDIO_BECOMING_NOISY must pause playback (REQ-OSI-042)",
            true,
            paused,
        )
    }

    @Test
    fun unrelatedBroadcast_doesNotPausePlayback() {
        // A negative-path assertion: a receiver that pauses on
        // every broadcast — including unrelated ones — would
        // pass the positive test but would be broken in
        // production (e.g. a clock-tick broadcast would pause
        // playback). Dispatching an arbitrary other action
        // confirms the receiver filters on intent.action.
        var paused = false
        val receiver = TestableBecomingNoisyReceiver(context) { paused = true }
        receiver.register()
        try {
            shadowOf(context).sendBroadcast(Intent("io.github.arrowplayer.app.UNRELATED"))
        } finally {
            receiver.unregister()
        }

        assertEquals(
            "Receiver must only react to ACTION_AUDIO_BECOMING_NOISY",
            false,
            paused,
        )
    }
}
