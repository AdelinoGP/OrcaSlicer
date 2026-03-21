#ifndef slic3r_GUI_AuxiliaryList_hpp_
#define slic3r_GUI_AuxiliaryList_hpp_

#include <map>
#include <vector>
#include <set>

#include <wx/bitmap.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/file.h>
#include <wx/dir.h>

#include "AuxiliaryDataViewModel.hpp"

// [INTENT] Hosts the auxiliary assets tree, exposing import/delete buttons and a
// wxDataViewCtrl so the GUI can plumb user-selected models into slicing flows.
// [STATE] Mirrors the command/control state for currently selected nodes and
// drag-drop context so the downstream job layer can snapshot the selected
// auxiliary sets during preparations.
// [UNITY] Replace with a UI Toolkit VisualElement tree built around ListView
// + GraphicRaycaster, wiring ListView.selectionChanged to a MonoBehaviour
// controller that owns a ScriptableObject-backed AuxiliaryModel and buttons.
class AuxiliaryList : public wxDataViewCtrl
{
public:
	AuxiliaryList(wxWindow* parent);
	~AuxiliaryList();
	wxSizer* get_top_sizer() { return m_sizer; }

	// [INTENT] Creates the default button panel + tree model and populates the
	// view; keeps the UI-ready state in sync with the underlying AuxiliaryModel.
	void init_auxiliary();

	// [STATE] Reloads configuration from disk and swaps the data model, so
	// reload invocations should happen on the UI thread but may be scheduled via
	// async I/O tasks that marshal results back to the main thread.
	// [THREAD] File I/O is initiated from the UI but may push heavy reads to a
	// worker before assigning to `m_auxiliary_model`.
	void reload(wxString aux_path);

private:
	// [INTENT] Internal helper that adds the selected file(s) to the tree model
	// and refreshes the ListView selection state.
	void do_import_file(AuxiliaryModelNode* folder);

	// [EVENT] Button/menu callbacks, each running on the UI thread and firing
	// wxWidgets events that the controller relies on for user flow.
	void on_create_folder(wxCommandEvent& evt);
	void on_import_file(wxCommandEvent& evt);
	void on_delete(wxCommandEvent& evt);

	// [EVENT] Drag/drop interaction points; these operate entirely on the UI
	// thread, so porting to Unity requires hooking to DragAndDrop.AcceptDrag and
	// ListView.dragAndDrop events in the UI Toolkit bridge.
	void on_context_menu(wxDataViewEvent& evt);
	void on_begin_drag(wxDataViewEvent& evt);
	void on_drop_possible(wxDataViewEvent& evt);
	void on_drop(wxDataViewEvent& evt);
	void on_editing_started(wxDataViewEvent& evt);
	void on_editing_done(wxDataViewEvent& evt);
	void on_left_dclick(wxMouseEvent& evt);

	void create_new_folder();
	void handle_key_event(wxKeyEvent& evt);

	// [STATE] Tracks the item currently being dragged so drop targets can validate
	// the transfer and prevent invalid hierarchy updates.
	wxDataViewItem m_dragged_item;
	// [STATE] Owned AuxiliaryModel persists the folder tree and is swapped during
	// reload/refresh; Unity should mirror this with a ScriptableObject cache.
	AuxiliaryModel* m_auxiliary_model;
	// [STATE] Root sizer that the parent frame uses for layout; in Unity this
	// becomes the VisualElement container hosting the ListView + buttons.
	wxSizer* m_sizer;

	// wxButton* m_nf_btn;
	//  [STATE] Buttons tied to auxiliary commands; they are essentially command
	//  hooks that the UI controller binds to the Model state.
	wxButton* m_if_btn;
	wxButton* m_of_btn;
	wxButton* m_del_btn;
	// [PORTING_HAZARD:P3] wxWidgets handles per-control drag/drop + context menus
	// that Unity UI Toolkit does not duplicate exactly, so explicit input
	// bridging is required to replicate these callbacks.
};

#endif // slic3r_GUI_AuxiliaryList_hpp_
