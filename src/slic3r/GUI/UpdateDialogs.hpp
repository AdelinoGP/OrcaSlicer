#ifndef slic3r_UpdateDialogs_hpp_
#define slic3r_UpdateDialogs_hpp_

#include <string>
#include <unordered_map>
#include <vector>
#include <wx/hyperlink.h>

#include "libslic3r/Semver.hpp"
#include "MsgDialog.hpp"

class wxBoxSizer;
class wxCheckBox;

namespace Slic3r { namespace GUI {

// [INTENT] A confirmation dialog listing application updates
// [UNITY] Use a modal Canvas/Window prefab with Text and Toggle components.
class MsgUpdateSlic3r : public MsgDialog
{
public:
    MsgUpdateSlic3r(const Semver& ver_current, const Semver& ver_online);
    MsgUpdateSlic3r(MsgUpdateSlic3r&&)                 = delete;
    MsgUpdateSlic3r(const MsgUpdateSlic3r&)            = delete;
    MsgUpdateSlic3r& operator=(MsgUpdateSlic3r&&)      = delete;
    MsgUpdateSlic3r& operator=(const MsgUpdateSlic3r&) = delete;
    virtual ~MsgUpdateSlic3r();

    // [INTENT] Report whether the user opted out of future version checks from this modal.
    bool disable_version_check() const;

    // [EVENT] Hyperlink click opens the release/update web target from inside the dialog.
    void on_hyperlink(wxHyperlinkEvent& evt);

private:
    // [STATE] Dialog-owned opt-out checkbox state; Unity should bind this to a Toggle in the same modal view.
    wxCheckBox* cbox;
};

// [INTENT] Confirmation dialog informing about configuration update. Lists updated bundles & their versions.
// [UNITY] Use a modal Canvas/Window prefab.
class MsgUpdateConfig : public DPIDialog
{
public:
    // [INTENT] Structure representing a single configuration update.
    struct Update
    {
        // [STATE] Snapshot of one updated bundle entry shown in the modal list.
        std::string vendor;
        Semver      version;
        std::string comment;
        // BBS: use changelog string instead of url
        std::string change_log;

        // BBS: use changelog string instead of url
        Update(std::string vendor, Semver version, std::string comment, std::string changelog)
            : vendor(std::move(vendor)), version(std::move(version)), comment(std::move(comment)), change_log(std::move(changelog))
        {}
    };

    // [STATE] Force-gate flag changes whether the dialog is a blocking pre-wizard step or a normal informational prompt.
    MsgUpdateConfig(const std::vector<Update>& updates, bool force_before_wizard = false);
    // [EVENT] DPI changes may resize/reflow the modal list and its header/footer controls.
    void on_dpi_changed(const wxRect& suggested_rect);
    // MsgUpdateConfig(MsgUpdateConfig &&)      = delete;
    // MsgUpdateConfig(const MsgUpdateConfig &) = delete;
    // MsgUpdateConfig &operator=(MsgUpdateConfig &&) = delete;
    // MsgUpdateConfig &operator=(const MsgUpdateConfig &) = delete;
    ~MsgUpdateConfig();
};

// [INTENT] Informs about currently installed bundles not being compatible with the running Slic3r. Asks about action.
// [UNITY] Modal alert prefab.
class MsgUpdateForced : public MsgDialog
{
public:
    // [INTENT] Update description
    struct Update
    {
        // [STATE] Same bundle snapshot shape as the regular update dialog, reused for compatibility gating.
        std::string vendor;
        Semver      version;
        std::string comment;
        // BBS: use changelog string instead of url
        std::string change_log;

        // BBS: use changelog string instead of url
        Update(std::string vendor, Semver version, std::string comment, std::string changelog)
            : vendor(std::move(vendor)), version(std::move(version)), comment(std::move(comment)), change_log(std::move(changelog))
        {}
    };

    MsgUpdateForced(const std::vector<Update>& updates);
    MsgUpdateForced(MsgUpdateForced&&)                 = delete;
    MsgUpdateForced(const MsgUpdateForced&)            = delete;
    MsgUpdateForced& operator=(MsgUpdateForced&&)      = delete;
    MsgUpdateForced& operator=(const MsgUpdateForced&) = delete;
    ~MsgUpdateForced();
};

// [INTENT] Informs about currently installed bundles not being compatible with the running Slic3r. Asks about action.
// [UNITY] Modal alert prefab.
class MsgDataIncompatible : public MsgDialog
{
public:
    // [INTENT] Map of "vendor name" -> "version restrictions"
    // [PORTING_HAZARD:P2] The map compresses multiple incompatibility causes into a single summary string; Unity should preserve per-vendor
    // detail rows rather than flattening early.
    MsgDataIncompatible(const std::unordered_map<std::string, wxString>& incompats);
    MsgDataIncompatible(MsgDataIncompatible&&)                 = delete;
    MsgDataIncompatible(const MsgDataIncompatible&)            = delete;
    MsgDataIncompatible& operator=(MsgDataIncompatible&&)      = delete;
    MsgDataIncompatible& operator=(const MsgDataIncompatible&) = delete;
    ~MsgDataIncompatible();
};

// Informs about absence of bundles requiring update.
class MsgNoUpdates : public MsgDialog
{
public:
    // [INTENT] Lightweight informational modal for the no-update path.
    // [UNITY] Reuse the same modal shell with a text-only content panel and standard dismiss button.
    MsgNoUpdates();
    MsgNoUpdates(MsgNoUpdates&&)                 = delete;
    MsgNoUpdates(const MsgNoUpdates&)            = delete;
    MsgNoUpdates& operator=(MsgNoUpdates&&)      = delete;
    MsgNoUpdates& operator=(const MsgNoUpdates&) = delete;
    ~MsgNoUpdates();
};

}} // namespace Slic3r::GUI

#endif
