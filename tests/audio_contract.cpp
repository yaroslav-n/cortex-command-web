#include "fmod/fmod.hpp"
#include "../runtime/fmod/internal.hpp"
#include "../runtime/lowpass.hpp"
#include "../runtime/limiter.hpp"
#include "../runtime/compressor.hpp"
#include "../runtime/volume_ramp.hpp"
#include <vector>
#include "fixtures/native_lowpass.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <thread>
#include <emscripten.h>
extern "C" int cortex_audio_render_test(FMOD::System*,float*,unsigned long long);
static int ended=0;
static FMOD_RESULT F_CALLBACK onEnd(FMOD_CHANNELCONTROL*,FMOD_CHANNELCONTROL_TYPE type,FMOD_CHANNELCONTROL_CALLBACK_TYPE event,void*,void*) {
 if(type==FMOD_CHANNELCONTROL_CHANNEL&&event==FMOD_CHANNELCONTROL_CALLBACK_END)++ended;return FMOD_OK;
}
static void check(bool value,const char* text){if(!value){std::printf("FAIL: %s\n",text);std::exit(1);}}
static void verifyLimiter(){
 struct Fixture{const char* file;float release,ceiling,makeup;bool linked;};
 const Fixture fixtures[]={{"10",10,0,0,false},{"100",100,0,0,false},{"ceiling",10,-6,6,false},{"linked",10,0,0,true}};
 for(const auto& fixture:fixtures){
  char path[128];std::snprintf(path,sizeof(path),"/audio/limiter/%s.f32",fixture.file);
  FILE* file=std::fopen(path,"rb");check(file!=nullptr,"native limiter fixture exists");
  std::vector<float> reference(8192*2);check(std::fread(reference.data(),sizeof(float),reference.size(),file)==reference.size(),"native limiter fixture complete");std::fclose(file);
  CortexAudio::Limiter limiter;limiter.configure(fixture.release,fixture.ceiling,fixture.makeup,fixture.linked,48000);
  float maxError=0;
  for(unsigned i=0;i<8192;++i){
   float left=.25f,right=.125f;
   if(i>=256&&i<384)left=4;if(i==1536)left=-2;if(i>=3072&&i<3328)right=2;
   float outLeft,outRight;limiter.process(left,right,outLeft,outRight);
   maxError=std::max({maxError,std::abs(outLeft-reference[i*2]),std::abs(outRight-reference[i*2+1])});
   check(std::abs(outLeft)<=limiter.ceiling+0.000001f&&std::abs(outRight)<=limiter.ceiling+0.000001f,"limiter ceiling on every sample");
  }
  std::printf("Native limiter %s: maximum sample error %.9g\n",fixture.file,maxError);
  // Native floating-point recovery differs slightly over long releases; this
  // tolerance records that remaining difference instead of claiming bit parity.
  check(maxError<(fixture.release==100?0.00006f:0.00001f),"native limiter attack/recovery/stereo response");
  if(fixture.release==10 && fixture.ceiling==0 && !fixture.linked){
   FILE* wav=std::fopen("/tmp/limiter-input.wav","wb");check(wav!=nullptr,"limiter WAV create");
   auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,wav);};
   std::fwrite("RIFF",1,4,wav);word(36+8192*8,4);std::fwrite("WAVEfmt ",1,8,wav);
   word(16,4);word(3,2);word(2,2);word(48000,4);word(48000*8,4);word(8,2);word(32,2);std::fwrite("data",1,4,wav);word(8192*8,4);
   for(unsigned i=0;i<8192;++i){float input[]={.25f,.125f};if(i>=256&&i<384)input[0]=4;if(i==1536)input[0]=-2;if(i>=3072&&i<3328)input[1]=2;std::fwrite(input,4,2,wav);}
   std::fclose(wav);
   FMOD::System* mixer=nullptr;check(FMOD::System_Create(&mixer)==FMOD_OK,"limiter mixer create");
   check(mixer->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"limiter mixer init");
   FMOD::Sound* sound=nullptr;FMOD::Channel* voice=nullptr;FMOD::DSP* effect=nullptr;
   check(mixer->createSound("/tmp/limiter-input.wav",FMOD_2D,nullptr,&sound)==FMOD_OK,"limiter input decode");
   check(mixer->playSound(sound,nullptr,true,&voice)==FMOD_OK,"limiter voice create");
   check(mixer->createDSPByType(FMOD_DSP_TYPE_LIMITER,&effect)==FMOD_OK,"limiter default effect create");
   check(voice->addDSP(0,effect)==FMOD_OK,"limiter graph attach");voice->setPaused(false);
   std::vector<float> rendered(8704*2);check(cortex_audio_render_test(mixer,rendered.data(),8704)==0,"limiter graph render");
   float error=0;for(unsigned i=0;i<8192*2;++i)error=std::max(error,std::abs(rendered[i+4]-reference[i]));
   std::printf("Native default limiter through mixer: maximum sample error %.9g\n",error);
   check(error<0.00001f,"native limiter default response through real mixer");
   check(mixer->release()==FMOD_OK,"limiter graph teardown");
  }

 }
}
static void verifyCompressor(){
 struct Fixture {const char* name;float attack,right;bool burst;int faderIndex=-1;bool channelVolume=false;};
 for(const auto& fixture: {Fixture{"burst",180,0,true},Fixture{"step1",1,1,false},Fixture{"step20",20,1,false},Fixture{"step180",180,1,false},Fixture{"mono",20,0,false},Fixture{"unequal",20,.5f,false},Fixture{"group-pre",20,1,false,1},Fixture{"group-post",20,1,false,0},Fixture{"channel-pre",20,1,false,1,true},Fixture{"channel-post",20,1,false,0,true}}){
  char path[128];std::snprintf(path,sizeof(path),"/audio/compressor/%s.f32",fixture.name);
  FILE* file=std::fopen(path,"rb");check(file!=nullptr,"compressor native fixture exists");
  std::vector<float> reference(8192*2),input(8192*2);
  check(std::fread(reference.data(),4,reference.size(),file)==reference.size(),"compressor native fixture complete");std::fclose(file);
  CortexAudio::Compressor compressor;compressor.configure(-10,3,fixture.attack,250,5,48000);
  float maximum=0;
  for(int i=0;i<8192;++i){
   float l=1,r=fixture.right;
   if(fixture.burst){l=i>=256&&i<384?4:(i==1536?-2:.25f);r=i>=3072&&i<3328?2:.125f;}
   input[i*2]=l;input[i*2+1]=r;
   float outLeft,outRight;compressor.process(fixture.faderIndex==0?l*.25f:l,fixture.faderIndex==0?r*.25f:r,outLeft,outRight);
   if(fixture.faderIndex==1){outLeft*=.25f;outRight*=.25f;}
   maximum=std::max({maximum,std::abs(outLeft-reference[i*2]),std::abs(outRight-reference[i*2+1])});
  }
  std::printf("Native compressor %s kernel: maximum sample error %.9g\n",fixture.name,maximum);
  check(maximum<0.00003f,"native compressor detector and gain response");
  FILE* wav=std::fopen("/tmp/compressor-input.wav","wb");check(wav!=nullptr,"compressor WAV create");
  auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,wav);};
  std::fwrite("RIFF",1,4,wav);word(36+8192*8,4);std::fwrite("WAVEfmt ",1,8,wav);
  word(16,4);word(3,2);word(2,2);word(48000,4);word(48000*8,4);word(8,2);word(32,2);std::fwrite("data",1,4,wav);word(8192*8,4);
  std::fwrite(input.data(),4,input.size(),wav);std::fclose(wav);
  FMOD::System* mixer=nullptr;check(FMOD::System_Create(&mixer)==FMOD_OK,"compressor mixer create");
  check(mixer->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"compressor mixer init");
  FMOD::Sound* sound=nullptr;FMOD::Channel* voice=nullptr;FMOD::DSP* effect=nullptr;
  check(mixer->createSound("/tmp/compressor-input.wav",FMOD_2D,nullptr,&sound)==FMOD_OK,"compressor input decode");
  FMOD::ChannelGroup* group=nullptr;
  if(fixture.faderIndex>=0&&!fixture.channelVolume){check(mixer->createChannelGroup("SFX",&group)==FMOD_OK,"compressor group create");check(group->setVolumeRamp(false)==FMOD_OK,"isolate compressor ordering from volume ramp");check(group->setVolume(.25f)==FMOD_OK,"compressor group gain");}
  check(mixer->playSound(sound,group,true,&voice)==FMOD_OK,"compressor voice create");
  if(fixture.channelVolume){check(voice->setVolumeRamp(false)==FMOD_OK,"channel ordering without ramp");check(voice->setVolume(.25f)==FMOD_OK,"channel ordering gain");}
  check(mixer->createDSPByType(FMOD_DSP_TYPE_COMPRESSOR,&effect)==FMOD_OK,"compressor effect create");
  const float parameters[]={-10,3,fixture.attack,250,5};
  for(int i=0;i<5;++i)check(effect->setParameterFloat(i,parameters[i])==FMOD_OK,"compressor parameters");
  check((group?group->addDSP(fixture.faderIndex,effect):voice->addDSP(fixture.channelVolume?fixture.faderIndex:0,effect))==FMOD_OK,"compressor graph attach");voice->setPaused(false);
  std::vector<float> rendered(8704*2);check(cortex_audio_render_test(mixer,rendered.data(),8704)==0,"compressor graph render");
  float error=0;for(unsigned i=0;i<input.size();++i)error=std::max(error,std::abs(rendered[i+(group?6:4)]-reference[i]));
  std::printf("Native compressor %s mixer: maximum sample error %.9g\n",fixture.name,error);
  check(error<0.00003f,"native compressor response through mixer");
  check(mixer->release()==FMOD_OK,"compressor mixer teardown");
 }
}
static void verifyVolumeRamp(){
 for(bool channelMode:{false,true})for(bool repeated:{false,true}){
  const char* name=channelMode?(repeated?"channel-repeat":"channel-events"):(repeated?"repeat":"events");char path[128];std::snprintf(path,sizeof(path),"/audio/volume/%s.f32",name);
  FILE* file=std::fopen(path,"rb");check(file!=nullptr,"native volume fixture exists");std::vector<float> reference(512*2);
  check(std::fread(reference.data(),4,reference.size(),file)==reference.size(),"native volume fixture complete");std::fclose(file);
  CortexAudio::VolumeRamp ramp;float target=.25f,error=0;
  for(unsigned frame=0;frame<512;++frame){
   if(frame==16)target=repeated?.25f:.75f;
   if(frame==128)target=-.5f;if(frame==256)target=0;if(frame==384)target=-.5f;
   error=std::max(error,std::abs(ramp.next(target)-reference[frame*2]));
  }
  std::printf("Native volume %s kernel: maximum sample error %.9g\n",name,error);check(error==0,"native ramp interruptions, repeated target, negative gain and mute");
  FILE* wav=std::fopen("/tmp/volume-input.wav","wb");check(wav!=nullptr,"volume WAV create");
  auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,wav);};
  std::fwrite("RIFF",1,4,wav);word(36+4096*8,4);std::fwrite("WAVEfmt ",1,8,wav);word(16,4);word(3,2);word(2,2);word(48000,4);word(48000*8,4);word(8,2);word(32,2);std::fwrite("data",1,4,wav);word(4096*8,4);
  for(unsigned i=0;i<4096*2;++i){float one=1;std::fwrite(&one,4,1,wav);}std::fclose(wav);
  FMOD::System* mixer=nullptr;check(FMOD::System_Create(&mixer)==FMOD_OK,"volume mixer create");check(mixer->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"volume mixer init");
  FMOD::ChannelGroup* group=nullptr;check(mixer->createChannelGroup("SFX",&group)==FMOD_OK,"volume group create");
  FMOD::Sound* sound=nullptr;FMOD::Channel* voice=nullptr;
  check(mixer->createSound("/tmp/volume-input.wav",FMOD_2D,nullptr,&sound)==FMOD_OK,"volume input decode");check(mixer->playSound(sound,group,false,&voice)==FMOD_OK,"volume voice start");
  std::vector<float> warmup(512*2);check(cortex_audio_render_test(mixer,warmup.data(),512)==0,"settle existing mixer startup delay");
  FMOD::ChannelControl* control=channelMode?static_cast<FMOD::ChannelControl*>(voice):static_cast<FMOD::ChannelControl*>(group);
  std::vector<float> rendered(514*2);
  auto render=[&](unsigned begin,unsigned count){check(cortex_audio_render_test(mixer,rendered.data()+begin*2,count)==0,"volume transition mixer render");};
  check(control->setVolume(.25f)==FMOD_OK,"volume initial target");render(0,16);
  check(control->setVolume(repeated?.25f:.75f)==FMOD_OK,"volume interrupted target");render(16,112);
  check(control->setVolume(-.5f)==FMOD_OK,"volume negative target");render(128,128);
  check(control->setMute(true)==FMOD_OK,"volume mute");render(256,128);
  check(control->setMute(false)==FMOD_OK,"volume unmute");render(384,130);
  // Downstream pitch-capable groups retain one sample each: master for a
  // group change, SFX plus master for a channel change. Align that existing delay.
  error=0;for(unsigned i=0;i<1024;++i)error=std::max(error,std::abs(rendered[i+(channelMode?4:2)]-reference[i]));
  std::printf("Native volume %s mixer: maximum sample error %.9g\n",name,error);
  check(error==0,"native volume ramp through mixer with downstream startup latency");
  check(mixer->release()==FMOD_OK,"volume mixer teardown");
 }
}
static void verifyNestedPause() {
 unsigned durations[4]{};
 for(int pausedRun=0;pausedRun<4;++pausedRun){
  FMOD::System* mixer=nullptr;check(FMOD::System_Create(&mixer)==FMOD_OK,"nested pause system");
  check(mixer->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"nested pause init");
  FMOD::ChannelGroup *parent=nullptr,*child=nullptr;
  check(mixer->createChannelGroup("parent",&parent)==FMOD_OK&&mixer->createChannelGroup("child",&child)==FMOD_OK,"nested groups");
  check(parent->addGroup(child)==FMOD_OK,"nested group routing");
  FMOD::Sound* sound=nullptr;FMOD::Channel* voice=nullptr;
  check(mixer->createSound("/audio/PistolFire.flac",FMOD_2D,nullptr,&sound)==FMOD_OK,"nested pause source");
  check(mixer->playSound(sound,child,false,&voice)==FMOD_OK,"nested pause play");
  std::array<float,256> output{};bool playing=true;unsigned frames=0;
  while(playing&&frames<100000){
   if(pausedRun&&frames==4096){
    FMOD::ChannelControl* independent=pausedRun==2?static_cast<FMOD::ChannelControl*>(child):static_cast<FMOD::ChannelControl*>(voice);
    if(pausedRun>=2)check(independent->setPaused(true)==FMOD_OK,"independent descendant pause");
    check(parent->setPaused(true)==FMOD_OK,"parent pause");
    for(int block=0;block<32;++block){
     check(cortex_audio_render_test(mixer,output.data(),128)==0,"paused nested render");
     if(block>0)for(float sample:output)check(std::abs(sample)<1e-7f,"parent pause silences descendants after downstream buffer drains");
     mixer->update();check(voice->isPlaying(&playing)==FMOD_OK&&playing,"paused descendant remains playing");
    }
    check(parent->setPaused(false)==FMOD_OK,"parent resume");
    if(pausedRun>=2){
     // Parent resume must leave the descendant's independent pause intact.
     for(int block=0;block<32;++block){
      check(cortex_audio_render_test(mixer,output.data(),128)==0,"independently paused render");
      if(block>0)for(float sample:output)check(std::abs(sample)<1e-7f,"parent resume preserves independent descendant pause");
      mixer->update();check(voice->isPlaying(&playing)==FMOD_OK&&playing,"independently paused voice remains live");
     }
     check(independent->setPaused(false)==FMOD_OK,"explicit descendant resume");
    }
   }
   check(cortex_audio_render_test(mixer,output.data(),128)==0,"nested render");
   frames+=128;mixer->update();check(voice->isPlaying(&playing)==FMOD_OK,"nested playing query");
  }
  check(!playing,"nested source reaches end");durations[pausedRun]=frames;
  check(mixer->release()==FMOD_OK,"nested pause teardown");
 }
 std::printf("Nested pause active render durations: baseline %u, parent %u, child %u, voice %u frames\n",durations[0],durations[1],durations[2],durations[3]);
 for(int run=1;run<4;++run)check(durations[0]==durations[run],"overlapping pauses preserve descendant playback position");
}
// Plays the sound to its end on an otherwise silent graph, then renders on until the graph is silent again.
static std::vector<float> renderToEnd(FMOD::System* system,FMOD::Sound* sound){
 ended=0;FMOD::Channel* channel=nullptr;check(system->playSound(sound,nullptr,false,&channel)==FMOD_OK,"decoded cache channel");channel->setCallback(onEnd);
 std::vector<float> output;std::array<float,1024> block{};
 auto render=[&]{check(cortex_audio_render_test(system,block.data(),512)==0,"decoded cache render");output.insert(output.end(),block.begin(),block.end());};
 while(ended==0){render();system->update();check(output.size()<48000*2*30,"decoded cache sound ends");}
 for(int i=0;i<8;++i)render();
 ended=0;return output;
}
static void writeSweepWav(const char* path,uint32_t frames){
 FILE* file=std::fopen(path,"wb");check(file!=nullptr,"sweep WAV create");
 auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,file);};
 std::fwrite("RIFF",1,4,file);word(36+frames*2,4);std::fwrite("WAVEfmt ",1,8,file);
 word(16,4);word(1,2);word(1,2);word(44100,4);word(44100*2,4);word(2,2);word(16,2);
 std::fwrite("data",1,4,file);word(frames*2,4);
 double phase=0;for(uint32_t i=0;i<frames;++i){phase+=2*M_PI*(200+8000.0*i/frames)/44100;word(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::sin(phase)*20000))),2);}
 std::fclose(file);
}
static std::vector<unsigned char> readFile(const char* path){FILE* f=std::fopen(path,"rb");std::vector<unsigned char> b;if(!f)return b;int c;while((c=std::fgetc(f))!=EOF)b.push_back(static_cast<unsigned char>(c));std::fclose(f);return b;}
// In the browser a sound's file can arrive after the game has started: the page lists
// it in advance (/audio-manifest.tsv) and leaves a stand-in file until the bytes are
// there. Such a sound must be creatable from the list alone, play silence while it
// waits, be asked for once, and then sound exactly as a sound that was there already.
static void verifyLateSoundFile(){
 const uint32_t frames=11025;
 writeSweepWav("/tmp/late-source.wav",frames);
 const std::vector<unsigned char> real=readFile("/tmp/late-source.wav");check(!real.empty(),"late source bytes");
 {FILE* standIn=std::fopen("/tmp/late-sound.wav","wb");check(standIn!=nullptr,"stand-in create");std::fputs(FMOD::c_SoundFileOnItsWay,standIn);std::fclose(standIn);}
 {FILE* list=std::fopen("/audio-manifest.tsv","w");check(list!=nullptr,"sound file list create");
  std::fprintf(list,"tmp/late-sound.wav\t%zu\t%u\t44100\tlate.wav\n",real.size(),frames);std::fclose(list);}
 EM_ASM({ Module['requestedSoundFiles']=[]; Module['requestSoundFile']=(path)=>Module['requestedSoundFiles'].push(path); });
 FMOD::System* system=nullptr;check(FMOD::System_Create(&system)==FMOD_OK,"late sound system create");
 check(system->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"late sound mixer init");
 FMOD::Sound* late=nullptr;check(system->createSound("/tmp/late-sound.wav",FMOD_2D,nullptr,&late)==FMOD_OK,"a listed sound is created before its file arrives");
 unsigned length=0;check(late->getLength(&length,FMOD_TIMEUNIT_PCM)==FMOD_OK&&length==frames,"a listed sound's length comes from the list");
 FMOD::Sound* present=nullptr;check(system->createSound("/tmp/late-source.wav",FMOD_2D,nullptr,&present)==FMOD_OK,"reference sound");
 ended=0;FMOD::Channel* channel=nullptr;check(system->playSound(late,nullptr,false,&channel)==FMOD_OK,"a waiting sound plays");channel->setCallback(onEnd);
 std::array<float,1024> block{};
 for(int i=0;i<6;++i){check(cortex_audio_render_test(system,block.data(),512)==0,"waiting render");for(float v:block)check(v==0,"a waiting sound is silent");system->update();}
 check(ended==0,"a waiting sound does not end");
 FMOD::Channel* second=nullptr;check(system->playSound(late,nullptr,false,&second)==FMOD_OK,"a second play while waiting");second->stop();system->update();
 check(ended==0,"stopping one waiting channel leaves the other");
 const int requested=EM_ASM_INT({ return Module['requestedSoundFiles'].length===1 && Module['requestedSoundFiles'][0]==='tmp/late-sound.wav' ? 1 : 0; });
 check(requested==1,"the file is asked for once, by its engine path");
 // The page delivers the file; the channel starts from the beginning at the next look.
 {FILE* file=std::fopen("/tmp/late-sound.wav","wb");std::fwrite(real.data(),1,real.size(),file);std::fclose(file);}
 std::this_thread::sleep_for(std::chrono::milliseconds(60));system->update();
 std::vector<float> arrived;
 while(ended==0){check(cortex_audio_render_test(system,block.data(),512)==0,"arrived render");arrived.insert(arrived.end(),block.begin(),block.end());system->update();check(arrived.size()<48000*2*10,"arrived sound ends");}
 for(int i=0;i<8;++i){check(cortex_audio_render_test(system,block.data(),512)==0,"arrived flush");arrived.insert(arrived.end(),block.begin(),block.end());}
 ended=0;
 const std::vector<float> reference=renderToEnd(system,present);
 size_t differing=0;for(size_t i=0;i<std::min(arrived.size(),reference.size());++i)differing+=arrived[i]!=reference[i];
 std::printf("Late sound file: silent while waiting, requested once, then %zu frames against %zu, %zu samples differ\n",arrived.size()/2,reference.size()/2,differing);
 check(arrived.size()==reference.size()&&differing==0,"a sound whose file arrived late sounds as one that was there");
 check(system->release()==FMOD_OK,"late sound teardown");
 std::remove("/audio-manifest.tsv");
}
// FMOD never refuses a sound because its channels are all playing: it steals the least
// important channel (the highest priority number), the oldest of those, and reports the
// stolen one as ended. The engine used a failed play's channel pointer regardless.
static std::vector<int> endedChannels;
static FMOD_RESULT F_CALLBACK recordEnd(FMOD_CHANNELCONTROL* control,FMOD_CHANNELCONTROL_TYPE type,FMOD_CHANNELCONTROL_CALLBACK_TYPE event,void*,void*){
 if(type==FMOD_CHANNELCONTROL_CHANNEL&&event==FMOD_CHANNELCONTROL_CALLBACK_END){int index=-1;reinterpret_cast<FMOD::Channel*>(control)->getIndex(&index);endedChannels.push_back(index);}return FMOD_OK;
}
static void verifyChannelStealing(){
 FMOD::System* system=nullptr;check(FMOD::System_Create(&system)==FMOD_OK,"stealing system create");
 check(system->init(4,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"stealing mixer init, four channels");
 FMOD::Sound* sound=nullptr;check(system->createSound("/audio/PistolFire.flac",FMOD_2D,nullptr,&sound)==FMOD_OK,"stealing sound");
 const int priorities[4]={128,200,128,50};
 for(int priority:priorities){FMOD::Channel* channel=nullptr;check(system->playSound(sound,nullptr,false,&channel)==FMOD_OK,"a play with a free channel");channel->setCallback(recordEnd);channel->setPriority(priority);}
 FMOD::Channel* fifth=nullptr;check(system->playSound(sound,nullptr,false,&fifth)==FMOD_OK,"a fifth play steals rather than fails");fifth->setCallback(recordEnd);
 int index=-1;fifth->getIndex(&index);check(endedChannels.size()==1&&endedChannels[0]==1&&index==1,"the least important channel is stolen and reported as ended");
 FMOD::Channel* sixth=nullptr;check(system->playSound(sound,nullptr,false,&sixth)==FMOD_OK,"a sixth play steals too");
 sixth->getIndex(&index);check(endedChannels.size()==2&&endedChannels[1]==0&&index==0,"of equal priority, the oldest is stolen");
 int playing=0;system->getChannelsPlaying(&playing);check(playing==4,"four channels still playing");
 std::array<float,1024> block{};check(cortex_audio_render_test(system,block.data(),512)==0,"stealing render");
 std::printf("Channel stealing: with 4 channels a fifth and sixth play stole channels 1 (priority 200) and 0 (oldest of priority 128), both reported ended\n");
 check(system->release()==FMOD_OK,"stealing teardown");
}
// A short sound is decoded once in the background and later plays from memory; it must
// sound exactly as it did while its first play decoded it: resampled, upmixed and looped.
static void verifyDecodedCache(){
 {
  FILE* file=std::fopen("/tmp/sweep-44100.wav","wb");check(file!=nullptr,"sweep WAV create");
  auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,file);};
  const uint32_t frames=22050;
  std::fwrite("RIFF",1,4,file);word(36+frames*2,4);std::fwrite("WAVEfmt ",1,8,file);
  word(16,4);word(1,2);word(1,2);word(44100,4);word(44100*2,4);word(2,2);word(16,2);
  std::fwrite("data",1,4,file);word(frames*2,4);
  double phase=0;for(uint32_t i=0;i<frames;++i){phase+=2*M_PI*(200+8000.0*i/frames)/44100;word(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::sin(phase)*20000))),2);}
  std::fclose(file);
 }
 FMOD::System* system=nullptr;check(FMOD::System_Create(&system)==FMOD_OK,"decoded cache system create");
 check(system->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"decoded cache mixer init");
 struct Case{const char* path;const char* name;int loops;};
 for(const Case& test:{Case{"/audio/PistolFire.flac","22,050 Hz FLAC played twice",1},Case{"/tmp/sweep-44100.wav","44,100 Hz mono WAV",0}}){
  FMOD::Sound* sound=nullptr;check(system->createSound(test.path,FMOD_2D,nullptr,&sound)==FMOD_OK,"decoded cache sound");
  check(sound->setLoopCount(test.loops)==FMOD_OK,"decoded cache loop count");
  const auto before=system->state->decoded->GetStatistics();
  const std::vector<float> decoding=renderToEnd(system,sound);
  system->state->decoded->WaitUntilIdle();
  const std::vector<float> fromMemory=renderToEnd(system,sound);
  const auto after=system->state->decoded->GetStatistics();
  check(after.playsDecoding==before.playsDecoding+1&&after.playsFromMemory==before.playsFromMemory+1,"first play decodes, second plays from memory");
  size_t differing=0;float maximum=0;
  for(size_t i=0;i<std::min(decoding.size(),fromMemory.size());++i)if(decoding[i]!=fromMemory[i]){++differing;maximum=std::max(maximum,std::abs(decoding[i]-fromMemory[i]));}
  std::printf("Decoded cache, %s: %zu and %zu frames rendered, %zu samples differ, maximum difference %.9g\n",test.name,decoding.size()/2,fromMemory.size()/2,differing,maximum);
  check(decoding.size()==fromMemory.size()&&differing==0,"a sound from memory renders exactly as it did decoding");
 }
 check(system->release()==FMOD_OK,"decoded cache teardown");
}
int main(){
 verifyChannelStealing();
 verifyDecodedCache();
 verifyLateSoundFile();
 verifyNestedPause();
 verifyVolumeRamp();
 verifyLimiter();
 verifyCompressor();
 for(const auto& fixture:nativeLowpassFixtures){
  CortexAudio::Lowpass filter;filter.configure(fixture.cutoff,0.707f,48000);
  float maxError=0;
  for(int i=0;i<128;++i){
   float actual=filter.process(i==0?1.0f:0.0f,0);
   maxError=std::max(maxError,std::abs(actual-fixture.samples[i]));
   check(filter.process(0,1)==0,"filter stereo state isolation");
  }
  std::printf("Native FMOD lowpass %.0f Hz: maximum sample error %.9g\n",fixture.cutoff,maxError);
  check(maxError<0.000002f,"native FMOD impulse response equivalence");
 }

 FMOD::System* system=nullptr;check(FMOD::System_Create(&system)==FMOD_OK,"system create");
 check(system->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"offline real mixer init");
 // Exercise the same filter through decoding, channel attachment, and the real mixer.
 {
  FILE* file=std::fopen("/tmp/filter-impulse.wav","wb");check(file!=nullptr,"impulse WAV create");
  auto word=[&](uint32_t value,int bytes){for(int i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,file);};
  std::fwrite("RIFF",1,4,file);word(36+4096*2*4,4);std::fwrite("WAVEfmt ",1,8,file);
  word(16,4);word(3,2);word(2,2);word(48000,4);word(48000*8,4);word(8,2);word(32,2);
  std::fwrite("data",1,4,file);word(4096*2*4,4);
  for(int i=0;i<4096;++i){float value=i==0?1.0f:0.0f;std::fwrite(&value,4,1,file);std::fwrite(&value,4,1,file);}
  std::fclose(file);
  FMOD::Sound* impulse=nullptr;check(system->createSound("/tmp/filter-impulse.wav",FMOD_2D,nullptr,&impulse)==FMOD_OK,"impulse decode");
  for(const auto& fixture:nativeLowpassFixtures){
   FMOD::Channel* voice=nullptr;FMOD::DSP* effect=nullptr;
   check(system->playSound(impulse,nullptr,true,&voice)==FMOD_OK,"impulse channel");
   check(system->createDSPByType(FMOD_DSP_TYPE_MULTIBAND_EQ,&effect)==FMOD_OK,"impulse filter");
   check(effect->setParameterFloat(1,fixture.cutoff)==FMOD_OK,"impulse cutoff");
   check(voice->addDSP(0,effect)==FMOD_OK,"impulse graph attachment");voice->setPaused(false);
   std::array<float,1024> rendered{};check(cortex_audio_render_test(system,rendered.data(),512)==0,"impulse render");
   float maximumError=0;
   for(int i=0;i<128;++i)for(int c=0;c<2;++c)maximumError=std::max(maximumError,std::abs(rendered[(i+2)*2+c]-fixture.samples[i]));
   std::printf("Native FMOD lowpass mixer %.0f Hz: maximum sample error %.9g\n",fixture.cutoff,maximumError);
   // Miniaudio pitch-capable channel and master nodes each add one startup frame.
   for(int i=0;i<4;++i)check(std::abs(rendered[i])<0.000002f,"two-frame mixer startup latency");
   check(maximumError<0.000002f,"native filter response through real mixer");voice->stop();system->update();
  }
 }
 // Isolate lifecycle checks from the impulse fixture graph and its filter tails.
 check(system->release()==FMOD_OK,"fixture graph teardown");
 check(FMOD::System_Create(&system)==FMOD_OK,"lifecycle system create");
 check(system->init(32,FMOD_INIT_MIX_FROM_UPDATE,nullptr)==FMOD_OK,"lifecycle mixer init");
 FMOD::ChannelGroup* group=nullptr;check(system->createChannelGroup("test",&group)==FMOD_OK,"group create");
 FMOD::Sound* sound=nullptr;check(system->createSound("/audio/PistolFire.flac",FMOD_2D,nullptr,&sound)==FMOD_OK,"real FLAC sound");
 unsigned length=0;check(sound->getLength(&length,FMOD_TIMEUNIT_PCM)==FMOD_OK,"source length query");check(length==18793,"original FLAC source PCM frame count");
 const unsigned renderedLength=40910; // Full 22,050-to-48,000 Hz decode, including resampler tail.
 check(sound->setLoopCount(1)==FMOD_OK,"two-play finite loop");
 FMOD::Channel* channel=nullptr;check(system->playSound(sound,group,true,&channel)==FMOD_OK,"paused channel create");
 channel->setCallback(onEnd);channel->setVolume(0.5f);
 std::array<float,1024> pcm{};double energy=0;
 for(int i=0;i<3;++i){check(cortex_audio_render_test(system,pcm.data(),512)==0,"paused render");for(float v:pcm)check(v==0,"paused silence");}
 check(channel->setPaused(false)==FMOD_OK,"unpause");
 unsigned total=0;
 while(ended==0&&total<renderedLength*3){check(cortex_audio_render_test(system,pcm.data(),512)==0,"render");for(float v:pcm){check(std::isfinite(v),"finite PCM");energy+=v*v;}total+=512;system->update();}
 check(ended==1,"one end callback");check(total>=renderedLength*2&&total<renderedLength*2+1024,"sample-accurate finite loop duration");check(energy>1,"non-silent mixer output");
 int count=-1;system->getChannelsPlaying(&count);check(count==0,"ended channel released");
 std::printf("Finite loop: %u frames rendered, expected %u; energy %.3f; one callback.\n",total,renderedLength*2,energy);
 sound->setLoopCount(-1);check(system->playSound(sound,group,false,&channel)==FMOD_OK,"recycled channel");
 FMOD::DSP* lowpass=nullptr;check(system->createDSPByType(FMOD_DSP_TYPE_MULTIBAND_EQ,&lowpass)==FMOD_OK,"filter create");
 check(lowpass->setParameterFloat(1,800)==FMOD_OK&&channel->addDSP(0,lowpass)==FMOD_OK,"real filter graph attachment");
 unsigned long long clock=0;channel->getDSPClock(nullptr,&clock);channel->addFadePoint(clock,1);channel->addFadePoint(clock+4800,0);
 for(int i=0;i<12;++i){check(cortex_audio_render_test(system,pcm.data(),512)==0,"filtered fade render");system->update();}
 double fadedEnergy=0;for(float v:pcm)fadedEnergy+=v*v;check(fadedEnergy<0.000001,"fade reaches silence");
 group->stop();system->update();check(ended==1,"recycled channel callback must not retain old owner");
 check(system->release()==FMOD_OK,"clean graph teardown");
 std::puts("Audio compatibility contract passed.");return 0;
}
