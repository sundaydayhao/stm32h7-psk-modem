# PSK Modem 主控芯片选型评估

日期：2026-08-13

## 结论

如果目标是最终产品性能、实时性和后续扩展能力，首选：

1. **NXP i.MX RT1176 + ESP32-C6/S31**
2. **HPM6750 + ESP32-C6/S31**
3. **Renesas RA8M2 + ESP32-C6/S31**

如果强调国内供应链、性价比和较低BOM风险，首选：

1. **HPM6750 + ESP32-C6**
2. HPM6450 + ESP32-C6
3. HPM6364 + ESP32-C6

如果必须单芯片内置 Wi-Fi/BT：

1. **ESP32-S31**
2. NXP RW612
3. ESP32-S3

但对当前PSK解码项目，不建议优先采用单芯片无线MCU。当前解码器的RAM和实时性压力已经超过多数无线MCU的舒适区。

## 当前项目的硬约束

当前项目路径：

`C:\projects\stm32h7-psk-modem`

当前H743固件不是普通低负载应用，而是已经接近STM32H743内部RAM和CPU实时边界的DSP/FEC应用。

关键证据：

- 当前PSK链路包括48 kHz音频输入、约16 kHz解码采样、复数混频、FIR、AGC、同步、AFC、差分相位、软判决、Viterbi/FEC。
- K=16 FEC使用大静态Viterbi工作区。
- 最新构建内部RAM大约使用：
  - D1 SRAM：约496 KB / 512 KB
  - D2 SRAM：256 KB / 288 KB
  - DTCM及栈：约90 KB
  - 合计约848 KB
- `8PSK125F` 双K=16分支可以实时运行。
- `8PSK250F` 双K=16分支会导致音频队列增长/溢出，目前只能退回单分支。

相关本地文件：

- `DEBUG_NOTES.md`
- `arch.md`
- `third_party/psk_rtty/psk_rtty.c`
- `STM32H743IITX_FLASH.ld`
- `CMakeLists.txt`

## 浮点性能判断

当前代码大量使用FP32：

- 复数NCO混频
- FIR和匹配滤波
- AGC、AFC、同步
- `sqrtf`
- `atan2f`
- `cosf` / `sinf`
- 频谱和瀑布图计算

当前STM32H743已启用硬件FPU：

- `-mcpu=cortex-m7`
- `-mfpu=fpv5-d16`
- `-mfloat-abi=hard`

并且核心DSP文件在Debug构建中也额外使用 `-O2`。因此当前瓶颈不是简单的“没开FPU”或“没开优化”，而是CPU最坏情况和内部RAM都已经接近边界。

对后续主控的要求：

- FP32性能必须明显高于当前400 MHz H743。
- FP64不是当前核心需求，但有硬件DP-FPU更好。
- 片上低延迟RAM应至少1.5 MB，最好2 MB以上。
- 不应依赖外部SDRAM/PSRAM承载Viterbi热数据。
- 如果有第二核心，应把UI、触摸、Wi-Fi/BT通信和日志从实时解码核心隔离出去。

## 推荐排序：综合量产版本

| 排名 | 方案 | 判断 |
|---:|---|---|
| 1 | **i.MX RT1176 + ESP32-C6/S31** | 1 GHz M7 + 400 MHz M4、2 MB SRAM、NXP生态成熟，综合最稳 |
| 2 | **HPM6750 + ESP32-C6/S31** | 双核816 MHz、2 MB SRAM、性价比高，国内供应链友好 |
| 3 | **Renesas RA8M2 + ESP32-C6/S31** | Cortex-M85 1 GHz、Helium、2 MB ECC SRAM，性能很强但较新 |
| 4 | STM32N657 + ESP32-C6/S31 | 800 MHz M55、Helium、4.2 MB SRAM，大RAM强，但部分资源对PSK不是刚需 |
| 5 | HPM6450 + ESP32-C6 | 单核约816 MHz、2 MB SRAM，性能强但少一个核心 |
| 6 | STM32H747 + ESP32-C6 | M7 480 + M4 240、1 MB RAM，供应链稳但RAM余量一般 |
| 7 | HPM6364 + ESP32-C6 | 648 MHz、800 KB SRAM、4 MB Flash，低成本但RAM不足 |
| 8 | RT1052 + ESP32-C6 | 600 MHz M7、512 KB SRAM，便宜成熟但必须依赖外部SDRAM |
| 9 | GD32H759/H757 + ESP32-C6 | 600 MHz M7、1 MB SRAM，算力提升但RAM仍紧 |
| 10 | STM32H750XB + ESP32-C6 | 480 MHz M7、1 MB RAM、128 KB Flash，适合H743降本，不适合性能升级 |
| 11 | ESP32-P4 + C6 | 双核400 MHz、768 KB片上RAM，RAM不适合当前Viterbi状态 |
| 12 | STM32H743 + AP6181 | 当前平台附近，性能和无线能力都不建议继续扩展 |

## 单芯片内置 Wi-Fi/BT 方案

| 芯片 | 无线 | CPU/RAM | 评价 |
|---|---|---|---|
| **ESP32-S31** | Wi-Fi 6、BLE 5.4、Bluetooth Classic、802.15.4 | 双核RISC-V 320 MHz、512 KB SRAM、外接PSRAM | 单芯片无线里最强，但片上RAM不足 |
| NXP RW612 | Wi-Fi 6、BLE 5.4、802.15.4 | Cortex-M33 260 MHz、约1.2 MB SRAM | RAM尚可，CPU/FPU不适合当前重DSP |
| ESP32-S3 | Wi-Fi 4、BLE | 双核240 MHz、512 KB SRAM | 成熟便宜，但算力不够 |
| ESP32-C5/C6 | Wi-Fi 6、BLE | 单核240/160 MHz级别 | 适合作无线协处理器，不适合主解码 |

结论：如果必须单芯片，选ESP32-S31。但如果目标是稳定实时解码，仍建议“高性能主控 + 独立无线芯片”。

## Wi-Fi/BT协处理器选择

无线能力建议独立出来，避免协议栈影响实时DSP。

- 只需要Wi-Fi + BLE：**ESP32-C6**
- 需要Wi-Fi + BLE + Bluetooth Classic/SPP/传统蓝牙音频：**ESP32-S31**
- 不建议AP6181：它偏Wi-Fi模块，且不满足完整BT能力要求。

主控和无线芯片通信：

- 优先：SPI
- 简化调试：UART AT
- 高吞吐网络：可评估SDIO，但驱动复杂度更高

## 重点候选点评

### i.MX RT1176

推荐等级：最高

优点：

- Cortex-M7 1 GHz + Cortex-M4 400 MHz。
- 2 MB片上SRAM。
- M7支持硬件浮点。
- NXP生态、工具链、SDK和全球供货成熟。
- M4可以承担UI、触摸、通信、日志。
- 适合把当前H743边界负载变成有充足余量的产品。

缺点：

- 相比低成本MCU，BOM较高。
- 需要外接Flash。
- PCB和启动链路复杂度高于普通STM32。

判断：

如果目标是“少踩坑、全球量产、性能余量充分”，RT1176是最稳的高性能选择。

### HPM6750

推荐等级：最高，尤其适合国内供应链和性价比优先

优点：

- 双核816 MHz RISC-V。
- 2 MB片上SRAM。
- 支持单/双精度FPU、DSP/SIMD。
- 性价比高。
- 第二核心可隔离UI/通信。

缺点：

- 生态、全球供应渠道和长期供货确定性弱于NXP/ST。
- RISC-V平台需要实际移植和benchmark确认。

判断：

如果可以接受HPM生态，HPM6750是当前最有性价比的性能方案。

### HPM6450

推荐等级：中高

优点：

- 单核约816 MHz。
- 2 MB片上SRAM。
- FPU/DSP能力强。
- 成本低于HPM6750。

缺点：

- 单核，UI、网络和解码不能天然隔离。
- 与HPM6750差价不大时不划算。

判断：

若BOM严格受限，HPM6450可作为HPM6750的降本版本；否则优先6750。

### HPM6364 / HPM6300

推荐等级：成本版可评估

优点：

- 最高约648 MHz。
- 单/双精度FPU。
- DSP/SIMD。
- HPM6364带4 MB内部Flash。
- 价格明显低于HPM6450/HPM6750。

缺点：

- 片上SRAM约800 KB，低于当前约848 KB实际内部RAM需求。
- 单核。
- 需要压缩Viterbi状态或把部分数据放外部RAM。

判断：

适合成本受限版本，不适合最高性能版本。如果差价能接受，HPM6450/6750的2 MB SRAM更关键。

### STM32H747

推荐等级：供应稳妥的保守选项

优点：

- M7 480 MHz + M4 240 MHz。
- ST生态成熟。
- 可复用较多当前STM32H7经验。
- M4可承担UI/通信。

缺点：

- 总RAM仍约1 MB。
- M7单核性能只比当前H743提升有限。
- 无法根本解决当前K=16双分支的性能/RAM压力。

判断：

适合重视ST生态、供应链和移植风险的产品，但不是最强性能选择。

### STM32H750XB

推荐等级：H743降本版本

优点：

- M7最高480 MHz。
- 1 MB SRAM。
- DP-FPU。
- ST生态成熟。
- 成本相对低。

缺点：

- 内部Flash只有128 KB，基本必须外接QSPI Flash。
- 单核。
- RAM和CPU没有跨代提升。
- 只适合当前能力附近，不适合作为下一代性能平台。

判断：

适合“当前H743方案降本”，不适合追求双K=16余量和后续扩展。

### GD32H759/H757

推荐等级：可评估，但不是首选

优点：

- Cortex-M7约600 MHz。
- 1 MB SRAM。
- DP-FPU。
- 国内采购相对方便。

缺点：

- 单核。
- RAM仍紧。
- TMU/FAC等加速器需要针对性改写代码，不会自动加速当前Viterbi。

判断：

比H743/H750更快，但不如HPM6450/6750的2 MB SRAM实用。

### ESP32-P4 + ESP32-C6

推荐等级：不建议作为当前主解码平台

优点：

- 双核RISC-V 400 MHz。
- 有单精度FPU和128位DSP扩展。
- 乐鑫生态好。
- 与C6搭配无线方便。

缺点：

- 片上RAM约768 KB，低于当前项目约848 KB需求。
- 需要外部PSRAM承载部分状态，实时确定性差。
- 当前代码不能直接利用其SIMD优势。

判断：

适合低成本HMI/联网产品，不适合当前重Viterbi实时解码主控。

### ESP32-S31

推荐等级：单芯片无线首选，但不建议作为本项目主控首选

优点：

- 单芯片内置Wi-Fi 6、BLE 5.4、Bluetooth Classic、802.15.4。
- 双核RISC-V 320 MHz。
- 支持FPU、CORDIC、SIMD相关能力。
- 集成度高。

缺点：

- 片上SRAM约512 KB。
- Wi-Fi/BT协议栈会占用RAM、CPU和中断。
- 当前约848 KB实时状态必须外放PSRAM。

判断：

如果产品必须单芯片无线，ESP32-S31最值得评估；如果要求稳定实时解码，不建议把它作为主DSP。

### RT1052

推荐等级：低成本NXP方案，可评估但不是最终性能平台

优点：

- Cortex-M7 600 MHz。
- FP32/FP64硬件FPU。
- 32 KB I-Cache + 32 KB D-Cache。
- NXP生态成熟。
- 支持SDRAM、LCD、SAI/I2S、USB、Ethernet、SDIO。
- 价格低，供货成熟。

缺点：

- 片上SRAM只有512 KB。
- 无内部Flash，必须外接Flash。
- 当前约848 KB实时状态无法全部放入片上RAM。
- Viterbi大数组必须放外部SDRAM时，实时最坏延迟风险明显。

判断：

RT1052性价比高，但更适合成本版或轻负载版本。若使用RT1052，必须优先做“Viterbi大数组放SDRAM”的实时性测试，不能只看600 MHz主频。

## 关于1 GHz是否大材小用

不算明显大材小用。

对本项目，1 GHz的价值不是平均负载跑满，而是提供最坏情况余量：

- 让`8PSK250F`双K=16从过载变成有余量。
- 让LCD、频谱、触摸、Wi-Fi/BT通信不挤压解码时限。
- 支持更长traceback、更稳健同步、更复杂软判决。
- 减少底层极限优化压力，缩短产品开发周期。
- 给温度、电压、编译器版本、外设中断抖动留余量。

如果只做当前已稳定模式，并且不要求双K=16、复杂网络/UI，HPM6364、STM32H750XB、RT1052这类成本方案可以考虑。

如果目标是一次做稳、后续算法不被算力卡住，RT1176或HPM6750并不浪费。

## 建议的下一步验证

建议实际购买或借用以下开发板做同代码benchmark：

1. i.MX RT1176 EVK
2. HPM6750 EVK
3. Renesas RA8M2评估板
4. HPM6364或RT1052作为成本对照

验证项目：

- 将当前`third_party/psk_rtty/psk_rtty.c`核心移植为裸机/RTOS benchmark。
- 使用同一组WAV或二进制采样输入。
- 测量每128样本block的最坏处理时间。
- 单独测量：
  - 非FEC BPSK1000
  - QPSK500
  - 8PSK125F双K=16
  - 8PSK250F双K=16
  - 12xPSK125多载波
- 测试有/无LCD刷新、有/无Wi-Fi流量时的最坏延迟。
- 统计最大音频队列深度和是否溢出。
- 记录片上RAM占用，不把Viterbi热数据放外部RAM作为首选目标。

## 最终工程建议

面向最终产品，不建议继续围绕H743/H750做性能扩展。

推荐两条路线：

### 性能稳妥路线

主控：i.MX RT1176  
无线：ESP32-C6 或 ESP32-S31  
适合：全球量产、长期维护、性能余量优先。

### 性价比路线

主控：HPM6750  
无线：ESP32-C6 或 ESP32-S31  
适合：国内供应链、成本敏感、仍需要明显性能余量。

如果需要Bluetooth Classic，优先选ESP32-S31作为无线协处理器；如果只需要Wi-Fi和BLE，ESP32-C6足够。
