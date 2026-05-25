/* elib_timer_types.h - Software Timer Type Definitions */
#ifndef ELIB_TIMER_TYPES_H
#define ELIB_TIMER_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "elib_timer_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loop count macro */
#define ELIB_TIMER_LOOP_INFINITE  0  /* Infinite loop, timer runs forever */

/* Timer state */
typedef enum {
    ELIB_TIMER_STATE_UNUSED = 0,      /* Unused */
    ELIB_TIMER_STATE_STOPPED,         /* Stopped */
    ELIB_TIMER_STATE_RUNNING,         /* Running */
    ELIB_TIMER_STATE_PAUSED,          /* Paused */
    ELIB_TIMER_STATE_EXPIRED,         /* Expired (one-shot complete) */
} elib_timer_state_t;

/* Callback execution mode */
typedef enum {
    ELIB_TIMER_MODE_IMMEDIATE = 0,    /* Interrupt mode: execute callback immediately */
    ELIB_TIMER_MODE_DELAYED,          /* Delayed mode: mark as pending for main loop */
} elib_timer_exec_mode_t;

/* Timer callback function type */
typedef void (*elib_timer_callback_t)(void *user_data);

/* Single timer instance structure */
typedef struct {
    /* User configuration */
    elib_timer_callback_t callback;    /* Callback function */
    void *user_data;                   /* User data pointer */

    /* Timer parameters */
    uint32_t period_ms;                /* Timer period (milliseconds) */
    uint32_t remaining_ms;             /* Remaining time (milliseconds) */
    uint32_t loop_count;               /* Loop count (ELIB_TIMER_LOOP_INFINITE=infinite) */
    uint32_t loop_remaining;           /* Remaining loop count */

    /* Bit-packed flags */
    struct {
        uint8_t exec_mode : 1;        /* Execution mode */
        uint8_t state : 3;            /* Timer state */
        uint8_t pending_execution : 1;/* Pending execution flag (delayed mode) */
    } bit_flags;
} elib_timer_instance_t;

/* Timer manager context structure (statically allocated by user) */
typedef struct {
    /* User configuration */
    elib_timer_instance_t *timers;     /* Timer instance array pointer */
    uint32_t max_timers;               /* Maximum timer count */

    /* Runtime state */
    uint32_t active_count;             /* Active timer count */

    /* Bit-packed flags */
    struct {
        uint8_t initialized : 1;      /* Initialization flag */
    } bit_flags;
} elib_timer_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_TIMER_TYPES_H */