# PPG 信号处理核心 — 第三代 EWA+CNN 搏动检测算法

## 1. 设计动机

前两代算法（低通滤波阈值法 / FFT 频域分析法）在静止或轻微运动时表现尚可，但在以下场景暴露明显短板：

| 场景 | 第一代（低通） | 第二代（FFT） | 第三代（CNN） |
|------|---------------|-------------|--------------|
| 静止 | 可用 | 良好 | 良好 |
| 手指抖动 | 误触发 | 频谱展宽、误检 | 抗噪（数据驱动） |
| 运动伪影 | 极易误检 | 低频能量污染 HR 带 | 时域特征鲁棒 |
| 快速 HR 变化 | 滞后 | 需要 2.56s 缓冲 | 1s 窗口快速响应 |

第三代使用**轻量 CNN** 在时域上直接学习搏动波形特征，替代 FFT 的频域峰值搜索，配合**指数加权平均（EWA）** 对输出概率做平滑，实现低延时、高抗噪的搏动检测。

---

## 2. 预处理

### 2.1 指数加权高通滤波

原始 IR/RED 信号包含大幅直流分量，使用 EWA 提取基线并剥离直流：

\[
\begin{aligned}
m_{IR}[0] &= x_{IR}[0] \\
m_{IR}[n] &= \alpha_{HP} \cdot x_{IR}[n] + (1 - \alpha_{HP}) \cdot m_{IR}[n-1]
\end{aligned}
\]

\[
x_{IR,HP}[n] = x_{IR}[n] - m_{IR}[n]
\]

参数：\(\alpha_{HP} = 0.04\)（等效时间常数 \(\tau \approx 25\) 采样点 = 250ms @100Hz）

RED 通道相同处理。

### 2.2 滑窗与归一化

定义窗口长度 \(W = 100\)（= 1s @100Hz）。

对于每个采样时刻 \(i \ge W\)，构造输入向量：

\[
\mathbf{x}_{IR} = \big[ x_{IR,HP}[i-W],\; x_{IR,HP}[i-W+1],\; \dots,\; x_{IR,HP}[i-1] \big]^\top
\]

\[
\mathbf{x}_{RED} = \big[ x_{RED,HP}[i-W],\; \dots,\; x_{RED,HP}[i-1] \big]^\top
\]

拼接后做 z-score 归一化（防止幅度波动影响推理）：

\[
\mathbf{c} = \begin{bmatrix} \mathbf{x}_{IR} \\ \mathbf{x}_{RED} \end{bmatrix},\quad
\mu = \frac{1}{2W}\sum_{j=0}^{2W-1} c_j,\quad
\sigma = \sqrt{\frac{1}{2W}\sum_{j=0}^{2W-1} (c_j - \mu)^2} + \varepsilon
\]

\[
\tilde{\mathbf{x}}_{IR} = \frac{\mathbf{x}_{IR} - \mu}{\sigma},\qquad
\tilde{\mathbf{x}}_{RED} = \frac{\mathbf{x}_{RED} - \mu}{\sigma}
\]

最终网络输入为 \(2 \times W\) 矩阵：

\[
\mathbf{X} = \begin{bmatrix}
\tilde{x}_{IR}[0] & \tilde{x}_{IR}[1] & \dots & \tilde{x}_{IR}[W-1] \\
\tilde{x}_{RED}[0] & \tilde{x}_{RED}[1] & \dots & \tilde{x}_{RED}[W-1]
\end{bmatrix}
\]

---

## 3. CNN 网络结构

采用轻量 `McuPpgNet`，共约 **6.5K 参数**，适合 MCU 实时推理。

### 3.1 网络定义

```
Layer          Type                  In→Out    Kernel   Groups   Output Shape
─────────────────────────────────────────────────────────────────────────────
conv1          Conv1d + BN + ReLU    2→8       7        2        (8, 100)
pool1          AvgPool1d(2)                    -        -        (8, 50)
conv2          Conv1d + BN + ReLU    8→16      5        4        (16, 50)
pool2          AvgPool1d(2)                    -        -        (16, 25)
dw3            Conv1d(dw) + BN+ReLU  16→16     3        16       (16, 25)
pw3            Conv1d(pw) + BN+ReLU  16→8      1        -        (8, 25)
pool3          AvgPool1d(5)                    -        -        (8, 5)
dw4            Conv1d(dw) + BN+ReLU  8→8       3        8        (8, 5)
pw4            Conv1d(pw) + BN+ReLU  8→4       1        -        (4, 5)
flatten        reshape                        -        -        (1, 20)
fc1            Linear + ReLU          20→8     -        -        (1, 8)
fc2            Linear + Sigmoid       8→1      -        -        (1, 1)
```

### 3.2 分组卷积说明

| 层 | groups | 含义 |
|----|--------|------|
| conv1 | 2 | 每个输入通道（IR/RED）独立生成 4 个特征通道 |
| conv2 | 4 | 每 2 个输入通道共享 1 组卷积核 |
| dw3/dw4 | C | Depthwise：每个通道独立卷积 |

### 3.3 前向计算数学描述

**conv1** （分组卷积分组 \(G=2\)，每组输入 1 通道，输出 4 通道）：

\[
\mathbf{h}^{(1)}_{g\cdot 4 + o}[p] = \text{ReLU}\left( \sum_{k=0}^{6} w^{(1)}_{g\cdot4+o,\;0,\;k} \cdot \tilde{x}_{g}[p+k] + b^{(1)}_{g\cdot4+o} \right)
\]

其中 \(g \in \{0,1\}\) 对应 IR/RED，输出 8 通道，每组 4 通道。

**pool1**（平均降采样）：

\[
\mathbf{h}^{(p1)}_c[p] = \frac{\mathbf{h}^{(1)}_c[2p] + \mathbf{h}^{(1)}_c[2p+1]}{2}
\]

**conv2 ~ pool2 ~ dw3 ~ pw3 ~ pool3 ~ dw4 ~ pw4** 类似堆叠，具体维度参见 3.1。

**flatten**：

\[
\mathbf{v} = \big[ \mathbf{h}^{(pw4)}_{0,0},\; \dots,\; \mathbf{h}^{(pw4)}_{0,4},\; \mathbf{h}^{(pw4)}_{1,0},\; \dots,\; \mathbf{h}^{(pw4)}_{3,4} \big]^\top \in \mathbb{R}^{20}
\]

**fc1**：

\[
\mathbf{z}^{(1)} = \text{ReLU}\big( \mathbf{W}^{(fc1)} \mathbf{v} + \mathbf{b}^{(fc1)} \big),\quad \mathbf{W}^{(fc1)} \in \mathbb{R}^{8 \times 20}
\]

**fc2 + Sigmoid**（最终搏动概率）：

\[
y = \sigma\big( \mathbf{w}^{(fc2)\top} \mathbf{z}^{(1)} + b^{(fc2)} \big) = \frac{1}{1 + e^{-(\mathbf{w}^\top \mathbf{z}^{(1)} + b)}}
\]

输出 \(y \in [0, 1]\) 表示当前时刻是搏动峰值的概率。

---

## 4. MCU 前向计算（BN 融合权重）

训练时 BatchNorm 的参数已融合进卷积权重，可避免 MCU 上逐层 BN 计算。

### 4.1 BN 融合公式

对于每个输出通道 \(c\)：

\[
\hat{w}_c[k] = \frac{\gamma_c}{\sqrt{\sigma_c^2 + \varepsilon}} \cdot w_c[k],\quad
\hat{b}_c = \frac{\gamma_c}{\sqrt{\sigma_c^2 + \varepsilon}} \cdot (b_c - \mu_c) + \beta_c
\]

其中 \(\gamma_c, \beta_c\) 为 BN 的 scale/shift，\(\mu_c, \sigma_c^2\) 为 running mean/var。

融合后的前向计算省略 BN 层：

\[
\mathbf{h}_c[p] = \text{ReLU}\left( \sum_{k} \hat{w}_c[k] \cdot \mathbf{x}[p+k] + \hat{b}_c \right)
\]

### 4.2 权重数据布局

权重头文件 `refs/cnn/ppg_cnn_weights.h` 包含 BN 融合后的所有参数：

| 符号 | 维度 | 说明 |
|------|------|------|
| `conv1_weight` | [8][1][7] | conv1 融合权重 |
| `conv1_bias` | [8] | conv1 融合偏置 |
| `conv2_weight` | [16][2][5] | conv2 融合权重 |
| `conv2_bias` | [16] | conv2 融合偏置 |
| `dw3_weight` | [16][1][3] | depthwise conv3 融合权重 |
| `dw3_bias` | [16] | depthwise conv3 融合偏置 |
| `pw3_weight` | [8][16][1] | pointwise conv3 融合权重 |
| `pw3_bias` | [8] | pointwise conv3 融合偏置 |
| `dw4_weight` | [8][1][3] | depthwise conv4 融合权重 |
| `dw4_bias` | [8] | depthwise conv4 融合偏置 |
| `pw4_weight` | [4][8][1] | pointwise conv4 融合权重 |
| `pw4_bias` | [4] | pointwise conv4 融合偏置 |
| `fc1_weight` | [8][20] | fc1 权重 |
| `fc1_bias` | [8] | fc1 偏置 |
| `fc2_weight` | [8] | fc2 权重（单输出） |
| `fc2_bias` | 1 | fc2 偏置 |

### 4.3 前向计算伪代码

```
function cnn_forward(x[2][W]):
    # conv1 (groups=2)
    for g in 0..1:        # IR / RED group
        for o in 0..3:    # 4 output channels per group
            c = g*4 + o
            for p in 0..W-1:
                sum = conv1_bias[c]
                for k in 0..6:
                    sum += conv1_weight[c][0][k] * x[g][p+k]
                h1[c][p] = relu(sum)
    # pool1
    for c in 0..7:
        for p in 0..49:
            h1p[c][p] = (h1[c][2*p] + h1[c][2*p+1]) / 2
    # ... 后续层类似 ...
    # flatten → fc1 → fc2 → sigmoid
    return sigmoid(fc2_out)
```

---

## 5. 后处理：EWA 平滑

CNN 输出的原始概率 \(y[n]\) 在时序上可能有抖动，使用第二级指数加权平均（EWA）做平滑：

\[
\begin{aligned}
s[0] &= y[0] \\
s[n] &= \alpha_{EWA} \cdot y[n] + (1 - \alpha_{EWA}) \cdot s[n-1]
\end{aligned}
\]

参数：\(\alpha_{EWA} = 0.3\)（等效窗口约 3~4 采样点 = 30~40ms）

平滑后概率轨迹 \(s[n]\) 用于最终搏动判决。

---

## 6. 搏动判决

### 6.1 阈值 + 不应期

\[
\text{beat}[n] = \begin{cases}
1, & s[n] \ge \theta \;\text{and}\; (n - n_{last}) \ge R \\
0, & \text{otherwise}
\end{cases}
\]

| 参数 | 默认值 | 说明 |
|------|--------|------|
| \(\theta\) | 0.5 | 概率阈值 |
| \(R\) | 20 | 不应期（采样点数）= 200ms @100Hz |

### 6.2 延时补偿

训练阶段观察到 CNN 输出相对于真实波形有约 30 采样点（300ms）的固定延时。

在训练数据集上做 `beat[t] → beat[t+30]` 移位补偿，使标注与网络输出对齐。  
MCU 实时推理不进行此移位（输出即为当前时刻概率）。

---

## 7. 完整处理流水线

```
IR[n], RED[n]
    │
    ▼
[EWA HPF]  x_hp[n] = x[n] - EWM(α=0.04)[n]
    │
    ▼
[环形缓冲] 保存最近 W=100 个样本
    │
    ▼
[归一化]   z-score(concat(IR_hp, RED_hp))
    │
    ▼
[CNN 推理]  McuPpgNet 前向 → 概率 y[n]
    │
    ▼
[EWA 平滑]  s[n] = 0.3·y[n] + 0.7·s[n-1]
    │
    ▼
[搏动判决]  s[n] ≥ θ 且 (n - n_last) ≥ R ?
    │
    ▼
beat_event (0/1)
```

---

## 8. 参数汇总

| 符号 | 值 | 用途 |
|------|-----|------|
| \(F_s\) | 100 Hz | 采样率 |
| \(W\) | 100 | CNN 输入窗长（= 1s） |
| \(\alpha_{HP}\) | 0.04 | 高通 EWA 系数 |
| \(\alpha_{EWA}\) | 0.3 | 后平滑 EWA 系数 |
| \(\theta\) | 0.5 | 搏动概率阈值 |
| \(R\) | 20 | 不应期（采样点） |
| \(\varepsilon\) | \(10^{-6}\) | 归一化防零除 |
| 模型参数量 | ~6,500 | float32 约 26KB |

---

## 9. 与 FFT 方案的对比

| 维度 | 第二代（FFT） | 第三代（EWA+CNN） |
|------|-------------|-----------------|
| 核心技术 | 512-FFT + 频域峰值搜索 | 1D-CNN 时域特征学习 |
| 缓冲延迟 | 256~512 采样点（2.56~5.12s） | 100 采样点（1s） |
| 输出频率 | 每 256 采样点更新 HR | 每采样点输出概率 |
| 运动鲁棒性 | ❌ 低频运动伪影易污染 HR 带 | ✅ 数据驱动，大量训练覆盖 |
| 计算量 | 较低（FFT + 峰值搜索） | 中等（6.5K 参数 Conv） |
| 存储需求 | 512 点 FFT 缓冲 ≈ 2KB | 100 点缓冲 + 26KB 权重 ≈ 27KB |
| 心率计算 | FFT 频域直接计算 | CNN 输出 beat → 统计间隔 → HR |
| SpO₂ 计算 | 频域 AC 幅值比值 | 暂由独立 DC/AC 分支处理 |
