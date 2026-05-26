# PPG 信号处理核心 — 第二代 FFT 分析算法

## 1. 采样模型

\[
x_{IR}[n],\; x_{RED}[n] \in [0, 65535], \quad n = 0, 1, 2, \dots
\]

\[
F_s = 100\,\text{Hz},\quad T_s = \frac{1}{F_s} = 10\,\text{ms}
\]

## 2. 直流剥离

采用与第一代相同的 DCRemover：

\[
\begin{aligned}
w[n] &= x[n] + \alpha \cdot w[n-1], \quad \alpha = 0.98 \\
x_{AC}[n] &= w[n] - w[n-1]
\end{aligned}
\]

直流基线用于 SpO₂ 计算：

\[
DC[n] = \beta \cdot DC[n-1] + (1 - \beta) \cdot x[n], \quad \beta = 0.999
\]

## 3. 去直流均值 (Mean Removal)

在加窗前从 FFT 帧中减去样本均值，消除残余直流分量：

\[
\bar{x}_{IR} = \frac{1}{N} \sum_{n=0}^{N-1} x_{AC,IR}[n], \quad
\bar{x}_{RED} = \frac{1}{N} \sum_{n=0}^{N-1} x_{AC,RED}[n]
\]

\[
s_{IR}[n] = (x_{AC,IR}[n] - \bar{x}_{IR}) \cdot w_H[n]
\]

\[
s_{RED}[n] = (x_{AC,RED}[n] - \bar{x}_{RED}) \cdot w_H[n]
\]

效果：防止 bin 0 残余能量泄漏污染低频 HR 带（bin 3~20），噪声基底降低约 6~10dB。

## 4. 加窗

长度为 \(N\) 的 Hanning 窗：

\[
w_H[n] = 0.5 - 0.5 \cos\left(\frac{2\pi n}{N-1}\right), \quad n = 0, 1, \dots, N-1
\]

加窗后信号：

\[
s_{IR}[n] = x_{AC,IR}[n] \cdot w_H[n], \quad
s_{RED}[n] = x_{AC,RED}[n] \cdot w_H[n]
\]

## 5. 离散傅里叶变换

\[
S[k] = \sum_{n=0}^{N-1} s[n] \cdot e^{-j\frac{2\pi}{N}kn}, \quad k = 0, 1, \dots, N-1
\]

频率分辨率：

\[
\Delta f = \frac{F_s}{N}
\]

## 6. 功率谱密度

单边功率谱：

\[
P[k] = \frac{1}{N} |S[k]|^2, \quad k = 0, 1, \dots, \frac{N}{2}-1
\]

对应频率：

\[
f[k] = k \cdot \Delta f = k \cdot \frac{F_s}{N}
\]

对 IR 和 RED 两通道分别计算 \(P_{IR}[k]\) 和 \(P_{RED}[k]\)。

## 7. 心率检测

### 7.1 搜索区间

### 7.2 峰值 bin

### 7.3 抛物线插值

### 7.4 心率

### 7.5 信号质量

\[
PSNR = 10 \cdot \log_{10} \frac{P_{IR}[k_{peak}]}{\frac{1}{K} \sum_{k \in [k_{low}, k_{high}]} P_{IR}[k]}, \quad K = k_{high} - k_{low} + 1
\]

\[
Q = \begin{cases}
1, & PSNR > \theta_{PSNR}(k_{peak}) \\
0, & \text{otherwise}
\end{cases}
\]

\[
\theta_{PSNR} = \begin{cases}
12\,\text{dB}, & k_{peak} \leq 5\;(35\!-\!59\,\text{bpm}) \\
8\,\text{dB}, & 6 \leq k_{peak} \leq 11\;(70\!-\!129\,\text{bpm}) \\
6\,\text{dB}, & k_{peak} \geq 12\;(140\!-\!234\,\text{bpm})
\end{cases}
\]

低 HR 区间（bin 3~5）在运动瞬态污染 FFT 缓冲时最容易被虚假低频能量主导，因此要求更高的 PSNR；正常 HR 区间适当放宽；高 HR 区间维持最低门槛。

当 \(Q = 0\) 时保持上一有效 HR 值。

## 8. SpO₂ 计算

### 8.1 频域 AC 幅值

### 8.2 AC/DC 比率

### 8.3 对数域映射

### 8.4 LUT

\[
k_{spo2} = \begin{cases}
R_{log} - 66, & R_{log} > 66 \\
R_{log} - 50, & 50 < R_{log} \leq 66 \\
0, & R_{log} \leq 50
\end{cases}
\]

\[
\hat{k} = \min(\max(k_{spo2}, 0), 42)
\]

SpO₂ 通过 9 节点 LUT 线性插值得到，节点为：

\[
\begin{array}{c|ccccccccc}
\hat{k} & 0 & 4 & 10 & 16 & 22 & 28 & 34 & 38 & 42 \\
\hline
SpO_2 & 100 & 99 & 98 & 97 & 96 & 95 & 94 & 93 & 93
\end{array}
\]
