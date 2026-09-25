#include <SDL3/SDL.h>
#include "allegro.h"
#include "BigTexture.h"
#include "TextureShadow.h"
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
  // Updates send only the pixels that changed. A texel altered behind BigTexture's back
  // shows that: an update with no bitmap pixel changed leaves it as it is.
  // (Checks compare the GPU with the bitmap, so the bitmap briefly holds what the GPU should.)
  auto setPixel=[&](int x,int y,const unsigned char* value){for(int c=0;c<channels;++c)bitmap->line[y][x*channels+c]=value[c];};
  auto getPixel=[&](int x,int y,unsigned char* value){for(int c=0;c<channels;++c)value[c]=bitmap->line[y][x*channels+c];};
  const unsigned char marker[4]={7,77,177,250},fresh[4]={31,63,127,191};unsigned char original[4],changed[4];
  glBindTexture(GL_TEXTURE_2D,textures[1]);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexSubImage2D(GL_TEXTURE_2D,0,2,2,1,1,depth==8?GL_RED:GL_RGBA,GL_UNSIGNED_BYTE,marker);glBindTexture(GL_TEXTURE_2D,0);
  tiled.Update(Box(Vector(0,0),11,9));
  getPixel(6,2,original);setPixel(6,2,marker);verify({0,0,11,9},1);setPixel(6,2,original);
  check(glGetError()==GL_NO_ERROR,"clean GL state after an unchanged update");
  setPixel(6,2,fresh);tiled.Update(Box(Vector(0,0),11,9));verify({0,0,11,9},1);
  // A change outside the updated area waits for an update that covers it.
  getPixel(1,1,original);setPixel(1,1,fresh);getPixel(9,7,changed);changed[0]^=0x55;setPixel(9,7,changed);
  tiled.Update(Box(Vector(8,6),3,3));
  setPixel(1,1,original);verify({0,0,11,9},1);setPixel(1,1,fresh);
  tiled.Update(Box(Vector(0,0),11,9));verify({0,0,11,9},1);
  GLint rowLength;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);check(rowLength==0,"row length back at 0");
 }
 for(auto id:buffers)check(!glIsBuffer(id),"upload buffers destroyed");
 for(auto id:textures)check(!glIsTexture(id),"tile textures destroyed");
 destroy_bitmap(bitmap);
 }
 // Changed rows far apart go up separately and near ones together; a change can sit
 // anywhere in a row, at the bitmap's edges, or on either side of a tile boundary.
 for(int depth:{8,32}) {
 const int channels=depth/8;
 auto* bitmap=create_bitmap_ex(depth,29,70);check(bitmap,"tall bitmap");
 for(int y=0;y<70;++y)for(int x=0;x<29;++x)for(int c=0;c<channels;++c)bitmap->line[y][x*channels+c]=(3+y*7+x*5+c*41)&255;
 BigTexture::s_MaxGLTextureSize=64;
 {
  BigTexture tall(bitmap);check(tall.m_Textures.size()==2,"a tile boundary at row 64");
  auto verifyAll=[&](){
   for(int top=0;top<70;top+=22){
    const Rectangle source{0,float(top),29,float(std::min(22,70-top))};
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    tall.Draw(source,{3,2,source.width,source.height});rlDrawRenderBatchActive();
    std::vector<unsigned char> pixels;check(ReadFramebufferRGBA(fb,32,24,pixels),"read GPU result");
    for(int y=0;y<int(source.height);++y)for(int x=0;x<29;++x)for(int c=0;c<channels;++c){
     const int expected=bitmap->line[top+y][x*channels+c],got=pixels[((y+2)*32+x+3)*4+c];
     if(got!=expected){std::fprintf(stderr,"depth %d pixel %d,%d channel %d expected %d got %d\n",depth,x,top+y,c,expected,got);check(false,"changed rows on the GPU");}
    }
   }
  };
  tall.Update(Box(Vector(0,0),29,70));verifyAll();
  const int changes[][2]={{0,0},{28,3},{14,10},{0,30},{28,31},{5,45},{27,46},{13,63},{14,64},{28,69},{0,69}};
  for(auto& at:changes)for(int c=0;c<channels;++c)bitmap->line[at[1]][at[0]*channels+c]^=0xA5;
  tall.Update(Box(Vector(0,0),29,70));verifyAll();
  // Every row changed, in columns narrower than the bitmap.
  for(int y=0;y<70;++y)for(int x=4;x<20;++x)for(int c=0;c<channels;++c)bitmap->line[y][x*channels+c]+=1;
  tall.Update(Box(Vector(0,0),29,70));verifyAll();
  check(glGetError()==GL_NO_ERROR,"clean GL state after changed rows");
 }
 destroy_bitmap(bitmap);
 }
 // The GUI layer's texture. A new WebGL texture is all zeros, so zero pixels need no
 // upload; after the texture is made again and the copy reset, the rest goes up again.
 {
  auto* gui=create_bitmap_ex(32,20,10);check(gui,"GUI bitmap");clear_to_color(gui,0);
  for(int x=0;x<20;x+=3)for(int c=0;c<4;++c)gui->line[x%10][x*4+c]=40+x*9+c;
  GLuint texture;glGenTextures(1,&texture);
  auto makeTexture=[&](){
   glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,20,10,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  };
  auto verifyGui=[&](){
   glBindTexture(GL_TEXTURE_2D,0);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
   DrawTexturePro(Texture2D{texture,20,10,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8},{0,0,20,10},{3,2,20,10},{0,0},0,{255,255,255,255});rlDrawRenderBatchActive();
   std::vector<unsigned char> pixels;check(ReadFramebufferRGBA(fb,32,24,pixels),"read GPU result");
   for(int y=0;y<10;++y)for(int x=0;x<20;++x)for(int c=0;c<4;++c){
    const int expected=gui->line[y][x*4+c],got=pixels[((y+2)*32+x+3)*4+c];
    if(got!=expected){std::fprintf(stderr,"GUI pixel %d,%d channel %d expected %d got %d\n",x,y,c,expected,got);check(false,"GUI layer on the GPU");}
   }
  };
  TextureShadow shadow;check(shadow.IsEmpty(),"a copy starts unsized");
  makeTexture();shadow.Reset(20,10,4);
  glBindTexture(GL_TEXTURE_2D,texture);shadow.Upload(gui,0,0,20,10,0,0,GL_RGBA);verifyGui();
  for(int c=0;c<4;++c){gui->line[0][0*4+c]=0;gui->line[4][7*4+c]=200+c;gui->line[9][19*4+c]=9;}
  glBindTexture(GL_TEXTURE_2D,texture);shadow.Upload(gui,0,0,20,10,0,0,GL_RGBA);verifyGui();
  makeTexture();shadow.Reset(20,10,4);
  glBindTexture(GL_TEXTURE_2D,texture);shadow.Upload(gui,0,0,20,10,0,0,GL_RGBA);verifyGui();
  // An area reaching past the texture or the bitmap is cut to fit.
  for(int c=0;c<4;++c)gui->line[9][18*4+c]=77;
  glBindTexture(GL_TEXTURE_2D,texture);shadow.Upload(gui,0,0,40,30,0,0,GL_RGBA);verifyGui();
  shadow.Upload(gui,-1,0,20,10,0,0,GL_RGBA);shadow.Upload(gui,0,0,20,10,25,0,GL_RGBA);
  check(glGetError()==GL_NO_ERROR,"clean GL state after GUI uploads");
  GLint rowLength;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);check(rowLength==0,"GUI upload leaves row length at 0");
  glDeleteTextures(1,&texture);destroy_bitmap(gui);
 }
 glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&color);
 std::puts("PASS: indexed and RGBA production tiled GPU upload/draw/readback, crops, scaling, partial updates, change-only uploads and resource deletion");
 return 0;
}
