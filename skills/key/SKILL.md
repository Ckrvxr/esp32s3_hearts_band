---
name: key
description: 为嵌入式项目（Zynq / ESP32 / STM32）编写基于 GPIO + FreeRTOS 的按键输入处理代码的通用指南
---

## 架构总览

### 分层设计

```
┌──────────────────────────────────┐
│  Key Handler     (平台无关)      │ ← 状态切换、参数修改逻辑
│  Key State Machine (平台无关)    │ ← 防抖、长按检测、事件产生
│  Key_Dispatch_Event (平台无关)   │ ← 按键 ID → handler 路由
├──────────────────────────────────┤
│  HAL 抽象层     (需平台适配)      │ ← gpio_read, gpio_init, get_tick
├──────────────────────────────────┤
│  Zynq / ESP32 / STM32 (平台相关) │ ← 具体 HAL 实现
└──────────────────────────────────┘
```

### 文件职责

| 文件 | 职责 |
|------|------|
| `key.h` | 事件枚举、按键 ID 枚举、按键句柄结构体、API 声明、HAL 接口声明 |
| `key.c` | 按键状态机、事件分发、所有按键 Handler 函数（状态切换 + 参数修改） |
| `display.h` | 通过 `extern volatile` 提供 `currentState` / `menu_index` / `slect_index` 供 key.c 读写 |

### 核心变量关系

- display task **只读**：根据 `currentState` / `menu_index` / `slect_index` 绘制
- key task **读写**：按键事件触发状态切换、修改索引、调用模块 API 改参数
- 其他模块（dds / fir 等）提供公开 API 供 key handler 调用

---

## 按键事件与状态机

### 事件类型

```c
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SINGLE_CLICK,
    KEY_EVENT_LONG_PRESS_HOLD
} Key_Event_Type_t;
```

### 按键 ID

```c
typedef enum {
    KEY_ID_UP      = 0,
    KEY_ID_DOWN    = 1,
    KEY_ID_CONFIRM = 2,
    KEY_ID_CANCEL  = 3,
    KEY_COUNT
} Key_ID_t;
```

### 按键句柄

```c
typedef struct {
    uint8_t id;                // 按键 ID（KEY_ID_UP 等）
    uint8_t gpio_pin;          // GPIO 引脚号（平台相关）
    void    *gpio_port;        // GPIO 端口实例（平台相关，如 XGpioPs*/GPIO_TypeDef*）

    Key_Event_Type_t last_event;
    TickType_t press_start_tick;    // 按下时刻 tick
    TickType_t last_trigger_tick;   // 上次长按重复触发 tick
    uint8_t is_pressed;             // 当前是否按下
    uint8_t long_press_triggered;   // 是否已触发长按
} Key_Handle_t;
```

### 防抖与长按时序参数

```c
enum {
    KEY_DEBOUNCE_MS          = 50,   // 防抖时间
    KEY_LONG_PRESS_MS        = 800,  // 长按触发时间
    KEY_LONG_PRESS_REPEAT_MS = 200,  // 长按重复间隔
};
```

### 状态机逻辑

```
释放状态（is_pressed=0）
  → 读取 GPIO=低电平（按下）
    → is_pressed=1, press_start_tick=now, long_press_triggered=0

按下状态（is_pressed=1）
  → 每 tick 检查持续时间
    → 达到 KEY_LONG_PRESS_MS 且未触发 → 触发 KEY_EVENT_LONG_PRESS_HOLD
    → 之后每 KEY_LONG_PRESS_REPEAT_MS → 重复触发 KEY_EVENT_LONG_PRESS_HOLD
  → 读取 GPIO=高电平（释放）
    → 若持续时间 >= KEY_DEBOUNCE_MS 且未触发长按 → 触发 KEY_EVENT_SINGLE_CLICK
    → is_pressed=0
```

---

## 事件分发

```c
static void Key_Dispatch_Event(uint8_t key_id, Key_Event_Type_t event) {
    switch (key_id) {
        case KEY_ID_UP:      Key_Handler_Up(key_id, event);      break;
        case KEY_ID_DOWN:    Key_Handler_Down(key_id, event);    break;
        case KEY_ID_CONFIRM: Key_Handler_Confirm(key_id, event); break;
        case KEY_ID_CANCEL:  Key_Handler_Cancel(key_id, event);  break;
    }
}
```

四个 Handler 函数分别对应 UP / DOWN / CONFIRM / CANCEL 四个按键，内部通过 `switch (currentState)` 区分不同屏幕下的行为。

---

## 添加新屏幕的按键响应 —— 完整步骤

### 1. 确认状态变量 extern

确保 key.c 中有：

```c
extern volatile DisplayState_t currentState;
extern volatile uint8_t menu_index;
extern volatile uint8_t slect_index;
```

### 2. 在每个 Handler 中加入新分支

```c
static void Key_Handler_Confirm(uint8_t key_id, Key_Event_Type_t event) {
    if (event == KEY_EVENT_SINGLE_CLICK) {
        // ... 现有分支 ...
        else if (currentState == STATE_YOUR_NEW_SCREEN) {
            if (slect_index == 0) {
                // 浏览模式：进入编辑或跳转
                slect_index = menu_index;
            } else if (slect_index == 1) {
                // 编辑模式：确认
                YourModule_Exec();
                slect_index = 0;
            }
        }
    }
}

static void Key_Handler_Cancel(uint8_t key_id, Key_Event_Type_t event) {
    if (event == KEY_EVENT_SINGLE_CLICK) {
        else if (currentState == STATE_YOUR_NEW_SCREEN) {
            if (slect_index == 0) {
                // 返回上级
                currentState = STATE_PARENT;
                menu_index = 1;
                slect_index = 0;
            } else {
                // 取消编辑
                YourModule_Cancel();
                slect_index = 0;
            }
        }
    }
}
```

### 3. Up/Down 的通用模式

```c
static void Key_Handler_Up(uint8_t key_id, Key_Event_Type_t event) {
    if (event == KEY_EVENT_SINGLE_CLICK) {
        else if (currentState == STATE_YOUR_NEW_SCREEN) {
            if (slect_index == 0) {
                // 上移菜单项
                if (menu_index > 1) menu_index--;
            } else if (slect_index == 1) {
                // 修改参数值（正向）
                YourModule_Adjust(+step);
            }
        }
    } else if (event == KEY_EVENT_LONG_PRESS_HOLD) {
        else if (currentState == STATE_YOUR_NEW_SCREEN) {
            if (slect_index == 1) {
                // 长按连续修改
                YourModule_Adjust(+step);
            }
        }
    }
}
```

---

## 菜单 / 编辑两级模式

### 浏览模式（slect_index == 0）

- **UP/DOWN** → 切换 `menu_index`（在 1..max_items 范围内循环或截止）
- **CONFIRM** → `slect_index = menu_index`（进入选中项的编辑模式）
- **CANCEL** → 返回上一级状态，重置 `menu_index = 1, slect_index = 0`

### 编辑模式（slect_index >= 1）

- **UP** → 增量调整参数（正向）
- **DOWN** → 减量调整参数（负向）
- **CONFIRM** → 确认修改，`slect_index = 0`
- **CANCEL** → 取消修改，`slect_index = 0`
- **长按 UP/DOWN** → 连续加速调整参数

### 变量约定

- `menu_index`：1-indexed，当前菜单项编号
- `slect_index`：0=浏览模式，1+=正在编辑第 menu_index 项

### 返回上级状态时的清理

切换状态时**必须**同时重置 `menu_index = 1` 和 `slect_index = 0`，避免残留值干扰新状态的显示和导航。

---

## 长按加速模式

在参数编辑场景下，长按超过一定时长后可加速步进：

```c
// 示例：频率编辑的指数加速
uint32_t duration_ms = (uint32_t)(tick_now - press_start_tick) * 1000 / configTICK_RATE_HZ;
if (duration_ms >= FREQ_ACCEL_THRESHOLD_MS) {   // 如 3000ms
    uint32_t excess_ms = duration_ms - FREQ_ACCEL_THRESHOLD_MS;
    uint32_t seconds = excess_ms / 1000;
    // 按秒分段提高步进
    switch (seconds) {
        case 0: step = 100;    break;
        case 1: step = 1000;   break;
        case 2: step = 10000;  break;
        default: step = 10000; break;
    }
}
```

也可按需加入对数插值（如 `step * frac_tbl[tenths-1] / 100`）实现平滑过渡。

---

## FreeRTOS 任务结构

```c
void Key_Task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(50);  // 20Hz 轮询

    while (1) {
        TickType_t now = xTaskGetTickCount();
        for (uint8_t i = 0; i < KEY_COUNT; i++) {
            // 步骤 1：读取 GPIO 电平
            uint8_t level = Key_HAL_ReadPin(keys[i].gpio_pin);

            // 步骤 2：更新按键状态机（防抖 + 长按检测）
            if (level == 0) {  // 按下（低电平）
                if (!keys[i].is_pressed) {
                    keys[i].is_pressed = 1;
                    keys[i].press_start_tick = now;
                    keys[i].long_press_triggered = 0;
                } else {
                    // 长按检测与重复触发（参考状态机小节）
                    // ...
                }
            } else {           // 释放（高电平）
                if (keys[i].is_pressed) {
                    // 检查防抖 → 触发单击事件
                    // ...
                    keys[i].is_pressed = 0;
                }
            }
        }
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}
```

使用 `vTaskDelayUntil` 保持固定周期，避免任务执行时间抖动。

---

## HAL 抽象层设计与平台适配

### HAL 接口定义

```c
// key_hal.h —— 平台需实现的 HAL 接口

/// 初始化 GPIO 引脚为输入模式（含上拉配置）
void Key_HAL_InitPin(uint8_t gpio_pin, void *gpio_port);

/// 读取 GPIO 引脚电平，返回 0=按下(低电平) / 1=释放(高电平)
uint8_t Key_HAL_ReadPin(uint8_t gpio_pin, void *gpio_port);

/// 获取当前系统 tick（毫秒），用于定时计算
TickType_t Key_HAL_GetTick(void);
```

### Zynq (Xilinx SDK / Vitis)

```c
// key_hal_zynq.c

#include "xgpiops.h"
#include "xparameters.h"

static XGpioPs Gpio;

void Key_HAL_InitPin(uint8_t gpio_pin, void *gpio_port) {
    (void)gpio_port;
    XGpioPs_SetDirectionPin(&Gpio, gpio_pin, 0);  // 输入
}

uint8_t Key_HAL_ReadPin(uint8_t gpio_pin, void *gpio_port) {
    (void)gpio_port;
    return XGpioPs_ReadPin(&Gpio, gpio_pin);
}

TickType_t Key_HAL_GetTick(void) {
    return xTaskGetTickCount();
}

// 初始化（只调用一次）
void Key_HAL_Init(void) {
    XGpioPs_Config *ConfigPtr;
    ConfigPtr = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
    XGpioPs_CfgInitialize(&Gpio, ConfigPtr, ConfigPtr->BaseAddr);
}
```

| 关键点 | 说明 |
|--------|------|
| GPIO 引脚 | PS7 MIO 引脚号（如 UP=54, DOWN=55, CONFIRM=56, CANCEL=57） |
| GPIO 驱动 | XGpioPs — Zynq PS7 自带 GPIO 控制器 |
| 初始化顺序 | `LookupConfig` → `CfgInitialize` → `SetDirectionPin(input)` |

### ESP32 (ESP-IDF, FreeRTOS)

```c
// key_hal_esp32.c

#include "driver/gpio.h"

static gpio_num_t pin_map[KEY_COUNT];

void Key_HAL_InitPin(uint8_t gpio_pin, void *gpio_port) {
    (void)gpio_port;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

uint8_t Key_HAL_ReadPin(uint8_t gpio_pin, void *gpio_port) {
    (void)gpio_port;
    return gpio_get_level((gpio_num_t)gpio_pin);
}

TickType_t Key_HAL_GetTick(void) {
    return xTaskGetTickCount();
}
```

| 关键点 | 说明 |
|--------|------|
| GPIO 引脚 | 使用 `GPIO_NUM_x` 宏（如 `GPIO_NUM_0`） |
| 内部上拉 | `pull_up_en = GPIO_PULLUP_ENABLE` 避免外部上拉电阻 |
| FreeRTOS | ESP-IDF 默认启用 FreeRTOS，`xTaskGetTickCount` 可用 |

### STM32 (STM32Cube HAL, FreeRTOS / 裸机)

```c
// key_hal_stm32.c

#include "stm32f4xx_hal.h"  // 按具体系列包含

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} Stm32GpioCfg;

static Stm32GpioCfg pin_cfg[KEY_COUNT];

void Key_HAL_InitPin(uint8_t gpio_pin, void *gpio_port) {
    // GPIO 初始化通常由 CubeMX MX_GPIO_Init() 完成
    // 此处只需记录端口和引脚号
    pin_cfg[gpio_pin].port = (GPIO_TypeDef *)gpio_port;
    pin_cfg[gpio_pin].pin  = gpio_pin;
    // 也可直接在此处调用 HAL_GPIO_Init 配置为上拉输入
}

uint8_t Key_HAL_ReadPin(uint8_t gpio_pin, void *gpio_port) {
    (void)gpio_port;
    return HAL_GPIO_ReadPin(pin_cfg[gpio_pin].port, pin_cfg[gpio_pin].pin);
}

TickType_t Key_HAL_GetTick(void) {
    #ifdef FREERTOS_ENABLED
        return xTaskGetTickCount();
    #else
        return HAL_GetTick();  // 裸机使用 SysTick
    #endif
}
```

| 关键点 | 说明 |
|--------|------|
| 引脚初始化 | CubeMX `MX_GPIO_Init()` 已配置好 GPIO，key task 只需记录端口指针 |
| 端口类型 | 使用 `GPIO_TypeDef *` 指针（如 `GPIOA`、`GPIOB`） |
| Tick 来源 | FreeRTOS 用 `xTaskGetTickCount()`；裸机用 `HAL_GetTick()` |
| 裸机替代 | 无 FreeRTOS 时可将按键扫描放入 `while(1)` 主循环或用定时器中断 |

### 核心逻辑与平台无关

```c
// key.c 核心部分与平台无关，使用 HAL 接口

static Key_Handle_t keys[KEY_COUNT] = {
    { .id = KEY_ID_UP,      .gpio_pin = 54, .gpio_port = NULL },
    { .id = KEY_ID_DOWN,    .gpio_pin = 55, .gpio_port = NULL },
    { .id = KEY_ID_CONFIRM, .gpio_pin = 56, .gpio_port = NULL },
    { .id = KEY_ID_CANCEL,  .gpio_pin = 57, .gpio_port = NULL },
};

void Key_Init(void) {
    // 平台相关：初始化 GPIO 控制器
    Key_HAL_PlatformInit();

    // 平台无关：初始化每个按键的 GPIO 引脚
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        Key_HAL_InitPin(keys[i].gpio_pin, keys[i].gpio_port);
        keys[i].last_event = KEY_EVENT_NONE;
        keys[i].is_pressed = 0;
        keys[i].long_press_triggered = 0;
    }
}
```

`gpio_pin` 和 `gpio_port` 在不同平台设置不同初始值：

- **Zynq**: `gpio_pin = 54~57`, `gpio_port = NULL`（无端口选择）
- **ESP32**: `gpio_pin = GPIO_NUM_0~3`, `gpio_port = NULL`
- **STM32**: `gpio_pin = GPIO_PIN_0`, `gpio_port = (void*)GPIOA`

---

## 事件类型选择原则

| 场景 | 推荐事件 |
|------|----------|
| 菜单导航（上/下） | `KEY_EVENT_SINGLE_CLICK` |
| 进入/确认/退出 | `KEY_EVENT_SINGLE_CLICK` |
| 单步修改值 | `KEY_EVENT_SINGLE_CLICK` |
| 连续快速修改值 | `KEY_EVENT_LONG_PRESS_HOLD` |
| 只读/非交互状态（学习/校准中） | handler 直接 `return` 忽略所有事件 |

---

## 跨模块变量引用约定

```c
// key.c 顶部引用 display 状态变量
extern volatile DisplayState_t currentState;
extern volatile uint8_t menu_index;
extern volatile uint8_t slect_index;

// 通过模块公开的 API 修改参数（而非直接操作内部变量）
// 正确：
DDS_Vpp_PlusorMinus(100);      // dds.c 的接口函数
// 错误：
// dds_vpp += 100;             // 绕过封装，破坏模块边界
```

---

## 与 display skill 的关系

| 方面 | display skill | key skill |
|------|---------------|-----------|
| 职责 | 渲染当前状态到屏幕 | 检测按键并切换状态 |
| 读写状态 | 只读 `currentState` / `menu_index` / `slect_index` | 读写这些变量 |
| 新增屏幕步骤 | 添加 enum、创建 Draw 函数、加入 switch、key 中处理 | 在每个 handler 中加入新分支 |
| 依赖的模块 | u8g2（图形库） | 无（仅依赖 GPIO 和 FreeRTOS） |

两个 skill 配合使用：display 负责"怎么画"，key 负责"按了什么、切到什么状态"。

