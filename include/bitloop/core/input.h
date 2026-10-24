#pragma once
#include <bitloop/core/types.h>

BL_BEGIN_NS

class Viewport;
class ProjectBase;
class CameraNavigator;
class CameraInfo;

enum struct MouseButton
{
    LEFT = 1,
    WHEEL = 1,
    RIGHT = 3,
    X1 = 4,
    X2 = 5,

    _COUNT = X2
};

struct MouseButtonState
{
    bool is_down = false;
    bool clicked = false;

    void clearStaleButtonStates()
    {
        clicked = false;
    }
};

class MouseInfo
{
    friend class ProjectBase;

    MouseButtonState button_states[(int)MouseButton::_COUNT];
    MouseButtonState& buttonState(MouseButton btn_type)
    {
        return button_states[(int)btn_type - 1];
    }

    void clearStaleButtonStates()
    {
        for (int i = 0; i < (int)MouseButton::_COUNT; i++)
            button_states[i].clearStaleButtonStates();
    }

public:

    Viewport* viewport = nullptr;

    f64  client_x = 0;
    f64  client_y = 0;
    f64  stage_x  = 0;
    f64  stage_y  = 0;
    int  scroll_delta = 0;

    [[nodiscard]] QDVec2 worldPosition(const CameraInfo& camera) const;

    const MouseButtonState& buttonState(MouseButton btn_type) const
    {
        return button_states[(int)btn_type-1];
    }

    bool buttonClicked(MouseButton btn_type) const
    {
        return buttonState(btn_type).clicked;
    }

    bool buttonDown(MouseButton btn_type) const
    {
        return buttonState(btn_type).is_down;
    }
};

class FingerInfo
{
    friend class CameraNavigator;
    friend class ProjectBase;

    Viewport* ctx_owner = nullptr;
    i64 fingerId;
    f64 x, y;
};

BL_END_NS
