#pragma once
#include <algorithm>
#include <cmath>
namespace CortexAudio {
// FMOD limiter parameters: release milliseconds, ceiling dB, maximizer dB,
// stereo linking. Native defaults are 10, 0, 0, false.
struct Limiter {
 float gains[2]={1,1};
 float recovery=0, ceiling=1, maximumGain=1;
 bool linked=false;
 void configure(float milliseconds,float ceilingDB,float gainDB,bool link,float rate) {
  recovery=1.0f/(std::clamp(milliseconds,1.0f,1000.0f)*0.001f*rate);
  ceiling=std::pow(10.0f,std::clamp(ceilingDB,-12.0f,0.0f)/20.0f);
  maximumGain=std::pow(10.0f,(std::clamp(gainDB,0.0f,12.0f)-std::clamp(ceilingDB,-12.0f,0.0f))/20.0f);
  linked=link;
 }
 void process(float left,float right,float& outLeft,float& outRight) {
  const float inputs[2]={left,right};
  float outputs[2];
  if(linked) {
   // Native FMOD updates a shared gain once per frame, then processes channels
   // in order. A right-channel peak affects the left starting next frame.
   float gain=std::min(gains[0],gains[1]);
   gain=std::min(maximumGain,gain+gain*recovery);
   for(int channel=0;channel<2;++channel){
    const float magnitude=std::abs(inputs[channel]);
    if(magnitude*gain>1.0f)gain=1.0f/magnitude;
    outputs[channel]=inputs[channel]*gain*ceiling;
   }
   gains[0]=gains[1]=gain;
  } else {
   for(int channel=0;channel<2;++channel) {
    float& gain=gains[channel];
    gain=std::min(maximumGain,gain+gain*recovery);
    const float magnitude=std::abs(inputs[channel]);
    if(magnitude*gain>1.0f)gain=1.0f/magnitude;
    outputs[channel]=inputs[channel]*gain*ceiling;
   }
  }
  outLeft=outputs[0];outRight=outputs[1];
 }
};
}
