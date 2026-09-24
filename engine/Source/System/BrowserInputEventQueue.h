#pragma once
#include <SDL3/SDL.h>
#include <deque>
#include <set>
#include <tuple>
#include <string>
#include <utility>
namespace RTE {
// Preserve every button edge until a menu/simulation update consumes its snapshot.
class BrowserInputEventQueue {
public:
 void Push(const SDL_Event& event) {
  PendingEvent pending{event,{}};
  // SDL owns these pointers only until a subsequent event pump. Our queue may
  // retain events across many pumps while simulation waits to consume an edge.
  if(event.type==SDL_EVENT_TEXT_INPUT && event.text.text) pending.text=event.text.text;
  if(event.type==SDL_EVENT_TEXT_EDITING && event.edit.text) pending.text=event.edit.text;
  m_Pending.push_back(std::move(pending));
 }
 void Consumed() { m_Delivered.clear(); }
 bool Pop(SDL_Event& event) {
  if(m_Pending.empty()) return false;
  const auto& next=m_Pending.front().event;
  std::tuple<Uint32,Uint32,int> button;
  bool transition=true;
  switch(next.type) {
   case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: button={SDL_EVENT_KEY_DOWN,next.key.which,next.key.scancode};break;
   case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: button={SDL_EVENT_MOUSE_BUTTON_DOWN,next.button.which,next.button.button};break;
   case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: button={SDL_EVENT_GAMEPAD_BUTTON_DOWN,next.gbutton.which,next.gbutton.button};break;
   case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP: button={SDL_EVENT_JOYSTICK_BUTTON_DOWN,next.jbutton.which,next.jbutton.button};break;
   default: transition=false;break;
  }
  if(transition&&!m_Delivered.insert(button).second) return false;
  event=next;
  // Keep the delivered text alive until the next successful Pop. Consumers in
  // PollSDLEvents handle the event synchronously before requesting another one.
  m_DeliveredText=std::move(m_Pending.front().text);
  if(event.type==SDL_EVENT_TEXT_INPUT && event.text.text) event.text.text=m_DeliveredText.c_str();
  if(event.type==SDL_EVENT_TEXT_EDITING && event.edit.text) event.edit.text=m_DeliveredText.c_str();
  m_Pending.pop_front();return true;
 }
private:
 struct PendingEvent { SDL_Event event; std::string text; };
 std::deque<PendingEvent> m_Pending;
 std::string m_DeliveredText;
 std::set<std::tuple<Uint32,Uint32,int>> m_Delivered;
};
}
