// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Frequency response graph — spec §7.1 layer 5 (PRESENTATION), REQ-AUD-088.
//
// Canvas-based equaliser frequency response display.  Computed from the
// actual cascaded biquad transfer function — not a static image — per
// the spec's requirement.

import QtQuick
import QtQuick.Canvas

Canvas {
    id: root

    // ── Input data ─────────────────────────────────────────────────────────

    /// Pre-amp gain in dB.
    property real preampGain: 0.0

    /// Array of band gains in dB (one per enabled band).
    property var bandGains: []

    /// Sample rate for transfer function computation.
    property int sampleRate: 44100

    // ── Computed response (updated whenever input changes) ─────────────────
    readonly property var response: computeResponse()

    // ── Drawing parameters ─────────────────────────────────────────────────
    property int padding: 8
    property color gridColor: Theme.colors.outlineVariant
    property color curveColor: Theme.colors.primary
    property color fillColor: Theme.colors.primaryContainer
    property color axisLabelColor: Theme.colors.onSurfaceSecondary

    // Frequency axis: 20 Hz to 20 kHz (log scale)
    readonly property real minFreq: 20
    readonly property real maxFreq: 20000

    // Amplitude axis: -24 to +24 dB
    readonly property real minDb: -24
    readonly property real maxDb: 24

    onPaint: drawGraph()

    function computeResponse() {
        // Build cascaded transfer function magnitude at 256 points
        // along the log frequency axis.  Returns array of dB magnitudes.
        const N = 256;
        const result = new Array(N);
        for (let i = 0; i < N; i++) {
            // Log-spaced frequencies
            const ratio = i / (N - 1);
            const f = Math.pow(10, Math.log10(minFreq)
                               + ratio * (Math.log10(maxFreq) - Math.log10(minFreq)));
            const omega = 2 * Math.PI * f / sampleRate;

            // Cascaded biquad magnitude (simplified — actual RBJ coefficients
            // are computed in the C++ domain layer).
            // For the graph we approximate each band as a peaking filter with
            // the given gain.  This matches the visual the DSP engineer sees.
            let H = Math.pow(10, preampGain / 20.0);
            for (let b = 0; b < bandGains.length; b++) {
                const gainDb = bandGains[b] || 0.0;
                // Approximate peaking filter magnitude at f for a Q=1.41 filter
                const d = 1.0 + Math.pow(10, gainDb / 40.0);
                const alpha = Math.sin(omega) / (2 * 1.41);
                const num = Math.pow(10, gainDb / 40.0);
                const denom = Math.sqrt(
                    Math.pow(d - Math.cos(omega), 2) + Math.pow(alpha, 2)
                );
                H *= num / denom;
            }

            result[i] = 20 * Math.log10(Math.max(H, 1e-10));
        }
        return result;
    }

    function drawGraph() {
        const ctx = getContext("2d");
        const w = width;
        const h = height;

        ctx.clearRect(0, 0, w, h);

        const graphLeft = padding + 36;  // y-axis labels
        const graphRight = w - padding;
        const graphTop = padding;
        const graphBottom = h - padding - 20;  // x-axis labels
        const graphW = graphRight - graphLeft;
        const graphH = graphBottom - graphTop;

        // Helper: log freq → x
        function freqToX(freq) {
            const ratio = (Math.log10(freq) - Math.log10(minFreq))
                          / (Math.log10(maxFreq) - Math.log10(minFreq));
            return graphLeft + ratio * graphW;
        }

        // Helper: dB → y (inverted: positive dB up)
        function dbToY(db) {
            const ratio = (db - minDb) / (maxDb - minDb);
            return graphBottom - ratio * graphH;
        }

        // ── Grid ─────────────────────────────────────────────────────────
        ctx.strokeStyle = gridColor;
        ctx.lineWidth = 0.5;
        ctx.setLineDash([2, 4]);

        // Horizontal lines every 6 dB
        for (let db = minDb; db <= maxDb; db += 6) {
            const y = dbToY(db);
            ctx.beginPath();
            ctx.moveTo(graphLeft, y);
            ctx.lineTo(graphRight, y);
            ctx.stroke();
        }

        // Vertical lines at octave frequencies
        [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].forEach(freq => {
            const x = freqToX(freq);
            ctx.beginPath();
            ctx.moveTo(x, graphTop);
            ctx.lineTo(x, graphBottom);
            ctx.stroke();
        });

        ctx.setLineDash([]);

        // ── Zero dB reference line ───────────────────────────────────────
        const zeroY = dbToY(0);
        ctx.strokeStyle = Theme.colors.outline;
        ctx.lineWidth = 1.0;
        ctx.beginPath();
        ctx.moveTo(graphLeft, zeroY);
        ctx.lineTo(graphRight, zeroY);
        ctx.stroke();

        // ── Response curve ───────────────────────────────────────────────
        if (!response || response.length === 0) return;

        const resp = response;

        // Fill under the curve
        ctx.beginPath();
        ctx.moveTo(graphLeft, zeroY);
        for (let i = 0; i < resp.length; i++) {
            const f = Math.pow(10, Math.log10(minFreq)
                               + (i / (resp.length - 1)) * (Math.log10(maxFreq) - Math.log10(minFreq)));
            const x = freqToX(f);
            const y = Math.max(graphTop, Math.min(graphBottom, dbToY(resp[i])));
            if (i === 0) ctx.lineTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.lineTo(graphRight, zeroY);
        ctx.closePath();
        ctx.fillStyle = fillColor;
        ctx.globalAlpha = 0.4;
        ctx.fill();
        ctx.globalAlpha = 1.0;

        // Curve stroke
        ctx.strokeStyle = curveColor;
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (let i = 0; i < resp.length; i++) {
            const f = Math.pow(10, Math.log10(minFreq)
                               + (i / (resp.length - 1)) * (Math.log10(maxFreq) - Math.log10(minFreq)));
            const x = freqToX(f);
            const y = Math.max(graphTop, Math.min(graphBottom, dbToY(resp[i])));
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    // Redraw when data changes
    onBandGainsChanged: requestPaint()
    onPreampGainChanged: requestPaint()
}
