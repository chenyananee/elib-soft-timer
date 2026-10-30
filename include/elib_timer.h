/* ============================================================================
 * elib_timer.h --- elib 软件定时器（elib-soft-timer）统一伞头
 * ============================================================================
 * 本库全部公共 API 与公共类型集中于本单头。v2 起子头全部拆除。
 * 版本：ELIB_TIMER_VERSION（当前 2.0.0）
 *
 * 游戏规则（elib 家族 v2 通用）：
 *   1. C99；arm-gcc -std=c99 -Wall -Wextra -Werror -Iinclude 必须零告警通过。
 *   2. 零动态内存、零全局：ctx 与实例数组全部由用户静态提供。
 *   3. init 无条件 memset 覆盖重初始化，任意历史状态的 ctx 均可重入；
 *      非法配置（NULL / max_timers==0）在 init 处拒收。
 *   4. 库自身不加锁；ISR/主循环跨上下文使用需用户挂临界区钩子（见下），
 *      未挂钩时声明为单上下文使用。
 *   5. 源文件直连本伞头：#include "elib_timer.h"，-Iinclude 构建；
 *      禁止任何相对路径跨层 include。
 *   6. 无随库构建系统：编译期只有源文件 + 本头。
 *
 * 时间模型：
 *   时钟由外部驱动——反复调用 elib_timer_manager(ctx, ms_elapsed)，
 *   传入自上次调用以来经过的毫秒数（典型：1ms tick ISR 中每次喂 1）。
 *   单次调用最多产生一次到期判定（丢拍语义）：ms_elapsed 远大于周期时，
 *   落在边界外的多拍被丢弃、表现为周期性漂移。故 tick 粒度应远小于最小周期。
 *
 * 并发模型：
 *   elib_timer_manager() 供 tick ISR 调用，其余 API 供主循环调用。
 *   同一 ctx 跨上下文并发使用时，用户应挂临界区钩子（须为保存/恢复式、
 *   支持嵌套）：
 *     #define ELIB_TIMER_CRITICAL_ENTER()  uint32_t _pm_ = __get_PRIMASK(); __disable_irq()
 *     #define ELIB_TIMER_CRITICAL_EXIT()   __set_PRIMASK(_pm_)
 *   黑暗面：回调执行期间不持有临界区；回调内禁止重入库 API 的临界段。
 *
 * 回调契约：
 *   - IMMEDIATE：到期回调在 manager() 调用方上下文内直接执行（ISR！），
 *     必须短促、非阻塞。
 *   - DELAYED：到期只置 pending 标志；主循环经 elib_timer_process_pending()
 *     提取并以主循环上下文执行。
 *   - 回调内只允许"操作自己"的重入：对本定时器 delete/stop/start/
 *     set_period/set_loop_count/get_remaining。
 *   - 回调内 create 其他定时器、删除其他定时器、重入 manager /
 *     process_pending 均为未定义行为。
 *   - manager 在 IMMEDIATE 回调返回后复查 state==RUNNING 才装填下一轮
 *     remaining：回调内把自己 delete/stop 的，本轮不再装填。
 * ==========================================================================*/
#ifndef ELIB_TIMER_H
#define ELIB_TIMER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ 版本 */
#define ELIB_TIMER_VERSION        "2.0.0"
#define ELIB_TIMER_VERSION_MAJOR  2
#define ELIB_TIMER_VERSION_MINOR  0
#define ELIB_TIMER_VERSION_PATCH  0

/* ------------------------------------------------------------------ 配置 */
/*
 * 临界区钩子（可选）：
 *   跨上下文使用（manager 在 ISR、其余在主循环）时由用户在外部预定义，
 *   须满足保存/恢复式、支持嵌套。未定义时默认空 = 单上下文契约。
 */
#ifndef ELIB_TIMER_CRITICAL_ENTER
#define ELIB_TIMER_CRITICAL_ENTER()
#endif
#ifndef ELIB_TIMER_CRITICAL_EXIT
#define ELIB_TIMER_CRITICAL_EXIT()
#endif

/* 循环次数特值：0 = 无限循环，定时器永久运行。 */
#define ELIB_TIMER_LOOP_INFINITE  0u

/* ------------------------------------------------------------------ 错误码 */
typedef enum {
    ELIB_TIMER_OK = 0,                 /* 成功 */
    ELIB_TIMER_ERR_INVALID_PARAM,      /* 非法参数 / 非法枚举值 */
    ELIB_TIMER_ERR_NOT_INITIALIZED,    /* manager 尚未 init（或已 deinit） */
    ELIB_TIMER_ERR_NOT_FOUND,          /* 定时器 ID 无效或槽位未占用 */
    ELIB_TIMER_ERR_FULL,               /* 实例池已满 */
    ELIB_TIMER_ERR_ALREADY_RUNNING,    /* 定时器已处于 RUNNING */
    ELIB_TIMER_ERR_NOT_RUNNING,        /* 定时器不处于可执行该操作的状态 */
    ELIB_TIMER_ERR_EXECUTION_FAILED,   /* 回调执行失败（保留位） */
} elib_timer_err_t;

/* ------------------------------------------------------------------ 类型 */
/* 定时器状态机：UNUSED →(create)→ STOPPED →(start)→ RUNNING ⇄ PAUSED
 * RUNNING →(到期且 loop 耗尽)→ EXPIRED；EXPIRED →(start)→ RUNNING 重开 */
typedef enum {
    ELIB_TIMER_STATE_UNUSED  = 0,      /* 槽位空闲 */
    ELIB_TIMER_STATE_STOPPED,          /* 已创建未运行（stop 后亦回到此态） */
    ELIB_TIMER_STATE_RUNNING,          /* 运行中，随 manager 倒计时 */
    ELIB_TIMER_STATE_PAUSED,           /* 暂停，remaining 冻结 */
    ELIB_TIMER_STATE_EXPIRED,          /* 有限 loop 计数耗尽（终态，可 start 重开） */
} elib_timer_state_t;

/* 回调执行模式 */
typedef enum {
    ELIB_TIMER_MODE_IMMEDIATE = 0,     /* 到期即在 manager 上下文执行回调 */
    ELIB_TIMER_MODE_DELAYED   = 1,     /* 到期置 pending，主循环统一提取执行 */
} elib_timer_exec_mode_t;

/* 回调函数原型 */
typedef void (*elib_timer_callback_t)(void *user_data);

/* 单个定时器实例（用户静态数组元素） */
typedef struct {
    /* 用户配置 */
    elib_timer_callback_t callback;    /* 到期回调 */
    void *user_data;                   /* 回调透传指针 */

    /* 运行参数（毫秒域） */
    uint32_t period_ms;                /* 周期 */
    uint32_t remaining_ms;             /* 本轮剩余（PAUSED 冻结、STOPPED 重置为 period） */
    uint32_t loop_count;               /* 总循环次数（0 = 无限） */
    uint32_t loop_remaining;           /* 剩余循环次数 */

    /* 位域标志 */
    struct {
        uint8_t exec_mode : 1;         /* elib_timer_exec_mode_t */
        uint8_t state : 3;             /* elib_timer_state_t */
        uint8_t pending_execution : 1; /* DELAYED 到期待执行标志 */
    } bit_flags;
} elib_timer_instance_t;

/* manager 上下文（用户静态分配 + init 初始化） */
typedef struct {
    /* init 填写 */
    elib_timer_instance_t *timers;     /* 实例数组首址 */
    uint32_t max_timers;               /* 数组容量 */

    /* 运行状态 */
    uint32_t active_count;             /* 非 UNUSED 槽位数 */

    struct {
        uint8_t initialized : 1;       /* init/deinit 标志 */
    } bit_flags;
} elib_timer_ctx_t;

/* ------------------------------------------------------------------ 生命周期 */
/**
 * @brief 初始化定时器 manager
 * @param ctx 用户提供的上下文指针（任意历史状态，会被 memset 覆盖）
 * @param timers 用户提供的实例数组
 * @param max_timers 数组容量（须 >= 1）
 * @return ELIB_TIMER_OK / ELIB_TIMER_ERR_INVALID_PARAM
 */
elib_timer_err_t elib_timer_init(elib_timer_ctx_t *ctx,
                                 elib_timer_instance_t *timers,
                                 uint32_t max_timers);

/**
 * @brief 反初始化 manager（各定时器实例原地保留，槽位不再可用）
 * @param ctx 上下文指针；NULL 安全
 */
void elib_timer_deinit(elib_timer_ctx_t *ctx);

/**
 * @brief 创建定时器（在第一个 UNUSED 槽位上，ID = 数组下标）
 * @param ctx 上下文
 * @param period_ms 周期毫秒（须 >= 1）
 * @param callback 到期回调（须非 NULL）
 * @param user_data 回调透传指针（可 NULL）
 * @param exec_mode ELIB_TIMER_MODE_IMMEDIATE / ELIB_TIMER_MODE_DELAYED
 * @param loop_count 循环次数：0 = 无限，N = 执行 N 次后转 EXPIRED
 * @param out_timer_id 输出 ID（须非 NULL）
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
 * @brief 删除定时器（槽位回归 UNUSED；清除 pending 标志）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_delete(elib_timer_ctx_t *ctx, uint32_t timer_id);

/* ------------------------------------------------------------------ 状态控制 */
/**
 * @brief 启动定时器（remaining 重置为 period；loop_remaining 重置为 loop_count）
 *
 * 允许自 STOPPED / PAUSED / EXPIRED 启动；RUNNING 返回 ALREADY_RUNNING。
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / ALREADY_RUNNING
 */
elib_timer_err_t elib_timer_start(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief 停止定时器（全重置语义：remaining 复位为 period，回到 STOPPED）
 *
 * 拒收 STOPPED / UNUSED；允许自 RUNNING / PAUSED / EXPIRED 停止。
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_stop(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief 暂停定时器（仅 RUNNING 可暂停；remaining 冻结在暂停瞬间值）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_pause(elib_timer_ctx_t *ctx, uint32_t timer_id);

/**
 * @brief 恢复定时器（仅 PAUSED 可恢复，从冻结值继续倒计数）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND / NOT_RUNNING
 */
elib_timer_err_t elib_timer_resume(elib_timer_ctx_t *ctx, uint32_t timer_id);

/* ------------------------------------------------------------------ 动态配置 */
/**
 * @brief 运行时修改周期
 *
 * RUNNING / PAUSED 定时器按比例缩放剩余时间
 * （remaining' = remaining * new / old，64 位中间量，向下取整，0 钳 1）；
 * STOPPED / EXPIRED 不缩放（stop/start 自会重置）。
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_set_period(elib_timer_ctx_t *ctx,
                                       uint32_t timer_id,
                                       uint32_t period_ms);

/**
 * @brief 运行时修改循环次数（loop_remaining 立即重置为新值，重新计数）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_set_loop_count(elib_timer_ctx_t *ctx,
                                           uint32_t timer_id,
                                           uint32_t loop_count);

/* ------------------------------------------------------------------ 查询 */
/**
 * @brief 查询剩余毫秒数（RUNNING：动态；PAUSED：冻结；STOPPED/STOP 后：period；EXPIRED：0）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / NOT_FOUND
 */
elib_timer_err_t elib_timer_get_remaining(elib_timer_ctx_t *ctx,
                                          uint32_t timer_id,
                                          uint32_t *out_remaining_ms);

/* ------------------------------------------------------------------ 驱动 */
/**
 * @brief 定时器驱动（tick ISR 中调用；每此喂入经过毫秒数）
 *
 * 单次调用最多一轮到期判定（丢拍语义）；IMMEDIATE 定时器到期回调在本
 * 调用内同步执行；DELAYED 只置 pending 由 process_pending 提取。
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_timer_err_t elib_timer_manager(elib_timer_ctx_t *ctx, uint32_t ms_elapsed);

/**
 * @brief 提取执行 DELAYED 模式的 pending 回调（主循环调用，按槽位序号顺序执行）
 * @return OK / INVALID_PARAM / NOT_INITIALIZED
 */
elib_timer_err_t elib_timer_process_pending(elib_timer_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_TIMER_H */
