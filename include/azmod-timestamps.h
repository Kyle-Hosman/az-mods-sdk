/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZTIMESTAMP_H
#define AZTIMESTAMP_H
#include <stdint.h>

enum az_timestamp_order {
    AZ_TIME_BEFORE = -1,
    AZ_TIME_EQUAL = 0,
    AZ_TIME_AFTER = 1,
    AZ_TIME_INVALID = 2
};

/* Both timestamps use units_per_second ticks/second in the same clock domain.
 * The observation window is window_samples/sample_rate seconds. Equality is
 * symmetric and strictly inside that window, including across bucket edges.
 * Missing clocks/identities remain the caller's validity checks. Approximate
 * equality is not transitive: this comparator is not a sorting predicate. */
static inline int az_compare_integer_timestamps(uint64_t first, uint64_t second,
                                     uint64_t units_per_second,
                                     uint64_t sample_rate,
                                     uint64_t window_samples) {
    if (!units_per_second || !sample_rate || !window_samples) return AZ_TIME_INVALID;
    uint64_t difference = first > second ? first - second : second - first;
    if ((__uint128_t) difference * sample_rate < (__uint128_t) window_samples * units_per_second)
        return AZ_TIME_EQUAL;
    return first < second ? AZ_TIME_BEFORE : AZ_TIME_AFTER;
}

/* Floating timestamps (normally seconds) keep their precision through the
 * difference calculation; integer clocks use the exact wide-integer path. */
static inline int az_compare_floating_timestamps(long double first, long double second,
                                                uint64_t units_per_second,
                                                uint64_t sample_rate,
                                                uint64_t window_samples) {
    if (!units_per_second || !sample_rate || !window_samples ||
        !__builtin_isfinite(first) || !__builtin_isfinite(second)) return AZ_TIME_INVALID;
    long double difference = __builtin_fabsl(first - second);
    if (difference * sample_rate < (long double) window_samples * units_per_second)
        return AZ_TIME_EQUAL;
    return first < second ? AZ_TIME_BEFORE : AZ_TIME_AFTER;
}

#ifdef __cplusplus
#include <type_traits>
/* Source/cache clock conversion can produce wider intermediate ticks. Avoid
 * multiplying an already-wide delta past 128 bits; an overflowing product is
 * necessarily outside the at-most-128-bit observation window. */
static inline int compare_timestamps(__uint128_t first, __uint128_t second,
                                     uint64_t units_per_second, uint64_t sample_rate,
                                     uint64_t window_samples) {
    if (!units_per_second || !sample_rate || !window_samples) return AZ_TIME_INVALID;
    __uint128_t difference = first > second ? first - second : second - first;
    if (difference <= ~__uint128_t(0) / sample_rate &&
        difference * sample_rate < __uint128_t(window_samples) * units_per_second) return AZ_TIME_EQUAL;
    return first < second ? AZ_TIME_BEFORE : AZ_TIME_AFTER;
}
template <class First, class Second>
static inline int compare_timestamps(First first, Second second,
                                    uint64_t units_per_second,
                                    uint64_t sample_rate,
                                    uint64_t window_samples) {
    static_assert(std::is_arithmetic<First>::value && std::is_arithmetic<Second>::value,
                  "timestamps must be arithmetic values");
    if (std::is_integral<First>::value && std::is_integral<Second>::value) {
        if (static_cast<__int128>(first) < 0 || static_cast<__int128>(second) < 0) return AZ_TIME_INVALID;
        return az_compare_integer_timestamps(static_cast<uint64_t>(first), static_cast<uint64_t>(second),
                                             units_per_second, sample_rate, window_samples);
    }
    return az_compare_floating_timestamps(first, second, units_per_second, sample_rate, window_samples);
}
#else
/* C11 type selection dispatches to functions; no comparison expression is
 * duplicated at call sites, and each timestamp argument is evaluated once. */
#define compare_timestamps(first, second, units, rate, window) \
    _Generic(((first) + (second)), \
        float: az_compare_floating_timestamps, \
        double: az_compare_floating_timestamps, \
        long double: az_compare_floating_timestamps, \
        default: az_compare_integer_timestamps)((first), (second), (units), (rate), (window))
#endif
#endif
