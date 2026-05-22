#ifndef __INCLUDED_OVERLAY_FRONTEND_H__
#define __INCLUDED_OVERLAY_FRONTEND_H__

#ifdef EMU_OVERLAY

#include <memory>

struct ImGuiIO;
class Steam_Overlay;

enum class OverlayFrontendKind
{
    ImGui = 0,
    CustomAero = 1,
};

class IOverlayFrontend
{
public:
    virtual ~IOverlayFrontend() {}
    virtual const char* Name() const = 0;
    virtual void Render(Steam_Overlay& overlay, ImGuiIO& io) = 0;
};

std::unique_ptr<IOverlayFrontend> CreateOverlayFrontend(OverlayFrontendKind kind);

#endif // EMU_OVERLAY

#endif // __INCLUDED_OVERLAY_FRONTEND_H__
