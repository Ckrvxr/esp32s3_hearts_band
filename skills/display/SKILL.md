---
name: display
description: 为基于 U8g2 + SSD1315/SSD1306 128x64 I2C OLED 的 Zynq FreeRTOS 嵌入式项目编写显示屏渲染代码的通用指南
---

## 架构总览

### 文件职责

| 文件 | 职责 |
|------|------|
| `display.h` | 公开 API：`DisplayState_t` 枚举、`Display_Init()`、`Display_Refresh()`、`extern volatile` 状态变量 |
| `display.c` | 驱动层（I2C 回调、u8g2 初始化）+ 所有 `Display_Draw_*()` 场景渲染函数 + 状态分发 |
| `key.c` | **修改**显示状态、`menu_index`、`slect_index`；display task 只读不写 |
| `main.c` | FreeRTOS 任务创建：`vDisplayTask` @50Hz (20ms) |

### 渲染循环

```
vDisplayTask (50Hz)
  → Display_Refresh()
    → u8g2_ClearBuffer()
    → switch (currentState) → Display_Draw_*()
    → u8g2_SendBuffer()
    → frame_count++
```

display task 不修改 `currentState`/`menu_index`/`slect_index`，只根据当前值绘制；按键任务负责修改这些变量实现状态切换。

### 硬件环境

- SSD1315 / SSD1306 OLED, 128x64 像素
- I2C 地址 0x3C (7-bit), u8g2 传入 0x78 (8-bit) 需要右移 1 位
- Zynq PS7 I2C #0, 400kHz, XIicPs 驱动
- u8g2 初始化：`u8g2_Setup_ssd1315_i2c_128x64_noname_f()`

---

## 添加新屏幕 —— 完整步骤

### 步骤 1：在 display.h 的 `DisplayState_t` enum 中新增状态

```c
typedef enum {
    STATE_MAIN_MENU,
    // ... 现有状态 ...
    STATE_YOUR_NEW_SCREEN,        // <-- 新增
} DisplayState_t;
```

### 步骤 2：在 display.c 中创建场景渲染函数

固定绘制序列（所有 `Display_Draw_*()` 函数必须遵守）：

```
1. u8g2_SetFont()                    → 选择字体
2. u8g2_DrawStr(title)               → 标题栏，例如 "[My Screen]"
3. Display_Draw_LiveAnimation(120,5) → 右上角动态指示器
4. u8g2_DrawHLine(0, 14, 128)       → 标题分隔线
5. 绘制菜单项 / 数值 / 图形         → 使用 Display_Draw_Cursor()
```

示例框架：

```c
static void Display_Draw_MyScreen(void) {
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 0, 10, "[My Screen]");
    Display_Draw_LiveAnimation(120, 5);
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

    // 菜单项（配合 Display_Draw_Cursor）
    Display_Draw_Cursor(30, (menu_index == 1), (slect_index == 1));
    u8g2_DrawStr(&u8g2, 18, 30, "Item 1");

    Display_Draw_Cursor(45, (menu_index == 2), (slect_index == 2));
    u8g2_DrawStr(&u8g2, 18, 45, "Item 2");
}
```

### 步骤 3：在 `Display_Refresh()` 的 switch 中加入 case

```c
void Display_Refresh(void) {
    u8g2_ClearBuffer(&u8g2);
    switch (currentState) {
        // ... 现有 case ...
        case STATE_YOUR_NEW_SCREEN: Display_Draw_MyScreen(); break;
    }
    u8g2_SendBuffer(&u8g2);
    frame_count++;
}
```

### 步骤 4：在 key.c 的按键 handler 中处理状态切换

在对应的 `Key_Handler_*()` 函数中修改 `currentState`、`menu_index`、`slect_index`。

```c
// 示例：按下确认后进入新屏幕
if (currentState == STATE_PREVIOUS) {
    if (menu_index == 1) {
        currentState = STATE_YOUR_NEW_SCREEN;
        menu_index = 1;
        slect_index = 0;
    }
}
// 示例：按下取消返回
if (currentState == STATE_YOUR_NEW_SCREEN) {
    currentState = STATE_PREVIOUS;
    menu_index = 1;
    slect_index = 0;
}
```

---

## 绘制 API 速查

| 函数 | 用途 | 示例 |
|------|------|------|
| `u8g2_SetFont(&u8g2, font)` | 设置字体 | `u8g2_font_ncenB08_tr` |
| `u8g2_DrawStr(&u8g2, x, y, str)` | 绘制字符串 (y 为基线) | `u8g2_DrawStr(&u8g2, 0, 10, "Title")` |
| `u8g2_DrawHLine(&u8g2, x, y, w)` | 水平线 | `u8g2_DrawHLine(&u8g2, 0, 14, 128)` |
| `u8g2_DrawVLine(&u8g2, x, y, h)` | 垂直线 | |
| `u8g2_DrawLine(&u8g2, x1,y1, x2,y2)` | 任意方向线 | |
| `u8g2_DrawPixel(&u8g2, x, y)` | 画点 | |
| `u8g2_DrawFrame(&u8g2, x, y, w, h)` | 空心矩形 | |
| `u8g2_DrawBox(&u8g2, x, y, w, h)` | 实心矩形 | |
| `u8g2_SetDrawColor(&u8g2, color)` | 1=白色 0=黑色 | |
| `u8g2_ClearBuffer(&u8g2)` | 清空帧缓冲 | `Display_Refresh()` 开头 |
| `u8g2_SendBuffer(&u8g2)` | 发送帧缓冲到屏幕 | `Display_Refresh()` 末尾 |

### 坐标系统

- 屏幕尺寸：128 x 64 像素
- 原点 (0,0) 在左上角
- X 轴向右，Y 轴向下
- `u8g2_DrawStr()` 的 y 参数是**基线**（字符底部），不是顶部

### 常用字体

| 字体 | 用途 |
|------|------|
| `u8g2_font_ncenB08_tr` | 标题和菜单项（8px 粗体） |

---

## 菜单 / 光标系统模式

### 变量定义

```c
volatile uint8_t menu_index;   // 1-indexed: 当前选中第几项
volatile uint8_t slect_index;  // 0=浏览模式, 1+=正在编辑第几项
```

### 游标绘制

```c
Display_Draw_Cursor(y, is_selected, is_editing);
```

- 未选中 → 不画
- 浏览 → `>` 字符 + 水平抖动动画
- 编辑 → `*` 字符 + 闪烁动画

### 典型使用模式

```c
Display_Draw_Cursor(30, (menu_index == 1), (slect_index == 1));
u8g2_DrawStr(&u8g2, 18, 30, "Item 1");
```

---

## 动画与帧计数

```c
static uint32_t frame_count = 0;  // Display_Refresh() 每帧自增
```

`frame_count` 是一个单调递增的计数器，可用于驱动：

- **光标闪烁**：`(frame_count / 4) % 2` 控制交替显示
- **动态指示器**：`(frame_count / 2) % 4` 切换旋转相位
- **波形滚动**：索引偏移 `(i + frame_count) % N`

> 注意避免直接在有符号类型和无符号类型之间做运算产生符号扩展问题。强制转换或使用一致的无符号类型。

---

## 数值格式化

### 千位分隔

```c
static void Format_With_Commas(uint32_t n, char *out_buf);
```

将 `1234567` 格式化为 `"1,234,567"`。内部用 `snprintf` 转字符串再反向插入逗号。

### 单位后缀惯例

```c
uint32_t value = 1234567;
char formatted[32], buf[64];
Format_With_Commas(value, formatted);
snprintf(buf, sizeof(buf), "%s mV", formatted);  // → "1,234,567 mV"
```

---

## 变量共享约定

- 显示状态变量必须用 `volatile`，因为被多个任务访问
- `display.h` 中 `extern volatile` 声明：`currentState`、`menu_index`、`slect_index`
- 其他模块的显示数据（如测量值）由各自模块的 .h 文件用 `extern` 暴露，display.c 直接引用
- 示例：`fir.h` 中的 `fir_progress`、`dds.h` 中的 `dds_vpp` 等

---

## 驱动层说明（I2C 回调）

u8g2 通过回调函数与硬件通信，本项目实现了两个回调：

### I2C 字节回调 (`u8x8_byte_zynq_hw_i2c`)

```
U8X8_MSG_BYTE_START_TRANSFER → 清空内部缓冲区索引
U8X8_MSG_BYTE_SEND           → 将数据追加到内部缓冲区
U8X8_MSG_BYTE_END_TRANSFER   → 通过 XIicPs_MasterSendPolled() 一次性发送整个缓冲区
```

关键细节：u8g2 传入的 I2C 地址是 8-bit 格式（如 0x78），Zynq XIicPs 需要 7-bit 地址，因此需要右移 1 位（`address >> 1` → 0x3C）。

### 延时回调 (`u8x8_gpio_and_delay_zynq`)

使用 FreeRTOS `vTaskDelay(pdMS_TO_TICKS(arg_int))` 替代忙等待，避免阻塞其他任务。

