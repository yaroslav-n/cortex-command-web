#include <SDL3/SDL.h>
#include "FramebufferReadback.h"
#include <cstdio>
#include <cstdlib>
#include <array>
static void check(bool value,const char* reason){if(!value){std::fprintf(stderr,"FAIL: %s (%s)\n",reason,SDL_GetError());std::exit(1);}}
static GLuint target(){GLuint texture,fb;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,3,2,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"fixture framebuffer");return fb;}
int main(int,char**){
 check(SDL_Init(SDL_INIT_VIDEO),"SDL init");
#ifdef __EMSCRIPTEN__
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#endif
 auto* window=SDL_CreateWindow("Frame readback contract",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(window,"window");auto context=SDL_GL_CreateContext(window);check(context,"GL context");
#ifndef __EMSCRIPTEN__
 check(gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)),"GL loader");
#endif
 GLuint capture=target();glViewport(0,0,3,2);glDisable(GL_DITHER);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);glScissor(0,1,3,1);glClearColor(0,1,0,0.25f);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
 GLuint draw=target(),read=target();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glReadBuffer(GL_COLOR_ATTACHMENT0);
 GLuint pack;glGenBuffers(1,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBufferData(GL_PIXEL_PACK_BUFFER,128,nullptr,GL_STREAM_READ);
 glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,7);glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_SKIP_PIXELS,1);
 std::vector<unsigned char> output;check(RTE::ReadFramebufferRGBA(capture,3,2,output),"read capture target");check(output.size()==24,"tight RGBA size");
 for(int y=0;y<2;++y)for(int x=0;x<3;++x){auto i=(y*3+x)*4;check(output[i]==(y?255:0)&&output[i+1]==(y?0:255)&&output[i+2]==0&&output[i+3]==(y?255:64),"top-down exact RGBA including alpha");}
 GLint value;for(auto pair:{std::pair<GLenum,GLint>{GL_READ_FRAMEBUFFER_BINDING,static_cast<GLint>(read)},{GL_DRAW_FRAMEBUFFER_BINDING,static_cast<GLint>(draw)},{GL_PIXEL_PACK_BUFFER_BINDING,static_cast<GLint>(pack)},{GL_PACK_ALIGNMENT,8},{GL_PACK_ROW_LENGTH,7},{GL_PACK_SKIP_ROWS,2},{GL_PACK_SKIP_PIXELS,1},{GL_READ_BUFFER,GL_COLOR_ATTACHMENT0}}){glGetIntegerv(pair.first,&value);check(value==pair.second,"GL read/draw/pack state restored");}

 auto original=output;check(!RTE::ReadFramebufferRGBA(capture,0,2,output)&&output==original,"invalid size leaves output unchanged");
 GLuint incomplete;glGenFramebuffers(1,&incomplete);check(!RTE::ReadFramebufferRGBA(incomplete,3,2,output)&&output==original,"incomplete framebuffer rejected without output mutation");glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&value);check(value==static_cast<GLint>(read),"failed read restores framebuffer");check(glGetError()==GL_NO_ERROR,"no GL errors");
 std::puts("Frame readback contract passed: native/WebGL top-down RGBA, independent framebuffer bindings, pack buffer/stride/offset restoration and failure isolation.");SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
