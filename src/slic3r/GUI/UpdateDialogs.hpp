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

    // Tells whether the user checked the "don't bother me again" checkbox
    bool disable_version_check() const;

    void on_hyperlink(wxHyperlinkEvent& evt);

private:
    // [STATE] Checkbox state
    // [UNITY] UnityEngine.UI.Toggle
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

    // force_before_wizard - indicates that check of updated is forced before ConfigWizard opening
    MsgUpdateConfig(const std::vector<Update>& updates, bool force_before_wizard = false);
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
    MsgNoUpdates();
    MsgNoUpdates(MsgNoUpdates&&)                 = delete;
    MsgNoUpdates(const MsgNoUpdates&)            = delete;
    MsgNoUpdates& operator=(MsgNoUpdates&&)      = delete;
    MsgNoUpdates& operator=(const MsgNoUpdates&) = delete;
    ~MsgNoUpdates();
};

}} // namespace Slic3r::GUI

#endif
