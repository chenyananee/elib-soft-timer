/* ============================================================================
 * elib_timer_core.c --- elib 软件定时器实现（v2）
 * ============================================================================
 * 唯一源文件，直连伞头。语义契约见 elib_timer.h 顶注：
 *   - 丢拍语义：manager 单次调用最多一轮到期判定。
 *   - IMMEDIATE 回调返回后复查 state==RUNNING 才装填下一轮 remaining。
 *   - delete 清除 pending 标志；process_pending 双保险跳过 UNUSED 槽位。
 *   - set_period 对 RUNNING / PAUSED 按比例缩放 remaining。
 *   - 所有跨上下文共享段由 ELIB_TIMER_CRITICAL_ENTER/EXIT 覆盖。
 * ==========================================================================*/
#include "elib_timer.h"
#include <string.h>

/* ------------------------------------------------------------------ 内部工具 */

/* 按下标取实例（ID = 数组下标），越界返回 NULL */
static elib_timer_instance_t *get_timer_by_id(elib_timer_ctx_t *ctx,
                                              uint32_t timer_id) {
    if (timer_id >= ctx->max_timers) {
        return NULL;
    }
    return &ctx->timers[timer_id];
}

/* ------------------------------------------------------------------ 生命周期 */

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

/* ------------------------------------------------------------------ create / delete */

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

    /* 找第一个 UNUSED 槽位（ID = 下标） */
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
    timer->bit_flags.pending_execution = 0u;    /* 防 pending+delete 竞态引爆 */
    ctx->active_count--;

    ELIB_TIMER_CRITICAL_EXIT();

    return ELIB_TIMER_OK;
}

/* ------------------------------------------------------------------ start / stop / pause / resume */

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

/* ------------------------------------------------------------------ 动态配置 */

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

    /* RUNNING / PAUSED 按比例缩放剩余时间；64 位中间量防溢出，0 钳 1 */
    if (timer->bit_flags.state == ELIB_TIMER_STATE_RUNNING ||
        timer->bit_flags.state == ELIB_TIMER_STATE_PAUSED) {
        uint64_t scaled =
            (uint64_t)timer->remaining_ms * (uint64_t)period_ms;
        timer->remaining_ms = (uint32_t)(scaled / (uint64_t)old_period);
        if (timer->remaining_ms == 0u) {
            timer->remaining_ms = 1u;
        }
    }
    /* STOPPED / EXPIRED 不缩放：stop/start 自会重置 remaining */

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

/* ------------------------------------------------------------------ 查询 */

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

/* ------------------------------------------------------------------ 驱动 */

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

        /* ---- 临界段：倒计数 / 到期判定 / 标志变更 */
        ELIB_TIMER_CRITICAL_ENTER();

        /* 锁内复查（主循环 API 可能刚改动过状态） */
        if (timer->bit_flags.state != ELIB_TIMER_STATE_RUNNING) {
            ELIB_TIMER_CRITICAL_EXIT();
            continue;
        }

        bool run_immediate = false;
        bool continue_running = true;

        if (timer->remaining_ms > ms_elapsed) {
            timer->remaining_ms -= ms_elapsed;
        } else {
            /* 到期（丢拍语义：越界的多余时间丢弃） */
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

        /* ---- 回调执行在临界段之外 */
        if (run_immediate) {
            timer->callback(timer->user_data);
        }

        /* ---- 下一轮装填：回调/主循环可能已 STOP 自己，复查后才装填 */
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

        /* 取标志 + 清标志在临界段内；UNUSED 槽位双保险跳过 */
        if (!timer->bit_flags.pending_execution ||
            timer->bit_flags.state == ELIB_TIMER_STATE_UNUSED) {
            ELIB_TIMER_CRITICAL_EXIT();
            continue;
        }
        timer->bit_flags.pending_execution = 0u;

        ELIB_TIMER_CRITICAL_EXIT();

        /* 回调在临界段外执行；回调内操作自己的契约仍然适用 */
        timer->callback(timer->user_data);
    }

    return ELIB_TIMER_OK;
}
