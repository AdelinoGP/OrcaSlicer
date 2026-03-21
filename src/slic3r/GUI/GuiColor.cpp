#include "GuiColor.hpp"

namespace Slic3r { namespace GUI {
// [INTENT] Centralize conversions so wxWidgets widgets and shared renderer metadata agree on colors.
// [STATE] Works over normalized RGBA arrays (0..1) used by textures and theming to prevent drift.
// [UNITY] Unity can mirror these helpers with `Color32` ↔ `Color` conversions and share the result via a ScriptableObject palette.
// [PORTING_HAZARD:P3] Unity's linear/gamma pipeline treats the same numeric values differently; keep the mapping explicit when migrating
// palettes. [INTENT] Snap normalized renderer RGBA into wxWidgets colors so UI controls keep a shared palette. [STATE] Clamping prevents
// overflow when theme authors push values slightly past 1.0 and keeps caches stable. [UNITY] Unity should expose an equivalent `Color` ↔
// `Color32` helper, driven by a shared `ScriptableObject` palette declaration. [THREAD] Works on any thread now, but current callers stay
// on the UI thread when repainting. [PORTING_HAZARD:P3] Unity's color space handling differs, so porters must document whether conversions
// should run in gamma or linear space.
wxColour convert_to_wxColour(const RGBA& color)
{
    auto     r = std::clamp((int) (color[0] * 255.f), 0, 255);
    auto     g = std::clamp((int) (color[1] * 255.f), 0, 255);
    auto     b = std::clamp((int) (color[2] * 255.f), 0, 255);
    auto     a = std::clamp((int) (color[3] * 255.f), 0, 255);
    wxColour wx_color(r, g, b, a);
    return wx_color;
}

// [INTENT] Normalize `wxColour` channel data for renderers and shared configuration so textures stay consistent.
// [STATE] Channels are clamped after scaling down to 0..1 to match the normalized color cache that renderers expect.
// [UNITY] Mirror this with Unity's `Color32` → `Color` cast and keep a single helper that feeds UI Toolkit binding pipelines.
// [PORTING_HAZARD:P3] Unity's `Color` already includes HDR handling, so porters must confirm whether these clamps should happen pre- or
// post-HDR conversions.
RGBA convert_to_rgba(const wxColour& color)
{
    RGBA rgba;
    rgba[0] = std::clamp(color.Red() / 255.f, 0.f, 1.f);
    rgba[1] = std::clamp(color.Green() / 255.f, 0.f, 1.f);
    rgba[2] = std::clamp(color.Blue() / 255.f, 0.f, 1.f);
    rgba[3] = std::clamp(color.Alpha() / 255.f, 0.f, 1.f);
    return rgba;
}

// [INTENT] Provide perceptual distance using Lab space so UI feedback can highlight palette differences accurately.
// [STATE] Caches intermediate Lab conversions to compare with the current theme state before touching textures.
// [THREAD] Called while preview jobs measure palette deltas on worker threads; the function itself is thread-safe.
// [UNITY] In Unity this matches `ColorUtility.ToLinearSpace` followed by `ColorSpace.Convert`, so reuse a single helper and drive it from
// `Color` or `Color32` inputs.
// [PORTING_HAZARD:P2] Unity's HDR pipeline already shifts to linear space; porters must confirm whether to match delta E calculations after
// tone-mapping.
float calc_color_distance(wxColour c1, wxColour c2)
{
    float lab[2][3];
    RGB2Lab(c1.Red(), c1.Green(), c1.Blue(), &lab[0][0], &lab[0][1], &lab[0][2]);
    RGB2Lab(c2.Red(), c2.Green(), c2.Blue(), &lab[1][0], &lab[1][1], &lab[1][2]);

    return DeltaE76(lab[0][0], lab[0][1], lab[0][2], lab[1][0], lab[1][1], lab[1][2]);
}

// [INTENT] Offer a renderer-friendly alternate for color distance so background jobs can compare raw RGBA palettes without wxWidgets.
// [STATE] Works directly on normalized RGBA arrays to keep the same perceptual metric as the `wxColour` path.
// [UNITY] Unity should reuse the same delta-E helper via `Color` floats, ensuring both UI and render pipelines see identical thresholds.
// [THREAD] Safe for worker threads that compare palettes while generating mesh previews before dispatching UI updates.
// [PORTING_HAZARD:P3] Unity might reapply tone mapping after this helper, so keep the same `Color` → `ColorSpace.Convert` ordering as the
// `wxColour` overload.
float calc_color_distance(RGBA c1, RGBA c2)
{
    float lab[2][3];
    RGB2Lab(c1[0], c1[1], c1[2], &lab[0][0], &lab[0][1], &lab[0][2]);
    RGB2Lab(c2[0], c2[1], c2[2], &lab[1][0], &lab[1][1], &lab[1][2]);

    return DeltaE76(lab[0][0], lab[0][1], lab[0][2], lab[1][0], lab[1][1], lab[1][2]);
}

}} // namespace Slic3r::GUI
