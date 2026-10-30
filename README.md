# elib-soft-timer

资源受限嵌入式系统用的轻量软件定时器：零动态内存、零全局变量、纯 C99，单头伞 API。

当前版本：**2.0.0**（`ELIB_TIMER_VERSION`），单头 `include/elib_timer.h`。

## 特性

- **零动态内存 / 零全局**：全部状态由用户静态提供的 `ctx + 实例数组` 承载
- **单头伞 API**：全部公共类型与函数集中在 `elib_timer.h`，无子头
- **1ms 时间域**：由外部喂入经过毫秒数驱动
- **双执行模式**：
  - `IMMEDIATE`：到期回调在 `elib_timer_manager()` 调用方上下文内直接执行（适合 tick ISR）
  - `DELAYED`：到期只置 pending 标志，主循环经 `elib_timer_process_pending()` 提取执行
- **完整生命周期**：create / start / stop / pause / resume / delete
- **动态配置**：运行中修改周期（按比例缩放剩余时间）与循环次数
- **可选临界区钩子**：支持 ISR 与主循环跨上下文安全使用

## 快速开始

### 1. 静态定义上下文与实例池

```c
#include "elib_timer.h"   /* 构建时加 -I<repo>/include */

#define MAX_TIMERS 4

static elib_timer_ctx_t g_timer_ctx;
static elib_timer_instance_t g_timers[MAX_TIMERS];

static void app_init(void)
{
    elib_timer_init(&g_timer_ctx, g_timers, MAX_TIMERS);
}
```

### 2. 创建并启动周期定时器

```c
static void led_toggle_cb(void *user_data)
{
    (void)user_data;
    toggle_led();
}

static uint32_t g_led_timer_id;

static void app_start(void)
{
    elib_timer_create(&g_timer_ctx, 500u, led_toggle_cb, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, ELIB_TIMER_LOOP_INFINITE,
                      &g_led_timer_id);
    elib_timer_start(&g_timer_ctx, g_led_timer_id);
}
```

### 3. 驱动时钟（tick ISR）与提取延迟回调（主循环）

```c
/* 1ms tick 中断 */
void SysTick_Handler(void)
{
    elib_timer_manager(&g_timer_ctx, 1u);
}

/* 主循环 */
for (;;) {
    elib_timer_process_pending(&g_timer_ctx);
    /* ... 其他任务 ... */
}
```

## API 一览

| 分组 | 函数 | 说明 |
|------|------|------|
| 生命周期 | `elib_timer_init(ctx, timers, max_timers)` | 初始化（memset 覆盖重初始化；非法配置拒收） |
| | `elib_timer_deinit(ctx)` | 反初始化（NULL 安全） |
| 实例 | `elib_timer_create(ctx, period_ms, callback, user_data, exec_mode, loop_count, out_id)` | 创建；ID = 首个 UNUSED 槽位下标 |
| | `elib_timer_delete(ctx, timer_id)` | 删除；清 pending，槽位回归 UNUSED |
| 状态 | `elib_timer_start(ctx, timer_id)` | 启动/重开；remaining 重置为 period |
| | `elib_timer_stop(ctx, timer_id)` | 停止；**全重置**（remaining 复位 period） |
| | `elib_timer_pause(ctx, timer_id)` | 暂停；remaining 冻结 |
| | `elib_timer_resume(ctx, timer_id)` | 恢复；从冻结值继续 |
| 配置 | `elib_timer_set_period(ctx, timer_id, period_ms)` | 运行中改周期（RUNNING/PAUSED 比例缩放） |
| | `elib_timer_set_loop_count(ctx, timer_id, loop_count)` | 运行中改循环次数（立即重新计数） |
| 查询 | `elib_timer_get_remaining(ctx, timer_id, &remaining_ms)` | 剩余毫秒数 |
| 驱动 | `elib_timer_manager(ctx, ms_elapsed)` | tick ISR 喂入经过时间 |
| | `elib_timer_process_pending(ctx)` | 主循环提取 DELAYED 回调（槽位序号顺序） |

错误码：`ELIB_TIMER_OK` / `ERR_INVALID_PARAM` / `ERR_NOT_INITIALIZED` / `ERR_NOT_FOUND` / `ERR_FULL` / `ERR_ALREADY_RUNNING` / `ERR_NOT_RUNNING`（`ERR_EXECUTION_FAILED` 为保留位）。

## 状态机

```
UNUSED --create--> STOPPED --start--> RUNNING --暂停--> PAUSED
                     ^                  |  ^               |
                     |                  |  +---resume------+
                     |                  |
                     |                  +--到期且 loop 耗尽--> EXPIRED
                     |                                        |
                     +---------------- stop -----------------+
                                                              |
                                    EXPIRED --start--> RUNNING（重开）
```

- `delete`：任意非 UNUSED 态 → UNUSED
- `stop`：RUNNING / PAUSED / EXPIRED → STOPPED（remaining 全重置为 period）
- `start`：STOPPED / PAUSED / EXPIRED → RUNNING（remaining 重置为 period，loop 计数重置）

## 语义契约

### 时间驱动与丢拍

`elib_timer_manager(ctx, ms_elapsed)` 由外部喂入自上次调用以来经过的毫秒数（典型 1ms tick）。

**丢拍语义**：单次 `manager` 调用最多产生一次到期判定。当 `ms_elapsed` 大于剩余时间时，越界的多余时间被丢弃、周期从整周期重新开始——表现为"调用一次、最多到期一次"的漂移。因此 tick 粒度应远小于最小定时周期。

### 周期修改的缩放规则

`set_period` 对运行中的定时器按比例缩放剩余时间：

```
remaining' = remaining * new_period / old_period   (64 位中间量，向下取整，0 钳 1)
```

| 状态 | 行为 |
|------|------|
| RUNNING | 比例缩放，继续倒计数 |
| PAUSED | 比例缩放冻结值，resume 后按新值继续 |
| STOPPED | 不缩放（start 时统一重置为 period） |
| EXPIRED | 不缩放（remaining 保持 0，start 重开时才重置） |

### 回调重入契约

回调内**只允许操作自己**：

- 允许：对本定时器 `delete` / `stop` / `start` / `set_period` / `set_loop_count` / `get_remaining`
- 禁止：`create`、删除其他定时器、重入 `manager` / `process_pending`（未定义行为）

`IMMEDIATE` 模式下，manager 在回调返回后会复查 `state == RUNNING` 才装填下一轮 remaining：回调内把自己 `delete`/`stop` 的，本轮不会再被复活/装填。

### DELAYED 模式

到期时只置 `pending_execution` 标志（状态照常推进）。主循环调用 `elib_timer_process_pending()` 时按槽位序号顺序执行并清除标志。

待执行期间删除定时器会连带清除 pending 标志——已删除的定时器不会再引爆回调。

## 并发模型（ISR + 主循环）

`elib_timer_manager()` 供 tick ISR 调用，其余 API 供主循环调用。库内所有共享状态变更段由临界区钩子包裹；用户按需在编译选项中预定义（须为保存/恢复式、支持嵌套）：

```c
/* 例：Cortex-M 下用 PRIMASK 保存/恢复（非嵌套） */
static uint32_t g_crit_save;

#define ELIB_TIMER_CRITICAL_ENTER()      \
    do {                                 \
        g_crit_save = __get_PRIMASK();   \
        __disable_irq();                 \
    } while (0)

#define ELIB_TIMER_CRITICAL_EXIT()       \
    do {                                 \
        __set_PRIMASK(g_crit_save);      \
    } while (0)
```

如需嵌套调用，请改用"保存/恢复栈 + 深度计数"的实现。

未定义时两个钩子为空宏 = 声明单上下文使用（如仅在主循环内调用 manager 与 API）。

回调执行期间**不持有**临界区。

## 编译

将 `include/` 加入头文件搜索路径，源文件编译为普通 C99 目标文件：

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude \
    -c src/elib_timer_core.c -o elib_timer_core.o
```

源文件直连伞头（`#include "elib_timer.h"`），不依赖任何相对路径，无随库构建系统。

## 测试

`test/` 下 11 个独立测试文件，每件自带 `main()`，可分别编译运行（宿主机有 gcc 时）：

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Iinclude "$t" src/elib_timer_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| 文件 | 覆盖 |
|------|------|
| `test_init.c` | init/deinit、非法配置、重初始化、deinit 后全 API 拒收 |
| `test_create.c` | 参数校验、ID 顺序、池满、非法 exec_mode、槽位复用 |
| `test_start_stop.c` | start/stop、全重置、EXPIRED 重开、双 start |
| `test_pause.c` | 暂停冻结、恢复续跑、状态门禁 |
| `test_set_period.c` | 四态缩放差异、0 钳 1、非法参数 |
| `test_set_loop_count.c` | 运行中改参、无限↔有限转换、一次性语义 |
| `test_expire_immediate.c` | 到期边界、循环 N 次、丢拍、回调内自删/自停/自改周期 |
| `test_expire_delayed.c` | pending 置位/提取、pending+delete 竞态、复用无陈旧标志、混合模式 |
| `test_delete.c` | 删除、ID 越界、运行中删除、复用净身 |
| `test_get_remaining.c` | 四态剩余值、参数校验 |
| `test_manager.c` | 0 毫秒、等值边界、分批累积、仅 RUNNING 被驱动、多槽批量 tick |

## 许可证

MIT License（见 `LICENSE`）。
