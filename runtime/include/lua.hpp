#pragma once
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int luaopen_bit(lua_State*);
}
#define LUA_BITLIBNAME "bit"
