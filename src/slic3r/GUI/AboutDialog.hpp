#ifndef slic3r_GUI_AboutDialog_hpp_
#define slic3r_GUI_AboutDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Custom panel to display the application logo.
class AboutDialogLogo : public wxPanel
{
public:
    AboutDialogLogo(wxWindow* parent);

private:
    // [STATE] Bitmap representing the application logo.
    // [UNITY] UnityEngine.UI.Image or UnityEngine.UI.RawImage
    ScalableBitmap logo;
    // [EVENT] Repaints the panel.
    void onRepaint(wxEvent& event);
};

// [INTENT] Dialog to show copyright information of the used libraries.
class CopyrightsDialog : public DPIDialog
{
public:
    CopyrightsDialog();
    ~CopyrightsDialog() {}

    struct Entry
    {
        Entry(const std::string& lib_name, const std::string& copyright, const std::string& link)
            : lib_name(lib_name), copyright(copyright), link(link)
        {}

        std::string lib_name;
        std::string copyright;
        std::string link;
    };

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [STATE] HTML component to display the copyright information.
    // [UNITY] Custom component or Unity UI Toolkit with HTML/Markdown rendering.
    // [PORTING_HAZARD:P1] wxHtmlWindow is not directly equivalent in Unity.
    wxHtmlWindow*      m_html;
    std::vector<Entry> m_entries;

    // [EVENT] Handles link clicks.
    void onLinkClicked(wxHtmlLinkEvent& event);
    // [EVENT] Handles dialog closure.
    void onCloseDialog(wxEvent&);

    void     fill_entries();
    wxString get_html_text();
};

// [INTENT] The main About dialog.
class AboutDialog : public DPIDialog
{
    // [STATE] Scalable bitmap for the logo.
    // [UNITY] UnityEngine.UI.Image or UnityEngine.UI.RawImage
    ScalableBitmap m_logo_bitmap;
    // [STATE] HTML component for the About information.
    // [PORTING_HAZARD:P1] wxHtmlWindow is not directly equivalent in Unity.
    wxHtmlWindow* m_html;
    // [STATE] Static bitmap for the logo.
    wxStaticBitmap* m_logo;
    int             m_copy_rights_btn_id{wxID_ANY};
    int             m_copy_version_btn_id{wxID_ANY};

public:
    AboutDialog();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [EVENT] Handles link clicks.
    void onLinkClicked(wxHtmlLinkEvent& event);
    // [EVENT] Handles dialog closure.
    void onCloseDialog(wxEvent&);
    // [EVENT] Handles copyright button click.
    void onCopyrightBtn(wxEvent&);
    // [EVENT] Handles copy to clipboard click.
    void onCopyToClipboard(wxEvent&);
};

class CopyrightsDialog : public DPIDialog
{
public:
    CopyrightsDialog();
    ~CopyrightsDialog() {}

    struct Entry
    {
        Entry(const std::string& lib_name, const std::string& copyright, const std::string& link)
            : lib_name(lib_name), copyright(copyright), link(link)
        {}

        std::string lib_name;
        std::string copyright;
        std::string link;
    };

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    wxHtmlWindow*      m_html;
    std::vector<Entry> m_entries;

    void onLinkClicked(wxHtmlLinkEvent& event);
    void onCloseDialog(wxEvent&);

    void     fill_entries();
    wxString get_html_text();
};

class AboutDialog : public DPIDialog
{
    ScalableBitmap  m_logo_bitmap;
    wxHtmlWindow*   m_html;
    wxStaticBitmap* m_logo;
    int             m_copy_rights_btn_id{wxID_ANY};
    int             m_copy_version_btn_id{wxID_ANY};

public:
    AboutDialog();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void onLinkClicked(wxHtmlLinkEvent& event);
    void onCloseDialog(wxEvent&);
    void onCopyrightBtn(wxEvent&);
    void onCopyToClipboard(wxEvent&);
};

}} // namespace Slic3r::GUI

#endif
