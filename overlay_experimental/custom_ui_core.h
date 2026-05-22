#pragma once

#include <cstdint>

namespace custom_ui {

struct Vec2
{
    float x;
    float y;
};

struct Rect
{
    float x;
    float y;
    float w;
    float h;

    bool Contains(float px, float py) const
    {
        return px >= x && py >= y && px <= (x + w) && py <= (y + h);
    }
};

struct InputState
{
    float mouse_x;
    float mouse_y;
    float mouse_dx;
    float mouse_dy;
    bool left_down;
    bool left_pressed;
    bool left_released;
    bool left_double_clicked;
    bool right_pressed;
};

class UiContext
{
public:
    UiContext();

    void BeginFrame(InputState const& input_state);
    void EndFrame();

    bool Button(std::uint32_t id, Rect const& rect, bool* hovered = nullptr, bool* held = nullptr);
    bool DragRegion(std::uint32_t id, Rect const& rect, Vec2* out_delta);

private:
    InputState input;
    std::uint32_t hot_id;
    std::uint32_t active_id;
};

std::uint32_t HashId(char const* text);

} // namespace custom_ui

