#include "TextureTileMapping.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace RTE;
static void check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool equal(Rectangle a,Rectangle b){return std::abs(a.x-b.x)<0.0001f&&std::abs(a.y-b.y)<0.0001f&&std::abs(a.width-b.width)<0.0001f&&std::abs(a.height-b.height)<0.0001f;}
int main(){
 TextureTileMapping m;
 check(MapTextureTile({0,0,250,180},{10,20,500,90},{100,100,100,80},m)&&equal(m.source,{0,0,100,80})&&equal(m.destination,{210,70,200,40}),"second row and column use tile-local texture coordinates");
 check(MapTextureTile({75,60,100,80},{20,30,200,240},{100,100,100,80},m)&&equal(m.source,{0,0,75,40})&&equal(m.destination,{70,150,150,120}),"cropped source origin controls destination mapping");
 check(MapTextureTile({110,115,20,30},{4,5,40,60},{100,100,100,80},m)&&equal(m.source,{10,15,20,30})&&equal(m.destination,{4,5,40,60}),"crop entirely within non-origin tile");
 auto prior=m;check(!MapTextureTile({0,0,50,50},{0,0,50,50},{50,0,50,50},m)&&equal(prior.source,m.source),"touching edge does not emit a tile or alter output");
 check(!MapTextureTile({0,0,0,50},{0,0,50,50},{0,0,50,50},m),"zero source width rejected before division");
 float area=0;
 for(int y=0;y<180;y+=100)for(int x=0;x<250;x+=100){
  Rectangle tile{float(x),float(y),float(std::min(100,250-x)),float(std::min(100,180-y))};
  if(MapTextureTile({75,60,150,100},{0,0,300,200},tile,m)){
   check(m.source.x>=0&&m.source.y>=0&&m.source.x+m.source.width<=tile.width&&m.source.y+m.source.height<=tile.height,"sampling stays within partial edge tiles");
   check(m.destination.x>=0&&m.destination.y>=0&&m.destination.x+m.destination.width<=300&&m.destination.y+m.destination.height<=200,"destination remains within requested crop");
   area+=m.destination.width*m.destination.height;
  }
 }
 check(area==60000,"multi-tile scaled crop covers full destination area");
 std::puts("Texture tile contract passed: local coordinates, cropped origins, scaling, edge clipping and destination coverage.");
}
