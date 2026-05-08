/* elib_timer.h - Software Timer Main Header */
#ifndef ELIB_TIMER_H
#define ELIB_TIMER_H

#include "elib_timer_err.h"
#include "elib_timer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize timer manager
 * @param ctx User-allocated context pointer
 * @param timers User-allocated timer instance array
 * @param max_timers Maximum number of timers
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_init(elib_timer_ctx_t *ctx,
                                  elib_timer_instance_t *timers,
                                  uint32_t max_timers);

/**
 * @brief Deinitialize timer manager
 * @param ctx Context pointer
 */
void elib_timer_deinit(elib_timer_ctx_t *ctx);

/**
 * @brief Create a new timer
 * @param ctx Context pointer
 * @param period_ms Timer period in milliseconds
 * @param callback Callback function
 * @param user_data User data pointer passed to callback
 * @param exec_mode Execution mode (IMMEDIATE or DELAYED)
 * @param loop_count Loop count (ELIB_TIMER_LOOP_INFINITE=infinite, N=execute N times)
 * @param out_timer_id Output timer ID
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_create(elib_timer_ctx_t *ctx,
                                    uint32_t period_ms,
                                    elib_timer_callback_t callback,
                                    void *user_data,
                                    elib_timer_exec_mode_t exec_mode,
                                    uint32_t loop_count,
                                    uint32_t *out_timer_id);

/**
 * @brief Delete a timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to delete
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_delete(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Start a timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to start
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_start(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Stop a timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to stop
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_stop(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Pause a running timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to pause
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_pause(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Resume a paused timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to resume
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_resume(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief Set timer period at runtime
 * @param ctx Context pointer
 * @param timer_id Timer ID to modify
 * @param period_ms New period in milliseconds
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_set_period(elib_timer_ctx_t *ctx,
                                        uint32_t timer_id,
                                        uint32_t period_ms);

/**
 * @brief Set timer loop count at runtime
 * @param ctx Context pointer
 * @param timer_id Timer ID to modify
 * @param loop_count New loop count (ELIB_TIMER_LOOP_INFINITE=infinite)
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_set_loop_count(elib_timer_ctx_t *ctx,
                                            uint32_t timer_id,
                                            uint32_t loop_count);

/**
 * @brief Get remaining time for a timer
 * @param ctx Context pointer
 * @param timer_id Timer ID to query
 * @param out_remaining_ms Output remaining time in milliseconds
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_get_remaining(elib_timer_ctx_t *ctx,
                                           uint32_t timer_id,
                                           uint32_t *out_remaining_ms);

/**
 * @brief Timer manager - call this with elapsed milliseconds
 * @param ctx Context pointer
 * @param ms_elapsed Milliseconds elapsed since last call
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_manager(elib_timer_ctx_t *ctx, uint32_t ms_elapsed);

/**
 * @brief Process pending callbacks (for DELAYED mode)
 * @param ctx Context pointer
 * @return elib_timer_err_t error code
 */
elib_timer_err_t elib_timer_process_pending(elib_timer_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_TIMER_H */