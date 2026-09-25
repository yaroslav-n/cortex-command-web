// The real UInputMan and GUIInputWrapper: Return and keypad Enter share one logical
// GUI key, and text input behaves as the original's does — at most 32 bytes of any
// one text event, every byte kept (char is signed on Apple arm64 and in WebAssembly,
// so the original's "<= 127" test passes UTF-8 through), events concatenated.
#include "UInputMan.h"
#include "GUIInputWrapper.h"
#include "ActivityMan.h"
#include "PresetMan.h"
#include "TimerMan.h"
#include "WindowMan.h"
#include "CameraMan.h"
#include "PerformanceMan.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
using namespace RTE;
static void check(bool value, const char* reason) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); } }
namespace RTE {
class GUIInputKeyboardFixture {
public:
 static void Verify(UInputMan& input) {
  GUIInputWrapper wrapper(-1);unsigned char keys[256]{};
  auto event=[&](SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.which=0;e.key.scancode=code;e.key.down=down;input.HandleInputEvent(e);};
  auto update=[&](){wrapper.UpdateKeyboardInput(0.02f);wrapper.GetKeyboard(keys);input.EndFrame();return keys[GUIInput::Key_Enter];};
  event(SDL_SCANCODE_RETURN,true);check(update()==GUIInput::Pushed,"Return emits GUI Enter press without keypad cancellation");
  check(update()==GUIInput::None,"held Return does not emit a premature release");
  event(SDL_SCANCODE_KP_ENTER,true);check(update()==GUIInput::None,"second Enter key does not duplicate logical press");
  event(SDL_SCANCODE_RETURN,false);check(update()==GUIInput::None,"releasing Return while keypad is held retains logical key");
  event(SDL_SCANCODE_KP_ENTER,false);check(update()==GUIInput::Released,"last Enter key release emits logical release");
  event(SDL_SCANCODE_KP_ENTER,true);check(update()==GUIInput::Pushed,"keypad Enter emits GUI press");
  event(SDL_SCANCODE_KP_ENTER,false);check(update()==GUIInput::Released,"keypad Enter emits GUI release");
 }
};
} // namespace RTE
int main(int, char**) {
 try {
 TimerMan::Construct();WindowMan::Construct();ActivityMan::Construct();CameraMan::Construct();
 PresetMan::Construct();PerformanceMan::Construct();UInputMan::Construct();
 auto& manager=g_UInputMan;
 GUIInputKeyboardFixture::Verify(manager);
 const std::string pasted="print(\"A pasted console command must survive beyond thirty-two bytes\")";
 SDL_Event text{};text.type=SDL_EVENT_TEXT_INPUT;text.text.text=pasted.c_str();
 manager.HandleInputEvent(text);
 check(manager.GetTextInput()==pasted.substr(0,32),"one text event keeps at most 32 bytes, as the original");
 const std::string suffix=" -- next event";text.text.text=suffix.c_str();manager.HandleInputEvent(text);
 check(manager.GetTextInput()==pasted.substr(0,32)+suffix,"text events concatenate in delivery order");
 manager.EndFrame();check(!manager.HasTextInput(),"text expires after consuming frame");
 const std::string utf8="UTF8: \xE6\x97\xA5\xE6\x9C\xAC";text.text.text=utf8.c_str();manager.HandleInputEvent(text);
 check(manager.GetTextInput()==utf8,"UTF-8 bytes pass, as in the original on Apple arm64");
 text.text.text="";manager.HandleInputEvent(text);check(manager.GetTextInput()==utf8,"empty text event leaves pending input intact");
 manager.EndFrame();
 // Back from another tab or app, the page takes the pointer's motion again at once, wherever the last position
 // it knew was: in play the pointer is locked and moved relatively, so that position can be outside the window.
 // Mouse 1 is SDL's for the browser's mouse (a motion from mouse 0 counts twice in the combined mouse, as upstream's does).
 SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.which=1;motion.motion.x=-128;motion.motion.y=50;motion.motion.xrel=-4;
 manager.HandleInputEvent(motion);manager.EndFrame();
 manager.DisableMouseMoving(true);
 motion.motion.xrel=6;manager.HandleInputEvent(motion);
 check(manager.GetMouseMovement(-1).m_X==0,"motion is ignored while the page has lost the focus");
 manager.EndFrame();
 manager.DisableMouseMoving(false);
 motion.motion.xrel=8;manager.HandleInputEvent(motion);
 check(manager.GetMouseMovement(-1).m_X==8,"motion is taken again as soon as the page has the focus back, the last position outside the window");
 manager.EndFrame();
 std::puts("GUI input contract passed: Return and keypad Enter share one GUI key; text input matches the original (32 bytes per event, UTF-8 kept) and expires per frame; mouse motion is taken again when the page gets the focus back.");
 return 0;
 } catch (const std::exception& error) { std::fprintf(stderr,"GUI input contract exception: %s\n",error.what()); return 1; }
}
