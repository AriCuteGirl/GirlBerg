#include "custom_ui_core.h"

namespace custom_ui {

namespace {
static std::uint32_t fnv1a_32(char const* text)
{
    std::uint32_t hash = 2166136261u;
    while (*text) {
        hash ^= static_cast<std::uint8_t>(*text++);
        hash *= 16777619u;
    }
    return hash;
}
} // namespace

UiContext::UiContext() :
    hot_id(0),
    active_id(0)
{
    input = {};
}

void UiContext::BeginFrame(InputState const& input_state)
{
    input = input_state;
    hot_id = 0;
}

void UiContext::EndFrame()
{
    if (!input.left_down && !input.left_pressed) {
        active_id = 0;
    }
}

bool UiContext::Button(std::uint32_t id, Rect const& rect, bool* hovered, bool* held)
{
    bool is_hovered = rect.Contains(input.mouse_x, input.mouse_y);
    if (is_hovered) {
        hot_id = id;
    }

    if (is_hovered && input.left_pressed) {
        active_id = id;
    }

    bool is_held = (active_id == id) && input.left_down;
    bool clicked = (active_id == id) && is_hovered && input.left_released;

    if (hovered) *hovered = is_hovered;
    if (held) *held = is_held;
    return clicked;
}

bool UiContext::DragRegion(std::uint32_t id, Rect const& rect, Vec2* out_delta)
{
    if (!out_delta) return false;
    out_delta->x = 0.0f;
    out_delta->y = 0.0f;

    bool hovered = rect.Contains(input.mouse_x, input.mouse_y);
    if (hovered) {
        hot_id = id;
    }

    if (hovered && input.left_pressed) {
        active_id = id;
    }

    if (active_id == id && input.left_down) {
        out_delta->x = input.mouse_dx;
        out_delta->y = input.mouse_dy;
        return (input.mouse_dx != 0.0f || input.mouse_dy != 0.0f);
    }

    return false;
}

std::uint32_t HashId(char const* text)
{
    return fnv1a_32(text);
}

} // namespace custom_ui

