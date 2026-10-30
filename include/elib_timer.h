/* ============================================================================
 * elib_timer.h --- elib software timer (elib-soft-timer) unified umbrella header
 * ============================================================================
 * The complete public API and public types of this library live in this single
 * header. All sub-headers were removed as of v2.
 * Version: ELIB_TIMER_VERSION (currently 2.0.0)
 *
 * Game rules (elib family v2 common principles):
 *   1. C99; must compile warning-free with
 *      arm-gcc -std=c99 -Wall -Wextra -Werror -Iinclude.
 *   2. Zero dynamic memory, zero globals: the ctx and the instance array are
 *      provided statically by the user.
 *   3. init memset-overwrites and re-initializes unconditionally; a ctx in any
 *      historical state is accepted; illegal configurations (NULL /
 *      max_timers == 0) are rejected by init.
 *   4. The library takes no locks of its own; cross-context use between an ISR
 *      and the main loop requires the user-mounted critical-section hooks below.
 *      When no hooks are mounted the library declares single-context use.
 *   5. Source files include this umbrella header directly: #include
 *      "elib_timer.h", built with -Iinclude; relative-path cross-layer includes
 *      are forbidden.
 *   6. No build system ships with the library: the only compile units are the
 *      source files plus this header.
 *
 * Time model:
 *   The clock is externally driven -- elib_timer_manager(ctx, ms_elapsed) is
 *   called repeatedly with the milliseconds elapsed since the previous call
 *   (typically 1 per 1 ms tick ISR). Each call produces at most one expiry
 *   decision (lost-tick semantics): when ms_elapsed exceeds the remaining time,
 *   the surplus beyond the boundary is dropped and the period restarts from a
 *   full period, which shows up as periodic drift. Keep the tick granularity
 *   far below the smallest timer period.
 *
 * Concurrency model:
 *   elib_timer_manager() is meant to run in the tick ISR; all other APIs run in
 *   the main loop. When one ctx is used concurrently from both contexts, mount
 *   the critical-section hooks (save/restore style, nesting-safe):
 *     #define ELIB_TIMER_CRITICAL_ENTER()  ...
 *     #define ELIB_TIMER_CRITICAL_EXIT()   ...
 *   Dark side: no critical section is held while a callback runs.
 *
 * Callback contract:
 *   - IMMEDIATE: expiry callbacks execute directly in the caller context of
 *     manager() (i.e. the ISR!); they must be short and non-blocking.
 *   - DELAYED: expiry only raises the pending flag; the main loop drains it via
 *     elib_timer_process_pending() and executes callbacks in main-loop context.
 *   - Reentrancy from a callback is legal only for operations on itself:
 *     delete/stop/start/set_period/set_loop_count/get_remaining on that timer.
 *   - Calling create, deleting other timers, or re-entering manager /
 *     process_pending from a callback is undefined behaviour.
 *   - After an IMMEDIATE callback returns, manager re-checks state == RUNNING
 *     before loading the next period into remaining: a timer deleted/stopped by
 *     its own callback is not reloaded in that round.
 * ==========================================================================*/
#ifndef ELIB_TIMER_H
#define ELIB_TIMER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- version */
#define ELIB_TIMER_VERSION        "2.0.0"
#define ELIB_TIMER_VERSION_MAJOR  2
#define ELIB_TIMER_VERSION_MINOR  0
#define ELIB_TIMER_VERSION_PATCH  0

/* ------------------------------------------------------------ configuration */
/*
 * Critical-section hooks (optional):
 *   Preset by the user for cross-context use (manager in the ISR, everything
 *   else in the main loop). They must be save/restore style and nesting-safe.
 *   When left undefined they expand to nothing = single-context contract.
 */
#ifndef ELIB_TIMER_CRITICAL_ENTER
#define ELIB_TIMER_CRITICAL_ENTER()
#endif
#ifndef ELIB_TIMER_CRITICAL_EXIT
#define ELIB_TIMER_CRITICAL_EXIT()
#endif

/* Loop-count special value: 0 = infinite, the timer runs forever. */
#define ELIB_TIMER_LOOP_INFINITE  0u

/* --------------------------------------------------------------- error codes */
typedef enum {
    ELIB_TIMER_OK = 0,                 /* Success */
    ELIB_TIMER_ERR_INVALID_PARAM,      /* Illegal parameter / enum value */
    ELIB_TIMER_ERR_NOT_INITIALIZED,    /* Manager not initialized (or deinitialized) */
    ELIB_TIMER_ERR_NOT_FOUND,          /* Invalid timer ID or unused slot */
    ELIB_TIMER_ERR_FULL,               /* Instance pool is full */
    ELIB_TIMER_ERR_ALREADY_RUNNING,    /* Timer is already RUNNING */
    ELIB_TIMER_ERR_NOT_RUNNING,        /* Timer is not in a state that allows the operation */
    ELIB_TIMER_ERR_EXECUTION_FAILED,   /* Callback execution failed (reserved) */
} elib_timer_err_t;

/* ------------------------------------------------------------------- types */
/* State machine: UNUSED -(create)-> STOPPED -(start)-> RUNNING <-> PAUSED
 * RUNNING -(expiry with loop count exhausted)-> EXPIRED;
 * EXPIRED -(start)-> RUNNING re-arms */
typedef enum {
    ELIB_TIMER_STATE_UNUSED  = 0,      /* Slot is free */
    ELIB_TIMER_STATE_STOPPED,          /* Created but not running (stop returns here) */
    ELIB_TIMER_STATE_RUNNING,          /* Running, counts down in manager */
    ELIB_TIMER_STATE_PAUSED,           /* Paused, remaining is frozen */
    ELIB_TIMER_STATE_EXPIRED,          /* Finite loop count exhausted (terminal, re-arm with start) */
} elib_timer_state_t;

/* Callback execution mode */
typedef enum {
    ELIB_TIMER_MODE_IMMEDIATE = 0,     /* Run the callback in manager context on expiry */
    ELIB_TIMER_MODE_DELAYED   = 1,     /* Raise pending; the main loop drains it */
} elib_timer_exec_mode_t;

/* Callback prototype */
typedef void (*elib_timer_callback_t)(void *user_data);

/* Single timer instance (one element of the user's static array) */
typedef struct {
    /* User configuration */
    elib_timer_callback_t callback;    /* Expiry callback */
    void *user_data;                   /* Opaque pointer passed to the callback */

    /* Runtime parameters (millisecond domain) */
    uint32_t period_ms;                /* Period */
    uint32_t remaining_ms;             /* Remaining in the current round (frozen while PAUSED, reset to period while STOPPED) */
    uint32_t loop_count;               /* Total loop count (0 = infinite) */
    uint32_t loop_remaining;           /* Remaining loop count */

    /* Bit-packed flags */
    struct {
        uint8_t exec_mode : 1;         /* elib_timer_exec_mode_t */
        uint8_t state : 3;             /* elib_timer_state_t */
        uint8_t pending_execution : 1; /* DELAYED pending-expiry flag */
    } bit_flags;
} elib_timer_instance_t;

/* Manager context (statically allocated by the user, initialized by init) */
typedef struct {
    /* Filled in by init */
    elib_timer_instance_t *timers;     /* Instance array base pointer */
    uint32_t max_timers;               /* Array capacity */

    /* Runtime state */
    uint32_t active_count;             /* Number of non-UNUSED slots */

    struct {
        uint8_t initialized : 1;       /* init/deinit flag */
    } bit_flags;
} elib_timer_ctx_t;

/* --------------------------------------------------------------- lifecycle */
/**
 * @brief Initialize the timer manager
 * @param ctx User-provided context pointer (any historical state; memset-overwritten)
 * @param timers User-provided instance array
 * @param max_timers Array capacity (must be >= 1)
 * @return ELIB_TIMER_OK / ELIB_TIMER_ERR_INVALID_PARAM
 */
elib_timer_err_t elib_timer_init(elib_timer_ctx_t *ctx,
                                 elib_timer_instance_t *timers,
                                 uint32_t max_timers);

/**
 * @brief Deinitialize the manager (instances are kept in place; slots no longer usable)
 * @param ctx Context pointer; NULL-safe
 */
void elib_timer_deinit(elib_timer_ctx_t *ctx);

/**
 * @brief Create a timer (first UNUSED slot; ID = array index)
 * @param ctx Context
 * @param period_ms Period in milliseconds (must be >= 1)
 * @param callback Expiry callback (must not be NULL)
 * @param user_data Opaque pointer passed to the callback (may be NULL)
 * @param exec_mode ELIB_TIMER_MODE_IMMEDIATE / ELIB_TIMER_MODE_DELAYED
 * @param loop_count Loop count: 0 = infinite, N = fire N times then EXPIRED
 * @param out_timer_id Output ID (must not be NULL)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / FULL
 */
elib_timer_err_t elib_timer_create(elib_timer_ctx_t *ctx,
                                   uint32_t period_ms,
                                   elib_timer_callback_t callback,
                                   void *user_data,
                                   elib_timer_exec_mode_t exec_mode,
                                   uint32_t loop_count,
                                   uint32_t *out_timer_id);

/**
 * @brief Delete a timer (slot returns to UNUSED; pending flag is cleared)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_delete(elib_timer_ctx_t *ctx, uint32_t timer_id);

/* ---------------------------------------------------------- state control */
/**
 * @brief Start a timer (remaining reset to period; loop_remaining reset to loop_count)
 *
 * Starting from STOPPED / PAUSED / EXPIRED is allowed; RUNNING returns
 * ALREADY_RUNNING.
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / ALREADY_RUNNING
 */
elib_timer_err_t elib_timer_start(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Stop a timer (full-reset semantics: remaining reset to period, state STOPPED)
 *
 * STOPPED / UNUSED are rejected; RUNNING / PAUSED / EXPIRED may be stopped.
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_stop(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Pause a timer (RUNNING only; remaining freezes at the pause instant)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_pause(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Resume a timer (PAUSED only; continues counting from the frozen value)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_resume(elib_timer_ctx_t *ctx, uint32_t timer_id);

/* --------------------------------------------------------- dynamic config */
/**
 * @brief Change the period at runtime
 *
 * RUNNING / PAUSED timers rescale their remaining time proportionally
 * (remaining' = remaining * new / old, 64-bit intermediate, truncated, clamped
 * to 1); STOPPED / EXPIRED are left unscaled (stop/start reset it anyway).
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_set_period(elib_timer_ctx_t *ctx,
                                       uint32_t timer_id,
                                       uint32_t period_ms);

/**
 * @brief Change the loop count at runtime (loop_remaining resets immediately and counting restarts)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_set_loop_count(elib_timer_ctx_t *ctx,
                                           uint32_t timer_id,
                                           uint32_t loop_count);

/* ------------------------------------------------------------------ query */
/**
 * @brief Query the remaining milliseconds (RUNNING: live; PAUSED: frozen;
 *        STOPPED/after stop: period; EXPIRED: 0)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_get_remaining(elib_timer_ctx_t *ctx,
                                          uint32_t timer_id,
                                          uint32_t *out_remaining_ms);

/* ------------------------------------------------------------------ driver */
/**
 * @brief Timer driver (call from the tick ISR; feed the elapsed milliseconds)
 *
 * One call performs at most one expiry decision (lost-tick semantics). IMMEDIATE
 * timers run their callback synchronously inside this call; DELAYED timers only
 * raise pending for process_pending to drain.
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_timer_err_t elib_timer_manager(elib_timer_ctx_t *ctx, uint32_t ms_elapsed);

/**
 * @brief Drain pending DELAYED callbacks (call from the main loop, in slot-index order)
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_timer_err_t elib_timer_process_pending(elib_timer_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_TIMER_H */
