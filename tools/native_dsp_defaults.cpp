#include <fmod/fmod.hpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static void check(FMOD_RESULT r){if(r!=FMOD_OK){fprintf(stderr,"FMOD error %d\n",r);exit(1);}}
int main(){FMOD::System* s;check(FMOD::System_Create(&s));check(s->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT));check(s->init(32,FMOD_INIT_NORMAL,nullptr));for(auto type:{FMOD_DSP_TYPE_MULTIBAND_EQ,FMOD_DSP_TYPE_COMPRESSOR,FMOD_DSP_TYPE_LIMITER}){FMOD::DSP* d;check(s->createDSPByType(type,&d));int count;check(d->getNumParameters(&count));printf("DSP %d parameters %d\n",type,count);for(int i=0;i<count;++i){FMOD_DSP_PARAMETER_DESC* p;check(d->getParameterInfo(i,&p));printf("%d %s type=%d ",i,p->name,p->type);if(p->type==FMOD_DSP_PARAMETER_TYPE_FLOAT){float v;check(d->getParameterFloat(i,&v,nullptr,0));printf("value=%g min=%g max=%g",v,p->floatdesc.min,p->floatdesc.max);}else if(p->type==FMOD_DSP_PARAMETER_TYPE_INT){int v;check(d->getParameterInt(i,&v,nullptr,0));printf("value=%d",v);}puts("");}check(d->release());}check(s->release());}
