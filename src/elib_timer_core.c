/* ============================================================================
 * elib_timer_core.c --- elib software timer implementation (v2)
 * ============================================================================
 * The single source file; includes the umbrella header directly. The semantic
 * contract lives in the header banner of elib_timer.h:
 *   - Lost-tick semantics: one manager call performs at most one expiry decision.
 *   - After an IMMEDIATE callback returns, state == RUNNING is re-checked before
 *     loading the next period into remaining.
 *   - delete clears the pending flag; process_pending additionally skips UNUSED
 *     slots as a second line of defence.
 *   - set_period rescales remaining for RUNNING / PAUSED timers.
 *   - Every section shared across contexts is covered by the
 *     ELIB_TIMER_CRITICAL_ENTER/EXIT hooks.
 * ==========================================================================*/
#include "elib_timer.h"
#include <string.h>

/* -------------------------------------------------------------- internal helpers */

/* Fetch the instance by index (ID = array index); NULL when out of range */
static elib_timer_instance_t *get_timer_by_id(elib_timer_ctx_t *ctx,
                                              uint32_t timer_id) {
    if (timer_id >= ctx->max_timers) {
        return NULL;
    }
    return &ctx->timers[timer_id];
}

/* -------------------------------------------------------------- lifecycle */

elib_timer_err_t elib_timer_init(elib_timer_ctx_t *ctx,
                                 elib_timer_instance_t *timers,
                                 uint32_t max_timers) {
    if (ctx == NULL || timers == NULL || max_timers == 0u) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(*ctx));
    memset(timers, 0, sizeof(elib_timer_instance_t) * max_timers);

    ctx->timers = timers;
    ctx->max_timers = max_timers;
    ctx->active_count = 0u;
    ctx->bit_flags.initialized = 1u;

    return ELIB_TIMER_OK;
}

void elib_timer_deinit(elib_timer_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    ELIB_TIMER_CRITICAL_ENTER();
    ctx->bit_flags.initialized = 0u;
    ELIB_TIMER_CRITICAL_EXIT();
}

/* -------------------------------------------------------------- create / delete */

elib_timer_err_t elib_timer_create(elib_timer_ctx_t *ctx,
                                   uint32_t period_ms,
                                   elib_timer_callback_t callback,
                                   void *user_data,
                                   elib_timer_exec_mode_t exec_mode,
                                   uint32_t loop_count,
                                   uint32_t *out_timer_id) {
    if (ctx == NULL || callback == NULL || out_timer_id == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }
    if (period_ms == 0u) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (exec_mode != ELIB_TIMER_MODE_IMMEDIATE &&
        exec_mode != ELIB_TIMER_MODE_DELAYED) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    /* Find the first UNUSED slot (ID = index) */
    uint32_t timer_id = ctx->max_timers;
    for (uint32_t i = 0u; i < ctx->max_timers; i++) {
        if (ctx->timers[i].bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
            timer_id = i;
            break;
        }
    }
    if (timer_id == ctx->max_timers) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_FULL;
    }

    elib_timer_instance_t *timer = &ctx->timers[timer_id];
    memset(timer, 0, sizeof(*timer));
    timer->callback = callback;
    timer->user_data = user_data;
    timer->period_ms = period_ms;
    timer->remaining_ms = period_ms;
    timer->loop_count = loop_count;
    timer->loop_remaining = loop_count;
    timer->bit_flags.exec_mode = (uint8_t)exec_mode;
    timer->bit_flags.state = ELIB_TIMER_STATE_STOPPED;
    timer->bit_flags.pending_execution = 0u;

    ctx->active_count++;

    ELIB_TIMER_CRITICAL_EXIT();

    *out_timer_id = timer_id;

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_delete(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    timer->bit_flags.state = ELIB_TIMER_STATE_UNUSED;
    timer->bit_flags.pending_execution = 0u;    /* Prevent the pending+delete race from firing */
    ctx->active_count--;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

/* -------------------------------------------------------------- start / stop / pause / resume */

elib_timer_err_t elib_timer_start(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->bit_flags.state == ELIB_TIMER_STATE_RUNNING) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_ALREADY_RUNNING;
    }

    timer->remaining_ms = timer->period_ms;
    timer->bit_flags.state = ELIB_TIMER_STATE_RUNNING;
    if (timer->loop_count > 0u) {
        timer->loop_remaining = timer->loop_count;
    }

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_stop(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->bit_flags.state == ELIB_TIMER_STATE_STOPPED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->remaining_ms = timer->period_ms;
    timer->bit_flags.state = ELIB_TIMER_STATE_STOPPED;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_pause(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    if (timer->bit_flags.state != ELIB_TIMER_STATE_RUNNING) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->bit_flags.state = ELIB_TIMER_STATE_PAUSED;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_resume(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    if (timer->bit_flags.state != ELIB_TIMER_STATE_PAUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->bit_flags.state = ELIB_TIMER_STATE_RUNNING;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

/* -------------------------------------------------------------- dynamic config */

elib_timer_err_t elib_timer_set_period(elib_timer_ctx_t *ctx,
                                       uint32_t timer_id,
                                       uint32_t period_ms) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }
    if (period_ms == 0u) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    uint32_t old_period = timer->period_ms;
    timer->period_ms = period_ms;

    /* RUNNING / PAUSED rescale remaining; 64-bit intermediate avoids overflow, clamped to 1 */
    if (timer->bit_flags.state == ELIB_TIMER_STATE_RUNNING ||
        timer->bit_flags.state == ELIB_TIMER_STATE_PAUSED) {
        uint64_t scaled =
            (uint64_t)timer->remaining_ms * (uint64_t)period_ms;
        timer->remaining_ms = (uint32_t)(scaled / (uint64_t)old_period);
        if (timer->remaining_ms == 0u) {
            timer->remaining_ms = 1u;
        }
    }
    /* STOPPED / EXPIRED are not rescaled: stop/start reset remaining anyway */

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_set_loop_count(elib_timer_ctx_t *ctx,
                                           uint32_t timer_id,
                                           uint32_t loop_count) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    timer->loop_count = loop_count;
    timer->loop_remaining = loop_count;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

/* -------------------------------------------------------------- query */

elib_timer_err_t elib_timer_get_remaining(elib_timer_ctx_t *ctx,
                                          uint32_t timer_id,
                                          uint32_t *out_remaining_ms) {
    if (ctx == NULL || out_remaining_ms == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    ELIB_TIMER_CRITICAL_ENTER();

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL || timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
        ELIB_TIMER_CRITICAL_EXIT();
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    *out_remaining_ms = timer->remaining_ms;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

/* -------------------------------------------------------------- driver */

elib_timer_err_t elib_timer_manager(elib_timer_ctx_t *ctx, uint32_t ms_elapsed) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }
    if (ms_elapsed == 0u) {
        return ELIB_TIMER_OK;
    }

    for (uint32_t i = 0u; i < ctx->max_timers; i++) {
        elib_timer_instance_t *timer = &ctx->timers[i];

        if (timer->bit_flags.state != ELIB_TIMER_STATE_RUNNING) {
            continue;
        }

        /* ---- Critical section: countdown / expiry decision / flag updates */
        ELIB_TIMER_CRITICAL_ENTER();

        /* Re-check under the lock (a main-loop API may have just changed the state) */
        if (timer->bit_flags.state != ELIB_TIMER_STATE_RUNNING) {
            ELIB_TIMER_CRITICAL_EXIT();
            continue;
        }

        bool run_immediate = false;
        bool continue_running = true;

        if (timer->remaining_ms > ms_elapsed) {
            timer->remaining_ms -= ms_elapsed;
        } else {
            /* Expired (lost-tick semantics: surplus time beyond the boundary is dropped) */
            timer->remaining_ms = 0u;

            if (timer->loop_count > 0u) {
                timer->loop_remaining--;
                if (timer->loop_remaining == 0u) {
                    continue_running = false;
                    timer->bit_flags.state = ELIB_TIMER_STATE_EXPIRED;
                }
            }

            if (timer->bit_flags.exec_mode == ELIB_TIMER_MODE_DELAYED) {
                timer->bit_flags.pending_execution = 1u;
            } else {
                run_immediate = true;
            }
        }

        ELIB_TIMER_CRITICAL_EXIT();

        /* ---- Callback runs outside the critical section */
        if (run_immediate) {
            timer->callback(timer->user_data);
        }

        /* ---- Next-round reload: the callback / main loop may have stopped this
         *      timer, so re-check before loading */
        if (continue_running) {
            ELIB_TIMER_CRITICAL_ENTER();
            if (timer->bit_flags.state == ELIB_TIMER_STATE_RUNNING) {
                timer->remaining_ms = timer->period_ms;
            }
            ELIB_TIMER_CRITICAL_EXIT();
        }
    }

    return ELIB_TIMER_OK;
}

elib_timer_err_t elib_timer_process_pending(elib_timer_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    for (uint32_t i = 0u; i < ctx->max_timers; i++) {
        elib_timer_instance_t *timer = &ctx->timers[i];

        ELIB_TIMER_CRITICAL_ENTER();

        /* Test-and-clear the flag inside the critical section; skip UNUSED slots
         * as a second line of defence */
        if (!timer->bit_flags.pending_execution ||
            timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
            ELIB_TIMER_CRITICAL_EXIT();
            continue;
        }
        timer->bit_flags.pending_execution = 0u;

        ELIB_TIMER_CRITICAL_EXIT();

        /* The callback runs outside the critical section; the self-only
         * reentrancy contract still applies */
        timer->callback(timer->user_data);
    }

    return ELIB_TIMER_OK;
}
