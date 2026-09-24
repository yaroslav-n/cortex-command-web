#pragma once
#include "raylib/raylib.h"
#include <algorithm>
namespace RTE {
struct TextureTileMapping { Rectangle source; Rectangle destination; };
// source and tile use whole-bitmap coordinates; output source is tile-local.
inline bool MapTextureTile(Rectangle source,Rectangle destination,Rectangle tile,TextureTileMapping& output) {
    if(source.width<=0 || source.height<=0 || tile.width<=0 || tile.height<=0) return false;
    const float left=std::max(source.x,tile.x),top=std::max(source.y,tile.y);
    const float right=std::min(source.x+source.width,tile.x+tile.width);
    const float bottom=std::min(source.y+source.height,tile.y+tile.height);
    if(right<=left || bottom<=top) return false;
    const float sx=destination.width/source.width,sy=destination.height/source.height;
    output={{left-tile.x,top-tile.y,right-left,bottom-top},
        {destination.x+(left-source.x)*sx,destination.y+(top-source.y)*sy,(right-left)*sx,(bottom-top)*sy}};
    return true;
}
}
