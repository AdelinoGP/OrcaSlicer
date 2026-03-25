#ifndef slic3r_BonjourDialog_hpp_
#define slic3r_BonjourDialog_hpp_

#include <cstddef>
#include <memory>

#include <boost/asio/ip/address.hpp>

#include <wx/dialog.h>
#include <wx/string.h>

#include "libslic3r/PrintConfig.hpp"

class wxListView;
class wxStaticText;
class wxTimer;
class wxTimerEvent;
class address;

namespace Slic3r {

// [INTENT] Forward declarations for Bonjour network discovery classes.
class Bonjour;
class BonjourReplyEvent;
class ReplySet;

// [INTENT] BonjourDialog performs network discovery using Bonjour (mDNS) to find printers.
// [INTENT] It displays a list of discovered printers and allows user selection.
// [STATE] list: wxListView displaying discovered printers.
// [STATE] replies: unique_ptr to ReplySet holding discovery results.
// [STATE] label: wxStaticText for status messages.
// [STATE] bonjour: shared_ptr to Bonjour network discovery engine.
// [STATE] timer: wxTimer for periodic lookup updates.
// [STATE] timer_state: internal timer state.
// [STATE] tech: PrinterTechnology for filtering discovery.
// [EVENT] on_reply: virtual handler for Bonjour reply events.
// [EVENT] on_timer: handler for timer events.
// [EVENT] on_timer_process: internal timer processing.
// [UNITY] Replace with UI Toolkit VisualElement dialog with ListView for printer list.
// [UNITY] Use C# async/await or Unity Coroutine for network discovery (Bonjour -> UDP multicast).
// [UNITY] Map wxListView to UI Toolkit ListView with custom item template.
// [UNITY] Map wxTimer to MonoBehaviour.StartCoroutine or Update loop.
// [PORTING_HAZARD:P2] Bonjour networking requires platform-specific implementation (Zeroconf on Windows, Bonjour on macOS).
// [PORTING_HAZARD:P3] wxDialog modal loop must be replaced with Unity's coroutine-based modal dialog.
class BonjourDialog : public wxDialog
{
public:
    BonjourDialog(wxWindow* parent, Slic3r::PrinterTechnology);
    BonjourDialog(BonjourDialog&&)                 = delete;
    BonjourDialog(const BonjourDialog&)            = delete;
    BonjourDialog& operator=(BonjourDialog&&)      = delete;
    BonjourDialog& operator=(const BonjourDialog&) = delete;
    ~BonjourDialog();

    // [INTENT] Show dialog and start Bonjour lookup, returning true if user selects a printer.
    bool show_and_lookup();
    // [INTENT] Return selected printer name.
    wxString get_selected() const;

private:
    wxListView*               list;
    std::unique_ptr<ReplySet> replies;
    wxStaticText*             label;
    std::shared_ptr<Bonjour>  bonjour;
    std::unique_ptr<wxTimer>  timer;
    unsigned                  timer_state;
    Slic3r::PrinterTechnology tech;

    virtual void on_reply(BonjourReplyEvent&);
    void         on_timer(wxTimerEvent&);
    void         on_timer_process();
};

// [INTENT] IPListDialog presents a list of IP addresses for a hostname, allowing user selection.
// [INTENT] Used when a hostname resolves to multiple IP addresses.
// [STATE] m_list: wxListView displaying IP addresses.
// [STATE] m_selected_index: reference to selected index.
// [UNITY] Replace with UI Toolkit VisualElement dialog with ListView.
// [UNITY] Map std::vector<boost::asio::ip::address> to string list.
// [PORTING_HAZARD:P3] Boost Asio dependency for IP address representation.
class IPListDialog : public wxDialog
{
public:
    IPListDialog(wxWindow* parent, const wxString& hostname, const std::vector<boost::asio::ip::address>& ips, size_t& selected_index);
    IPListDialog(IPListDialog&&)                 = delete;
    IPListDialog(const IPListDialog&)            = delete;
    IPListDialog& operator=(IPListDialog&&)      = delete;
    IPListDialog& operator=(const IPListDialog&) = delete;
    ~IPListDialog();

    // [EVENT] Override EndModal to capture selection before closing.
    virtual void EndModal(int retCode) wxOVERRIDE;

private:
    wxListView* m_list;
    size_t&     m_selected_index;
};

} // namespace Slic3r

#endif
