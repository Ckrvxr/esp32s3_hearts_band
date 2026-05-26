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
w[n] &= x[n] + \alpha \cdot w[n-1], \quad \alpha = 0.95 \\
x_{AC}[n] &= w[n] - w[n-1]
\end{aligned}
\]

直流基线用于 SpO₂ 计算：

\[
DC[n] = \beta \cdot DC[n-1] + (1 - \beta) \cdot x[n], \quad \beta = 0.999
\]

## 3. 加窗

长度为 \(N\) 的 Hanning 窗：

\[
w_H[n] = 0.5 - 0.5 \cos\left(\frac{2\pi n}{N-1}\right), \quad n = 0, 1, \dots, N-1
\]

加窗后信号：

\[
s_{IR}[n] = x_{AC,IR}[n] \cdot w_H[n], \quad
s_{RED}[n] = x_{AC,RED}[n] \cdot w_H[n]
\]

## 4. 离散傅里叶变换

\[
S[k] = \sum_{n=0}^{N-1} s[n] \cdot e^{-j\frac{2\pi}{N}kn}, \quad k = 0, 1, \dots, N-1
\]

频率分辨率：

\[
\Delta f = \frac{F_s}{N}
\]

## 5. 功率谱密度

单边功率谱：

\[
P[k] = \frac{1}{N} |S[k]|^2, \quad k = 0, 1, \dots, \frac{N}{2}-1
\]

对应频率：

\[
f[k] = k \cdot \Delta f = k \cdot \frac{F_s}{N}
\]

对 IR 和 RED 两通道分别计算 \(P_{IR}[k]\) 和 \(P_{RED}[k]\)。

## 6. 心率检测

### 6.1 搜索区间

\[
k_{low} = \left\lceil \frac{f_{low} \cdot N}{F_s} \right\rceil, \quad
k_{high} = \left\lfloor \frac{f_{high} \cdot N}{F_s} \right\rfloor
\]

\[
f_{low} = 0.5\,\text{Hz},\quad f_{high} = 4.0\,\text{Hz}
\]

### 6.2 峰值 bin

\[
k_{peak} = \arg\max_{k \in [k_{low}, k_{high}]} P_{IR}[k]
\]

### 6.3 抛物线插值

设 \(y_{-1} = P[k_{peak} - 1],\; y_0 = P[k_{peak}],\; y_1 = P[k_{peak} + 1]\)：

\[
\delta = \frac{y_{-1} - y_1}{2 \cdot (y_{-1} - 2y_0 + y_1)}, \quad \delta \in [-0.5, 0.5]
\]

修正后频率：

\[
f_{HR} = (k_{peak} + \delta) \cdot \frac{F_s}{N}
\]

### 6.4 心率

\[
HR = 60 \cdot f_{HR} \quad [\text{bpm}]
\]

### 6.5 信号质量

\[
PSNR = 10 \cdot \log_{10} \frac{P_{IR}[k_{peak}]}{\frac{1}{K} \sum_{k \in [k_{low}, k_{high}]} P_{IR}[k]}, \quad K = k_{high} - k_{low} + 1
\]

\[
Q = \begin{cases}
1, & PSNR > \theta_{PSNR} \\
0, & \text{otherwise}
\end{cases}
\]

当 \(Q = 0\) 时保持上一有效 HR 值。

## 7. SpO₂ 计算

### 7.1 频域 AC 幅值

\[
AC_{IR} = \sqrt{\frac{2}{N}} \cdot |S_{IR}[k_{peak}]|, \quad
AC_{RED} = \sqrt{\frac{2}{N}} \cdot |S_{RED}[k_{peak}]|
\]

### 7.2 AC/DC 比率

\[
R = \frac{AC_{RED} / DC_{RED}}{AC_{IR} / DC_{IR}}
\]

### 7.3 对数域映射

\[
R_{log} = 100 \cdot \frac{\ln(AC_{RED}^2)}{\ln(AC_{IR}^2)}
\]

### 7.4 LUT

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
