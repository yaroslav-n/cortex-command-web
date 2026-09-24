#include <fmod/fmod.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static FILE* destination;
static unsigned captured=0;
static void check(FMOD_RESULT value){if(value!=FMOD_OK){std::fprintf(stderr,"FMOD error %d\n",value);std::exit(1);}}
static FMOD_RESULT F_CALLBACK capture(FMOD_DSP_STATE*,float* input,float* output,unsigned frames,int channels,int*){
 std::memcpy(output,input,frames*channels*sizeof(float));
 std::fwrite(input,sizeof(float),frames*channels,destination);captured+=frames;return FMOD_OK;
}
int main(int argc,char** argv){
 if(argc<2)return 2;
 const bool channelMode=argc>2&&std::strstr(argv[2],"channel");
 const bool repeated=argc>2&&std::strstr(argv[2],"repeat");
 destination=std::fopen(argv[1],"wb");if(!destination)return 2;
 FMOD::System* system=nullptr;check(FMOD::System_Create(&system));
 check(system->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT));
 check(system->setSoftwareFormat(48000,FMOD_SPEAKERMODE_STEREO,0));
 check(system->setDSPBufferSize(16,4));check(system->init(32,FMOD_INIT_NORMAL,nullptr));
 FMOD::ChannelGroup* group=nullptr;check(system->createChannelGroup("SFX",&group));
 std::vector<float> pcm(4096*2,1);
 FMOD_CREATESOUNDEXINFO info{};info.cbsize=sizeof(info);info.length=pcm.size()*sizeof(float);
 info.numchannels=2;info.defaultfrequency=48000;info.format=FMOD_SOUND_FORMAT_PCMFLOAT;
 FMOD::Sound* sound=nullptr;check(system->createSound(reinterpret_cast<char*>(pcm.data()),FMOD_OPENMEMORY|FMOD_OPENRAW,&info,&sound));
 FMOD::Channel* channel=nullptr;check(system->playSound(sound,group,true,&channel));
 FMOD_DSP_DESCRIPTION description{};description.pluginsdkversion=FMOD_PLUGIN_SDK_VERSION;
 std::strcpy(description.name,"Volume capture");description.numinputbuffers=description.numoutputbuffers=1;description.read=capture;
 FMOD::DSP* tap=nullptr;check(system->createDSP(&description,&tap));FMOD::ChannelControl* control=channelMode?static_cast<FMOD::ChannelControl*>(channel):static_cast<FMOD::ChannelControl*>(group);check(control->addDSP(0,tap));
 check(channel->setPaused(false));
 auto volume=[&](float gain){std::printf("volume frame=%u gain=%g\n",captured,gain);check(control->setVolume(gain));};
 volume(.25f);check(system->update());volume(repeated?.25f:.75f);
 while(captured<128)check(system->update());volume(-.5f);
 while(captured<256)check(system->update());std::printf("mute frame=%u\n",captured);check(control->setMute(true));
 while(captured<384)check(system->update());std::printf("unmute frame=%u\n",captured);check(control->setMute(false));
 while(captured<512)check(system->update());
 check(system->release());std::fclose(destination);std::printf("captured %u frames\n",captured);
}
