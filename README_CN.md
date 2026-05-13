# Pico-Eurorack

中文 | [English](README.md)

![Pico_Icon](./images/Pico_Icon.jpg)

这是一个面向 `2HPico` 和 `2HPico DSP` Eurorack 模块的脚本仓库，源码位于 `Sketches/`。
该仓库带有一个用户友好的 Web 客户端，可以浏览模块说明、编辑采样、选择最多 6 个功能加入 Bootloader 槽位，并生成可直接拖拽烧录的 `.uf2` 固件。

这是 2HPico 与 2HPico DSP 的一套二次开发脚本集合，原始仓库见：
https://github.com/rheslip/2HPico-DSP-Sketches;
https://github.com/rheslip/2HPico-Sketches;

这个仓库有什么区别？
用户友好的客户端;
更多脚本；
更多颜色选项；
为 Pico 与 PicoFX sketch 提供统一的文件结构；

## Web 客户端

客户端位于 `Client/`，后端会调用 `Bootloader/Tools/pico_boot_apps.py` 编译选中的 sketch，并把 Bootloader selector 与多个 app slot 打包成一个 `.uf2` 文件。

### 启动

在仓库根目录运行：

```sh
python3 Client/server.py
```

启动后打开：

```text
http://127.0.0.1:8765/
```

如果 `8765` 被占用，后端会自动尝试后续端口，终端中会打印实际访问地址。

### 使用流程

1. 在右上角选择 `Pico` 或 `PicoFX`。
2. 在 `App Catalog` 中浏览功能；每个功能卡片会显示当前固件容量估算。
3. 将功能拖入上方 Slots，或从 Slots 中删除不需要的功能。
4. 容量条会显示 `已占用 / 3.5MiB`。超过上限时 `Generate` 会变灰且不可点击；容量允许时按钮为绿色。
5. 点击 `Generate` 后，页面会显示编译进度条；点击右侧叉号可取消编译。
6. 编译完成后浏览器会立即下载生成的 `.uf2`。

### 采样功能

`GridsSampler` 与 `OneshotSampler` 带有采样编辑入口：

- `GridsSampler`：上传的 WAV 会加入当前采样库，不做 Bank 区分，后端读取 `Samples/Samples.h` 或 `Samples/samples.h` 中的库文件。
- `OneshotSampler`：支持 Bank 列表、删除 Bank。

当前 `GridsSampler` 的默认采样库使用 `OneshotSampler` 中的 `TR-808` 采样；`OneshotSampler` 默认保留 `TR-606`、`TR-808`、`TR-909` Bank。

### 生成与缓存

客户端使用以下目录作为临时构建区：

- `Bootloader/build/client/sizes/`：容量估算缓存，只保留各 app 的 `.ino.uf2`。
- `Bootloader/build/client/slotN-AppName/`：Generate 过程中的临时编译目录，生成结束后会自动删除。
- `Bootloader/build/client/output/`：最终 `.uf2` 的临时下载目录，浏览器下载后会自动删除。
- `Bootloader/build/client/sample-defaults/`：后端默认采样快照，用于恢复 `GridsSampler` 和 `OneshotSampler` 的默认采样。

首页加载只读取已有容量缓存，不会自动编译缺失容量；如果某个功能显示 `Build`，可以在采样面板点击 Upload 触发容量测算，或在 Generate 时由后端按需测算。

### 环境要求

生成固件需要本机安装并配置：

- `python3`
- `arduino-cli`
- Arduino-Pico core，例如 `rp2040:rp2040`
- Pico SDK / CMake，用于构建 Bootloader selector
- 本仓库的 `Sketches/lib` 与 Arduino 库目录

如果 selector UF2 不存在，后端会尝试从 `Bootloader/Selector` 自动构建；

## Pico

![Pico](./images/Pico.jpg)

1. 16Step_Sequencer: 一个 16 步 CV/Gate 音序器，支持单步重复、音阶量化、时钟分频、序列长度和整体音高控制；
2. Branches: 一个受 "Mutable Instruments Branches" 启发的伯努利门，将一路 trigger 按概率分配到两个互斥输出；
3. Braids: 一个受 "Mutable Instruments Braids" 启发的宏振荡器，提供多个合成模型与包络控制；
4. DRUMS: 一个多模型鼓声源，包含 808 kick、909 kick、808 snare、909 snare 和 hi-hat 引擎；
5. DejaVu: 一个带 "DejaVu" 记忆控制的半随机音序器，捕获并重新回放音符/门序列，在循环与随机之间取变化；
6. DualClock: 一个双路时钟发生器/分频器，支持tap tempo、摇摆、随机时序和联动时钟比率；
7. Grids_Sampler: 一个将 "Mutable Instruments Grids" 节奏生成与四通道采样播放结合起来的采样鼓机；
8. Rings: 一个基于 "Mutable Instruments Rings" 的共振器音源，提供 trigger 与 pitch CV 输入，以及最高4复音的6个共振模型；
9. Modulation: 一个结合 ADSR 与 LFO 的调制源，可在包络模式与多波形可同步 LFO 模式之间切换；
10. Moogvoice: 一个单音减法合成器音源，包含三个振荡器、Moog 风格 ladder filter、ADSR 包络和 LFO 调制；
11. Motion_Recorder: 一个双通道 CV 动作录音器，可循环记录旋钮运动、与外部时钟同步，并支持排队重录；
12. Oneshot_Sampler: 一个 one-shot 采样器，支持 V/Oct 音高控制、包络塑形、音色控制、随机选样和反向播放；
13. Plaits: 一个受 "Mutable Instruments Plaits" 启发的宏振荡器；
14. PlaitsFM: 一个受 "Mutable Instruments Plaits" 启发的 6OP-FM 振荡器；
15. TripleOSC: 一个紧凑的三振荡器音源，提供共享波形选择、独立调谐、FM 输入以及每个振荡器的静音/校准支持；

## PicoFX

![PicoFX](./images/PicoFX.jpg)

1. Chorus: 一个立体声 chorus 效果器，支持延迟、反馈、LFO 速率/深度、干湿比和输出电平控制；
2. Delay: 一个 delay 效果器，支持反馈、干湿比、输出电平，以及自由延迟时间或外部时钟同步加 ping-pong 模式；
3. Flanger: 一个立体声 flanger 效果器，支持延迟、反馈、LFO 深度/速率、立体声宽度和多种调制波形；
4. Pitchshifter: 一个 pitch shifter 效果器，支持调整延迟窗口、移调、随机调制和干湿比；
5. Reverb: 一个立体声 reverb 效果器，支持混响时间、低通阻尼、干湿比和输出电平控制；
6. Granular: 一个颗粒处理器，可将输入音频切成 grains，并调节颗粒大小、密度、音高和干湿比；
7. Ladderfilter: 一个 Moog ladder low-pass filter 效果器，支持共振、基础截止频率、1V/Oct 截止频率 CV 缩放和输出电平控制；
8. Bitcrush: 一个单声道 bitcrusher 效果器，支持 1Bit 到 16Bit 分辨率、1x 到 20x 降采样、CV 控制干湿比以及输出电平；
9. Panner: 一个立体声扩展/自动声像器，可将单声道输入在立体声声场中移动，并调节宽度、速率、LFO 形状和输出电平；
10. Sidechain: 一个 trigger-ducking sidechain 效果器，支持 attack、decay、曲线和输出电平调节；
11. Spectral Smash: 一个频谱撕裂效果器，可抓取当前短窗频谱并继续做 warp、blur、time smear 与干湿比控制，适合冻结后撕裂、拉伸和漂移纹理；
12. BeatBreaker: 一个按外部时钟切片并重组输入音频的 beat slicer，可按概率从最近几拍中抽取片段、倒放，并在单拍内做 2 到 8 次重复触发；

## Test
1. Button: 一个简单的硬件测试程序，每次按下按钮都会切换前面板 RGB LED 的颜色；
2. Calibration: 一个 DAC 校准工具，可在 Pico 的两路 CV 输出上输出固定参考电压；

## Credits
- 感谢 Rich Hesslip 提供原始 2HPico 与 2HPico DSP 库和 sketches；
- 感谢 Mutable Instruments，其原始模块启发了本仓库中的许多 sketches；
- 感谢开源 Arduino 与 TinyUSB 社区提供相关工具；
- 感谢 SYNSO 提供 DRUMS sketch；
- 感谢你查看并支持开源 Eurorack 开发~