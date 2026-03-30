# Pico-Eurorack
![Pico_Icon](./images/Pico_Icon.jpg)

这是一个面向 `2HPico` 和 `2HPico DSP` Eurorack 模块的脚本仓库，源码位于 `Sketches/`，预编译好的 `uf2` 固件位于 `Build/`。

这是 2HPico 与 2HPico DSP 的一套二次开发脚本集合，原始仓库见：
https://github.com/rheslip/2HPico-DSP-Sketches;
https://github.com/rheslip/2HPico-Sketches;

这个仓库有什么区别？
更多脚本；
更多颜色选项；
更易用的校准 sketch；
为 Pico 与 PicoFX sketch 提供统一的文件结构；

## Pico

![Pico](./images/Pico.jpg)

1. 16Step_Sequencer: 一个 16 步 CV/Gate 音序器，支持单步重复、音阶量化、时钟分频、序列长度和整体音高控制；
2. Branches: 一个受 "Mutable Instruments Branches" 启发的伯努利门，将一路 trigger 按概率分配到两个互斥输出；
3. Braids: 一个受 "Mutable Instruments Braids" 启发的宏振荡器，提供多个合成模型与包络控制；
4. DRUMS: 一个多模型鼓声源，包含 808 kick、909 kick、808 snare、909 snare 和 hi-hat 引擎；
5. DejaVu: 一个带 "DejaVu" 记忆控制的半随机音序器，捕获并重新回放音符/门序列，在循环与随机之间取变化；
6. DualClock: 一个双路时钟发生器/分频器，支持tap tempo、摇摆、随机时序和联动时钟比率；
7. Grids_Sampler: 一个将 "Mutable Instruments Grids" 节奏生成与四通道采样播放结合起来的采样鼓机；
8. Modal: 一个受 "Mutable Instruments Rings" 启发的 modal resonator，通过 trigger 和 pitch CV 激励共振结构；
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
8. Panner: 一个立体声扩展/自动声像器，可将单声道输入在立体声声场中移动，并调节宽度、速率、LFO 形状和输出电平；
9. Sidechain: 一个 trigger-ducking sidechain 效果器，支持 attack、decay、曲线和输出电平调节；

## Test
1. Button: 一个简单的硬件测试程序，每次按下按钮都会切换前面板 RGB LED 的颜色；
2. Calibration: 一个 DAC 校准工具，可在 Pico 的两路 CV 输出上输出固定参考电压；
