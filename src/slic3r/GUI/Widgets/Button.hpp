#ifndef slic3r_GUI_Button_hpp_
#define slic3r_GUI_Button_hpp_

#include "../wxExtensions.hpp"
#include "StaticBox.hpp"

// [INTENT] Centralized gap helpers keep dialog button spacing consistent across multiple layouts before Unity/SignalR skin definitions.
// [UNITY] Reimplement these numbers as part of a `ButtonStyle` ScriptableObject that feeds into GridLayout spacing rules.
class ButtonProps
{
public:
    static int ChoiceButtonGap() { return 10; };
    static int WindowButtonGap() { return 10; };
};

enum class ButtonStyle {
    Regular,
    Confirm,
    Alert,
    Disabled,
};

enum class ButtonType {
    Compact,   // Font10  FullyRounded  For spaces with less areas
    Window,    // Font12  FullyRounded  For regular buttons in windows and not related with parameter boxes
    Choice,    // Font14  Semi-Rounded  For dialog/window choice buttons
    Parameter, // Font14  Semi-Rounded  For buttons that near parameter boxes
    Expanded,  // Font14  Semi-Rounded  For full length buttons. ex. buttons in static box
};

class wxTipWindow;
class Button : public StaticBox
{
    // [STATE] caches for layout (_textSize/_padding) plus `ScalableBitmap` icon pairs track active vs. inactive imagery so `messureSize`
    // can recompute bounds.
    wxRect         textSize;
    wxSize         minSize; // set by outer
    wxSize         paddingSize;
    ScalableBitmap active_icon;
    ScalableBitmap inactive_icon;

    // [STATE] Color cache routed through `StateColor` so label tint respects hover/checked/disabled combinations before Unity color blocks
    // override them.
    StateColor text_color;

    // [STATE] Interaction flags to avoid redundant events (pressed/focus/align). [THREAD] Owned by UI thread via wxWidgets event loop.
    bool pressedDown = false;
    bool m_selected  = true;
    bool canFocus    = true;
    bool isCenter    = true;
    bool vertical    = false;

    // [STATE] Runtime tooltip window pointer so disabled hover text can be manually shown on Windows.
    wxTipWindow* tipWindow = nullptr;

    static const int buttonWidth  = 200;
    static const int buttonHeight = 50;

public:
    // [INTENT] Specialized StaticBox-derived button that batches layout, state, and event wiring for consistent multi-platform behaviour.
    // [UNITY] Replace with a MonoBehaviour-driven VisualElement or Canvas+Selectable pair that exposes styles, icons, and pointer events clearly.
    Button();

    Button(wxWindow* parent, wxString text, wxString icon = "", long style = 0, int iconSize = 0, wxWindowID btn_id = wxID_ANY);

    bool Create(wxWindow* parent, wxString text, wxString icon = "", long style = 0, int iconSize = 0, wxWindowID btn_id = wxID_ANY);

    void SetLabel(const wxString& label) override;

    bool SetFont(const wxFont& font) override;

    void SetIcon(const wxString& icon);

    void SetInactiveIcon(const wxString& icon);

    void SetMinSize(const wxSize& size) override;
    void SetMaxSize(const wxSize& size) override;

    void SetPaddingSize(const wxSize& size);

    // [INTENT] Style+type combos centralize DPI-aware padding/font/border overrides while tying into `StateColor` palettes shown in the
    // implementation. [PORTING_HAZARD:P2] Unity's Selectable + ColorBlock needs an explicit state machine to cover the bitmasked
    // hover/checked combos in the source.
    void SetStyle(const ButtonStyle style /*= ButtonStyle::Regular*/, const ButtonType type /*= ButtonType::None*/);

    void SetTextColor(StateColor const& color);

    void SetTextColorNormal(wxColor const& color);

    void SetSelected(bool selected = true) { m_selected = selected; }

    // [EVENT][STATE] `Enable` toggles wxEVT_ENABLE_CHANGED so parent listeners can update interactability; Unity equivalent flips
    // `interactable` + fires a UnityEvent.
    bool Enable(bool enable = true) override;
    // [EVENT] Tooltips are manually surfaced when disabled so hover events propagate even though the button cannot own them.
    void EnableTooltipEvenDisabled(); // The tip will be shown even if the button is disabled

    // [STATE] `canFocus` controls whether SetFocus will succeed; port to Unity by gating Selectable/Focus events with `Selectable.enabled`.
    void SetCanFocus(bool canFocus) override;

    // [STATE][UNITY] Mirrors a Selectable `isOn`/`isPressed` state; align with Unity toggles and feed into style updates.
    void SetValue(bool state);

    bool GetValue() const;

    void SetCenter(bool isCenter);

    void SetVertical(bool vertical = true);

    // [INTENT] Triggered when DPI or style assets change, so the button rescales icon bitmaps and reapplies cached styles before re-layout.
    void Rescale();

protected:
#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    bool AcceptsFocus() const override;

private:
    bool        m_has_style = false;
    ButtonStyle m_style;
    ButtonType  m_type;

    // [EVENT] paint handler entry point that keeps all drawing on the UI thread before dispatching to render.
    void paintEvent(wxPaintEvent& evt);

    // [OPENGL] Stateless draw helper invoked by paintEvent; Unity reimplementation should feed these layers into OnPopulateMesh or a Graphic.
    void render(wxDC& dc);

    // [STATE][UNITY] Recomputes cached text/icon bounds; Unity will replace it with LayoutElement preferred size queries backed by
    // TextGenerator/TMP.
    void messureSize();

    // some useful events
    // [EVENT] Mouse/keyboard callbacks keep the pressed state, capture, focus, and keyboard navigation tethered to the main thread.
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseCaptureLost(wxMouseCaptureLostEvent& event);
    void keyDownUp(wxKeyEvent& event);

    // [EVENT] Synthesizes wxEVT_COMMAND_BUTTON_CLICKED so parent handlers stay decoupled from the internal state machine.
    void sendButtonEvent();

    // parent motion
    // [EVENT] Parent motion hooks drive disabled tooltips, so the tooltip window stays centered even when the button cannot own hover events.
    void OnParentMotion(wxMouseEvent& event);
    void OnParentLeave(wxMouseEvent& event);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
