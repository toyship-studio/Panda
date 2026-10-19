#include "Bamboo/Input.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

bool Input::isKeyPressed(Key key) {
    return Panda::ExternalCalls::input_IsKeyPressed((int)key);
}

bool Input::isKeyJustPressed(Key key) {
    return Panda::ExternalCalls::input_IsKeyJustPressed((int)key);
}

bool Input::isMouseButtonPressed(MouseButton mouseButton) {
    return Panda::ExternalCalls::input_IsMouseButtonPressed((int)mouseButton);
}

bool Input::isMouseButtonJustPressed(MouseButton mouseButton) {
    return Panda::ExternalCalls::input_IsMouseButtonJustPressed((int)mouseButton);
}

int Input::touchCount() {
    return Panda::ExternalCalls::input_GetTouchCount();
}

bool Input::tryGetTouch(int index, Touch &touch) {
    int id = 0;
    float x = 0.f;
    float y = 0.f;
    if (!Panda::ExternalCalls::input_GetTouch(index, &id, &x, &y)) { return false; }
    touch = Touch(id, x, y);
    return true;
}

Input::Touch Input::getTouch(int index) {
    Touch touch;
    tryGetTouch(index, touch);
    return touch;
}

double Input::getMouseDeltaX() {
    return Panda::ExternalCalls::input_GetMouseDeltaX();
}

double Input::getMouseDeltaY() {
    return Panda::ExternalCalls::input_GetMouseDeltaY();
}

} // namespace Bamboo
