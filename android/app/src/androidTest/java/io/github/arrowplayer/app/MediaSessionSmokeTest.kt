// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Contract smoke for `androidx.media3.session.MediaSession` (REQ-OSI-040,
// REQ-UIX-027). Both REQs are surfaced by the same `MediaSession`
// instance: REQ-OSI-040 wants a `MediaLibrarySession` (subclass of
// `MediaSession`) hosted by a `MediaLibraryService` foreground service,
// and REQ-UIX-027 wants the session to back the `MediaStyle`
// notification. The sessionToken is what binds the two — a non-null
// token means the platform session machinery (the
// `androidx.media3.session.MediaSessionManager` + `MediaController`
// pair) can attach, and a null token is the precise failure mode that
// would silently leave a foreground service without a notification.
//
// The test deliberately does not depend on any `MediaLibraryService`
// class the service-code subagent is in flight to land (Task 11). The
// contract under test is the `MediaSession.Builder` + `sessionToken`
// surface itself — that is what the subagent's service will hand back
// to the rest of the app, and the failure mode this test exists to
// catch is "the service's session builds but the token is null",
// which is invisible until the notification tries to attach.
//
// We do not pull in ExoPlayer. `SimpleBasePlayer` from `media3-common`
// is the documented no-op `Player` for tests; it has the same
// `Player` interface shape the production code expects, without
// dragging in a decoder pipeline. The Context is the test runner's
// `Application` context, which the framework guarantees is non-null
// under the `AndroidJUnit4` runner.

package io.github.arrowplayer.app

import android.os.Looper
import androidx.media3.common.SimpleBasePlayer
import androidx.media3.session.MediaSession
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MediaSessionSmokeTest {
    private var session: MediaSession? = null

    @Test
    fun mediaSessionBuilder_producesNonNullSessionToken() {
        // SimpleBasePlayer is abstract and requires a `Looper`. The
        // main looper is what the production `MediaSession` runs
        // callbacks on, so passing it here keeps the test on the
        // same thread the real service uses — the alternative
        // (Looper.getMainLooper() under instrumentation, or a
        // hand-rolled looper) is more code for no observable
        // difference in this smoke test.
        //
        // `State` is a `protected static final class` nested inside
        // `SimpleBasePlayer`. The anonymous-subclass scope inherits
        // access to the protected nested class, so the unqualified
        // name resolves here. Referencing it as
        // `SimpleBasePlayer.State` from a top-level import would be
        // a visibility error in Kotlin (the nested class is
        // protected) — leaving the reference local to the subclass
        // body is the form the test's compile depends on.
        val player = object : SimpleBasePlayer(Looper.getMainLooper()) {
            override fun getState(): State = State.Builder().build()
        }
        val built = MediaSession.Builder(
            ApplicationProvider.getApplicationContext(),
            player,
        ).build()
        session = built

        // REQ-OSI-040 / REQ-UIX-027: the session token is the handle
        // the system uses to attach a controller (and, via the
        // service, the MediaStyle notification). A non-null token
        // means the binder and platform MediaSessionManager have
        // accepted the session; a null token is the failure mode
        // that would render a service that compiles but does nothing.
        assertNotNull(
            "MediaSession.sessionToken must be non-null — the service's " +
                "MediaStyle notification (REQ-UIX-027) and the Auto " +
                "controller attachment (REQ-AUT-002) both consume this token",
            built.sessionToken,
        )
    }

    @After
    fun releaseSession() {
        // REQ-OSI-040: "the service MUST stop when playback ends and
        // the notification is dismissed, and MUST NOT be killed while
        // playing". The release() call is the analogue at the
        // session level: until it runs, the underlying platform
        // MediaSession holds a binder reference. Leaving it alive
        // across tests is the kind of leak that makes
        // connectedAndroidTest flake on the next run.
        session?.release()
        session = null
    }
}
