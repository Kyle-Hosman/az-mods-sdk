/* SPDX-License-Identifier: GPL-3.0-only */
/* Timing model for the synthetic harness provider, matching the core contract. */
#include <math.h>
#include "azmod-timestamps.h"
/* The synthetic stored grid follows the native ANLZ millisecond clock. */
/* No extrapolation beyond the captured grid; no constant-tempo or 4/4 assumption. */
static inline int ht_time_to_beat(const az_track_snapshot *s, double time, double *out) {
    if (!s || ht_record_size(s) < sizeof *s || s->version != AZ_DATA_VERSION || !out || !isfinite(time)) return AZ_INVALID;
    if (s->beat_count && !s->beats) return AZ_INVALID;
    if (s->grid_info.availability != AZ_AVAIL_READY || s->beat_count < 2) return AZ_UNSUPPORTED;
    const az_beat *b = s->beats;
    uint32_t lo = 0, hi = s->beat_count - 1;
    if (!isfinite(b[lo].time_s) || !isfinite(b[hi].time_s) || !(compare_timestamps(b[hi].time_s, b[lo].time_s, 1, 1000, 1) == AZ_TIME_AFTER)) return AZ_FAILED;
    if (compare_timestamps(time, b[lo].time_s, 1, 1000, 1) == AZ_TIME_BEFORE || compare_timestamps(time, b[hi].time_s, 1, 1000, 1) == AZ_TIME_AFTER) return AZ_NOT_FOUND;
    if (compare_timestamps(time, b[hi].time_s, 1, 1000, 1) == AZ_TIME_EQUAL) {
        *out = hi;
        return AZ_OK;
    }
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!isfinite(b[mid].time_s) || !(compare_timestamps(b[mid].time_s, b[lo].time_s, 1, 1000, 1) == AZ_TIME_AFTER) || !(compare_timestamps(b[mid].time_s, b[hi].time_s, 1, 1000, 1) == AZ_TIME_BEFORE)) return AZ_FAILED;
        if (compare_timestamps(b[mid].time_s, time, 1, 1000, 1) <= AZ_TIME_EQUAL) lo = mid;
        else hi = mid;
    }
    double span = b[hi].time_s - b[lo].time_s;
    if (!isfinite(span) || !(compare_timestamps(span, 0., 1, 1000, 1) == AZ_TIME_AFTER)) return AZ_FAILED;
    double value = lo + (time - b[lo].time_s) / span;
    if (!isfinite(value)) return AZ_FAILED;
    *out = value;
    return AZ_OK;
}
static inline int ht_beat_to_time(const az_track_snapshot *s, double beat, double *out) {
    if (!s || ht_record_size(s) < sizeof *s || s->version != AZ_DATA_VERSION || !out || !isfinite(beat)) return AZ_INVALID;
    if (s->beat_count && !s->beats) return AZ_INVALID;
    if (s->grid_info.availability != AZ_AVAIL_READY || s->beat_count < 2) return AZ_UNSUPPORTED;
    if (beat < 0 || beat > s->beat_count - 1) return AZ_NOT_FOUND;
    uint32_t i = (uint32_t) beat;
    uint32_t lo = i + 1 < s->beat_count ? i : i - 1;
    double first = s->beats[lo].time_s, last = s->beats[lo + 1].time_s;
    double span = last - first;
    if (!isfinite(first) || !isfinite(last) || !isfinite(span) || !(compare_timestamps(span, 0., 1, 1000, 1) == AZ_TIME_AFTER)) return AZ_FAILED;
    double value = i == lo ? first + (beat - i) * span : last;
    if (!isfinite(value)) return AZ_FAILED;
    *out = value;
    return AZ_OK;
}
static inline int ht_interval(const az_track_snapshot *s, double a, double b, double rate, az_time_interval *out) {
    if (s && (ht_record_size(s) < sizeof *s || s->version != AZ_DATA_VERSION)) return AZ_INVALID;
    if (!out || ht_record_size(out) < sizeof *out || !isfinite(a) || !isfinite(b) || compare_timestamps(a, 0., 1, 1000, 1) == AZ_TIME_BEFORE || compare_timestamps(b, a, 1, 1000, 1) == AZ_TIME_BEFORE || !isfinite(rate) || rate < 0) return AZ_INVALID;
    az_time_interval v = {0};
    v.size = out->size;
    v.version = AZ_DATA_VERSION;
    v.valid = AZ_INTERVAL_SOURCE;
    int source_order = compare_timestamps(b, a, 1, 1000, 1);
    v.source_s = source_order == AZ_TIME_EQUAL ? 0 : b - a;
    double x, y;
    if (ht_time_to_beat(s, a, &x) == AZ_OK && ht_time_to_beat(s, b, &y) == AZ_OK) {
        v.beats = source_order == AZ_TIME_EQUAL ? 0 : y - x;
        v.valid |= AZ_INTERVAL_BEATS;
    }
    double playback = rate > 0 ? v.source_s / rate : 0;
    /* FP_ZERO detects representation underflow after division; timestamp
     * equality and ordering were already decided by compare_timestamps. */
    if (rate > 0 && isfinite(playback) && (source_order == AZ_TIME_EQUAL || fpclassify(playback) != FP_ZERO)) {
        v.playback_rate = rate;
        v.playback_s = playback;
        v.valid |= AZ_INTERVAL_PLAYBACK;
    }
    memcpy(out, &v, sizeof v);
    return AZ_OK;
}
