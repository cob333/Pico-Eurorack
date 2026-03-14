# Pico-Eurorack

面向 2HP Pico / PicoFX Eurorack 模块的一套 RP2350 固件仓库，整合了：

- `Sketches/Pico`：常规 2HPico 模块 sketch
- `Sketches/PicoFX`：DSP / 效果器 sketch
- `Sketches/Test`：校准与硬件自检 sketch
- `Sketches/lib`：随仓库提供的 Arduino 库
- `AudioBootloader`：通过 3.5mm 音频线更新固件的 bootloader 与工具链

这个仓库建立在 Rich Heslip 的 `2HPico-Sketches` 和 `2HPico-DSP-Sketches` 思路之上，目标是把普通模块、DSP 模块、测试程序和音频更新流程放到同一个目录结构里，便于维护、编译和分发。

## 这个仓库和上游的区别

- 新增和扩展了多种模块功能
- 统一了 `Pico` / `PicoFX` / `Test` / `lib` 的目录结构
- 增加了独立的音频脚本写入机制

## 如果你只想写入某个 sketch

### 1. 第一步：
按住RP2350的BOOT按钮，然后连接USB线到电脑上，进入bootloader模式；

### 2. 第二步：
此时你会在电脑上看到一个磁盘`RP2350`（或者`RPI-RP2`），将 `AudioBootloader/build/audio_bootloader.uf2` 拖入此磁盘上；

### 3. 第三步：
拔掉USB线，把模块装回Eurorack，重新上电，模块将进入bootloader模式，此时闪烁蓝色灯；

### 4. 第四步：
用3.5mm音频线把你的电脑连接到模块的顶部插孔，播放你想写入的sketch对应的`.wav`文件，播放时调整电脑音量，也可以使用`Pot1`微调，让LED主要保持在绿色，避免出现蓝色/橙色/红色；

### 5. 第五步：
如果写入成功，LED会绿色闪烁3次，并自动跳转到新应用；如果写入失败，按下模块按钮后重试；

### 6. 第六步：
一旦写入了`audio_bootloader.uf2`，你就可以直接通过音频线写入新的sketch了，按住模块按钮上电进入更新模式，播放新的`.wav`，无需通过USB连接；

# 如果你想要编译和自定义sketch，请继续阅读下面的说明。
## 目录结构

| 路径 | 说明 |
| --- | --- |
| `Sketches/Pico` | 2HPico 常规模块：时钟、序列器、调制源、合成器 voice 等 |
| `Sketches/PicoFX` | PicoFX / DSP 效果器：Delay、Reverb、Chorus、Granular 等 |
| `Sketches/Test` | `Calibration` 与 `Button` 等测试程序 |
| `Sketches/lib` | 仓库内置库：`2HPicolib`、`STMLIB`、`PLAITS`、`6opFM` |
| `AudioBootloader/audio_bootloader` | RP2350 bootloader 固件，使用 pico-sdk + CMake 构建 |
| `AudioBootloader/tools/audio_bootloader` | 把 Arduino sketch 编译成固定 app slot 镜像，并生成可播放的 `.wav` |

## 硬件与软件前提

### 硬件

- 默认按 RP2350 / Raspberry Pi Pico 2 目标验证
- 适用于 2HP Pico / PicoFX 风格 Eurorack 硬件
- 若某个 sketch 需要把中间插孔切换为 CV 输入、CV 输出或立体声输出，请先检查该 sketch 的README.md 说明

### 软件

- Arduino IDE 2.x，或 `arduino-cli`
- Arduino-Pico core：`rp2040:rp2040`
- `Adafruit NeoPixel`
- DaisySP 类 sketch 需要额外安装 DaisySP: `https://github.com/rheslip/DaisySP_Teensy`

### 仓库内已带的库

`Sketches/lib` 已包含以下库：

- `2HPicolib`
- `STMLIB`
- `PLAITS`
- `6opFM`

如果你使用 Arduino IDE，建议把 `Sketches/lib` 里的这些目录复制或软链接到你的 sketchbook `libraries/` 目录。  
如果你使用 `arduino-cli`，可以直接在编译命令里加上 `--libraries Sketches/lib`。

## 快速开始

### 1. 安装 core 和基础依赖

```bash
arduino-cli core install rp2040:rp2040
arduino-cli lib install "Adafruit NeoPixel"
```

`I2S` 随 Arduino-Pico core 提供，不需要单独安装。  
`DaisySP` 不在本仓库内，需要你自己安装。

### 2. 选择板卡

推荐直接选择：

```text
Raspberry Pi Pico 2
```

对应 `arduino-cli` 的 FQBN：

```text
rp2040:rp2040:rpipico2
```

### 3. 编译示例

下面这些命令已经按当前仓库结构验证过可以编译：

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --libraries Sketches/lib Sketches/Test/Button

arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --libraries Sketches/lib Sketches/Pico/Branches

arduino-cli compile --fqbn rp2040:rp2040:rpipico2:freq=150,opt=Small --libraries Sketches/lib Sketches/PicoFX/DaisySP_Delay

arduino-cli compile --fqbn rp2040:rp2040:rpipico2:freq=250,opt=Small --libraries Sketches/lib Sketches/Pico/Plaits
```

如果你在 Arduino IDE 里编译：

1. 打开对应 sketch 所在目录。
2. 选择 `Raspberry Pi Pico 2`。
3. 按 sketch 说明设置频率，例如 `150 MHz` 或 `250 MHz`。
4. 安装缺失库后直接上传。

## 常见使用约定

- 4 个旋钮通常通过页面切换复用多个参数
- 按钮通常用于切页、切模式或触发特殊功能
- RGB LED 通常表示当前页面、模式或状态
- 顶部 / 中部 / 底部插孔的功能随 sketch 不同而变化

每个 sketch 的文件头部都写了更详细的面板映射，那里是最准确的说明。

## Sketch 一览

### `Sketches/Pico`

| Sketch | 功能 | 备注 |
| --- | --- | --- |
| `16Step_Sequencer` | 16 步 CV / Gate 序列器 | 4 页步进编辑，适合量化旋律序列 |
| `Branches` | Bernoulli gate / toggle 模块 | 灵感来自 Mutable Instruments Branches |
| `DejaVu` | 带“主题回放”概念的随机序列器 | 偏实验性 |
| `Drums` | 4 个 DaisySP 鼓模型 | Analog BD / Synth BD / HiHat / Synth Snare |
| `DualClock` | 双时钟发生器 / 分频器 | 支持外部时钟、tap tempo、swing、random |
| `Grids_Sampler` | Grids 风格鼓序列器 + 采样播放 | 4 通道 |
| `Modal` | 物理建模 modal voice | 对主频要求较高 |
| `Modulation` | LFO + ADSR 调制源 | 同一 sketch 双模式 |
| `Moogvoice` | 三振荡器合成器 voice | 含 Moog 风格低通、ADSR、LFO |
| `Motion_Recorder` | 双通道 CV 录制 / 回放 | 最多 16 步 |
| `Plaits` | 精简版 Mutable Instruments Plaits | 建议 250 MHz |
| `PlaitsFM` | 6opFM / DX7 patch bank 版 voice | 8 个 bank，每 bank 32 个 patch |
| `TripleOSC` | 三振荡器基础 voice | 适合做校准和基础音色实验 |
| `Turing_Machine(WIP)` | Turing Machine 风格随机步进序列器 | 仍在开发中 |

### `Sketches/PicoFX`

| Sketch | 功能 | 备注 |
| --- | --- | --- |
| `DaisySP_Chorus` | Chorus | 负载较轻 |
| `DaisySP_Delay` | Delay / Ping-Pong Delay | 负载较轻 |
| `DaisySP_Flanger` | Flanger | 需要较高主频 |
| `DaisySP_Pitchshifter` | Pitch Shifter | 负载较轻 |
| `DaisySP_Reverb` | Reverb | 内存占用高 |
| `Granular` | Granular 效果器 | 计算量较大 |
| `Ladderfilter` | Moog Ladder Filter | 可把中间插孔作为 cutoff CV |

### `Sketches/Test`

| Sketch | 功能 | 备注 |
| --- | --- | --- |
| `Calibration` | DAC 校准 helper | 循环输出 `0V / +1V / +2V / +4V / -1V` |
| `Button` | 按钮 + RGB LED 自检 | 适合上电后快速确认面板交互是否正常 |

## 频率与性能建议

不同 sketch 的采样率和负载差异很大。源码注释里已经写了各自预期，推荐先按下列经验值配置：

| 建议频率 | 典型 sketch |
| --- | --- |
| `150 MHz` | `Branches`、`Button`、`Calibration`、`DaisySP_Chorus`、`DaisySP_Delay`、`DaisySP_Pitchshifter`、`DaisySP_Reverb`、`Ladderfilter` |
| `200-250 MHz` | `Modal`、`Moogvoice` |
| `250 MHz` | `Plaits`、`PlaitsFM`、`DaisySP_Flanger`、`Granular` |

如果遇到爆音、卡顿或参数切换不稳定，优先检查：

- 板卡频率是否和 sketch 注释一致
- DaisySP / STMLIB / PLAITS 的版本是否匹配
- 中间插孔的跳线模式是否正确

## 校准建议

这个仓库里的 `V/Oct` 和 CV 标定常量并不是全局统一放在一个文件里，很多 sketch 都在各自源码中定义了类似：

- `CVOUT_VOLT`
- `CVIN_VOLT`
- `CV_VOLT`

建议流程：

1. 先烧录 `Sketches/Test/Calibration`。
2. 用万用表确认中间 / 底部输出是否准确落在 `0V / +1V / +2V / +4V / -1V`。
3. 再回到目标 sketch 中，按需微调对应的 `CVOUT_VOLT` 或 `CVIN_VOLT` 常量。

对 `Plaits`、`PlaitsFM`、`Modal`、`TripleOSC`、`Moogvoice` 这类依赖音高跟踪的 sketch，校准尤其重要。

## Audio Bootloader

`AudioBootloader` 目录提供了一套 RP2350 音频 bootloader。它允许你先把 bootloader 烧到模块上，之后只通过一根 3.5mm 音频线，就把新的 sketch 以 `.wav` 的形式写进 flash。

### 构建 bootloader

下面的 CMake 流程已验证可用：

```bash
cmake -S AudioBootloader/audio_bootloader -B build/audio_bootloader
cmake --build build/audio_bootloader -j4
```

生成物位于：

- `build/audio_bootloader/audio_bootloader.uf2`
- `build/audio_bootloader/audio_bootloader.bin`

### 生成可播放的升级 WAV

当前仓库里的 `make_audio_wav.py` 已兼容这个统一目录结构，可以直接对 `Sketches` 下的 `.ino` 使用：

```bash
python3 AudioBootloader/tools/audio_bootloader/make_audio_wav.py \
  Sketches/Test/Button/Button.ino \
  --output build/Button_boot.wav
```

如果目标 sketch 需要更高主频，可以显式写入 CPU 频率元数据：

```bash
python3 AudioBootloader/tools/audio_bootloader/make_audio_wav.py \
  Sketches/Pico/Plaits/Plaits.ino \
  --cpu-hz 250000000 \
  --output build/Plaits_boot.wav
```

这个工具会：

- 把 sketch 重新链接到固定 app slot
- 视情况使用 zlib 压缩镜像
- 把 CPU 频率写进镜像头
- 生成可通过音频口播放的 FSK `.wav`

### 在模块上使用 bootloader

1. 按住 `BOOTSEL`，通过 USB 把 `audio_bootloader.uf2` 拖到 `RPI-RP2` / `RP2350` 盘符。
2. 把模块装回 Eurorack 后，上电时按住前面板按钮进入 bootloader。
3. 用 3.5mm 线把播放设备接到顶部插孔。
4. 播放生成好的 `.wav`。
5. 用 `Pot1` 当输入增益修整，尽量让 LED 主要停在绿色，避免长时间红/橙色削波。
6. 成功后 LED 会绿色闪烁 3 次并跳转到新应用；失败时按按钮复位后重试。

### 开发者备注

- Bootloader 占用 flash 起始 `0x10000000 - 0x1001FFFF`
- 应用 slot 从 `0x10020000` 开始
- bootloader 会把上一次成功写入的 `cpu_hz` 保存到 metadata，并在下次冷启动时恢复

## 参考项目

- 原始 2HPico sketch 集合：<https://github.com/rheslip/2HPico-Sketches>
- 原始 2HPico DSP sketch 集合：<https://github.com/rheslip/2HPico-DSP-Sketches>
- 2HPico Eurorack 硬件：<https://github.com/rheslip/2HPico-Eurorack-Module-Hardware>
- Arduino 上传教程视频：<https://www.bilibili.com/video/BV1EZcJzuEpn/>

## License

根目录采用 MIT License。  
另外，仓库中还包含来自 Rich Heslip、Mutable Instruments、STMLIB、PLAITS、6opFM、DaisySP 等项目的代码或移植版本，请同时参考各子目录和源文件头部的许可证说明。
