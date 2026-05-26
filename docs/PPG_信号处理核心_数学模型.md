# PPG 信号处理核心 — 数学模型

## 1. 采样模型

MAX30100 以固定采样率 \(F_s\) 对光电容积脉搏波进行采样，输出离散时间序列：

\[
x_{IR}[n],\; x_{RED}[n] \in [0, 65535], \quad n = 0,1,2,\dots
\]

当前配置：

\[
F_s = 100\,\text{Hz},\quad T_s = \frac{1}{F_s} = 10\,\text{ms}
\]

每周期交替点亮红外（IR, 940nm）和红光（RED, 660nm）LED，接收端对环境光做硬件差分消除后存入 FIFO。

---

## 2. 直流剥离（DCRemover）

### 2.1 模型

原始 PPG 信号 \(x[n]\) 可分解为：

\[
x[n] = x_{DC}[n] + x_{AC}[n]
\]

其中 \(x_{DC}[n]\) 为缓慢变化的基线（呼吸、组织密度、手指压力），\(x_{AC}[n]\) 为脉搏波动分量。

采用一阶 IIR 高通滤波器（指数移动平均差值法）：

**低通分支（跟踪直流）：**

\[
w[n] = x[n] + \alpha \cdot w[n-1]
\]

**高通输出（提取交流）：**

\[
y[n] = w[n] - w[n-1]
\]

### 2.2 传递函数

\[
H_{DC}(z) = \frac{Y(z)}{X(z)} = \frac{1 - z^{-1}}{1 - \alpha z^{-1}}
\]

- 零点：\(z = 1\)（直流完全抑制）
- 极点：\(z = \alpha\)（一阶低通）

### 2.3 参数

\[
\alpha = 0.95
\]

**截止频率计算：**

\[
f_c = \frac{F_s}{2\pi} \cdot \arccos\left(\frac{2\alpha}{1+\alpha^2}\right)
     \approx 0.08\,\text{Hz}
\]

对应时间常数：

\[
\tau \approx \frac{T_s}{1-\alpha} = \frac{10\,\text{ms}}{0.05} = 200\,\text{ms}
\]

### 2.4 物理意义

低于 0.08Hz 的频率分量（呼吸 0.2~0.3Hz 的慢波被部分保留但大幅衰减，纯直流被完全抑制）。脉搏波主频在 0.5~4Hz 范围内，该滤波器对其影响 < 0.1dB。

---

## 3. 低通滤波器（4 阶 Butterworth）

### 3.1 模拟原型

归一化 4 阶 Butterworth 低通滤波器的幅度平方响应：

\[
|H_a(j\Omega)|^2 = \frac{1}{1 + \Omega^{2N}}, \quad N = 4
\]

极点位于左半平面单位圆上：

\[
s_k = e^{j\pi\frac{2k + N -1}{2N}}, \quad k = 0,1,\dots,N-1
\]

具体位置：

\[
\begin{aligned}
s_0 &= -0.3827 + j0.9239 \\
s_1 &= -0.9239 + j0.3827 \\
s_2 &= -0.9239 - j0.3827 \\
s_3 &= -0.3827 - j0.9239
\end{aligned}
\]

### 3.2 双线性变换

预畸变截止频率：

\[
\Omega_c = \tan\left(\frac{\pi f_c}{F_s}\right)
         = \tan\left(\frac{\pi \cdot 5}{100}\right)
         = 0.1584
\]

将模拟极点映射到 z 域：

\[
s = \Omega_c \cdot \frac{1 - z^{-1}}{1 + z^{-1}}
\]

每个共轭极点对构成一个双二阶节（Biquad）：

\[
H_k(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{1 + a_1 z^{-1} + a_2 z^{-2}}
\]

### 3.3 级联实现

4 阶滤波器分解为两个 2 阶节的级联：

\[
H(z) = H_1(z) \cdot H_2(z)
\]

#### 第一节

\[
H_1(z) = \frac{0.0205 + 0.0410 z^{-1} + 0.0205 z^{-2}}
              {1 - 1.5928 z^{-1} + 0.6712 z^{-2}}
\]

差分方程：

\[
y_1[n] = 0.0205(x[n] + 2x[n-1] + x[n-2]) + 1.5928 y_1[n-1] - 0.6712 y_1[n-2]
\]

#### 第二节

\[
H_2(z) = \frac{1.0000 + 2.0000 z^{-1} + 1.0000 z^{-2}}
              {1 - 1.4409 z^{-1} + 0.5421 z^{-2}}
\]

差分方程：

\[
y_2[n] = (y_1[n] + 2y_1[n-1] + y_1[n-2]) + 1.4409 y_2[n-1] - 0.5421 y_2[n-2]
\]

### 3.4 幅频特性

\[
|H(e^{j\omega})| = \frac{1}{\sqrt{1 + (\tan(\omega/2) / \Omega_c)^{2N}}}
\]

| 频率 | 归一化 \(\omega\) | 衰减 |
|---|---|---|
| 0.5 Hz | 0.0314 rad | 0 dB |
| 1.0 Hz | 0.0628 rad | 0 dB |
| 4.0 Hz | 0.2513 rad | 0 dB |
| 5.0 Hz | 0.3142 rad | -3 dB |
| 10 Hz | 0.6283 rad | -24 dB |
| 50 Hz | 3.1416 rad | -50 dB |

### 3.5 级联等效响应

与 DCRemover 级联后的等效传递函数：

\[
H_{total}(z) = H_{DC}(z) \cdot H_1(z) \cdot H_2(z)
\]

有效带宽：**0.08 Hz ~ 5 Hz**（-3dB 点）

---

## 4. 自适应阈值心跳检测（BeatDetector）

### 4.1 输入信号

前级输出信号取反后送入检测器：

\[
p[n] = -y_2[n]
\]

取反原因：脉搏波下降沿陡峭程度优于上升沿，取反后获得更尖锐的正向脉冲，利于阈值比较。

### 4.2 阈值自适应更新

#### 检测前跟随

当处于追踪状态且信号高于阈值时，阈值跟随信号最大值（带上限）：

\[
\theta[n] = \min\left(p[n],\; \theta_{max}\right)
\]

其中 \(\theta_{max} = 800\)。

#### 检测后阈值重置

检测到心跳后，阈值以比例因子回落至上一次峰值的固定比例：

\[
\theta[n] = \theta_{falloff} \cdot p_{max}
\]

其中 \(\theta_{falloff} = 0.3\)，即阈值降至波峰值的 30%。

#### 无检测时指数衰减

当长时间无心跳（超过一个预期周期），阈值按指数衰减趋近下限：

\[
\theta[n] = \gamma \cdot \theta[n-1]
\]

其中 \(\gamma = 0.99\)，每个样本衰减 1%，下限 \(\theta_{min} = 20\)。

#### 有有效周期时的线性衰减

当存在有效心率周期时，阈值按预期斜率线性衰减：

\[
\Delta\theta = \frac{p_{max} \cdot (1 - \theta_{falloff})}{T_{expected} / T_s}
\]

其中 \(T_{expected} = beatPeriod\)（上次心跳间隔），\(\theta_{falloff} = 0.3\)。

### 4.3 心率间隔估计

每次检测到心跳时，记录时间戳 \(t_k\)，计算间隔：

\[
\Delta t_k = t_k - t_{k-1}
\]

经一阶 EMA 平滑：

\[
\bar{T}_k = \alpha \cdot \Delta t_k + (1-\alpha) \cdot \bar{T}_{k-1}, \quad \alpha = 0.6
\]

瞬时心率：

\[
HR = \frac{60000}{\bar{T}_k} \quad [\text{bpm}]
\]

### 4.4 状态机逻辑

定义 5 个状态 \(S \in \{0,1,2,3,4\}\)：

| S | 状态 | 条件 |
|---|---|---|
| 0 | INIT | \(n < N_{init}\), \(N_{init} = 200\) (2s) |
| 1 | WAITING | \(p[n] \leq \theta[n]\) |
| 2 | FOLLOWING_SLOPE | \(p[n] > \theta[n]\) |
| 3 | MAYBE_DETECTED | \(p[n] < \theta[n]\) 且之前处于 FOLLOWING |
| 4 | MASKING | \(\Delta t < T_{mask}\), \(T_{mask} = 200\)ms |

转移函数：

\[
S_{n+1} = f(S_n, p[n], \theta[n], t)
\]

检测条件（确认心跳）：

\[
p[n] + \delta < \theta[n], \quad \delta = 30
\]

即信号幅值比阈值低至少 \(\delta\) 时确认为一次完整心跳。

### 4.5 超时重置

若 \(t - t_{lastBeat} > T_{invalid} = 2000\)ms，则重置心率估计：

\[
\bar{T}_k = 0,\quad p_{max} = 0
\]

---

## 5. SpO₂ 计算

### 5.1 Lambert-Beer 定律

动脉血中氧合血红蛋白（HbO₂）和脱氧血红蛋白（Hb）对红光和红外光的吸光度不同。根据修正的 Lambert-Beer 定律：

\[
I = I_0 \cdot e^{-\epsilon(\lambda) \cdot C \cdot d}
\]

其中：
- \(I_0\)：入射光强度
- \(\epsilon(\lambda)\)：摩尔消光系数（波长相关）
- \(C\)：物质浓度
- \(d\)：光路长度

### 5.2 AC/DC 比率

脉搏搏动引起动脉血容积变化 \(\Delta d\)，导致透射光强度脉动变化 \(\Delta I\)：

\[
\frac{\Delta I}{I_{DC}} \approx -\epsilon(\lambda) \cdot C \cdot \Delta d
\]

定义 AC/DC 比率：

\[
R_{OS} = \frac{AC}{DC}
\]

对于双波长系统：

\[
R = \frac{R_{OS}(\lambda_{RED})}{R_{OS}(\lambda_{IR})}
    = \frac{AC_{RED} / DC_{RED}}{AC_{IR} / DC_{IR}}
\]

### 5.3 RMS 累积

每样本累加交流分量的平方和：

\[
S_{IR} = \sum_{i=1}^{N} x_{AC,IR}^2[i], \quad
S_{RED} = \sum_{i=1}^{N} x_{AC,RED}^2[i]
\]

每 \(N_{beats} = 3\) 次心跳计算一次：

\[
\overline{AC}_{IR} = \sqrt{\frac{S_{IR}}{M}}, \quad
\overline{AC}_{RED} = \sqrt{\frac{S_{RED}}{M}}
\]

\[
R = \frac{\overline{AC}_{RED} / DC_{RED}}{\overline{AC}_{IR} / DC_{IR}}
\]

### 5.4 对数域映射

工程上使用对数域近似简化 LUT 索引：

\[
R_{log} = 100 \cdot \frac{\ln(\overline{AC}_{RED}^2)}{\ln(\overline{AC}_{IR}^2)}
       = 100 \cdot \frac{\ln(S_{RED}/M)}{\ln(S_{IR}/M)}
\]

### 5.5 查找表

SpO₂ 与 \(R_{log}\) 呈分段单调关系，通过 43 项经验 LUT 映射：

\[
\text{SpO}_2[k] = L[k], \quad k = 0,1,\dots,42
\]

索引计算：

\[
k = \begin{cases}
R_{log} - 66, & R_{log} > 66 \\
R_{log} - 50, & 50 < R_{log} \leq 66 \\
0,            & R_{log} \leq 50
\end{cases}
\]

\[
k = \min(k, 42)
\]

LUT 值域：

\[
L[k] \in [93, 100], \quad \text{单调递减}
\]

| \(k\) | 0 | 4 | 10 | 16 | 22 | 28 | 34 | 38 | 42 |
|---|---|---|---|---|---|---|---|---|---|
| SpO₂ | 100 | 99 | 98 | 97 | 96 | 95 | 94 | 93 | 93 |

---

## 6. 级联系统总传递函数

整体处理链的复频域描述：

\[
S_{total}:\; x_{IR}[n] \xrightarrow{H_{DC}(z)} x_{AC}[n] \xrightarrow{H_{BP}(z)} p[n] \xrightarrow{\text{BeatDet}} HR,\; \text{SpO}_2
\]

其中：

\[
H_{BP}(z) = H_1(z) \cdot H_2(z)
\]

等效连续时间系统带宽：

\[
BW = [0.08\,\text{Hz},\; 5\,\text{Hz}]
\]

对应心率范围：

\[
HR_{range} = [0.08 \times 60,\; 5 \times 60] = [4.8,\; 300]\;\text{bpm}
\]

实际有效检测范围由 BeatDetector 参数限制为 **30~240 bpm**（0.5~4 Hz）。

---

## 7. 信号质量评估

基于 AC 信号短时方差：

\[
\mu_N = \frac{1}{N}\sum_{i=n-N+1}^{n} p[i], \quad
\sigma_N^2 = \frac{1}{N}\sum_{i=n-N+1}^{n} (p[i] - \mu_N)^2
\]

判决：

\[
Q = \begin{cases}
0\ (\text{无信号}), & \sigma_N^2 < \sigma_{none}^2 \\
1\ (\text{运动中}), & \sigma_N^2 > \sigma_{motion}^2 \\
2\ (\text{稳定}),  & \text{otherwise}
\end{cases}
\]

其中 \(N = 50\)（0.5s 窗口），\(\sigma_{none}^2 < 100\)，\(\sigma_{motion}^2 > 500\)。
