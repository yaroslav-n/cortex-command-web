#pragma once
// Browser compatibility surface used by Cortex's AudioMan. Constants retain upstream values.
#include <fmod/fmod_common.h>
#include <memory>
namespace FMOD {
class System; class Sound; class Channel; class ChannelGroup; class DSP; class DSPConnection;
struct ControlState; struct SoundState; struct SystemState; struct DSPState;
class ChannelControl {
public:
 std::unique_ptr<ControlState> state;
 ChannelControl(); ~ChannelControl();
 FMOD_RESULT setVolume(float); FMOD_RESULT setVolumeRamp(bool); FMOD_RESULT getVolume(float*); FMOD_RESULT setMute(bool);
 FMOD_RESULT setPaused(bool); FMOD_RESULT getPitch(float*); FMOD_RESULT setPitch(float); FMOD_RESULT setPan(float);
 FMOD_RESULT stop(); FMOD_RESULT isPlaying(bool*); FMOD_RESULT getAudibility(float*);
 FMOD_RESULT set3DAttributes(const FMOD_VECTOR*,const FMOD_VECTOR* = nullptr);
 FMOD_RESULT get3DAttributes(FMOD_VECTOR*,FMOD_VECTOR*);
 FMOD_RESULT set3DLevel(float); FMOD_RESULT get3DLevel(float*);
 FMOD_RESULT get3DMinMaxDistance(float*,float*); FMOD_RESULT getMode(FMOD_MODE*);
 FMOD_RESULT setUserData(void*); FMOD_RESULT getUserData(void**);
 FMOD_RESULT setCallback(FMOD_CHANNELCONTROL_CALLBACK);
 FMOD_RESULT addDSP(int,DSP*); FMOD_RESULT getDSP(int,DSP**);
 FMOD_RESULT getDSPClock(unsigned long long*,unsigned long long*);
 FMOD_RESULT addFadePoint(unsigned long long,float);
};
class Channel : public ChannelControl {
public:
 FMOD_RESULT getCurrentSound(Sound**); FMOD_RESULT getIndex(int*);
 FMOD_RESULT setLoopCount(int); FMOD_RESULT setPriority(int);
};
class ChannelGroup : public ChannelControl {
public:
 FMOD_RESULT addGroup(ChannelGroup*,bool = true,DSPConnection** = nullptr);
 FMOD_RESULT getChannel(int,Channel**); FMOD_RESULT getNumChannels(int*);
};
class Sound {
public:
 std::unique_ptr<SoundState> state; Sound(); ~Sound();
 FMOD_RESULT getLength(unsigned int*,FMOD_TIMEUNIT);
 FMOD_RESULT set3DMinMaxDistance(float,float); FMOD_RESULT setLoopCount(int); FMOD_RESULT setMode(FMOD_MODE);
};
class DSP {
public:
 std::unique_ptr<DSPState> state; DSP(); ~DSP();
 FMOD_RESULT setParameterFloat(int,float);
 FMOD_RESULT setParameterBool(int,bool);
};
class System {
public:
 std::unique_ptr<SystemState> state; System(); ~System();
 FMOD_RESULT init(int,FMOD_INITFLAGS,void*); FMOD_RESULT release(); FMOD_RESULT update();
 FMOD_RESULT setAdvancedSettings(FMOD_ADVANCEDSETTINGS*); FMOD_RESULT getAdvancedSettings(FMOD_ADVANCEDSETTINGS*);
 FMOD_RESULT setSoftwareChannels(int); FMOD_RESULT getSoftwareChannels(int*);
 FMOD_RESULT set3DSettings(float,float,float); FMOD_RESULT set3DNumListeners(int);
 FMOD_RESULT set3DListenerAttributes(int,const FMOD_VECTOR*,const FMOD_VECTOR*,const FMOD_VECTOR*,const FMOD_VECTOR*);
 FMOD_RESULT getSoftwareFormat(int*,FMOD_SPEAKERMODE*,int*);
 FMOD_RESULT getMasterChannelGroup(ChannelGroup**);
 FMOD_RESULT createChannelGroup(const char*,ChannelGroup**);
 FMOD_RESULT createDSPByType(FMOD_DSP_TYPE,DSP**);
 FMOD_RESULT createSound(const char*,FMOD_MODE,FMOD_CREATESOUNDEXINFO*,Sound**);
 FMOD_RESULT playSound(Sound*,ChannelGroup*,bool,Channel**);
 FMOD_RESULT getChannel(int,Channel**); FMOD_RESULT getChannelsPlaying(int*,int* = nullptr);
};
FMOD_RESULT System_Create(System**,unsigned int = FMOD_VERSION);
}
