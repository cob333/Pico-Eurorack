// DaisySP chorus subset packaged for the 2HPico DaisySP_Chorus sketch.
//
// Original DaisySP Chorus by Ben Sergentanis.
// Contributor: Wenhao Yang
//
// This local header keeps DaisySP_Chorus independent from an external DaisySP
// library install while preserving the daisysp::Chorus API used by the sketch.

#pragma once
#ifndef TWOHPICO_DAISYSP_CHORUS_H
#define TWOHPICO_DAISYSP_CHORUS_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

namespace daisysp
{
inline float fclamp(float in, float min, float max)
{
    return fminf(fmaxf(in, min), max);
}

/** Simple Delay line.
November 2019

Converted to Template December 2019

declaration example: (1 second of floats)

DelayLine<float, SAMPLE_RATE> del;

By: shensley
*/
template <typename T, size_t max_size>
class DelayLine
{
  public:
    DelayLine() {}
    ~DelayLine() {}
    /** initializes the delay line by clearing the values within, and setting delay to 1 sample.
    */
    void Init() { Reset(); }
    /** clears buffer, sets write ptr to 0, and delay to 1 sample.
    */
    void Reset()
    {
        for(size_t i = 0; i < max_size; i++)
        {
            line_[i] = T(0);
        }
        write_ptr_ = 0;
        delay_     = 1;
    }

    /** sets the delay time in samples
        If a float is passed in, a fractional component will be calculated for interpolating the delay line.
    */
    inline void SetDelay(size_t delay)
    {
        frac_  = 0.0f;
        delay_ = delay < max_size ? delay : max_size - 1;
    }

    /** sets the delay time in samples
        If a float is passed in, a fractional component will be calculated for interpolating the delay line.
    */
    inline void SetDelay(float delay)
    {
        int32_t int_delay = static_cast<int32_t>(delay);
        frac_             = delay - static_cast<float>(int_delay);
        delay_ = static_cast<size_t>(int_delay) < max_size ? int_delay
                                                           : max_size - 1;
    }

    /** writes the sample of type T to the delay line, and advances the write ptr
    */
    inline void Write(const T sample)
    {
        line_[write_ptr_] = sample;
        write_ptr_        = (write_ptr_ - 1 + max_size) % max_size;
    }

    /** returns the next sample of type T in the delay line, interpolated if necessary.
    */
    inline const T Read() const
    {
        T a = line_[(write_ptr_ + delay_) % max_size];
        T b = line_[(write_ptr_ + delay_ + 1) % max_size];
        return a + (b - a) * frac_;
    }

    /** Read from a set location */
    inline const T Read(float delay) const
    {
        int32_t delay_integral   = static_cast<int32_t>(delay);
        float   delay_fractional = delay - static_cast<float>(delay_integral);
        const T a = line_[(write_ptr_ + delay_integral) % max_size];
        const T b = line_[(write_ptr_ + delay_integral + 1) % max_size];
        return a + (b - a) * delay_fractional;
    }

    inline const T ReadHermite(float delay) const
    {
        int32_t delay_integral   = static_cast<int32_t>(delay);
        float   delay_fractional = delay - static_cast<float>(delay_integral);

        int32_t     t     = (write_ptr_ + delay_integral + max_size);
        const T     xm1   = line_[(t - 1) % max_size];
        const T     x0    = line_[(t) % max_size];
        const T     x1    = line_[(t + 1) % max_size];
        const T     x2    = line_[(t + 2) % max_size];
        const float c     = (x1 - xm1) * 0.5f;
        const float v     = x0 - x1;
        const float w     = c + v;
        const float a     = w + v + (x2 - x0) * 0.5f;
        const float b_neg = w + a;
        const float f     = delay_fractional;
        return (((a * f) - b_neg) * f + c) * f + x0;
    }

    inline const T Allpass(const T sample, size_t delay, const T coefficient)
    {
        T read  = line_[(write_ptr_ + delay) % max_size];
        T write = sample + coefficient * read;
        Write(write);
        return -write * coefficient + read;
    }

  private:
    float  frac_;
    size_t write_ptr_;
    size_t delay_;
    T      line_[max_size];
};

/**
    @brief Single Chorus engine. Used in Chorus.
    @author Ben Sergentanis
*/
class ChorusEngine
{
  public:
    ChorusEngine() {}
    ~ChorusEngine() {}

    /** Initialize the module
        \param sample_rate Audio engine sample rate.
    */
    void Init(float sample_rate);

    /** Get the next sample
        \param in Sample to process
    */
    float Process(float in);

    /** How much to modulate the delay by.
        \param depth Works 0-1.
    */
    void SetLfoDepth(float depth);

    /** Set lfo frequency.
        \param freq Frequency in Hz
    */
    void SetLfoFreq(float freq);

    /** Set the internal delay rate.
        \param delay Tuned for 0-1. Maps to .1 to 50 ms.
    */
    void SetDelay(float delay);

    /** Set the delay time in ms.
        \param ms Delay time in ms.
    */
    void SetDelayMs(float ms);

    /** Set the feedback amount.
        \param feedback Amount from 0-1.
    */
    void SetFeedback(float feedback);

  private:
    float                    sample_rate_;
    static constexpr int32_t kDelayLength
        = 2400; // 50 ms at 48kHz = .05 * 48000

    //triangle lfos
    float lfo_phase_;
    float lfo_freq_;
    float lfo_amp_;

    float feedback_;

    float delay_;

    DelayLine<float, kDelayLength> del_;

    float ProcessLfo();
};

//wraps up all of the chorus engines
/**
    @brief Chorus Effect.
    @author Ben Sergentanis
    @date Jan 2021
    Based on https://www.izotope.com/en/learn/understanding-chorus-flangers-and-phasers-in-audio-production.html \n
    and https://www.researchgate.net/publication/236629475_Implementing_Professional_Audio_Effects_with_DSPs \n
*/
class Chorus
{
  public:
    Chorus() {}
    ~Chorus() {}

    /** Initialize the module
        \param sample_rate Audio engine sample rate
    */
    void Init(float sample_rate);

    /** Get the net floating point sample. Defaults to left channel.
        \param in Sample to process
    */
    float Process(float in);

    /** Get the left channel's last sample */
    float GetLeft();

    /** Get the right channel's last sample */
    float GetRight();

    /** Pan both channels individually.
        \param panl Pan the left channel. 0 is left, 1 is right.
        \param panr Pan the right channel.
    */
    void SetPan(float panl, float panr);

    /** Pan both channels.
        \param pan Where to pan both channels to. 0 is left, 1 is right.
    */
    void SetPan(float pan);

    /** Set both lfo depths individually.
        \param depthl Left channel lfo depth. Works 0-1.
        \param depthr Right channel lfo depth.
    */
    void SetLfoDepth(float depthl, float depthr);

    /** Set both lfo depths.
        \param depth Both channels lfo depth. Works 0-1.
    */
    void SetLfoDepth(float depth);

    /** Set both lfo frequencies individually.
        \param depthl Left channel lfo freq in Hz.
        \param depthr Right channel lfo freq in Hz.
    */
    void SetLfoFreq(float freql, float freqr);

    /** Set both lfo frequencies.
        \param depth Both channel lfo freqs in Hz.
    */
    void SetLfoFreq(float freq);

    /** Set both channel delay amounts individually.
        \param delayl Left channel delay amount. Works 0-1.
        \param delayr Right channel delay amount.
    */
    void SetDelay(float delayl, float delayr);

    /** Set both channel delay amounts.
        \param delay Both channel delay amount. Works 0-1.
    */
    void SetDelay(float delay);

    /** Set both channel delay individually.
        \param msl Left channel delay in ms.
        \param msr Right channel delay in ms.
    */
    void SetDelayMs(float msl, float msr);

    /** Set both channel delay in ms.
        \param ms Both channel delay amounts in ms.
    */
    void SetDelayMs(float ms);

    /** Set both channels feedback individually.
        \param feedbackl Left channel feedback. Works 0-1.
        \param feedbackr Right channel feedback.
    */
    void SetFeedback(float feedbackl, float feedbackr);

    /** Set both channels feedback.
        \param feedback Both channel feedback. Works 0-1.
    */
    void SetFeedback(float feedback);

  private:
    ChorusEngine engines_[2];
    float        gain_frac_;
    float        pan_[2];

    float sigl_, sigr_;
};

//ChorusEngine stuff
void ChorusEngine::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    del_.Init();
    lfo_amp_  = 0.f;
    feedback_ = .2f;
    SetDelay(.75);

    lfo_phase_ = 0.f;
    SetLfoFreq(.3f);
    SetLfoDepth(.9f);
}

float ChorusEngine::Process(float in)
{
    float lfo_sig = ProcessLfo();
    del_.SetDelay(lfo_sig + delay_);

    float out = del_.Read();
    del_.Write(in + out * feedback_);

    return (in + out) * .5f; //equal mix
}

void ChorusEngine::SetLfoDepth(float depth)
{
    depth    = fclamp(depth, 0.f, .93f);
    lfo_amp_ = depth * delay_;
}

void ChorusEngine::SetLfoFreq(float freq)
{
    freq = 4.f * freq / sample_rate_;
    freq *= lfo_freq_ < 0.f ? -1.f : 1.f;  //if we're headed down, keep going
    lfo_freq_ = fclamp(freq, -.25f, .25f); //clip at +/- .125 * sr
}

void ChorusEngine::SetDelay(float delay)
{
    delay = (.1f + delay * 7.9f); //.1 to 8 ms
    SetDelayMs(delay);
}

void ChorusEngine::SetDelayMs(float ms)
{
    ms     = fmax(.1f, ms);
    delay_ = ms * .001f * sample_rate_; //ms to samples

    lfo_amp_ = fmin(lfo_amp_, delay_); //clip this if needed
}

void ChorusEngine::SetFeedback(float feedback)
{
    feedback_ = fclamp(feedback, 0.f, 1.f);
}

float ChorusEngine::ProcessLfo()
{
    lfo_phase_ += lfo_freq_;

    //wrap around and flip direction
    if(lfo_phase_ > 1.f)
    {
        lfo_phase_ = 1.f - (lfo_phase_ - 1.f);
        lfo_freq_ *= -1.f;
    }
    else if(lfo_phase_ < -1.f)
    {
        lfo_phase_ = -1.f - (lfo_phase_ + 1.f);
        lfo_freq_ *= -1.f;
    }

    return lfo_phase_ * lfo_amp_;
}

//Chorus Stuff
void Chorus::Init(float sample_rate)
{
    engines_[0].Init(sample_rate);
    engines_[1].Init(sample_rate);
    SetPan(.25f, .75f);

    gain_frac_ = .5f;
    sigl_ = sigr_ = 0.f;
}

float Chorus::Process(float in)
{
    sigl_ = 0.f;
    sigr_ = 0.f;

    for(int i = 0; i < 2; i++)
    {
        float sig = engines_[i].Process(in);
        sigl_ += (1.f - pan_[i]) * sig;
        sigr_ += pan_[i] * sig;
    }

    sigl_ *= gain_frac_;
    sigr_ *= gain_frac_;

    return sigl_;
}

float Chorus::GetLeft()
{
    return sigl_;
}

float Chorus::GetRight()
{
    return sigr_;
}

void Chorus::SetPan(float panl, float panr)
{
    pan_[0] = fclamp(panl, 0.f, 1.f);
    pan_[1] = fclamp(panr, 0.f, 1.f);
}

void Chorus::SetPan(float pan)
{
    SetPan(pan, pan);
}

void Chorus::SetLfoDepth(float depthl, float depthr)
{
    engines_[0].SetLfoDepth(depthl);
    engines_[1].SetLfoDepth(depthr);
}

void Chorus::SetLfoDepth(float depth)
{
    SetLfoDepth(depth, depth);
}

void Chorus::SetLfoFreq(float freql, float freqr)
{
    engines_[0].SetLfoFreq(freql);
    engines_[1].SetLfoFreq(freqr);
}

void Chorus::SetLfoFreq(float freq)
{
    SetLfoFreq(freq, freq);
}

void Chorus::SetDelay(float delayl, float delayr)
{
    engines_[0].SetDelay(delayl);
    engines_[1].SetDelay(delayr);
}

void Chorus::SetDelay(float delay)
{
    SetDelay(delay, delay);
}

void Chorus::SetDelayMs(float msl, float msr)
{
    engines_[0].SetDelayMs(msl);
    engines_[1].SetDelayMs(msr);
}

void Chorus::SetDelayMs(float ms)
{
    SetDelayMs(ms, ms);
}

void Chorus::SetFeedback(float feedbackl, float feedbackr)
{
    engines_[0].SetFeedback(feedbackl);
    engines_[1].SetFeedback(feedbackr);
}

void Chorus::SetFeedback(float feedback)
{
    SetFeedback(feedback, feedback);
}

} //namespace daisysp

#endif
