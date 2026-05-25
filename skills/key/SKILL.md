---
name: key
description: ESP32-S3 GPIO 按键驱动 —— 消抖 + 状态机 + 单击/双击/长按事件，基于 ESP-IDF + FreeRTOS
---

## 架构总览

### 分层设计

```
┌──────────────────────────────────────────┐
│  Key_Event_Handler  (按键行为分派)        │ ← 二级 switch(key_id→evt)
├──────────────────────────────────────────┤
│  Key_Scan()         (状态机 + 消抖 + 事件)│ ← 返回 enum KeyEvent_t
├──────────────────────────────────────────┤
│  driver/gpio.h      (ESP-IDF GPIO)        │ ← gpio_get_level / gpio_config
└──────────────────────────────────────────┘
```

### 文件职责

| 文件 | 职责 |
|------|------|
| `key.h` | `KeyEvent_t`、`KeyIndex_t` 枚举；`Key_Init()`、`Key_Scan()`、`Key_Event_Handler()` 声明 |
| `key.c` | 消抖 + 四键状态机 + 事件产生 + `Key_Event_Handler()` 二级分发 |
| `display.h` | extern volatile 提供 `currentState` / `menu_index` / `slect_index` 供 handler 读写 |

### 核心变量关系

- display task **只读**：根据 `currentState` / `menu_index` / `slect_index` 绘制
- key task **读写**：`Key_Event_Handler()` 接收事件后修改这些变量
- 其他模块提供公开 API 供 key handler 调用

---

## 硬件参数

| 参数 | 值 |
|------|----|
| 消抖 | 60ms (3 次 × 20ms 扫描) |
| 长按阈值 | 1000ms |
| 双击窗口 | 400ms |
| 扫描周期 | 20ms (50Hz) |
| 按键电平 | 低电平有效 (按下=0) |
| 上拉方式 | GPIO 内部上拉 |

### 引脚映射

| 功能 | GPIO | 备注 |
|------|------|------|
| CANCEL | 6 | Key 1 |
| CONFIRM | 7 | Key 2 |
| DOWN | 15 | Key 3 |
| UP | 16 | Key 4 |

---

## 事件枚举

```c
typedef enum {
    KEY_EVENT_NONE,
    KEY_EVENT_CLICK,
    KEY_EVENT_DOUBLE_CLICK,
    KEY_EVENT_LONG_PRESS,
} KeyEvent_t;
```

### 事件类型选择原则

| 场景 | 推荐事件 |
|------|----------|
| 菜单导航 (上/下) | `KEY_EVENT_CLICK` |
| 进入/确认/退出 | `KEY_EVENT_CLICK` |
| 快捷操作 (如回到顶部) | `KEY_EVENT_LONG_PRESS` |
| 特殊功能 (如进入设置) | `KEY_EVENT_DOUBLE_CLICK` |

### 按键 ID

```c
typedef enum {
    KEY_IDX_UP = 0,
    KEY_IDX_DOWN,
    KEY_IDX_CONFIRM,
    KEY_IDX_CANCEL,
} KeyIndex_t;
```

---

## 按键上下文结构体

```c
typedef enum {
    KEY_STATE_IDLE,
    KEY_STATE_PRESSED,
    KEY_STATE_WAIT_DOUBLE,
    KEY_STATE_WAIT_RELEASE,
} key_state_t;

typedef struct {
    uint8_t gpio;
    uint8_t state;
    uint8_t curr;
    uint8_t prev;
    uint8_t cnt;
    uint8_t long_press_sent;
    TickType_t tick;
} key_ctx_t;
```

### 字段说明

| 字段 | 说明 |
|------|------|
| `gpio` | 按键对应的 GPIO 引脚号 |
| `state` | 当前状态机状态 |
| `curr` / `prev` | 当前 / 上一次原始电平 (消抖用) |
| `cnt` | 连续一致采样计数 |
| `long_press_sent` | 是否已输出长按事件 |
| `tick` | 状态进入时刻的 FreeRTOS tick |

---

## 状态机

```
IDLE ──按下稳定──► PRESSED (记录 tick)

PRESSED ──释放 (未长按) ──────► WAIT_DOUBLE (记录 tick)
PRESSED ──释放 (已发长按) ────► WAIT_RELEASE
PRESSED ──按住 >=1s ─────────► LONG_PRESS (一次性, long_press_sent=1)

WAIT_DOUBLE ──400ms 内再按 ──► DOUBLE_CLICK → WAIT_RELEASE
WAIT_DOUBLE ──超时 400ms ────► CLICK → IDLE

WAIT_RELEASE ──释放──► IDLE
```

关键行为：
- 长按只触发**一次** `LONG_PRESS`，不会重复触发
- 长按释放后**不会**再生成 `CLICK` 或 `DOUBLE_CLICK`
- `CLICK` 在释放后 400ms 无再次按下时才生成（避免与双击混淆）
- `DOUBLE_CLICK` 在 400ms 内检测到再次按压时立即生成

---

## API 参考

| 函数 | 签名 | 说明 |
|------|------|------|
| `Key_Init()` | `void Key_Init(void)` | GPIO 初始化 (内部上拉，低电平有效) |
| `Key_Scan()` | `KeyEvent_t Key_Scan(uint8_t *out_key)` | 单次扫描，返回事件类型，按键 ID 通过出参带回 |
| `Key_Event_Handler()` | `void Key_Event_Handler(KeyEvent_t evt, uint8_t key_id)` | 二级 switch 分派事件，各 case 填写具体行为 |

### Key_Init

内部调用 `gpio_config()` 配置四个 GPIO 为上拉输入模式，无中断。

```c
void Key_Init(void)
{
    key_gpio_init();
    ESP_LOGI(TAG, "Key driver initialized");
}
```

### Key_Scan

每个扫描周期遍历 4 个按键，执行消抖 → 状态机 → 事件产生。

- 消抖算法：连续 3 次采样一致才认为电平稳定
- 状态机根据稳定后的电平变化驱动事件产生
- 有事件时立即返回该按键 ID 和事件类型；无事件返回 `KEY_EVENT_NONE`

### Key_Event_Handler

二级 switch 结构：外层 `switch(key_id)`、内层 `switch(evt)`。当前所有 case 只打日志，后续填入实际行为。

```c
void Key_Event_Handler(KeyEvent_t evt, uint8_t key_id)
{
    switch (key_id) {
        case KEY_IDX_UP:
            switch (evt) {
                case KEY_EVENT_CLICK:        /* UP CLICK */     break;
                case KEY_EVENT_DOUBLE_CLICK: /* UP DOUBLE */    break;
                case KEY_EVENT_LONG_PRESS:   /* UP LONG */      break;
                default: break;
            }
            break;
        case KEY_IDX_DOWN:
            // ...
        case KEY_IDX_CONFIRM:
            // ...
        case KEY_IDX_CANCEL:
            // ...
    }
}
```

---

## FreeRTOS 任务结构

`vKeyTask` 在 `main.c` 中创建，20ms 固定周期：

```c
static void vKeyTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint8_t key_id;

    while (1) {
        KeyEvent_t evt = Key_Scan(&key_id);
        if (evt != KEY_EVENT_NONE) {
            Key_Event_Handler(evt, key_id);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}
```

使用 `vTaskDelayUntil` 保持固定周期，避免任务执行时间抖动。

创建语句：

```c
xTaskCreatePinnedToCore(vKeyTask, "KeyTask", 2048, NULL, 1, NULL, tskNO_AFFINITY);
```

| 任务 | 周期 | 优先级 | 说明 |
|------|------|--------|------|
| `MainTask` | 2s | 1 | 心跳日志 |
| `vDisplayTask` | 20ms (50Hz) | 2 | 固定周期调用 `Display_Refresh()` |
| `vKeyTask` | 20ms (50Hz) | 1 | 调用 `Key_Scan()` → `Key_Event_Handler()` |

---

## 添加新屏幕的按键响应 —— 完整步骤

### 1. 确认状态变量 extern

key.c 顶部已有：

```c
extern volatile DisplayState_t currentState;
extern volatile uint8_t menu_index;
extern volatile uint8_t slect_index;
```

这些变量由 `display.h` 声明。

### 2. 在 Key_Event_Handler 对应 case 中加入分支

```c
case KEY_IDX_CONFIRM:
    switch (evt) {
        case KEY_EVENT_CLICK:
            if (currentState == STATE_YOUR_NEW_SCREEN) {
                if (slect_index == 0) {
                    // 浏览模式：进入编辑
                    slect_index = menu_index;
                } else {
                    // 编辑模式：确认
                    YourModule_Exec();
                    slect_index = 0;
                }
            }
            break;
        case KEY_EVENT_DOUBLE_CLICK:
            if (currentState == STATE_YOUR_NEW_SCREEN) {
                // 双击快捷操作
            }
            break;
        default: break;
    }
    break;

case KEY_IDX_CANCEL:
    switch (evt) {
        case KEY_EVENT_CLICK:
            if (currentState == STATE_YOUR_NEW_SCREEN) {
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
            break;
        default: break;
    }
    break;
```

### 3. UP/DOWN 的通用模式

```c
case KEY_IDX_UP:
    switch (evt) {
        case KEY_EVENT_CLICK:
            if (currentState == STATE_YOUR_NEW_SCREEN) {
                if (slect_index == 0) {
                    // 上移菜单项
                    if (menu_index > 1) menu_index--;
                } else {
                    // 正向调整参数
                    YourModule_Adjust(+step);
                }
            }
            break;
        case KEY_EVENT_LONG_PRESS:
            if (currentState == STATE_YOUR_NEW_SCREEN) {
                if (slect_index == 0) {
                    menu_index = 1;           // 回到顶部
                } else {
                    YourModule_Adjust(+fast); // 快速调整
                }
            }
            break;
        default: break;
    }
    break;
```

---

## 菜单 / 编辑两级模式

### 浏览模式（slect_index == 0）

- **UP/DOWN CLICK** → 切换 `menu_index`（最小 1 或循环）
- **CONFIRM CLICK** → `slect_index = menu_index`（进入选中项的编辑模式）
- **CANCEL CLICK** → 返回上一级状态，重置 `menu_index = 1, slect_index = 0`
- **UP LONG_PRESS** → 回到 `menu_index = 1`

### 编辑模式（slect_index >= 1）

- **UP CLICK** → 正向增量调整参数
- **DOWN CLICK** → 负向增量调整参数
- **CONFIRM CLICK** → 确认修改，`slect_index = 0`
- **CANCEL CLICK** → 取消修改，`slect_index = 0`
- **UP/DOWN LONG_PRESS** → 快速连续加速调整

### 变量约定

- `menu_index`：1-indexed，当前菜单项编号
- `slect_index`：0 = 浏览模式，1+ = 正在编辑第 menu_index 项

### 返回上级状态时的清理

切换状态时**必须**同时重置 `menu_index = 1` 和 `slect_index = 0`，避免残留值干扰新状态的显示和导航。

---

## 与 display skill 的关系

| 方面 | display skill | key skill |
|------|---------------|-----------|
| 职责 | 渲染当前状态到屏幕 | 检测按键并切换状态 |
| 读写状态 | 只读 `currentState` / `menu_index` / `slect_index` | 读写这些变量 |
| 新增屏幕步骤 | 添加 enum、创建 Draw 函数、加入 switch、key 中处理 | 在 `Key_Event_Handler` 中加入分支 |
| 依赖的模块 | u8g2（图形库） | ESP-IDF `driver/gpio.h` + FreeRTOS |

两个 skill 配合使用：display 负责"怎么画"，key 负责"按了什么、切到什么状态"。
