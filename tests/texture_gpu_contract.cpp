#include <SDL3/SDL.h>
#include "allegro.h"
#include "BigTexture.h"
#include "GLResourceMan.h"
#include "FramebufferReadback.h"
#include "raylib/rlgl.h"
#include <cstdio>
#include <cstdlib>
#include <cerrno>
using namespace RTE;
static void check(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
int main(int,char**){
 check(SDL_Init(SDL_INIT_VIDEO),"SDL init");
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
 auto* window=SDL_CreateWindow("Texture GPU contract",32,24,SDL_WINDOW_OPENGL);check(window,"window");
 auto context=SDL_GL_CreateContext(window);check(context,"context");
 rlLoadExtensions((void*)SDL_GL_GetProcAddress);rlglInit(32,24);
 install_allegro(SYSTEM_NONE,&errno,std::atexit);GLResourceMan::Construct();
 GLuint color,fb;glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,24,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
 check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"target");
 glViewport(0,0,32,24);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_DITHER);
 rlMatrixMode(RL_PROJECTION);rlLoadIdentity();rlOrtho(0,32,24,0,-1,1);
 rlMatrixMode(RL_MODELVIEW);rlLoadIdentity();
 for(int depth:{8,32}) {
 const int channels=depth/8;
 auto* bitmap=create_bitmap_ex(depth,11,9);check(bitmap,"bitmap");
 for(int y=0;y<9;++y)for(int x=0;x<11;++x)for(int c=0;c<channels;++c)bitmap->line[y][x*channels+c]=1+y*11+x+c*37;
 BigTexture::s_MaxGLTextureSize=4;
 std::vector<GLuint> buffers,textures;
 {
  GLuint constructionTexture,constructionBuffer;
  glGenTextures(1,&constructionTexture);glGenBuffers(1,&constructionBuffer);
  glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,constructionTexture);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,constructionBuffer);
  glBufferData(GL_PIXEL_UNPACK_BUFFER,1,nullptr,GL_STREAM_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
  glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
  BigTexture tiled(bitmap);
  check(glGetError()==GL_NO_ERROR,"construction ignores caller's tiny unpack buffer");
  GLint constructionValue;
  for(auto pair:{std::pair<GLenum,GLint>{GL_ACTIVE_TEXTURE,GL_TEXTURE3},
    {GL_TEXTURE_BINDING_2D,static_cast<GLint>(constructionTexture)},
    {GL_PIXEL_UNPACK_BUFFER_BINDING,static_cast<GLint>(constructionBuffer)},
    {GL_UNPACK_ALIGNMENT,8},{GL_UNPACK_ROW_LENGTH,13},
    {GL_UNPACK_SKIP_ROWS,2},{GL_UNPACK_SKIP_PIXELS,1}}){
   glGetIntegerv(pair.first,&constructionValue);
   check(constructionValue==pair.second,"construction preserves caller GL unpack state");
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
  glDeleteBuffers(1,&constructionBuffer);glDeleteTextures(1,&constructionTexture);
  buffers=tiled.m_UploadBuffers;for(auto t:tiled.m_Textures)textures.push_back(t.id);
  check(textures.size()==9,"forced partial edge tiles");
  tiled.Update(Box(Vector(0,0),11,9));
  auto verify=[&](Rectangle source,int scale){
   glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
   tiled.Draw(source,{3,2,source.width*scale,source.height*scale});rlDrawRenderBatchActive();
   std::vector<unsigned char> pixels;check(ReadFramebufferRGBA(fb,32,24,pixels),"read GPU result");
   for(int y=0;y<24;++y)for(int x=0;x<32;++x){
    const bool inside=x>=3&&y>=2&&x<3+source.width*scale&&y<2+source.height*scale;
    for(int c=0;c<channels;++c){
     const int expected=inside?bitmap->line[int(source.y)+(y-2)/scale][(int(source.x)+(x-3)/scale)*channels+c]:(c==3?255:0);
     if(pixels[(y*32+x)*4+c]!=expected){std::fprintf(stderr,"depth %d pixel %d,%d channel %d expected %d got %d\n",depth,x,y,c,expected,pixels[(y*32+x)*4+c]);check(false,"tile GPU pixels");}
    }
   }
   check(glGetError()==GL_NO_ERROR,"clean GL state");
  };
  verify({0,0,11,9},1);verify({2,3,8,5},2);verify({5,5,2,2},3);
  for(int y=3;y<7;++y)for(int x=3;x<8;++x)for(int c=0;c<channels;++c)bitmap->line[y][x*channels+c]=200-c*23;
  GLuint priorTexture,priorBuffer;glGenTextures(1,&priorTexture);glGenBuffers(1,&priorBuffer);
  glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,priorTexture);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,priorBuffer);glBufferData(GL_PIXEL_UNPACK_BUFFER,4096,nullptr,GL_STREAM_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
  glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
  tiled.Update(Box(Vector(3,3),5,4));
  // Updates run several times a frame, so they no longer query and restore the
  // caller's state; they leave the packed layout and no bindings, as the original does.
  GLint value;bool preserved=true;
  for(auto pair:{std::pair<GLenum,GLint>{GL_ACTIVE_TEXTURE,GL_TEXTURE3},{GL_TEXTURE_BINDING_2D,0},
    {GL_PIXEL_UNPACK_BUFFER_BINDING,0},{GL_UNPACK_ALIGNMENT,1},
    {GL_UNPACK_ROW_LENGTH,0},{GL_UNPACK_SKIP_ROWS,0},{GL_UNPACK_SKIP_PIXELS,0}}){
   glGetIntegerv(pair.first,&value);if(value!=pair.second)preserved=false;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
  verify({0,0,11,9},2);check(preserved,"partial upload ignores the caller's unpack state and leaves the original's");
  glDeleteBuffers(1,&priorBuffer);glDeleteTextures(1,&priorTexture);
 }
 for(auto id:buffers)check(!glIsBuffer(id),"upload buffers destroyed");
 for(auto id:textures)check(!glIsTexture(id),"tile textures destroyed");
 destroy_bitmap(bitmap);
 }
 glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&color);
 std::puts("PASS: indexed and RGBA production tiled GPU upload/draw/readback, crops, scaling, partial updates and resource deletion");
 return 0;
}
