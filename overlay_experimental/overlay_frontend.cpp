#include "overlay_frontend.h"

#ifdef EMU_OVERLAY

#include "steam_overlay.h"
#include <imgui.h>

class ImGuiOverlayFrontend final : public IOverlayFrontend
{
public:
    const char* Name() const override { return "ImGui"; }

    void Render(Steam_Overlay& overlay, ImGuiIO& io) override
    {
        overlay.RenderOverlayWindows(io);
    }
};

class CustomAeroOverlayFrontend final : public IOverlayFrontend
{
public:
    const char* Name() const override { return "CustomAero"; }

    void Render(Steam_Overlay& overlay, ImGuiIO& io) override
    {
        overlay.RenderCustomAeroOverlayWindows(io);
    }
};

std::unique_ptr<IOverlayFrontend> CreateOverlayFrontend(OverlayFrontendKind kind)
{
    switch (kind) {
        case OverlayFrontendKind::CustomAero:
            return std::unique_ptr<IOverlayFrontend>(new CustomAeroOverlayFrontend());
        case OverlayFrontendKind::ImGui:
        default:
            return std::unique_ptr<IOverlayFrontend>(new ImGuiOverlayFrontend());
    }
}

#endif // EMU_OVERLAY
