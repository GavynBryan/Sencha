#include "authoring/UiPreviewSession.h"

namespace
{
    constexpr std::string_view kThemeSheetName = "theme.rcss";
}

UiPreviewSession::UiPreviewSession(UiService& ui)
    : Ui(ui)
{
    SurfaceId = Ui.CreateSurface("preview", Size);
    // Offscreen from the first frame: the window feature must never draw a
    // document that is being previewed inside a panel.
    Ui.SetSurfaceDestination(SurfaceId, UiSurfaceDestination::Offscreen);
    Ui.SetSurfaceScale(SurfaceId, Scale);
    ApplyInputPolicy();
}

UiPreviewSession::~UiPreviewSession()
{
    Close();
    if (SurfaceId.IsValid())
        Ui.DestroySurface(SurfaceId);
}

bool UiPreviewSession::Open(std::string packagePath, UiPreviewModel model)
{
    Close();
    Package = std::move(packagePath);
    Current = std::move(model);
    Screen = Ui.OpenScreen(SurfaceId, Current.Describe(Package));
    if (!Screen.IsValid())
        return false;
    Current.Publish(Ui, Screen);
    return true;
}

bool UiPreviewSession::Reopen()
{
    if (Package.empty())
        return false;
    // The model is kept; only the screen is remade against it.
    UiPreviewModel model = Current;
    return Open(Package, std::move(model));
}

void UiPreviewSession::Close()
{
    if (Screen.IsValid())
    {
        Ui.CloseScreen(Screen);
        Screen = {};
    }
}

void UiPreviewSession::SetResolution(RenderExtent size)
{
    if (size.Width == 0 || size.Height == 0)
        return;
    Size = size;
    Ui.SetSurfaceSize(SurfaceId, Size);
}

void UiPreviewSession::SetDisplayScale(float scale)
{
    Scale = scale;
    Ui.SetSurfaceScale(SurfaceId, Scale);
}

void UiPreviewSession::SetPlacement(std::optional<Rect2d> windowRect)
{
    Ui.SetSurfacePlacement(SurfaceId, windowRect);
}

void UiPreviewSession::SetPointerMode(PointerMode mode)
{
    Pointer = mode;
    ApplyInputPolicy();
}

void UiPreviewSession::SetActivated(bool activated)
{
    Activated = activated;
    ApplyInputPolicy();
}

void UiPreviewSession::ApplyInputPolicy()
{
    UiSurfaceInputPolicy policy = UiSurfaceInputPolicy::Disabled;
    if (Pointer == PointerMode::Interact)
        policy = Activated ? UiSurfaceInputPolicy::Full : UiSurfaceInputPolicy::Pointer;
    Ui.SetSurfaceInputPolicy(SurfaceId, policy);
}

void UiPreviewSession::SetHostTheme(std::optional<std::string> themeRcss)
{
    Theme = std::move(themeRcss);
    // Empty text puts packages back on the copy they were cooked with.
    (void)Ui.SetHostStyleSheet(kThemeSheetName, Theme.value_or(std::string{}));
}

void UiPreviewSession::Poll()
{
    for (UiDiagnostic& d : Ui.DrainDiagnostics())
    {
        if (DiagnosticHistory.size() >= kDiagnosticHistory)
            DiagnosticHistory.pop_front();
        DiagnosticHistory.push_back(std::move(d));
    }
    if (!Screen.IsValid())
        return;
    for (UiAction& a : Ui.DrainActions(Screen))
    {
        if (ActionHistory.size() >= kActionHistory)
            ActionHistory.pop_front();
        ActionHistory.push_back(std::move(a));
    }
}
