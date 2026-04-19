#pragma once

#include <string>

namespace vksumi
{
    // true once on the rising edge of every key in `combo` being pressed.
    // `combo` is "+"-separated X11 keysym names like "Shift_R+F9". lazily
    // opens the local X display, returns false if $DISPLAY isn't set or the
    // combo cant be parsed. cheap enough to call once per present.
    bool toggleKeyJustPressed(const std::string& combo);
}
