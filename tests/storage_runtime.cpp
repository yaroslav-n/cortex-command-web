// The runtime tests/storage-race-check.html drives from JavaScript. crash() aborts it
// the way a crashing game aborts, to check what the journal still commits afterwards.
#include <cstdlib>
#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE void crash() { std::abort(); }

int main() { return 0; }
