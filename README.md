# elib-soft-timer

轻量级软件定时器实现，适用于资源受限的嵌入式系统。

## 特性

- **零动态内存**：所有资源由用户静态分配
- **动态周期配置**：运行时可修改定时器周期
- **1ms时间精度**：基于用户提供的时间基准
- **双模式回调执行**：
  - 立即模式：在定时器管理器中立即执行回调（适用于中断上下文）
  - 延迟模式：标记为待执行，在主循环中执行
- **可配置循环次数**：0 = 无限循环，N = 执行N次后停止
- **完整生命周期管理**：创建、启动、停止、暂停、恢复、删除

## 快速开始

### 1. 定义定时器上下文和实例

```c
/* 最大定时器数量 */
#define MAX_TIMERS 4

/* 用户分配的上下文和定时器数组 */
static elib_timer_ctx_t g_timer_ctx;
static elib_timer_instance_t g_timers[MAX_TIMERS];
```

### 2. 初始化定时器管理器

```c
void app_init(void) {
    elib_timer_init(&g_timer_ctx, g_timers, MAX_TIMERS);
}
```

### 3. 创建并启动周期性定时器

```c
/* LED切换回调 */
static void led_toggle_cb(void *user_data) {
    (void)user_data;
    toggle_led();
}

/* 创建500ms周期性LED定时器（立即模式，无限循环） */
uint32_t led_timer_id;
elib_timer_create(&g_timer_ctx, 500, led_toggle_cb, NULL,
                  ELIB_TIMER_MODE_IMMEDIATE, ELIB_TIMER_LOOP_INFINITE, &led_timer_id);
elib_timer_start(&g_timer_ctx, led_timer_id);
```

### 4. 在tick中断或主循环中调用定时器管理器

```c
/* 在tick中断中（1ms tick） */
void SysTick_Handler(void) {
    elib_timer_manager(&g_timer_ctx, 1);
}
```

### 5. 对于延迟模式，在主循环中处理待执行回调

```c
/* 在主循环中 */
while (1) {
    elib_timer_process_pending(&g_timer_ctx);
    /* 其他主循环任务 */
}
```

## API参考

### 生命周期
- `elib_timer_init(ctx, timers, max_timers)` - 初始化定时器管理器
- `elib_timer_deinit(ctx)` - 反初始化定时器管理器

### 定时器操作
- `elib_timer_create(ctx, period_ms, callback, user_data, exec_mode, loop_count, out_timer_id)` - 创建定时器
- `elib_timer_delete(ctx, timer_id)` - 删除定时器
- `elib_timer_start(ctx, timer_id)` - 启动定时器
- `elib_timer_stop(ctx, timer_id)` - 停止定时器
- `elib_timer_pause(ctx, timer_id)` - 暂停定时器
- `elib_timer_resume(ctx, timer_id)` - 恢复定时器

### 动态配置
- `elib_timer_set_period(ctx, timer_id, period_ms)` - 运行时设置周期
- `elib_timer_set_loop_count(ctx, timer_id, loop_count)` - 运行时设置循环次数

### 状态查询
- `elib_timer_get_remaining(ctx, timer_id, out_remaining_ms)` - 获取剩余时间

### 定时器管理
- `elib_timer_manager(ctx, ms_elapsed)` - 更新定时器状态（在tick中断中调用）
- `elib_timer_process_pending(ctx)` - 处理待执行回调（在主循环中调用）

## 定时器状态

```
UNUSED → STOPPED → RUNNING → EXPIRED
              ↑         │
              │    ┌────┴────┐
              └───►│  PAUSED │
                   └─────────┘
```

## 执行模式

### 立即模式（IMMEDIATE）
当定时器到期时，回调立即在 `elib_timer_manager()` 中执行。适用于中断上下文。

### 延迟模式（DELAYED）
定时器到期时设置待执行标志。当调用 `elib_timer_process_pending()` 时执行回调。适用于主循环上下文。

## 循环次数

- `loop_count = ELIB_TIMER_LOOP_INFINITE`：无限循环（定时器永久运行）
- `loop_count = N`：执行回调N次后停止

## 示例：传感器轮询

```c
static void sensor_read_cb(void *user_data) {
    sensor_data_t *data = (sensor_data_t *)user_data;
    read_sensor(data);
}

sensor_data_t sensor_data;
uint32_t sensor_timer_id;

/* 创建10秒传感器轮询定时器（延迟模式） */
elib_timer_create(&g_timer_ctx, 10000, sensor_read_cb, &sensor_data,
                  ELIB_TIMER_MODE_DELAYED, ELIB_TIMER_LOOP_INFINITE, &sensor_timer_id);
elib_timer_start(&g_timer_ctx, sensor_timer_id);

/* 在主循环中 */
int main(void) {
    while (1) {
        elib_timer_process_pending(&g_timer_ctx);
        process_sensor_data(&sensor_data);
        delay_ms(100);
    }
}
```

## 编译

将 `elib-soft-timer/src` 和 `elib-soft-timer/include` 添加到您的构建路径中。

## 测试

```bash
gcc -o test_elib_timer elib-soft-timer/test/test_elib_timer.c \
    elib-soft-timer/src/elib_timer_core.c -I elib-soft-timer/include
./test_elib_timer
```

## 许可证

MIT License