# PSK 解码实现评估与优化空间分析

日期：2026-08-13

本文档基于对以下文件的完整review：

- `third_party/psk_rtty/psk_rtty.c`（核心解码引擎，1624行）
- `third_party/psk_rtty/include/psk_rtty.h`、`pskcore_rx_tables.h`、`PSKTables.h`
- `app/modem/psk_modem.c`、`app/modem/psk_modes.c`
- `app/platform/audio_input.c`、`app/dsp/spectrum.c`
- `Core/Src/main.c`、`STM32H743IITX_FLASH.ld`、`CMakeLists.txt`、`CMakePresets.json`
- `app/ui/display.c`
- 现有文档：`arch.md`、`DEBUG_NOTES.md`、`README.md`、`SESSION_HANDOFF.md`、`tools/host_validation_notes.md`、`MCU_SELECTION_EVALUATION.md`

并结合 fldigi（`C:\projects\fldigi`，作为行业参考实现）以及商用/专业PSK modem（如WinWarbler、MultiPSK、专业HF调制解调设备）的通行做法进行对比。

**本文档不修改任何源代码，仅做只读评估与建议归纳。**

---

## 1. 结论摘要

当前项目在协议正确性层面已经相当成熟：27个PSK模式全部标记`decode_supported=1`，并已针对fldigi的软判决公式、双支路比特相位歧义解决、基于回溯路径度量的支路投票等细节做过对齐修正（详见`DEBUG_NOTES.md`）。这在业余固件项目中是不常见的严谨程度。

但从"稳定实时解码 + 可维护 + 可扩展"的商用modem标准看，仍有五类明显差距：

1. **实时确定性不足**：轮询式UART、非双缓冲LCD刷写、`main.c`超循环中DSP处理与UI/诊断打印共享同一线程时序，缺少商用设备常见的硬中断驱动I/O分离。
2. **算力/RAM已到达平台上限**：`8PSK250F`被迫退化为单支路顺序相位搜索，而不是fldigi式的双K=16支路，这是**协议保真度对硬件限制的妥协**，不是设计选择。
3. **诊断能力弱于商用设备**：现有BER/CER统计包含捕获噪声（未按payload窗口过滤），没有星座图/眼图/相位轨迹等专业modem常见的调试可视化。
4. **架构不统一**：RTTY走一条完全独立、较简单的解码路径，未纳入统一PSK pipeline，长期看会造成两套并行的维护负担。
5. **前端信号处理仍有可进一步逼近商用modem的空间**：AGC/AFC仅在单载波非8PSK时启用，多载波和8PSK缺乏频率跟踪；符号定时恢复是16相位能量跟踪器，比fldigi的做法更简单但尚可接受；频谱分析用的是O(bins×N)的旋转累加法而非FFT。

以下按模块展开。

---

## 2. 当前实现状态总览（按模式家族）

| 家族 | 模式数 | FEC | 关键实现特征 | 与fldigi的差距 |
| --- | ---: | --- | --- | --- |
| BPSK 非FEC (31/63/125/250/500/1000) | 6 | 无 | 差分符号→硬判决比特→PSK varicode | 基本对等 |
| BPSK63F | 1 | K=7 | 软比特→双支路FEC/Viterbi→MFSK varicode | 对等（已按fldigi修正） |
| 多载波 xPSK (12x125/6x250/2x500/4x500/2x800/2x1000) | 6 | 无 | 每载波独立NCO/滤波器状态，AFC对多载波关闭 | fldigi多载波AFC也普遍受限，基本对等 |
| QPSK (31/63/125/250/500) | 5 | K=5 | 软比特→FEC/Viterbi→PSK varicode | 对等 |
| 8PSK 非FEC (125/250/500/1000) | 4 | 无 | 差分符号→3硬比特→MFSK varicode | 对等 |
| 8PSK125F | 1 | K=16 | 双支路交织FEC/Viterbi | 对等 |
| **8PSK250F** | 1 | K=16 | **单支路顺序相位搜索**（`FEC_PHASE_EVAL_PAIRS=896`），而非双支路 | **明显弱于fldigi**，是CPU/队列过载后的降级方案 |
| 8PSK500F | 1 | K=13, punctured | 软比特+打孔→FEC/Viterbi | 对等 |
| 8PSK1000F/1200F | 2 | K=7, punctured | 软比特+打孔→FEC/Viterbi | 对等 |
| RTTY | 1 | 无 | **完全独立的简单NCO+积分清零路径**，不经过统一PSK pipeline | 架构层面不统一 |

结论：**协议层面的正确性已经很接近fldigi**，唯一明确的功能性退化是`8PSK250F`；其余差距主要在实时工程/诊断/架构整洁度层面，而不是解调算法本身。

---

## 3. 与fldigi/商用PSK modem的对比细节

### 3.1 Viterbi/FEC 实现

现状（`psk_rtty.c`）：
- 静态预计算的pred/output表（`prepare_viterbi_tables`），按K/多项式缓存，避免重复计算——这是好的工程实践，商用DSP实现常见。
- `VIT_MAX_TRACEBACK=64`，而fldigi对应实现使用`K*12`（K=16时为192）。回溯深度直接影响误码率在深衰落/突发噪声下的恢复能力。
- 输出chunk是8-bit打包，fldigi部分路径用4-bit，影响的是内存粒度而非解码质量本身。
- `vit_traceback`在emit的chunk上**重新计算路径度量**而不是使用累积状态总度量——这是为了在有限traceback窗口下获得更准的"chunk内"度量，属于针对STM32限制做的合理工程折中，但也意味着无法做全局代价比较（例如无法在多个候选路径间做全局MLSE比较）。

**差距**：主要是`8PSK250F`从双支路降为顺序相位搜索。fldigi在算力充裕的PC平台上对R=1/2、K=16码总是用双支路（覆盖两种起始相位假设）以最大化首次同步命中率和抗突发错误能力；单支路顺序搜索本质上是"用时间换空间"，在深衰落信道下収敛慢、且在信道条件切换时反应更慢。

**商用设备参照**：专业HF/卫星modem（如军用/海事MIL-STD-188-110、STANAG 4285类调制解调器)对深衰落信道的Viterbi解码普遍采用更长回溯（>10×K）与并行假设检验（多相位/多起始状态并发），且很多专用DSP硬件用SIMD/向量指令并行做butterfly更新——这是本项目在STM32H743上难以完全达到的。

### 3.2 AFC/AGC/符号同步

现状：
- AFC仅在单载波、非8PSK情形下启用（`arch.md`已文档化此限制）。
- 8PSK和所有多载波模式没有独立频偏跟踪，完全依赖初始assume的音频tone误差范围。
- 符号定时用16相位能量跟踪器（`symbol_sync`），是一种简化的"最大能量相位"算法。

**差距**：fldigi对8PSK同样有AFC上的限制（8PSK因为星座旋转对称性使频偏估计更难，这是协议本身的特性，不是本项目独有短板）。但商用modem通常会引入：
- Costas环或类似的载波跟踪环路，能持续闭环纠正频偏和相位漂移，而不仅仅是"first-lock"式AFC；
- 更精细的symbol timing recovery（如Gardner算法、Mueller & Müller算法），比固定16相位bin的能量比较有更高定时抖动容限。

这部分是**协议无关的通用DSP改进空间**，即使不改变FEC/Viterbi，也可能改善弱信号下的锁定稳定性。

### 3.3 星座软判决与量化

现状：8PSK软比特通过`push_8psk_soft_bits`基于相位质量做erasure（不确定符号打软标记而非直接丢弃）。QPSK/BPSK软比特路径类似。这与fldigi的做法基本一致，是恰当的软判决设计。

无明显差距。

---

## 4. 已文档化但尚未解决的限制（引用自DEBUG_NOTES.md / host_validation_notes.md）

以下内容已经被项目维护者自己记录，这里做汇总以便和新发现的问题一起排定优先级：

1. **`8PSK250F`双K=16分支导致音频队列溢出**，目前退回顺序相位搜索（单支路）。`DEBUG_NOTES.md`"架构权衡"章节将其列为已知代价。
2. **`VIT_MAX_TRACEBACK=64` vs fldigi的`K*12`**：影响深衰落恢复能力，尤其是K=16模式（fldigi等效192深度 vs 本项目64）。
3. **BER/CER统计包含获取噪声**：当前诊断打印的误码率统计未按"payload窗口"过滤，导致数值比真实链路质量更差，不能直接和fldigi或商用设备的误码率指标横向比较。`DEBUG_NOTES.md`已列为Follow-up。
4. **UART命令解析为轮询式**（`poll_serial_commands`，每次最多drain 64字符），而不是中断/DMA环形缓冲。在DSP负载高峰期可能造成串口响应延迟或丢字符风险。
5. **音频时钟依赖PLL1Q复用**（48kHz请求，实际约48828.125Hz），已有详细数学换算和明确"不要复用PLL2"的告警（会破坏SDRAM/LCD时钟树）。`DEBUG_NOTES.md`建议后续用专用PLL精确锁定时钟，减少时钟误差对解码基准的系统性影响。
6. **`tools/host_validation_notes.md`明确提出的未来加固项**："add fldigi-style dual-phase FEC decoding for R=1/2 streams"——这与3.1节的`8PSK250F`退化问题是同一个根因，值得合并处理。
7. **D1/D2 SRAM占用已到88.89%~94.77%**，几乎没有余量应对未来新增模式或加大traceback/chunk size。

---

## 5. Review中新发现、尚未被现有文档记录的优化空间

以下是本次review过程中观察到、但在`arch.md`/`DEBUG_NOTES.md`/`MCU_SELECTION_EVALUATION.md`中均未提及的问题：

### 5.1 RTTY解码路径与统一PSK pipeline完全割裂

`psk_rtty.c`中`finish_symbol`/`push_rtty_bit`/`rtty_rx_feed`是一套独立于`psk_channel_rx_feed`的简单NCO+积分清零(FSK)解调实现，不共享AGC/AFC/squelch/符号同步的公共基础设施。这意味着：
- 未来对squelch质量、AFC策略的改进需要在两处分别实现和验证；
- RTTY路径的信噪比表现无法从PSK pipeline的改进中受益（例如更好的symbol timing算法）。

**建议**：中长期可考虑把RTTY的FSK解调抽象为PSK pipeline的一个"family"，复用公共的AGC/squelch框架，仅保留FSK特有的双音频解调逻辑。这是架构整洁度问题，不影响当前功能正确性，可以列为低优先级技术债。

### 5.2 频谱分析器是O(bins×N)的旋转累加DFT，而非FFT

`app/dsp/spectrum.c`对每个频谱bin独立做逐样本复数旋转累加（sliding DFT via rotation），复杂度是`O(SPECTRUM_BINS × count)`。虽然这不在解码路径上（`arch.md`已明确标注spectrum.c仅用于UI瀑布图，不影响解码），但：
- 如果bin数增加或想要更高频率分辨率，计算成本会线性增长而不是像FFT一样是`O(N log N)`；
- 在CPU已经接近实时边界的当前状态下，UI频谱计算与解码线程共享CPU时间片，即使逻辑上独立，仍会挤占最坏情况下的可用周期。

**建议**：如果未来需要提高频谱分辨率或刷新率，应评估改为分块FFT（CMSIS-DSP的`arm_rfft_fast_f32`性能优于手写旋转累加），可以显著降低UI对解码实时性的间接影响。当前bin数下影响有限，可标记为中低优先级。

### 5.3 ITCMRAM（64KB）完全未使用

`STM32H743IITX_FLASH.ld`定义了ITCMRAM区域，但没有任何`.text`段被显式放置进去。ITCM对Cortex-M7是零等待周期的指令内存，通常商用DSP固件会把最热的中断服务例程或最内层DSP kernel（例如FIR/NCO/Viterbi butterfly这类每样本/每符号调用的函数）放进ITCM以消除Flash访问延迟和分支预测miss的影响。

**建议**：可以评估把`psk_channel_rx_feed`内部最热的几个函数（NCO混频、`calc_bit_filter`、Viterbi蝶形更新）通过链接器脚本`.itcm_text`段迁移到ITCM。这是纯性能优化，不改变算法行为，风险低、收益取决于当前Flash等待周期设置（H743在400MHz下Flash通常已有ART加速缓存，实际收益需要benchmark验证，但值得一试）。

### 5.4 CMakeLists.txt的Debug-only `-O2`覆盖，且全项目无LTO

`CMakeLists.txt`仅对`spectrum.c`和`psk_rtty.c`在**Debug**配置下强制`-O2`（其余文件走Debug默认的`-O0`/`-Og`），且Release/Debug都没有开启LTO（`-flto`）。这意味着：
- Debug构建下的性能benchmark结果和Release可能有系统性差异，如果开发过程中习惯用Debug构建做实时性验证，可能导致误判裕度；
- 没有LTO意味着跨文件（例如`psk_modem.c`到`psk_rtty.c`的转发调用、`psk_modes.c`的mode_info查表）无法被内联优化，对于`psk_modem_process`这种每音频块调用一次的热路径有轻微的额外开销。

**建议**：
1. 明确文档化"性能相关的实时性测试只应在Release构建上进行"（如果已经是团队默认认知，可以补一句到`DEBUG_NOTES.md`或`README.md`，避免未来协作者用Debug构建做基准测试产生误判）。
2. 评估在Release构建上开启`-flto`，配合`-Wl,--gc-sections`已有设置，可能进一步压缩代码体积并提升热路径内联率，代价是编译时间增加，链接期错误定位变复杂——需要实测验证收益是否值得。

### 5.5 LTDC显示刷新无双缓冲/vsync同步痕迹

`app/ui/display.c`中的渲染直接写向显示buffer，没有看到帧缓冲切换或LTDC vsync等待的同步逻辑（`main.c`里是固定500ms周期刷新）。这在当前"低刷新率状态显示"场景下问题不大（500ms周期远低于产生撕裂感的阈值），但如果未来要做实时星座图/眼图动画（商用modem常见的调试可视化功能，见5.6），没有vsync同步会在高刷新率下出现明显的画面撕裂。

**建议**：低优先级，仅在未来真正需要高刷新率可视化（如实时星座图）时才需要处理；当前用途下不构成实际问题。

### 5.6 缺少商用/专业modem常见的信号质量可视化诊断

对比WinWarbler、MultiPSK等商用/半商用软件modem，以及专业硬件modem的调试接口，通常都提供：
- 实时星座图（constellation scatter plot）——直接可视化相位噜声、频偏残留、AGC是否稳定；
- 眼图（eye diagram）——直观展示symbol timing裕度；
- SNR/信号强度的时间序列曲线，而不仅是当前数值。

当前项目的LCD只有频谱瀑布图和文本状态栏（`debug->fec_metric`等数值型debug snapshot），没有星座图类可视化。这类功能对**协议正确性**没有帮助，但对**现场调试和用户信任度**（用户能直观看到"为什么没解出来"）有明显价值，是商用设备和本项目在用户体验层面的差距。

**建议**：中优先级，可作为后续UI功能追加——利用已有的`psk_rtty_complex_t`每载波复数样本（在`finish_channel_psk_symbol`中已经计算过差分相位所需的复数值），几乎不需要额外DSP计算，只需要把已有中间值透出到UI层做散点绘制。

---

## 6. 优先级排序的改进建议

综合第4节（已知问题）和第5节（新发现问题），按"协议正确性影响"和"工程可行性"排序：

### P0 — 直接影响解码质量，且已有明确根因

1. **`8PSK250F`从双支路FEC恢复为dual-branch**：需要先解决音频队列溢出的根本原因（CPU时序或RAM），而不是仅仅恢复双支路导致重新溢出。这与`MCU_SELECTION_EVALUATION.md`的下一代主控建议直接相关——如果留在H743平台，这个限制可能无法从软件层面根本解决，只能等待新主控。若坚持H743，可评估：缩短`FEC_PHASE_EVAL_PAIRS`换取更快相位收敛（有效性需要实测数据支持）、或进一步裁剪`8PSK250F`单支路路径上的非必要计算为双支路腾出CPU余量。
2. **修正BER/CER统计口径为payload窗口**：这是纯软件工程量，不依赖新硬件，应该优先做——因为当前的误码率数字失真，会影响所有其它优化的效果评估基准（包括本文档里其它建议的验证）。

### P1 — 明确的工程债务，中等工作量

3. **VIT_MAX_TRACEBACK从64提升到更接近fldigi的K*12**（K=16时192）：需要评估RAM/CPU代价，D1/D2已经88%+占用，可能需要先做5.3/5.4的省内存/省周期优化才有空间容纳。
4. **UART改为中断/DMA环形缓冲**：降低轮询延迟风险，是`DEBUG_NOTES.md`已列出的Follow-up，工作量可控。
5. **音频时钟改用专用PLL精确锁定**：消除系统性时钟误差对所有模式解码基准的影响，是"地基"级别的精度改进。

### P2 — 架构整洁度/长期可维护性

6. **RTTY路径并入统一PSK pipeline框架**（5.1）：不紧急，但拖得越久重复维护成本越高。
7. **ITCM热路径迁移**（5.3）：低风险性能优化，可作为独立的benchmark驱动任务，不依赖其它改动。
8. **Release-only性能基准测试的文档化约定 + LTO评估**（5.4）：低成本高杠杆的流程改进。

### P3 — 用户体验/调试能力，不影响协议正确性

9. **星座图/眼图类可视化**（5.6）：对现场调试和最终产品的用户信任度有价值，适合作为下一代硬件（配合`MCU_SELECTION_EVALUATION.md`推荐的高算力主控）上的差异化功能。
10. **频谱分析器改为FFT实现**（5.2）：仅在需要提高频谱分辨率/刷新率时才有必要。

### 平台级（已在MCU_SELECTION_EVALUATION.md中详细论证，此处仅做交叉引用）

上述P0/P1中的多项改进（尤其是恢复`8PSK250F`双支路、提升traceback深度）本质上都受限于STM32H743的RAM/CPU天花板。`MCU_SELECTION_EVALUATION.md`已经系统性论证了这一点，并给出了i.MX RT1176 / HPM6750两条推荐路线。**本文档的P0-1和P1-3两项建议，与该文档的结论完全一致**：如果坚持在H743上做，只能通过牺牲其它余量（内存布局、精简功能）来换取局部改进；如果目标是同时满足双支路8PSK250F、更深traceback、以及未来更多模式扩展，根本解法是换代主控。

---

## 7. 与fldigi差距的总体定性评价

把当前项目放到"业余开源实现 → 成熟开源实现（fldigi） → 商用专业设备"这个谱系上看：

- **协议/算法保真度**：已经非常接近fldigi水准，多处细节（软判决公式、双支路歧义解决、支路投票）经过明确的对齐修正，这是超出一般嵌入式移植项目严谨度的工作。
- **实时工程化程度**：介于"嵌入式移植项目"和"商用设备"之间——已经有清晰的内存分区策略（DTCM/D1/D2/D3热数据布局）和host/board一致性验证方法论（这点甚至比很多商用嵌入式项目做得更规范），但I/O子系统（UART轮询、无双缓冲LCD）仍是嵌入式原型阶段的典型写法。
- **诊断与用户体验**：明显落后于商用设备——缺少星座图/眼图类的信号质量可视化，误码率统计口径不准确，这些是"能跑"和"好用/好调试"之间的差距。
- **平台算力天花板**：这是最根本的差距来源，也是`MCU_SELECTION_EVALUATION.md`已经详尽论证过的问题——`8PSK250F`的降级不是实现能力不足，而是当前主控物理上限已经摸到了。

总体判断：**继续在软件层面按本文档P0-P2的顺序推进，可以在不换主控的前提下把当前架构的"确定性"和"精度"打磨到接近其硬件上限；但要恢复8PSK250F的完整双支路解码能力和拉大K=16的traceback深度这类协议保真度的最后一段差距，最终仍需要`MCU_SELECTION_EVALUATION.md`中论证的下一代主控。**
