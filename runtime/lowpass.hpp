#pragma once
#include <algorithm>
#include <cmath>

namespace CortexAudio {
// FMOD multiband EQ band A defaults to LOWPASS_12DB, Q=0.707.
// Transposed direct-form II preserves delay state across cutoff changes.
struct Lowpass {
 float b0=0, b1=0, b2=0, a1=0, a2=0;
 float delay[2][2]{};
 void configure(float frequency, float q, float sampleRate) {
  const double omega=6.2831853071795864769*std::clamp(frequency,20.0f,sampleRate*0.499f)/sampleRate;
  const double cosine=std::cos(omega), alpha=std::sin(omega)/(2*std::clamp(q,0.1f,10.0f));
  b0=(1-cosine)/(2*(1+alpha)); b1=2*b0; b2=b0;
  a1=-2*cosine/(1+alpha); a2=(1-alpha)/(1+alpha);
 }
 float process(float input,int channel) {
  const float output=b0*input+delay[channel][0];
  delay[channel][0]=b1*input-a1*output+delay[channel][1];
  delay[channel][1]=b2*input-a2*output;
  return output;
 }
};
}
