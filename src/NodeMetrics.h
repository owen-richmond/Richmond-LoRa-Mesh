/**
 * @file NodeMetrics.h
 * @brief Node overload and congestion metrics for the fullsleep_sync mesh network.
 *
 * Accumulates statistics over configurable epochs and emits machine-readable
 * reports to Serial. All output lines begin with "METRIC " so they can be
 * extracted from noisy serial logs with a simple grep/Python script.
 *
 * == Metrics and their academic basis ==
 *
 *  QUEUE  — TX queue depth (peak + time-averaged sample)
 *    Primary congestion signal in queuing theory (M/M/1, Kleinrock 1974).
 *    When arrival rate > service rate the queue fills; peak depth > 50% of
 *    MESH_TX_QUEUE_SIZE is the congestion onset threshold used here.
 *
 *  DROPS  — forward-drop rate and RX-queue-drop rate
 *    Drop rate is the canonical overload indicator in Internet congestion
 *    control (Floyd & Jacobson, RED 1993).  Any drops in an epoch mean the
 *    node cannot drain its queue fast enough and is actively losing data.
 *
 *  CHANNEL — CAD channel-busy events per epoch
 *    Each CAD-busy event means the radio detected another transmitter on
 *    the shared channel before this node could send.  High rates ≈ channel
 *    saturation (Bor et al., "Do LoRa Low-Power WANs Scale?", 2016).
 *
 *  SYNC   — sleep-control retry rate
 *    Retries occur when the sync master cannot reach a follower; persistent
 *    retries indicate either link failure or channel congestion preventing
 *    control packets from getting through.
 *
 *  LINK   — per-epoch RSSI / SNR min / avg / max
 *    Separates congestion from poor link quality: low RSSI + high drops =
 *    link problem; good RSSI + high drops = congestion.
 *
 *  ENERGY — duty-cycle-based radio-on-time estimate
 *    Radio energy ∝ active fraction of time (Anastasi et al., 2009 WSN
 *    survey).  effectiveWindow / cycleMs gives the scheduled duty cycle;
 *    multiplied by epoch length gives estimated radio-on milliseconds.
 *
 *  OVERLOAD SCORE — composite 0-100 index
 *    Weighted combination of the above signals:
 *      30 pts  peak queue depth fraction   (primary congestion signal)
 *      25 pts  any forward drops           (definitive overload signal)
 *      20 pts  CAD busy events             (channel saturation)
 *      15 pts  sleep-control retries       (sync / link instability)
 *      10 pts  TX queue pressure > 50%     (leading-edge congestion)
 *
 * == Usage ==
 *
 *   In main.cpp:
 *     #ifdef MESH_METRICS_ENABLED
 *     #include "NodeMetrics.h"
 *     static NodeMetrics metrics;
 *     #endif
 *
 *   In loop():
 *     #ifdef MESH_METRICS_ENABLED
 *     metrics.tick(meshNode);
 *     #endif
 *
 *   After sending a packet from application code:
 *     #ifdef MESH_METRICS_ENABLED
 *     metrics.onAppTx();
 *     #endif
 *
 * == Build flags ==
 *   -DMESH_METRICS_ENABLED=1
 *   -DMETRICS_EPOCH_MS=60000          (default 60 s)
 *   -DMETRICS_QUEUE_SAMPLE_MS=500     (default 500 ms)
 */

#ifndef NODE_METRICS_H
#define NODE_METRICS_H

#ifdef MESH_METRICS_ENABLED

#include <Arduino.h>
#include "MeshNode.h"

// --- Compile-time tunables ---------------------------------------------------

#ifndef METRICS_EPOCH_MS
#define METRICS_EPOCH_MS 60000UL
#endif

#ifndef METRICS_QUEUE_SAMPLE_MS
#define METRICS_QUEUE_SAMPLE_MS 500UL
#endif

// Overload thresholds used in score and level string
#define METRICS_SCORE_LOW      25
#define METRICS_SCORE_MODERATE 50
#define METRICS_SCORE_HIGH     75

// -----------------------------------------------------------------------------

class NodeMetrics {
public:
    NodeMetrics() = default;

    /**
     * @brief Call once per main-loop iteration.
     *
     * Samples the TX queue depth at METRICS_QUEUE_SAMPLE_MS intervals and
     * fires an epoch report + reset when METRICS_EPOCH_MS has elapsed.
     * Also detects new packet receptions via lastProcessedPacketID changes.
     */
    void tick(MeshNode& node) {
        const uint32_t now = millis();

        // Lazy init: capture baseline counter values so deltas start at zero.
        if (!_initialised) {
            _epochStartMs  = now;
            _lastSampleMs  = now;
            _snapCounters(node);
            _initialised = true;
            return;
        }

        // --- Queue depth sampling ---
        if ((uint32_t)(now - _lastSampleMs) >= (uint32_t)METRICS_QUEUE_SAMPLE_MS) {
            _lastSampleMs = now;
            const uint8_t depth = node.getTxQueueDepth();
            _qSamples++;
            _qDepthSum += depth;
            if (depth > _qPeak) _qPeak = depth;
            if (depth > 0)      _qPressureSamples++;
        }

        // --- Detect new received packet (RSSI/SNR capture) ---
        {
            const uint16_t pid = node.getLastProcessedPacketID();
            if (pid != _lastSeenPacketId) {
                _lastSeenPacketId = pid;
                _recordRx(node.getLastRxRssi(), node.getLastRxSnr());
            }
        }

        // --- Epoch boundary ---
        if ((uint32_t)(now - _epochStartMs) >= (uint32_t)METRICS_EPOCH_MS) {
            _report(node, now);
            _resetEpoch(node, now);
        }
    }

    /**
     * @brief Record an application-originated TX (sensor or coordination packet).
     *        Call this immediately after meshNode.sendSensorPacket() /
     *        meshNode.sendCoordinationPacket() in main.cpp.
     */
    void onAppTx() { _appTxCount++; }

private:
    // ---- State ---------------------------------------------------------------

    bool     _initialised     = false;
    uint32_t _epochStartMs    = 0;
    uint32_t _epochNumber     = 0;
    uint32_t _lastSampleMs    = 0;
    uint16_t _lastSeenPacketId = 0;

    // Counter snapshots at epoch start (for per-epoch deltas)
    uint32_t _snapDupDrops  = 0;
    uint32_t _snapFwdQueued = 0;
    uint32_t _snapFwdDrops  = 0;
    uint32_t _snapCadBusy   = 0;
    uint32_t _snapScRetry   = 0;

    // Accumulated within epoch
    uint32_t _rxCount        = 0;
    int32_t  _rssiSum        = 0;
    int16_t  _rssiMin        = 0;
    int16_t  _rssiMax        = 0;
    int32_t  _snrSum         = 0;
    int8_t   _snrMin         = 0;
    int8_t   _snrMax         = 0;
    bool     _rxHasData      = false;

    uint32_t _appTxCount        = 0;

    // Queue depth sampling
    uint32_t _qSamples          = 0;
    uint32_t _qDepthSum         = 0;
    uint8_t  _qPeak             = 0;
    uint32_t _qPressureSamples  = 0;   // samples where depth > 0

    // ---- Helpers -------------------------------------------------------------

    void _snapCounters(MeshNode& node) {
        _snapDupDrops  = node.getDuplicateDropCount();
        _snapFwdQueued = node.getForwardQueuedCount();
        _snapFwdDrops  = node.getForwardDropCount();
        _snapCadBusy   = node.getCadBusyCount();
        _snapScRetry   = node.getSleepControlRetryCount();
    }

    void _recordRx(int16_t rssi, int8_t snr) {
        _rxCount++;
        _rssiSum += (int32_t)rssi;
        _snrSum  += (int32_t)snr;
        if (!_rxHasData) {
            _rssiMin = _rssiMax = rssi;
            _snrMin  = _snrMax  = snr;
            _rxHasData = true;
        } else {
            if (rssi < _rssiMin) _rssiMin = rssi;
            if (rssi > _rssiMax) _rssiMax = rssi;
            if (snr  < _snrMin)  _snrMin  = snr;
            if (snr  > _snrMax)  _snrMax  = snr;
        }
    }

    void _resetEpoch(MeshNode& node, uint32_t now) {
        _epochStartMs = now;
        _epochNumber++;
        _snapCounters(node);

        _rxCount       = 0;
        _rssiSum       = 0; _rssiMin = 0; _rssiMax = 0;
        _snrSum        = 0; _snrMin  = 0; _snrMax  = 0;
        _rxHasData     = false;

        _appTxCount       = 0;
        _qSamples         = 0;
        _qDepthSum        = 0;
        _qPeak            = 0;
        _qPressureSamples = 0;
    }

    // ---- Epoch report --------------------------------------------------------

    void _report(MeshNode& node, uint32_t now) {
        const uint32_t epochMs = (uint32_t)(now - _epochStartMs);

        // Per-epoch deltas
        const uint32_t dupDrops  = node.getDuplicateDropCount() - _snapDupDrops;
        const uint32_t fwdQueued = node.getForwardQueuedCount() - _snapFwdQueued;
        const uint32_t fwdDrops  = node.getForwardDropCount()   - _snapFwdDrops;
        const uint32_t cadBusy   = node.getCadBusyCount()       - _snapCadBusy;
        const uint32_t scRetry   = node.getSleepControlRetryCount() - _snapScRetry;

        // Queue stats
        const uint8_t qAvg =
            (_qSamples > 0) ? (uint8_t)(_qDepthSum / _qSamples) : 0;
        const uint8_t txPressurePct =
            (_qSamples > 0)
            ? (uint8_t)(_qPressureSamples * 100UL / _qSamples)
            : 0;

        // Link quality
        const int16_t rssiAvg =
            (_rxCount > 0) ? (int16_t)(_rssiSum / (int32_t)_rxCount) : 0;
        const int8_t snrAvg =
            (_rxCount > 0) ? (int8_t)(_snrSum / (int32_t)_rxCount) : 0;

        // Energy proxy — effective duty cycle from compile-time constants.
        // effectiveWindow = MESH_WAKE_WINDOW_MS + LORA_PREAMBLE_MARGIN_MS
        //                   + MESH_SYNC_COORD_BUFFER_MS
        // This matches exactly what effectiveWakeWindowMs() computes in MeshNode.
#if MESH_WAKE_CYCLE_MS > 0
        const uint32_t effWindowMs =
            (uint32_t)MESH_WAKE_WINDOW_MS +
            (uint32_t)LORA_PREAMBLE_MARGIN_MS +
            (uint32_t)MESH_SYNC_COORD_BUFFER_MS;
        const uint32_t cycleMs    = (uint32_t)MESH_WAKE_CYCLE_MS;
        const uint8_t  dutyCyclePct =
            (uint8_t)(effWindowMs * 100UL / cycleMs);
        // Estimate radio-on ms for the epoch
        const uint32_t fullCycles   = epochMs / cycleMs;
        const uint32_t partialMs    = epochMs % cycleMs;
        const uint32_t partialOn    =
            (partialMs < effWindowMs) ? partialMs : effWindowMs;
        const uint32_t radioOnEstMs = fullCycles * effWindowMs + partialOn;
#else
        // Always-awake build (TEST_MODE_AWAKE or no schedule)
        const uint8_t  dutyCyclePct  = 100;
        const uint32_t radioOnEstMs  = epochMs;
#endif

        // ---- Overload score (0-100) ----------------------------------------
        //
        //  30 pts  Peak queue depth fraction vs MESH_TX_QUEUE_SIZE
        //          (M/M/1 queue saturation — Kleinrock 1974)
        //  25 pts  Any forward drops in epoch
        //          (drop-rate signal — RED/WRED, Floyd & Jacobson 1993)
        //  20 pts  CAD channel-busy events (≥4 = full contribution)
        //          (channel utilization — Bor et al. 2016)
        //  15 pts  Sleep-control retries (≥4 = full contribution)
        //          (sync stability / link health indicator)
        //  10 pts  TX queue non-empty > 50% of sampled intervals
        //          (leading-edge congestion pressure)
        //
        const uint8_t maxQ = (uint8_t)MESH_TX_QUEUE_SIZE;
        uint32_t score = 0;

        // 30 pts: queue depth fraction
        score += (uint32_t)_qPeak * 30UL / (maxQ > 0 ? maxQ : 1);

        // 25 pts: forward drops (binary — any drop = overloaded)
        if (fwdDrops > 0) score += 25;

        // 20 pts: CAD busy (cap at 4 events = full saturation signal)
        score += (cadBusy >= 4) ? 20 : (cadBusy * 5);

        // 15 pts: sleep control retries (cap at 4)
        score += (scRetry >= 4) ? 15 : (scRetry * 4);

        // 10 pts: persistent TX pressure
        if (txPressurePct > 50) score += 10;

        if (score > 100) score = 100;

        const char* level =
            (score < METRICS_SCORE_LOW)      ? "LOW"      :
            (score < METRICS_SCORE_MODERATE) ? "MODERATE" :
            (score < METRICS_SCORE_HIGH)     ? "HIGH"     :
                                               "SATURATED";

        // ---- Serial output (grep-friendly, parseable) ----------------------
        // All lines prefixed with "METRIC " for easy extraction:
        //   grep "^METRIC " /dev/ttyUSB0 | python3 parse_metrics.py

        Serial.printf(
            "\nMETRIC epoch=%lu node=%u uptime_ms=%lu duration_ms=%lu\n",
            (unsigned long)(_epochNumber + 1),
            (unsigned)node.getDeviceId(),
            (unsigned long)now,
            (unsigned long)epochMs);

        Serial.printf(
            "METRIC rx=%lu app_tx=%lu dup_drops=%lu"
            " fwd_queued=%lu fwd_drops=%lu\n",
            (unsigned long)_rxCount,
            (unsigned long)_appTxCount,
            (unsigned long)dupDrops,
            (unsigned long)fwdQueued,
            (unsigned long)fwdDrops);

        Serial.printf(
            "METRIC queue_peak=%u queue_avg=%u tx_pressure_pct=%u\n",
            (unsigned)_qPeak,
            (unsigned)qAvg,
            (unsigned)txPressurePct);

        Serial.printf(
            "METRIC cad_busy=%lu sc_retries=%lu\n",
            (unsigned long)cadBusy,
            (unsigned long)scRetry);

        if (_rxHasData) {
            Serial.printf(
                "METRIC rssi_avg=%d rssi_min=%d rssi_max=%d"
                " snr_avg=%d snr_min=%d snr_max=%d\n",
                (int)rssiAvg, (int)_rssiMin, (int)_rssiMax,
                (int)snrAvg,  (int)_snrMin,  (int)_snrMax);
        } else {
            Serial.println("METRIC rssi=NA snr=NA");
        }

        Serial.printf(
            "METRIC duty_cycle_pct=%u radio_on_est_ms=%lu\n",
            (unsigned)dutyCyclePct,
            (unsigned long)radioOnEstMs);

        Serial.printf(
            "METRIC overload_score=%lu overload_level=%s\n",
            (unsigned long)score,
            level);
    }
};

#endif // MESH_METRICS_ENABLED
#endif // NODE_METRICS_H
