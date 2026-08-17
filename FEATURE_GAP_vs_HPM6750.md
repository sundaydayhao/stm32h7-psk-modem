# 功能差异清单：hpm6750-psk-modem 有、stm32h7-psk-modem 无

生成日期：2026-08-17
对比对象：`C:\projects\hpm6750-psk-modem`（源）vs `C:\projects\stm32h7-psk-modem`（目标，本文档存放于此）

## 结论摘要（先看这段）

两个项目共享同一套 PSK/QPSK/8PSK 调制解调与 FEC 核心代码：
- `app/modem/psk_modes.c`（27 种模式表）逐字节比对完全一致（去掉换行符差异后 diff 无输出）。
- `third_party/psk_rtty/psk_rtty.c` 核心算法逻辑一致，仅有内存分区宏（DTCM/RAM_D2/RAM_D3 vs 统一 FAST 宏）和少量调参常量（Viterbi 回溯深度、FEC 相位搜索遍历对数）不同，这些是**针对各自 MCU 的 RAM/CPU 预算调优**，不是功能缺失，**不要盲目照抄数值**。

真正的功能差距集中在**平台层（音频前端控制、CLI 诊断命令）**和**UI 实现方式**，以下按优先级列出，供 codex 逐项实现。

---

## 一、音频前端运行时控制（缺口最大，建议优先实现）

STM32H7 当前的 `app/platform/audio_input.c` / `.h` 只在初始化时固定设置一次 codec 增益（`wm8978_set_mic_gain(0)`、`wm8978_set_line_gain(6)`），运行期间**没有任何可调整能力**，对应的调试结构体 `audio_input_debug_t` 字段也比 hpm6750 少一大截。

### 1.1 自动增益控制（AGC）
- **hpm6750 实现**：`app/platform/audio_input.c` 中的 `update_frontend_agc()`，基于每块采样的峰值（`AGC_CLIP_PEAK=30000`）和低信号驱留计数（`AGC_LOW_DWELL_BLOCKS=96`）动态调整 codec 的 ADC/模拟增益寄存器；可通过 `audio_input_set_auto_gain()` 开关。
- **STM32H7 现状**：完全没有；增益是编译期常量，永远不变。
- **建议实现**：在 `audio_input.c` 增加 `update_frontend_agc()`，复用 hpm6750 的削波检测+驱留步进思路，落到 WM8978 的 MIC/LINE 增益寄存器（`wm8978_set_mic_gain`/`wm8978_set_line_gain` 已存在，改造成运行期可调）。新增 `audio_input_set_auto_gain(uint8_t enabled)`。

### 1.2 手动增益设置
- **hpm6750**：`audio_input_set_codec_gain(uint8_t adc_volume, uint8_t ana_volume)`。
- **STM32H7**：无对应 API，`wm8978_set_mic_gain`/`set_line_gain` 只在 init 里调用一次，未导出。
- **建议**：导出等价函数，参数按 WM8978 寄存器范围（mic 0-63、line 0-7）做 clamp。

### 1.3 Codec 输入源切换（mic / line）
- **hpm6750**：`audio_input_set_codec_input(uint8_t input)`，支持 0-5 共 6 种输入源选择，写入 `codec_input` 状态字段。
- **STM32H7**：WM8978 输入源在 init 阶段硬编码，运行期不可切换。
- **建议**：视 WM8978 寄存器能力实现等价接口（不需要和 hpm6750 的 0-5 编号完全一致，但要能在 mic/line 间切换）。

### 1.4 音频通道（lane）手动强制选择
- **hpm6750**：`audio_input_set_lane(int8_t lane)`，`-1`=自动按能量选，`0`/`1`=强制左右声道；结构体里有 `forced_lane` 字段。
- **STM32H7**：只有自动选择逻辑（按 abs-avg 能量挑左右声道），**没有强制覆盖能力**，也没有 `forced_lane` 字段。
- **建议**：给 `audio_input_debug_t` 加 `forced_lane`，并加 `audio_input_set_lane()`。

### 1.5 削波事件计数
- **hpm6750**：`audio_input_debug_t.clip_events` 记录削波次数，供诊断行输出。
- **STM32H7**：结构体里没有这个字段（对照 `app/platform/audio_input.h`，STM32H7 结构体在 `dma_ndtr` 后就直接结束，缺少 `codec_input`/`forced_lane`/`codec_adc_volume`/`codec_ana_volume`/`auto_gain_enabled`/`clip_events` 六个字段）。
- **建议**：随 1.1-1.4 一起把这六个字段补齐到结构体，`STATUS`/`[diag]` 输出才有数据可打。

---

## 二、串口 CLI 命令（依赖上面第一项，建议同批实现）

对照文件：hpm6750 `src/main.c` 的 `poll_serial_commands()` vs stm32h7 `Core/Src/main.c` 同名函数。

两边共有：`MODE`、`TONE`、`AFC`、`SQL`、`TEST`、`LOG`、`STATUS`（已确认 stm32h7 已实现，无需重做）。

STM32H7 **完全缺失**以下命令（已用 grep 确认 `Core/Src/main.c` 中无匹配）：

| 命令 | 作用 | 依赖 |
|---|---|---|
| `INPUT <0..5>` | 选择 codec 输入源 | 依赖 1.3 |
| `LANE AUTO\|0\|1` | 强制/自动选择音频声道 | 依赖 1.4 |
| `GAIN <adc> <ana>` | 手动设置 codec 增益 | 依赖 1.2 |
| `AGC [0\|1]` | 开关自动增益控制 | 依赖 1.1 |
| `DUMP [n]` | 转储接下来 n(≤8) 个原始采样块为十六进制 | 见第三节 |
| `CAP [n]` | 开始捕获最多 32768 个原始采样点 | 见第三节 |
| `CAPDUMP` | 打印已捕获的采样点（十六进制） | 见第三节 |

实现顺序建议：先做第一节的音频前端 API，再在 `poll_serial_commands()` 里按 hpm6750 `src/main.c` 中 `INPUT`/`LANE`/`GAIN`/`AGC` 分支的命令解析结构照抄（命令名、参数校验逻辑基本可以直接复用，函数名换成 STM32H7 侧的对应实现）。

---

## 三、原始采样捕获与十六进制转储（诊断能力，STM32H7 完全没有）

- **hpm6750 实现**：`src/main.c` 里的 `g_capture[32768]` 缓冲区（放在 framebuffer 专用 RAM 段），配合 `CAP`/`CAPDUMP`/`DUMP` 三个命令，可以在不接示波器/逻辑分析仪的情况下，通过串口把 ADC 原始采样直接抓出来分析。
- **STM32H7 现状**：完全没有这套机制（已用 grep 确认 `g_capture`/`CAPDUMP` 在整个项目里无匹配）。目前 STM32H7 唯一的现场调试手段是 `tools/board_wav_regression.ps1` 那套"电脑放音频喂给板子线路输入、再从串口读解码结果"的闭环，**没有直接抓取板载原始采样**的能力。
- **建议**：在 STM32H7 找一段空闲 RAM（注意 D1/D2/D3 域和 DTCM 分配，别和 Viterbi workspace 冲突），实现等价的捕获缓冲区 + `CAP`/`CAPDUMP`/`DUMP` 命令。这对后续排查音频链路问题（增益、削波、DC 偏置）会非常有用，建议优先级仅次于第一、二节。

---

## 四、FEC 解码能力差异：8PSK250F 的双分支搜索

- **代码位置**：`third_party/psk_rtty/psk_rtty.c` 中 `rx->fec_phase_search` 的初始化。
  - hpm6750：无条件设为 `0`（即所有模式，包括 8PSK250F，都跑完整双分支相位搜索）。
  - stm32h7：`(mode == PSK_MODE_8PSK250F) ? 1 : 0`，即对 8PSK250F 专门退化成单分支顺序相位搜索，注释/文档里写明是"CPU 算力受限的妥协"（见 `doc/PSK_DECODE_OPTIMIZATION_REVIEW.md`）。
- **含义**：这不是 STM32H7 "缺了一个功能"，而是 STM32H7 CPU 余量不足以支撑 8PSK250F 全双分支解码时做的性能妥协；hpm6750（RISC-V @ 更高主频/更强算力）不需要这个妥协。
- **建议**：**不建议直接抹掉这个妥协**去照搬 hpm6750 的行为，除非先做实测确认 STM32H7 在 8PSK250F 模式下双分支解码不会导致丢块/超时。可以作为一个"待验证的性能预算问题"记录，不列为常规功能任务。

---

## 五、用户界面实现方式（架构级差异，非必须迁移）

- **hpm6750**：`app/ui/display.c` 基于 LVGL（`CONFIG_LVGL=1`，见 `CMakeLists.txt`）——用 `lv_obj_t`/`lv_chart_series_t` 等构建标签、频谱 chart 组件、27 个模式按钮的事件回调（`mode_button_event_cb`）。
- **STM32H7**：`third_party/lvgl` 和 `lv_conf.h` 都在仓库里，但 **`CMakeLists.txt` 里没有把 LVGL 源码加入编译**，`app/ui/display.c` 实际是手写的裸framebuffer绘制（矩形填充+位图字体），`app/board/fire_lcd.c` 直写 LTDC framebuffer。
- **含义**：这不是"缺功能"，是两种不同的 UI 实现路径，STM32H7 现有方案已经能跑（状态栏、频谱条、滚动文本、触摸选模式）。除非用户明确要换成 LVGL（好处是后续加新界面/组件更方便），否则**不建议作为本次任务范围**，仅记录在案。

---

## 六、工具链/回归测试脚本的次要差异（低优先级，可选）

对照 `tools/` 目录内容：

- **hpm6750 独有**：
  - `openocd_connect_cmsis_dap.ps1`、`openocd_flash_cmsis_dap.ps1` —— 基于 OpenOCD + CMSIS-DAP 的连接/烧录脚本。STM32H7 目前用 pyOCD（见 `doc/SESSION_HANDOFF.md`），如果 STM32H7 也有 CMSIS-DAP 探针场景，可以照搬。
  - `run_host_regression_cygwin.sh` —— Cygwin GCC 版本的host回归脚本（STM32H7 只有 MSVC 版 `build_host_tools.ps1`/`build_ab.ps1`）。
  - `short_regression_input.txt`、`schematic_audio_*.png`、`python3.cmd` —— 辅助资产/wrapper，非核心功能。
- **建议**：仅在需要跨工具链验证或使用 CMSIS-DAP 探针时再补，不影响固件功能，优先级最低。

---

## 七、明确核实过、两边一致的部分（避免重复劳动）

以下内容在两个项目里逐字节/逐逻辑核对过，**完全相同，不需要迁移**：
- 27 种 PSK/QPSK/8PSK 模式表及元数据（`app/modem/psk_modes.c`）
- PSK/QPSK/8PSK 调制解调、AFC、squelch、信号质量估计、Viterbi/FEC 解码算法主体（`third_party/psk_rtty/psk_rtty.c`，除第四节提到的 8PSK250F 分支策略外）
- `audio_decim.c`（FIR 抽取+DC blocker）逐字节相同
- 触摸控制器掉线重试逻辑（1000ms 间隔重新 init）一致
- `CLI` 命令 `MODE`/`TONE`/`AFC`/`SQL`/`TEST`/`LOG`/`STATUS` 均已在 STM32H7 实现
- RX 信号打开时复位 modem 状态（`rx_input_open`/`RX_INPUT_OPEN_ABS_AVG=500`）、RX 文本日志按大小/时间 flush（`RX_LOG_FLUSH_BYTES`/`RX_LOG_FLUSH_MS`）逻辑一致

---

## 建议实现顺序

1. 音频前端运行时控制（第一节：AGC、手动增益、输入源切换、lane 强制、削波计数）——其他两项功能都依赖它。
2. 对应的 CLI 命令（第二节：`INPUT`/`LANE`/`GAIN`/`AGC`）。
3. 原始采样捕获与转储（第三节：`CAP`/`CAPDUMP`/`DUMP`）——独立功能，可与 1/2 并行开发。
4. 第四、五、六节仅记录，按需评估，不建议默认排期。
