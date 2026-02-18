// TargetTracker.qml
// Pure-logic helper for clustering noisy bearings into stable tracks with fade/TTL.
// Qt 6 / QML (no visuals)

import QtQuick

QtObject {
    id: tracker

    // --- Inputs / tuning ----------------------------------------------------
    // Max number of tracks returned (sorted by freshness, then score)
    property int maxTargets: 15

    // Degrees: if a new bearing is within this distance (circular), it updates an existing track
    property real matchThresholdDeg: 4.0

    // Remove track if it wasn't seen for this long
    property int ttlMs: 12000

    // Visual fade time (used for alpha calculation)
    property int fadeMs: 8000

    // Exponential decay factor for "score" each ingest call
    property real scoreDecayPerIngest: 0.98

    // How strongly a track azimuth moves toward a matched measurement (0..1)
    property real azimuthLerpK: 0.25

    // Current time (ms since epoch). You typically set this from a UI timer.
    property real nowMs: 0

    // Optional: if true, keeps tracks array stable by mutating existing objects when possible
    // (Helpful for bindings/repeaters; default true.)
    property bool stableObjects: true

    // --- State --------------------------------------------------------------
    // Public tracks for drawing:
    // [{ az: Number(0..360), lastSeen: ms, score: Number }]
    property var tracks: []

    // --- Public API ---------------------------------------------------------
    // Feed new raw bearings (array of degrees).
    function ingest(bearingsDeg) {
        if (!bearingsDeg || bearingsDeg.length === 0) {
            // Still decay + prune to keep fade/ttl working
            _decayScores()
            prune()
            return
        }

        _decayScores()

        var now = nowMs > 0 ? nowMs : Date.now()

        for (var i = 0; i < bearingsDeg.length; i++) {
            var meas = _norm360(bearingsDeg[i])

            var bestIdx = -1
            var bestD = 1e9

            for (var j = 0; j < tracks.length; j++) {
                var d = _wrapDiff(tracks[j].az, meas)
                if (d < bestD) { bestD = d; bestIdx = j }
            }

            if (bestIdx >= 0 && bestD <= matchThresholdDeg) {
                // Update existing track
                var tr = tracks[bestIdx]
                tr.az = _circularLerp(tr.az, meas, azimuthLerpK)
                tr.lastSeen = now
                tr.score = (tr.score || 0) + 1
            } else {
                // Create new track
                tracks.push({ az: meas, lastSeen: now, score: 1 })
            }
        }

        prune()
    }

    // Remove stale tracks, sort and cap to maxTargets
    function prune() {
        var now = nowMs > 0 ? nowMs : Date.now()

        // Remove stale
        var kept = []
        for (var i = 0; i < tracks.length; i++) {
            var tr = tracks[i]
            if ((now - tr.lastSeen) <= ttlMs)
                kept.push(tr)
        }

        // Sort: freshest first, then score desc
        kept.sort(function(a, b) {
            var aAge = now - a.lastSeen
            var bAge = now - b.lastSeen
            if (aAge !== bAge) return aAge - bAge
            return (b.score || 0) - (a.score || 0)
        })

        // Cap
        if (kept.length > maxTargets)
            kept = kept.slice(0, maxTargets)

        if (stableObjects) {
            // Mutate existing array in-place to reduce churn for Repeaters
            tracks = kept
        } else {
            tracks = kept
        }
    }

    // Returns age in ms for a track object (safe for missing fields)
    function ageMs(track) {
        var now = nowMs > 0 ? nowMs : Date.now()
        if (!track || track.lastSeen === undefined) return 1e12
        return Math.max(0, now - track.lastSeen)
    }

    // Returns alpha (0..1) for a track based on exponential fade
    function alpha(track) {
        var aMs = ageMs(track)
        if (fadeMs <= 0) return 1.0
        // tau ~ fade/3 gives nice drop: exp(-3) ~ 0.05 at fadeMs
        var tau = Math.max(1.0, fadeMs / 3.0)
        var v = Math.exp(-aMs / tau)
        // Clamp
        if (v < 0) return 0
        if (v > 1) return 1
        return v
    }

    // Convenience: most fresh track (or null)
    function freshest() {
        if (!tracks || tracks.length === 0) return null
        return tracks[0]
    }

    // Clear all tracks
    function clear() {
        tracks = []
    }

    // --- Internal helpers ---------------------------------------------------
    function _decayScores() {
        if (!tracks || tracks.length === 0) return
        var k = scoreDecayPerIngest
        for (var i = 0; i < tracks.length; i++) {
            tracks[i].score = (tracks[i].score || 0) * k
        }
    }

    function _norm360(deg) {
        var a = deg % 360.0
        if (a < 0) a += 360.0
        return a
    }

    // signed delta in [-180, 180]
    function _deltaDeg(fromDeg, toDeg) {
        var d = _norm360(toDeg) - _norm360(fromDeg)
        if (d > 180) d -= 360
        else if (d < -180) d += 360
        return d
    }

    // shortest circular distance in [0..180]
    function _wrapDiff(a, b) {
        var d = Math.abs(_norm360(a) - _norm360(b))
        return Math.min(d, 360 - d)
    }

    // circular lerp from -> to by k (0..1)
    function _circularLerp(fromDeg, toDeg, k) {
        var cur = _norm360(fromDeg)
        var d = _deltaDeg(cur, toDeg)   // signed [-180..180]
        return _norm360(cur + d * k)
    }
}
