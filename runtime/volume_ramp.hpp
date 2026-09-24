#pragma once
namespace CortexAudio {
// Native FMOD's automatic fader smoothing is 64 output samples. New targets
// restart from the next sample's gain; assigning the same target preserves it.
class VolumeRamp {
 float current=1, target=1, increment=0;
 unsigned remaining=0;
public:
 float next(float requested,bool enabled=true){
  if(!enabled){current=target=requested;remaining=0;return current;}
  if(requested!=target){target=requested;remaining=64;increment=(target-current)/64.0f;}
  const float value=current;
  if(remaining && --remaining==0)current=target;
  else if(remaining)current+=increment;
  return value;
 }
};
}
