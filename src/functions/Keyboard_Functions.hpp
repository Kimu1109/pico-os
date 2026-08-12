#pragma once

#include "widgets/interfaces/ITextInputTarget.hpp"

namespace KeyboardFunctions {
    void Setup();
    void RegisterInputTarget(ITextInputTarget *target);
    void UnregisterInputTarget(ITextInputTarget *target);
}