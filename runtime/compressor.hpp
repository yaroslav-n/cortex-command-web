#pragma once
#include <algorithm>
#include <cmath>
namespace CortexAudio {
// Stereo power detector, measured against native FMOD with step and burst signals.
class Compressor {
 float first=0, second=0, attack=0, release=0, threshold=1, exponent=0, makeup=1;
public:
 void configure(float thresholdDb,float ratio,float attackMs,float releaseMs,float makeupDb,float rate) {
  constexpr float settling=3.11126983722f; // 2.2 * sqrt(2), two cascaded detector stages.
  attack=std::exp(-settling/(std::max(0.01f,attackMs)*0.001f*rate));
  release=std::exp(-settling/(std::max(0.01f,releaseMs)*0.001f*rate));
  threshold=std::pow(10.0f,thresholdDb/10.0f);
  exponent=(1.0f/std::max(1.0f,ratio)-1.0f)*0.5f;
  makeup=std::pow(10.0f,makeupDb/20.0f);
 }
 void process(float left,float right,float& outLeft,float& outRight) {
  const float power=left*left+right*right;
  const float coefficient=power>second?attack:release;
  first=coefficient*first+(1-coefficient)*power;
  second=coefficient*second+(1-coefficient)*first;
  const float gain=makeup*(second>threshold?std::pow(second/threshold,exponent):1.0f);
  outLeft=left*gain;outRight=right*gain;
 }
};
}
