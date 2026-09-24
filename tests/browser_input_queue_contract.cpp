#include "BrowserInputEventQueue.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace RTE;
static void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 BrowserInputEventQueue queue;SDL_Event down{},up{},out{};
 down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.which=1;down.button.button=SDL_BUTTON_LEFT;down.button.down=true;
 up=down;up.type=SDL_EVENT_MOUSE_BUTTON_UP;up.button.down=false;
 queue.Push(down);queue.Push(up);
 check(queue.Pop(out)&&out.button.down,"mouse press delivered");
 for(int frame=0;frame<10;++frame)check(!queue.Pop(out),"render-only polls cannot consume pending release");
 queue.Consumed();check(queue.Pop(out)&&!out.button.down,"simulation consumes press before release");
 queue.Push(down);check(!queue.Pop(out),"next press waits for release consumption");queue.Consumed();check(queue.Pop(out)&&out.button.down,"next click survives");queue.Consumed();
 down={};down.type=SDL_EVENT_KEY_DOWN;down.key.which=3;down.key.scancode=SDL_SCANCODE_RETURN;down.key.down=true;
 up=down;up.type=SDL_EVENT_KEY_UP;up.key.down=false;queue.Push(down);queue.Push(up);
 check(queue.Pop(out)&&out.key.down,"keyboard press");check(!queue.Pop(out),"keyboard release retained without update");queue.Consumed();check(queue.Pop(out)&&!out.key.down,"keyboard release");queue.Consumed();
 down={};down.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;down.gbutton.which=1;down.gbutton.button=2;down.gbutton.down=true;
 queue.Push(down);down.gbutton.which=2;queue.Push(down);
 check(queue.Pop(out)&&out.gbutton.which==1,"first controller");check(queue.Pop(out)&&out.gbutton.which==2,"separate controller not blocked");
 SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;queue.Push(motion);check(queue.Pop(out)&&out.type==SDL_EVENT_MOUSE_MOTION,"nonbutton events retain order");
 // SDL frees event text on a later event pump. Retained events must own it.
 queue.Consumed();
 down={};down.type=SDL_EVENT_KEY_DOWN;down.key.scancode=SDL_SCANCODE_A;down.key.down=true;
 up=down;up.type=SDL_EVENT_KEY_UP;up.key.down=false;
 queue.Push(down);queue.Push(up);
 char input[]="typed before next pump";
 SDL_Event text{};text.type=SDL_EVENT_TEXT_INPUT;text.text.text=input;queue.Push(text);
 check(queue.Pop(out)&&out.type==SDL_EVENT_KEY_DOWN,"text fixture press");
 check(!queue.Pop(out),"text is retained behind the pending release");
 std::memset(input,'x',sizeof(input)-1);
 queue.Consumed();check(queue.Pop(out)&&out.type==SDL_EVENT_KEY_UP,"text fixture release");
 check(queue.Pop(out)&&out.type==SDL_EVENT_TEXT_INPUT&&std::strcmp(out.text.text,"typed before next pump")==0,"queued text owns its original bytes after SDL storage changes");
 char editing[]="composition";text={};text.type=SDL_EVENT_TEXT_EDITING;text.edit.text=editing;text.edit.start=2;text.edit.length=3;queue.Push(text);
 editing[0]='X';
 check(queue.Pop(out)&&std::strcmp(out.edit.text,"composition")==0&&out.edit.start==2&&out.edit.length==3,"editing text and selection survive queue storage");
 std::puts("PASS browser input queue: press/release survive render-only frames, keyboard taps and independent devices");
}
