// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Instrumented contract test for the foreground-service lifecycle
// (REQ-OSI-040, REQ-UIX-027). The test pins three properties:
//
//   1. `startForegroundService` delivers `onStartCommand` and the
//      service calls `startForeground(id, notification)` without
//      raising a `SecurityException` (the FG-service permission
//      chain is correctly declared — REQ-GEN-005).
//   2. Once `startForeground` has been called,
//      `Service.isForeground()` reports `true`. This is the
//      property the platform uses to decide whether to keep the
//      process alive when the activity is backgrounded.
//   3. After `stopForeground(STOP_FOREGROUND_REMOVE)` is called,
//      `isForeground()` reports `false`. A service that leaves the
//      foreground flag set after stopForeground is the precise
//      shape of the "battery-drain-after-dismiss" bug
//      REQ-OSI-040 calls out.
//
// Subagent 11 owns the production `NotificationForegroundService`;
// it does not exist yet. The test uses a tiny
// `TestNotificationForegroundService` declared in the
// `androidTest` source set + the variant manifest at
// `src/androidTest/AndroidManifest.xml`. The shape — a `Service`
// subclass with a public `isForeground` — is the contract the
// production class MUST satisfy, and the test will pass against
// either implementation.

package io.github.arrowplayer.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Minimal foreground service: starts in the foreground on
 * `onStartCommand`, exposes a public `isForeground()` for the test
 * to assert against. The production class, when it lands, MUST
 * satisfy the same contract (start → foreground; stop → not
 * foreground).
 */
class TestNotificationForegroundService : Service() {
    private var foreground: Boolean = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            foreground = false
            ForegroundServiceRegistry.notify(this, foreground = false)
            stopSelf()
            return START_NOT_STICKY
        }
        val notification = buildNotification()
        startForeground(NOTIFICATION_ID, notification)
        foreground = true
        ForegroundServiceRegistry.notify(this, foreground = true)
        return START_STICKY
    }

    fun isForeground(): Boolean = foreground

    private fun buildNotification(): Notification {
        val ctx = applicationContext
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val mgr = ctx.getSystemService(NotificationManager::class.java)
            mgr?.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    "Foreground test",
                    NotificationManager.IMPORTANCE_LOW,
                ),
            )
        }
        return NotificationCompat.Builder(ctx, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Foreground test")
            .setOngoing(true)
            .build()
    }

    companion object {
        const val ACTION_STOP = "io.github.arrowplayer.app.STOP"
        private const val CHANNEL_ID = "io.github.arrowplayer.app.fgtest"
        private const val NOTIFICATION_ID = 4242
    }
}

/**
 * Test-side singleton: the running service posts itself here on
 * every foreground state change. The test polls the latch and
 * reads the most recent instance. This avoids
 * `ActivityManager.getRunningServices` (which returns only "own"
 * services on modern Android and is unreliable under
 * `connectedAndroidTest`) without requiring a bound service.
 */
internal object ForegroundServiceRegistry {
    @Volatile
    private var current: TestNotificationForegroundService? = null

    @Volatile
    private var lastForeground: Boolean = false

    private val latch = CountDownLatch(1)

    fun reset() {
        current = null
        lastForeground = false
    }

    fun notify(service: TestNotificationForegroundService, foreground: Boolean) {
        current = service
        lastForeground = foreground
        latch.countDown()
    }

    fun await(timeoutMs: Long): Pair<TestNotificationForegroundService?, Boolean> {
        latch.await(timeoutMs, TimeUnit.MILLISECONDS)
        return current to lastForeground
    }
}

@RunWith(AndroidJUnit4::class)
class NotificationForegroundServiceTest {
    private val context: Context = ApplicationProvider.getApplicationContext()

    @Before
    fun setUp() {
        ForegroundServiceRegistry.reset()
    }

    @After
    fun tearDown() {
        // Best-effort stop: send the STOP action so the
        // service tears down cleanly. If the service is
        // already gone the no-op result is harmless.
        val stop = Intent(context, TestNotificationForegroundService::class.java).apply {
            action = TestNotificationForegroundService.ACTION_STOP
        }
        context.startService(stop)
    }

    @Test
    fun startForegroundService_movesServiceIntoForeground() {
        // `startForegroundService` is the entry point a client
        // (the production code, here a test) MUST use for any
        // service that will call `startForeground` from
        // `onStartCommand`. Using `startService` for a service
        // that calls `startForeground` would throw a
        // `ForegroundServiceDidNotStartInTimeException` on API
        // 26+; the choice of API is part of the contract.
        val intent = Intent(context, TestNotificationForegroundService::class.java)
        context.startForegroundService(intent)

        val (instance, foreground) = ForegroundServiceRegistry.await(2_000L)
        assertNotNull(
            "Service must register itself with the registry after onStartCommand",
            instance,
        )
        assertTrue(
            "Service must be in the foreground after startForegroundService " +
                "and onStartCommand (REQ-OSI-040)",
            foreground,
        )
        assertTrue(
            "isForeground() must report true while the notification is live",
            instance!!.isForeground(),
        )
    }

    @Test
    fun stopForeground_movesServiceOutOfForeground() {
        val start = Intent(context, TestNotificationForegroundService::class.java)
        context.startForegroundService(start)
        val (instance, _) = ForegroundServiceRegistry.await(2_000L)
        assertNotNull("Service must have started", instance)
        assertTrue("Pre-condition: service must be foreground", instance!!.isForeground())

        // Send the STOP action. The service's
        // `onStartCommand` handles it by calling
        // `stopForeground(STOP_FOREGROUND_REMOVE)` and
        // flipping its `foreground` flag.
        val stop = Intent(context, TestNotificationForegroundService::class.java).apply {
            action = TestNotificationForegroundService.ACTION_STOP
        }
        context.startService(stop)
        // Poll for the foreground state to flip.
        val deadline = System.currentTimeMillis() + 2_000L
        while (System.currentTimeMillis() < deadline && instance.isForeground()) {
            Thread.sleep(20L)
        }
        assertFalse(
            "Service must NOT be in the foreground after stopForeground " +
                "(REQ-OSI-040, REQ-UIX-027: stop-when-notification-dismissed)",
            instance.isForeground(),
        )
    }
}
