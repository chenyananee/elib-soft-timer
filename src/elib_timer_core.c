/* elib_timer_core.c - Software Timer Core Implementation */
#include "../include/elib_timer.h"
#include <string.h>

/* Forward declarations for internal functions */
static elib_timer_instance_t* find_free_timer(elib_timer_ctx_t *ctx);
static elib_timer_instance_t* get_timer_by_id(elib_timer_ctx_t *ctx, uint32_t timer_id);

/* Find a free timer slot */
static elib_timer_instance_t* find_free_timer(elib_timer_ctx_t *ctx) {
    for (uint32_t i = 0; i < ctx->max_timers; i++) {
        if (ctx->timers[i].state == ELIB_TIMER_STATE_UNUSED) {
            return &ctx->timers[i];
        }
    }
    return NULL;
}

/* Get timer by ID */
static elib_timer_instance_t* get_timer_by_id(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (timer_id >= ctx->max_timers) {
        return NULL;
    }
    return &ctx->timers[timer_id];
}

/* Initialize timer manager */
elib_timer_err_t elib_timer_init(elib_timer_ctx_t *ctx,
                                  elib_timer_instance_t *timers,
                                  uint32_t max_timers) {
    if (ctx == NULL || timers == NULL || max_timers == 0) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(elib_timer_ctx_t));
    memset(timers, 0, sizeof(elib_timer_instance_t) * max_timers);

    ctx->timers = timers;
    ctx->max_timers = max_timers;
    ctx->active_count = 0;
    ctx->initialized = 1;

    return ELIB_TIMER_OK;
}

/* Deinitialize timer manager */
void elib_timer_deinit(elib_timer_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->initialized = 0;
}

/* Create a new timer */
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
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }
    if (period_ms == 0) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    elib_timer_instance_t *timer = find_free_timer(ctx);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_FULL;
    }

    /* Find timer ID */
    uint32_t timer_id = (uint32_t)(timer - ctx->timers);

    /* Initialize timer */
    memset(timer, 0, sizeof(elib_timer_instance_t));
    timer->callback = callback;
    timer->user_data = user_data;
    timer->exec_mode = exec_mode;
    timer->period_ms = period_ms;
    timer->remaining_ms = period_ms;
    timer->loop_count = loop_count;
    timer->loop_remaining = loop_count;
    timer->state = ELIB_TIMER_STATE_STOPPED;
    timer->pending_execution = false;

    *out_timer_id = timer_id;
    ctx->active_count++;

    return ELIB_TIMER_OK;
}

/* Delete a timer */
elib_timer_err_t elib_timer_delete(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    timer->state = ELIB_TIMER_STATE_UNUSED;
    ctx->active_count--;

    return ELIB_TIMER_OK;
}

/* Start a timer */
elib_timer_err_t elib_timer_start(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_RUNNING) {
        return ELIB_TIMER_ERR_ALREADY_RUNNING;
    }

    timer->state = ELIB_TIMER_STATE_RUNNING;
    timer->remaining_ms = timer->period_ms;
    if (timer->loop_count > 0) {
        timer->loop_remaining = timer->loop_count;
    }

    return ELIB_TIMER_OK;
}

/* Stop a timer */
elib_timer_err_t elib_timer_stop(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_STOPPED) {
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->state = ELIB_TIMER_STATE_STOPPED;
    timer->remaining_ms = timer->period_ms;

    return ELIB_TIMER_OK;
}

/* Pause a timer */
elib_timer_err_t elib_timer_pause(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state != ELIB_TIMER_STATE_RUNNING) {
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->state = ELIB_TIMER_STATE_PAUSED;

    return ELIB_TIMER_OK;
}

/* Resume a timer */
elib_timer_err_t elib_timer_resume(elib_timer_ctx_t *ctx, uint32_t timer_id) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state != ELIB_TIMER_STATE_PAUSED) {
        return ELIB_TIMER_ERR_NOT_RUNNING;
    }

    timer->state = ELIB_TIMER_STATE_RUNNING;

    return ELIB_TIMER_OK;
}

/* Set timer period at runtime */
elib_timer_err_t elib_timer_set_period(elib_timer_ctx_t *ctx,
                                        uint32_t timer_id,
                                        uint32_t period_ms) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }
    if (period_ms == 0) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    /* Save old period for ratio calculation */
    uint32_t old_period = timer->period_ms;

    /* Set new period */
    timer->period_ms = period_ms;

    /* If running, adjust remaining time proportionally */
    if (timer->state == ELIB_TIMER_STATE_RUNNING && old_period != 0) {
        /* Use 64-bit to prevent intermediate overflow */
        uint64_t temp = (uint64_t)timer->remaining_ms * period_ms;
        timer->remaining_ms = (uint32_t)(temp / old_period);
        if (timer->remaining_ms == 0) {
            timer->remaining_ms = 1;
        }
    }

    return ELIB_TIMER_OK;
}

/* Set timer loop count at runtime */
elib_timer_err_t elib_timer_set_loop_count(elib_timer_ctx_t *ctx,
                                            uint32_t timer_id,
                                            uint32_t loop_count) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    timer->loop_count = loop_count;
    timer->loop_remaining = loop_count;

    return ELIB_TIMER_OK;
}

/* Get remaining time */
elib_timer_err_t elib_timer_get_remaining(elib_timer_ctx_t *ctx,
                                           uint32_t timer_id,
                                           uint32_t *out_remaining_ms) {
    if (ctx == NULL || out_remaining_ms == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    elib_timer_instance_t *timer = get_timer_by_id(ctx, timer_id);
    if (timer == NULL) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }
    if (timer->state == ELIB_TIMER_STATE_UNUSED) {
        return ELIB_TIMER_ERR_NOT_FOUND;
    }

    *out_remaining_ms = timer->remaining_ms;

    return ELIB_TIMER_OK;
}

/* Timer manager - update timer states */
elib_timer_err_t elib_timer_manager(elib_timer_ctx_t *ctx, uint32_t ms_elapsed) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    for (uint32_t i = 0; i < ctx->max_timers; i++) {
        elib_timer_instance_t *timer = &ctx->timers[i];

        if (timer->state != ELIB_TIMER_STATE_RUNNING) {
            continue;
        }

        /* Update remaining time */
        if (timer->remaining_ms > ms_elapsed) {
            timer->remaining_ms -= ms_elapsed;
            continue;
        }

        /* Timer expired */
        timer->remaining_ms = 0;

        /* Handle loop count - determine if should continue FIRST */
        bool continue_running = true;
        if (timer->loop_count > 0) {
            timer->loop_remaining--;
            if (timer->loop_remaining == 0) {
                continue_running = false;
                timer->state = ELIB_TIMER_STATE_EXPIRED;
            }
        }

        /* Execute callback based on mode */
        switch (timer->exec_mode) {
            case ELIB_TIMER_MODE_IMMEDIATE:
                timer->callback(timer->user_data);
                break;

            case ELIB_TIMER_MODE_DELAYED:
                timer->pending_execution = true;
                break;
        }

        /* Reset timer for next cycle ONLY if still running */
        if (continue_running) {
            timer->remaining_ms = timer->period_ms;
        }
    }

    return ELIB_TIMER_OK;
}

/* Process pending callbacks (delayed mode) */
elib_timer_err_t elib_timer_process_pending(elib_timer_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_TIMER_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_TIMER_ERR_NOT_INITIALIZED;
    }

    for (uint32_t i = 0; i < ctx->max_timers; i++) {
        elib_timer_instance_t *timer = &ctx->timers[i];

        if (!timer->pending_execution) {
            continue;
        }

        /* Clear pending flag */
        timer->pending_execution = false;

        /* Execute callback */
        timer->callback(timer->user_data);
    }

    return ELIB_TIMER_OK;
}