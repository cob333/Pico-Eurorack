// Copyright 2025 Rich Heslip
//
// Author: Rich Heslip
// Contributor: SYNSO, Performance Optimization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
/*
Combined Drum Module - Optimized Version
优化版本：查找表预计算、LED限频、音频路径查表

Top Jack - trigger input
Middle Jack - Decay CV input
Bottom Jack - output

Top pot - Level
Second pot - Tone
Third pot - Decay
Fourth pot - Frequency

Button: switch model
RED: 808 Bass
ORANGE: 909 Bass  
YELLOW: 808 Snare
GREEN: 909 Snare
TIFFANY: Hihat
*/

#include <2HPico.h>
#include <EEPROM.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <stdlib.h>
#include "pico/multicore.h"

// ==========================================
// 配置与优化开关
// ==========================================
//#define DEBUG         // 注释掉以移除调试代码
//#define MONITOR_CPU1  // 定义以启用CPU1监控（占用GPIO，建议关闭以提升性能）

// 采样率选择：11025节省CPU，22050平衡，44100高质量
//#define SAMPLERATE 22050
#define SAMPLERATE 32000
//#define SAMPLERATE 44100
// LED刷新率限制（毫秒）- 30fps约33ms，60fps约16ms
#define LED_REFRESH_MS 33

// 查找表大小（256提供足够精度，占用1KB RAM每表）
#define LUT_SIZE 256

// ADC范围（根据analogReadResolution设置，通常12bit=4096）
#ifndef AD_RANGE
#define AD_RANGE 4096
#endif

#define DECAY_CV_GAIN 2.0f
#define DECAY_CV_DEADZONE 20
#define TRIG_DEBOUNCE 20  // 假设值，根据2HPico.h实际值调整
#define LONG_PRESS_SAVE_MS 3000
#define EEPROM_BYTES 256
#define DRUMS_STORE_MAGIC 0x3244524Du // "2DRM"
#define DRUMS_STORE_VERSION 1u
#define SAVE_FLASH_COUNT 3
#define SAVE_FLASH_ON_MS 120
#define SAVE_FLASH_OFF_MS 120

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

#include "daisysp.h"
#include "drums/analogbassdrum.cpp"
#include "drums/synthbassdrum.cpp"
#include "drums/synthsnaredrum.cpp"
#include "drums/hihat.cpp"
#include "Synthesis/oscillator.h"
#include "filters/svf.cpp"

#include "DrumModels.h"

float samplerate = SAMPLERATE;

// ==========================================
// 查找表（LUT）声明 - 用于消除运行时浮点运算
// ==========================================
float freq_lut[LUT_SIZE];        // 频率查找表
float decay_909_lut[LUT_SIZE];   // 909衰减参数查找表
float decay_time_lut[LUT_SIZE];  // 衰减时间查找表（秒）
// 在 CMakeLists.txt 中指定段
uint32_t __attribute__((section(".scratch_x"))) lut_freq[LUT_SIZE];

// ==========================================
// 全局对象
// ==========================================
BassDrum808 bassDrum808;
BassDrum909 bassDrum909;
SnareDrum808 snareDrum808;
SnareDrum909 snareDrum909;
Hihat hihat;

enum Model { 
  MODEL_BassDrum808,
  MODEL_BassDrum909,
  MODEL_SnareDrum808,
  MODEL_SnareDrum909,
  MODEL_Hihat
};

volatile Model current_model = MODEL_BassDrum808;

// UI状态变量
volatile float output_level = 1.0f;
float param_tone = 0.5f;
float ui_decay = 0.5f;
float ui_freq = 0.5f;
uint16_t cv2_zero = 0;

bool trigger = 0;
bool button = 0;
bool button_long_handled = 0;
uint32_t buttontimer, trigtimer, parameterupdate;
uint32_t buttonpress = 0;

struct DrumStore {
  uint32_t magic;
  uint16_t version;
  uint8_t model;
  uint8_t reserved0;
  float output_level;
  float param_tone;
  float ui_decay;
  float ui_freq;
  uint32_t checksum;
};

// ==========================================
// 辅助函数（内联强制编译器优化）
// 注意：删除自定义mapf，使用库中的mapf或直接用除法
// ==========================================
static inline float clamp01(float x) {
  return (x < 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline uint16_t norm_to_adc(float value) {
  value = clamp01(value);
  return (uint16_t)(value * (float)(AD_RANGE - 1));
}

// 快速ADC到浮点转换（0.0-1.0）
static inline float adc_to_norm(uint16_t adc_val) {
  return (float)adc_val / (float)AD_RANGE;
}

// 快速ADC到浮点转换（0.0-1.0，上限AD_RANGE-1防止越界）
static inline float adc_to_norm_safe(uint16_t adc_val) {
  if (adc_val >= AD_RANGE) adc_val = AD_RANGE - 1;
  return (float)adc_val / (float)(AD_RANGE - 1);
}

static inline uint16_t calibrate_decay_cv_zero() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; ++i) {
    sum += sampleCV2();
  }
  return (uint16_t)(sum / 16);
}

static inline float read_decay_cv_amount() {
  int32_t cv_delta = (int32_t)cv2_zero - (int32_t)sampleCV2();
  if (cv_delta < DECAY_CV_DEADZONE) return 0.0f;
  if (cv_delta > AD_RANGE) cv_delta = AD_RANGE;
  return clamp01((float)(cv_delta - DECAY_CV_DEADZONE) / (float)(AD_RANGE - DECAY_CV_DEADZONE));
}

// 传统计算函数（仅用于LUT预计算）
static inline float knob_to_decay_time_calc(float knob) {
  const float DECAY_MIN_SEC = 0.005f;
  const float DECAY_MAX_SEC = 2.0f;
  knob = clamp01(knob);
  return DECAY_MIN_SEC + knob * (DECAY_MAX_SEC - DECAY_MIN_SEC);
}

static inline float knob_to_freq_hz_calc(float knob) {
  const float FREQ_MIN_HZ = 10.0f;
  const float FREQ_MAX_HZ = 120.0f;
  knob = clamp01(knob);
  const float ratio = FREQ_MAX_HZ / FREQ_MIN_HZ;
  return FREQ_MIN_HZ * powf(ratio, knob);
}

static inline float decay_time_to_909_param_calc(float t, float sr) {
  if (t <= 0.0f || sr <= 0.0f) return 0.0f;
  const float exp_term = expf(-1.0f / (t * sr));
  const float a = (0.02f * sr) * (1.0f - exp_term);
  if (a <= 0.0f) return 0.0f;
  float decay_internal = -0.2f * (logf(a) / logf(2.0f));
  decay_internal = clamp01(decay_internal);
  return sqrtf(decay_internal);
}

static uint32_t model_color(Model model) {
  switch (model) {
    case MODEL_BassDrum808: return RED;
    case MODEL_BassDrum909: return ORANGE;
    case MODEL_SnareDrum808: return YELLOW;
    case MODEL_SnareDrum909: return GREEN;
    case MODEL_Hihat: return TIFFANY;
    default: return RED;
  }
}

static void set_led_color(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static void set_led_for_model(Model model) {
  set_led_color(model_color(model));
}

static uint32_t drum_store_checksum(const DrumStore &data) {
  uint32_t hash = 2166136261u;
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);
  for (uint32_t i = 0; i < (uint32_t)(sizeof(DrumStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static void copy_state_to_store(DrumStore &data) {
  memset(&data, 0, sizeof(data));
  data.magic = DRUMS_STORE_MAGIC;
  data.version = DRUMS_STORE_VERSION;
  data.model = (uint8_t)current_model;
  data.output_level = clamp01(output_level);
  data.param_tone = clamp01(param_tone);
  data.ui_decay = clamp01(ui_decay);
  data.ui_freq = clamp01(ui_freq);
}

static bool validate_store(const DrumStore &data) {
  if (data.magic != DRUMS_STORE_MAGIC) return 0;
  if (data.version != DRUMS_STORE_VERSION) return 0;
  if (data.model > (uint8_t)MODEL_Hihat) return 0;
  if (data.output_level < 0.0f || data.output_level > 1.0f) return 0;
  if (data.param_tone < 0.0f || data.param_tone > 1.0f) return 0;
  if (data.ui_decay < 0.0f || data.ui_decay > 1.0f) return 0;
  if (data.ui_freq < 0.0f || data.ui_freq > 1.0f) return 0;
  return data.checksum == drum_store_checksum(data);
}

static bool load_state_from_flash() {
  DrumStore data;
  EEPROM.get(0, data);
  if (!validate_store(data)) return 0;

  current_model = (Model)data.model;
  output_level = data.output_level;
  param_tone = data.param_tone;
  ui_decay = data.ui_decay;
  ui_freq = data.ui_freq;

  pot[0] = norm_to_adc(output_level);
  pot[1] = norm_to_adc(param_tone);
  pot[2] = norm_to_adc(ui_decay);
  pot[3] = norm_to_adc(ui_freq);
  return 1;
}

static bool save_state_to_flash() {
  DrumStore data;
  copy_state_to_store(data);
  data.checksum = drum_store_checksum(data);
  EEPROM.put(0, data);
  return EEPROM.commit();
}

static void flash_save_success() {
  for (int i = 0; i < SAVE_FLASH_COUNT; ++i) {
    set_led_color(GREEN);
    delay(SAVE_FLASH_ON_MS);
    set_led_color(0);
    delay(SAVE_FLASH_OFF_MS);
  }
  set_led_for_model((Model)current_model);
}

// ==========================================
// 查找表初始化（setup中调用一次）
// ==========================================
void init_lookup_tables() {
  for(int i=0; i<LUT_SIZE; i++) {
    float knob = i / (float)(LUT_SIZE - 1);
    
    freq_lut[i] = knob_to_freq_hz_calc(knob);
    float decay_sec = knob_to_decay_time_calc(knob);
    decay_time_lut[i] = decay_sec;
    decay_909_lut[i] = decay_time_to_909_param_calc(decay_sec, samplerate);
  }
}

// 快速LUT访问（内联）
static inline float fast_freq(uint16_t adc_val) {
  uint32_t idx = ((uint32_t)adc_val * (LUT_SIZE - 1)) / AD_RANGE;
  if(idx >= LUT_SIZE) idx = LUT_SIZE - 1;
  return freq_lut[idx];
}

static inline float fast_decay_909(uint16_t adc_val) {
  uint32_t idx = ((uint32_t)adc_val * (LUT_SIZE - 1)) / AD_RANGE;
  if(idx >= LUT_SIZE) idx = LUT_SIZE - 1;
  return decay_909_lut[idx];
}

static inline float fast_decay_time(uint16_t adc_val) {
  uint32_t idx = ((uint32_t)adc_val * (LUT_SIZE - 1)) / AD_RANGE;
  if(idx >= LUT_SIZE) idx = LUT_SIZE - 1;
  return decay_time_lut[idx];
}

// ==========================================
// 音频处理函数指针表优化（Core 1）
// ==========================================
typedef float (*DrumProcessFunc)(void);

static inline float proc_808(void) { return bassDrum808.Process(); }
static inline float proc_909(void) { return bassDrum909.Process(); }
static inline float proc_snare808(void) { return snareDrum808.Process(); }
static inline float proc_snare909(void) { return snareDrum909.Process(); }
static inline float proc_hihat(void) { return hihat.Process(); }

DrumProcessFunc const DRUM_PROCESS_TABLE[] = {
  proc_808,
  proc_909,
  proc_snare808,
  proc_snare909,
  proc_hihat
};

const float MODEL_GAIN[] = {
  8.0f,
  1.0f,
  0.5f,
  0.5f,
  1.0f
};

#define NUM_DRUM_MODELS 5

// ==========================================
// Setup
// ==========================================
void setup() {


  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  set_led_for_model((Model)current_model);

  analogReadResolution(AD_BITS);
  cv2_zero = calibrate_decay_cv_zero();
  samplepots();
  EEPROM.begin(EEPROM_BYTES);
  bool loaded = load_state_from_flash();
  (void)loaded;
  output_level = adc_to_norm_safe(pot[0]);

  init_lookup_tables();

  bassDrum808.Init(samplerate);
  bassDrum909.Init(samplerate);
  snareDrum909.Init(samplerate);
  snareDrum808.Init(samplerate);
  hihat.Init(samplerate);

  const float init_freq = fast_freq(AD_RANGE/2);
  const float init_decay_time = fast_decay_time(AD_RANGE/4);
  const float init_decay_909 = fast_decay_909(AD_RANGE/4);

  bassDrum808.SetTone(param_tone / 2);
  bassDrum808.SetDecay(init_decay_time);
  bassDrum808.SetFreq(init_freq * 2);
  bassDrum808.SetAttackFmAmount(0.5f);
  bassDrum808.SetSelfFmAmount(1.0f);
  bassDrum808.SetSustain(0.0f);

  bassDrum909.SetDirtiness(0.2f);
  bassDrum909.SetFmEnvelopeAmount(0.5f);
  bassDrum909.SetFmEnvelopeDecay(0.3f);
  bassDrum909.SetFreq(init_freq);
  bassDrum909.SetDecay(init_decay_909);
  bassDrum909.SetTone(param_tone);

  snareDrum909.SetFmAmount(0.5f);
  snareDrum909.SetSnappy(0.7f);
  snareDrum909.SetFreq(init_freq);
  snareDrum909.SetDecay(init_decay_909);
  snareDrum909.SetSustain(0);

  snareDrum808.SetTone(0.5f);
  snareDrum808.SetSnappy(0.7f);
  snareDrum808.SetFreq(init_freq);
  snareDrum808.SetDecay(init_decay_909);
  snareDrum808.SetSustain(0);

  hihat.SetTone(0.5f);
  hihat.SetNoisiness(0.5f);
  hihat.SetFreq(init_freq*4);
  hihat.SetDecay(init_decay_909);
  hihat.SetSustain(0.0f);

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);

  set_led_for_model((Model)current_model);
  lockpots();

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}

// ==========================================
// Main Loop (Core 0) - UI与逻辑处理
// ==========================================
void loop() {
  // 按钮处理
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      buttonpress = millis();
      button_long_handled = 0;
    } else if (button && !button_long_handled
               && ((millis() - buttonpress) >= LONG_PRESS_SAVE_MS)) {
      button_long_handled = 1;
      if (save_state_to_flash()) {
        flash_save_success();
      } else {
        set_led_for_model((Model)current_model);
      }
    }
  } else {
    if (button && !button_long_handled) {
      switch (current_model) {
        case MODEL_BassDrum808: current_model = MODEL_BassDrum909; break;
        case MODEL_BassDrum909: current_model = MODEL_SnareDrum808; break;
        case MODEL_SnareDrum808: current_model = MODEL_SnareDrum909; break;
        case MODEL_SnareDrum909: current_model = MODEL_Hihat; break;
        default: current_model = MODEL_BassDrum808; break;
      }
      lockpots();
    }
    buttontimer = millis();
    button = 0;
    button_long_handled = 0;
  }

  // 参数更新（每10ms）
  if ((millis() - parameterupdate) > 10) {
    parameterupdate = millis();
    samplepots();

    // 使用直接除法替代mapf，避免函数重载歧义
    output_level = adc_to_norm_safe(pot[0]);
    if (!potlock[1]) param_tone = adc_to_norm(pot[1]);
    if (!potlock[2]) ui_decay = adc_to_norm(pot[2]);
    if (!potlock[3]) ui_freq = adc_to_norm_safe(pot[3]);

    uint16_t freq_adc = pot[3];
    
    float freq_hz = fast_freq(freq_adc);
    float decay_cv = read_decay_cv_amount();
    float decay_knob = clamp01(ui_decay + (decay_cv * DECAY_CV_GAIN));
    float decay_sec = knob_to_decay_time_calc(decay_knob);
    float decay_909 = decay_time_to_909_param_calc(decay_sec, samplerate);
 
    switch(current_model) {
      case MODEL_BassDrum808:
        bassDrum808.SetTone(param_tone / 4);
        bassDrum808.SetDecay(decay_sec);
        bassDrum808.SetFreq(freq_hz * 2);
        break;
        
      case MODEL_BassDrum909:
        bassDrum909.SetFreq(freq_hz * 2);
        bassDrum909.SetDecay(decay_909);
        bassDrum909.SetTone(param_tone / 2);
        bassDrum909.SetFmEnvelopeAmount(param_tone);
        bassDrum909.SetDirtiness(param_tone / 4);
        break;
        
      case MODEL_SnareDrum808:
        snareDrum808.SetDecay(decay_sec / 4);
        snareDrum808.SetFreq(freq_hz * 3);
        snareDrum808.SetTone(ui_freq/2);
        snareDrum808.SetSnappy(param_tone);
        break;
        
      case MODEL_SnareDrum909:
        snareDrum909.SetDecay(decay_sec / 2);
        snareDrum909.SetFreq(freq_hz * 4);
        snareDrum909.SetFmAmount(param_tone);
        snareDrum909.SetSnappy(param_tone);
        break;
        
      case MODEL_Hihat:
        hihat.SetDecay(decay_sec / 2);
        hihat.SetFreq(freq_hz);
        hihat.SetTone(ui_freq/3+0.5);
        hihat.SetNoisiness(param_tone);
        break;
    }
  }

  // 触发处理
  if (!digitalRead(TRIGGER)) {
    if (((micros() - trigtimer) > TRIG_DEBOUNCE) && !trigger) {
      trigger = 1;
      switch(current_model) {
        case MODEL_BassDrum808: bassDrum808.Trig(); break;
        case MODEL_BassDrum909: bassDrum909.Trig(); break;
        case MODEL_SnareDrum808: snareDrum808.Trig(); break;
        case MODEL_SnareDrum909: snareDrum909.Trig(); break;
        case MODEL_Hihat: hihat.Trig(); break;
      }
    }
  } else {
    trigtimer = micros();
    trigger = 0;
  }

  // LED刷新率优化
  static uint32_t last_led_update = 0;
  static Model last_model = MODEL_BassDrum808;
  uint32_t now = millis();
  
  if (now - last_led_update >= LED_REFRESH_MS) {
    last_led_update = now;
    
    if (current_model != last_model) {
      last_model = current_model;
      
      uint32_t color;
      color = model_color(current_model);
      set_led_color(color);
    }
  }
}

// ==========================================
// Core 1 Setup
// ==========================================
void setup1() {
  delay(1000);
}

// ==========================================
// Core 1 Audio Loop - 极致优化版本
// ==========================================
void loop1() {
  float sig;        // 移除register关键字(C++17兼容)
  int32_t outsample;
  
  uint8_t model_idx = (uint8_t)current_model;
  if (model_idx >= NUM_DRUM_MODELS) model_idx = 0;

  sig = DRUM_PROCESS_TABLE[model_idx]();
  sig *= MODEL_GAIN[model_idx];
  sig *= output_level;
  
  if (sig > 1.0f) sig = 1.0f;
  else if (sig < -1.0f) sig = -1.0f;
  
  outsample = (int32_t)(sig * 32767.0f);
  int16_t out = (int16_t)outsample;
  
  DAC.write(out);
  DAC.write(out);

}
