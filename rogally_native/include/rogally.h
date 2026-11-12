// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef _ROGALLY_H
#define _ROGALLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct rogally_t Rogally;

/* Event types currently only motion, but extensible for future */
typedef enum {
    /* Event will have motion set (accel, gyro) */
    ROGALLY_EVENT_MOTION,
    /* Event will have error set */
    ROGALLY_EVENT_ERROR
} RogallyEventType;

typedef struct rogally_motion_t {
    /* Accelerometer in units of g (1.0 = 9.81 m/s^2) */
    float accel_x, accel_y, accel_z;
    /* Gyroscope in radians per second */
    float gyro_x, gyro_y, gyro_z;
    /* Monotonic timestamp in microseconds */
    uint64_t timestamp_us;
} RogallyMotion;

typedef struct rogally_event_t {
    RogallyEventType type;
    union {
        RogallyMotion motion;
        bool error;
    };
} RogallyEvent;

typedef void (*RogallyEventCb)(RogallyEvent *event, void *user);

/* Create a new Rogally instance. Returns NULL if sensors are not available. */
Rogally *rogally_new(void);

/* Free a Rogally instance */
void rogally_free(Rogally *rogally);

/* Poll for new sensor data and invoke callback for each event */
void rogally_read(Rogally *rogally, RogallyEventCb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* _ROGALLY_H */
