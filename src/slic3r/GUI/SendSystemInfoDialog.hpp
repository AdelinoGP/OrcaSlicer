#ifndef slic3r_SendSystemInfoDialog_hpp_
#define slic3r_SendSystemInfoDialog_hpp_

namespace Slic3r { namespace GUI {

// [INTENT] Entry point for the privacy-consent gate that decides whether the system-info dialog should appear.
// [UNITY] Model this as a modal controller entry method that can open a consent panel and then hand off to an async upload service.
// [PORTING_HAZARD:P2] The implementation snapshots machine details and may trigger network I/O, so the consent boundary must stay explicit.
void show_send_system_info_dialog_if_needed();

}} // namespace Slic3r::GUI

#endif // slic3r_SendSystemInfoDialog_hpp_
