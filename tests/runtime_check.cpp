#include <SDL3/SDL.h>
#include <cstdio>
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <fstream>
#include <sstream>
#include "BrowserShaderSource.h"

EM_ASYNC_JS(int, persistCheck, (), {
    if (typeof window === "undefined") return 0;
    const path = "/Userdata/.browser-runtime-check";
    let previous = 0;
    try { previous = Number(FS.readFile(path, {encoding:"utf8"})); } catch (error) {
        if (error.errno !== 44) throw error;
    }
    FS.writeFile(path, String(previous + 1));
    await new Promise((resolve, reject) => FS.syncfs(false, error => error ? reject(error) : resolve()));
    Module.print("Persistent browser save check: visit " + (previous + 1));
    return 0;
});
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int luaopen_bit(lua_State*);
}
int checkShaders() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    auto* window = SDL_CreateWindow("Cortex shader verification", 640, 480, SDL_WINDOW_OPENGL);
    if (!window) { std::puts(SDL_GetError()); return 4; }
    auto context = SDL_GL_CreateContext(window);
    if (!context) { std::puts(SDL_GetError()); return 5; }
    const char* names[] = {"Blit8.vert", "PostProcess.vert", "ScreenBlit.vert", "Background.frag", "Blit8.frag", "Dissolve.frag", "Flat.frag", "PostProcess.frag", "ScreenBlit.frag"};
    for (const char* name : names) {
        std::ifstream input(std::string("/shaders/") + name);
        if (!input) return 6;
        std::ostringstream stream; stream << input.rdbuf();
        auto source = RTE::BrowserShaderSource(stream.str());
        auto shader = glCreateShader(std::string(name).ends_with(".vert") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        const char* text = source.c_str(); glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
        GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[4096]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); std::printf("FAIL %s: %s\n", name, log); return 7; }
        std::printf("WebGL shader compiled: %s\n", name);
        glDeleteShader(shader);
    }
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
int main() {
    lua_State* state = luaL_newstate();
    if (!state) return 1;
    luaL_openlibs(state);
    luaopen_bit(state);
    lua_pop(state, 1);
    if (luaL_dostring(state, "assert(bit.tobit(4294967295)==-1); assert(bit.rol(1,31)==-2147483648); assert(bit.band(255,15)==15)")) return 8;
    if (luaL_dostring(state, "local t={}; for i=1,10000 do t[i]=i end; assert(t[10000]==10000); coroutine.wrap(function() coroutine.yield(42) end)()")) {
        std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
        lua_close(state);
        return 2;
    }
    lua_close(state);
    std::printf("Lua 5.1 interpreter works in WebAssembly; SDL %d linked.\n", SDL_GetVersion());
    if (emscripten_run_script_int("typeof window !== 'undefined'")) {
        const int graphics = checkShaders();
        if (graphics) return graphics;
    }
    return persistCheck();
}
