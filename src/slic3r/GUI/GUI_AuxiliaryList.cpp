#include <wx/button.h>
#include "GUI_AuxiliaryList.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

#include <boost/filesystem.hpp>

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"

using namespace Slic3r::GUI;
using namespace Slic3r;

// [INTENT][UNITY] Provide a focused auxiliary tree controller so Unity can reproduce the tree + toolbar combination with consistent command wiring.

AuxiliaryList::AuxiliaryList(wxWindow* parent)
	: wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_NO_HEADER)
{
	// [INTENT] Manage the auxiliary file tree that lives alongside the Plater model and expose it through a sortable tree control.
	wxDataViewTextRenderer* tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn* column0 = new wxDataViewColumn("", tr, 0, 200, wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	this->AppendColumn(column0);

	m_auxiliary_model = new AuxiliaryModel();
	// [STATE] The AuxiliaryModel owns canonical auxiliary paths so toolbar/context actions operate on a shared tree.
	this->AssociateModel(m_auxiliary_model);
	// [THREAD] Construction runs on the main UI thread so the tree and toolbar wiring stay locked to a single scheduler for Unity's main loop replacement.
	m_sizer = new wxBoxSizer(wxVERTICAL);
	// [STATE][UNITY] Store the sizer so Unity's VisualElement layout can treat this tree + toolbar block as one unit.
	m_sizer->Add(this, 1, wxEXPAND | wxALL, 0);

	wxPanel* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(21)));
	// [INTENT] Host an inline toolbar row under the explorer so Add/Open/Delete stay visually tied to the tree.
	//panel->SetBackgroundColour(*wxLIGHT_GREY);

#if 0
	wxBitmap if_bitmap = create_scaled_bitmap("import_file.png", nullptr, FromDIP(21));
	wxBitmap nf_bitmap = create_scaled_bitmap("new_folder.png", nullptr, FromDIP(21));
	wxBitmap del_bitmap = create_scaled_bitmap("delete.png", nullptr, FromDIP(21));

	wxBitmapButton* m_if_btn = new wxBitmapButton(panel, wxID_OPEN, if_bitmap);
	wxBitmapButton* m_nf_btn = new wxBitmapButton(panel, wxID_NEW, nf_bitmap);
	wxBitmapButton* m_del_btn = new wxBitmapButton(panel, wxID_DELETE, del_bitmap);
#endif

	//m_nf_btn = new wxButton(panel, wxID_NEW, _L("New Folder"));
	m_if_btn = new wxButton(panel, wxID_ADD, _L("Import File"));
	m_of_btn = new wxButton(panel, wxID_OPEN, _("Open File"));
	m_del_btn = new wxButton(panel, wxID_DELETE, _L("Delete"));
	// [STATE][UNITY] Keep toolbar actions centralized so Unity buttons can map to the same commands on a shared controller.

	wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
	//hsizer->Add(m_nf_btn, 0, wxRIGHT, 5);
	hsizer->Add(m_if_btn, 0, wxLEFT | wxRIGHT, 5);
	hsizer->Add(m_of_btn, 0, wxLEFT | wxRIGHT, 5);
	hsizer->Add(m_del_btn, 0, wxLEFT | wxRIGHT, 5);
	panel->SetSizer(hsizer);

	m_sizer->Add(panel, 0, wxEXPAND | wxALL, 5);

	EnableDragSource(wxDF_UNICODETEXT);
	EnableDropTarget(wxDF_UNICODETEXT);
	// [EVENT][THREAD] Enable native drag/drop on the UI thread; Unity will need a DragAndDrop bridge that uses this signal for reordering.

	// Keyboard events
	Bind(wxEVT_CHAR, [this](wxKeyEvent& event) { this->handle_key_event(event); });
	// [THREAD] Key events run on the UI thread, so Unity's Input System should marshal them before this handler.

	// Button events
	//m_nf_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_create_folder, this, wxID_NEW);
	m_if_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_import_file, this, wxID_ADD);
	m_del_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_delete, this, wxID_DELETE);
	m_of_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		wxDataViewItem sel_item = this->GetSelection();
		AuxiliaryModelNode* sel = (AuxiliaryModelNode*)sel_item.GetID();
		if (sel != nullptr && !sel->IsContainer()) {
			wxLaunchDefaultApplication(sel->path, 0);
		}
		else {
			evt.Skip();
		}
	}, wxID_OPEN);
	// [EVENT][PORTING_HAZARD:P3][UNITY] Launching files uses platform APIs; Unity should call Application.OpenURL/Process.Start on the UI thread.

	// Dataview events
	this->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &AuxiliaryList::on_context_menu, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &AuxiliaryList::on_begin_drag, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &AuxiliaryList::on_drop_possible, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_DROP, &AuxiliaryList::on_drop, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &AuxiliaryList::on_editing_started, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &AuxiliaryList::on_editing_done, this);

	// Mouse events
	wxWindow* win = this->GetMainWindow();
	win->Bind(wxEVT_LEFT_DCLICK, &AuxiliaryList::on_left_dclick, this);

	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
		wxDataViewItem sel_item = event.GetItem();
		AuxiliaryModelNode* sel_node = (AuxiliaryModelNode*)sel_item.GetID();
		if (sel_node == nullptr)
			return;

		m_del_btn->Enable(!sel_node->IsContainer());
	});
    // [STATE][EVENT] Keep delete interactability in lock-step with selection so Unity UI buttons mirror the same state.
}

AuxiliaryList::~AuxiliaryList()
{
	// [INTENT] Quietly release the auxiliary tree before the list goes away so Unity can mirror the same deterministic teardown order.
	// [STATE][THREAD] Disassociate the model and clean it up on the UI thread so bound controls never reference freed data.
	this->AssociateModel(nullptr);
	delete m_auxiliary_model;
	// [UNITY] Unity should dispose of the matching tree controller and cached state so the selection/mode cleanup mirrors the wx teardown.
}

void AuxiliaryList::init_auxiliary()
{
	// [INTENT] Pull the Plater's auxiliary temp path into the view so the Unity tree can point at the identical source folder.
	Model& model = wxGetApp().plater()->model();
	std::string aux_path = encode_path(model.get_auxiliary_file_temp_path().c_str());
	m_auxiliary_model->Init(aux_path);
	// [STATE] Model initialization pulls from the Plater auxiliary temp path so Unity needs to mirror that shared path before exposing tree entries.
	// [PORTING_HAZARD:P3] wxGetApp() is a wxWidgets singleton that Unity lacks; inject the Plater/root controller so the tree still sees the auxiliary temp directory.
	// [UNITY] Expose the same temp directory (e.g., via a ScriptableObject path provider) before Unity's tree populates so both views share the same source.
	// [THREAD] This runs on the UI thread because it reads wxGetApp() state and updates the data view before the tree becomes visible.
}

void AuxiliaryList::reload(wxString aux_path)
{
	m_auxiliary_model->Reload(aux_path);

	wxDataViewItemArray items;
	m_auxiliary_model->GetChildren(wxDataViewItem(nullptr), items);
	for (wxDataViewItem item : items) {
		Expand(item);
	}
	// [STATE] Keep every node expanded right after reload so the UI reflects imports or deletions without manual expansion.
	// [UNITY] Mirror this refresh pattern in Unity by expanding the TreeView entries immediately so the list matches user expectations.
	// [THREAD] Expand/Select operations assume the UI thread because wxDataViewCtrl mutations are not thread-safe.
}

void AuxiliaryList::create_new_folder()
{
	// [INTENT][UNITY] Drive inline folder creation so Unity's controller can call this helper for toolbar/context "New Folder" actions.
	wxDataViewItem folder_item = m_auxiliary_model->CreateFolder(wxEmptyString);
	AuxiliaryModelNode* folder = (AuxiliaryModelNode*)folder_item.GetID();
	if (folder == nullptr)
		return;

	Select(folder_item);

	wxDataViewColumn* col = GetColumn(0);
	wxDataViewCellMode mode = col->GetRenderer()->GetMode();
	col->GetRenderer()->SetMode(wxDATAVIEW_CELL_EDITABLE);
	EditItem(folder_item, col);
	// [STATE] Selection moves to the new folder and the renderer enters edit mode so Unity can mirror inline rename preparation.
	col->GetRenderer()->SetMode(mode);
}

void AuxiliaryList::do_import_file(AuxiliaryModelNode* folder)
{
	if (folder == nullptr || !folder->IsContainer())
		return;

	wxString src_path;
	wxString dst_path;
	wxFileDialog dialog(this, _L("Choose files"), wxEmptyString, wxEmptyString,
		wxFileSelectorDefaultWildcardStr, wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
	if (dialog.ShowModal() == wxID_OK) {
		wxArrayString sel_paths;
		dialog.GetPaths(sel_paths);
		wxDataViewItemArray file_items = m_auxiliary_model->ImportFile(folder, sel_paths);
		if (!file_items.empty()) {
			wxDataViewItem file_item = file_items[0];
			AuxiliaryModelNode* file_node = (AuxiliaryModelNode*)file_item.GetID();
			if (file_node != nullptr) {
				if (!m_auxiliary_model->IsOrphan(file_item)) {
					Expand(wxDataViewItem(file_node->GetParent()));
				}
				Select(file_item);
				m_del_btn->Enable(true);
			}
		}
	}
	// [EVENT][THREAD][UNITY][STATE][PORTING_HAZARD:P2] File dialogs block the UI thread and hold the folder selection, so Unity should host
	// an async native picker while keeping the folder state stable.
}

void AuxiliaryList::on_create_folder(wxCommandEvent& evt)
{
	// [EVENT][UNITY] Toolbar/context buttons route here so Unity can reuse this entry point for the same verbs.
	create_new_folder();
}

void AuxiliaryList::on_import_file(wxCommandEvent& evt)
{
	wxDataViewItem sel_item = this->GetSelection();
	AuxiliaryModelNode* sel_node = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel_node == nullptr)
		return;

	AuxiliaryModelNode* folder_node = sel_node;
	if (!folder_node->IsContainer()) {
		wxDataViewItem folder_item = m_auxiliary_model->GetParent(sel_item);
		folder_node = (AuxiliaryModelNode*)folder_item.GetID();

		if (folder_node == nullptr)
			return;
	}

	do_import_file(folder_node);
	// [EVENT][STATE][UNITY] Centralize the import workflow so Unity's controller can reuse this entry point when wiring toolbar buttons
	// or menu actions and keep the selection-based folder target intact.
}

void AuxiliaryList::on_delete(wxCommandEvent& evt)
{
	m_auxiliary_model->Delete(this->GetSelection());
	// [THREAD][PORTING_HAZARD:P3] Deletion must stay on the UI thread because the shared model is not thread-safe; Unity should marshal the Delete command before updating the tree.
	// [EVENT][STATE][UNITY] Deletes always funnel through this helper so Unity can tie toolbar buttons and hotkeys to the same state change.
}

void AuxiliaryList::on_context_menu(wxDataViewEvent& evt)
{
	wxMenu* menu = new wxMenu();
	// [STATE][THREAD] Menu contents mirror the selected node type and must be built on the UI thread before the popup displays.
	// [INTENT][UNITY] Compose this context menu so Unity can offer the same verbs via its right-click overlay and re-use the existing handlers.
	wxDataViewItem item = evt.GetItem();
	AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
	if (node == nullptr) {
		append_menu_item(menu, wxID_ANY, _L("New Folder"), wxEmptyString,
			[this](wxCommandEvent&)
			{
				create_new_folder();
			});
	}
	else if (node->IsContainer()) {
		append_menu_item(menu, wxID_ANY, _L("Import File"), wxEmptyString,
			[this, node](wxCommandEvent&)
			{
				do_import_file(node);
			});
		append_menu_item(menu, wxID_ANY, _L("Delete"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				m_auxiliary_model->Delete(item);
			});
	}
	else {
		append_menu_item(menu, wxID_ANY, _L("Open"), wxEmptyString,
			[this, node](wxCommandEvent&)
			{
				wxLaunchDefaultApplication(node->path, 0);
			});
		append_menu_item(menu, wxID_ANY, _L("Delete"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				m_auxiliary_model->Delete(item);
			});
		append_menu_item(menu, wxID_ANY, _L("Rename"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				wxDataViewColumn* col = this->GetColumn(0);
				wxDataViewCellMode mode = col->GetRenderer()->GetMode();
				col->GetRenderer()->SetMode(wxDATAVIEW_CELL_EDITABLE);
				this->EditItem(item, col);
				col->GetRenderer()->SetMode(mode);
			});
	}

	PopupMenu(menu);
	// [EVENT][INTENT][UNITY][STATE][THREAD] Keep context menus aligned with toolbar verbs so Unity can reuse the same helper methods for right-click overlays (menu creation must stay on the UI thread).
}

void AuxiliaryList::on_begin_drag(wxDataViewEvent& evt)
{
	wxDataViewItem sel_item = evt.GetItem();
	AuxiliaryModelNode* sel = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel == nullptr || sel->IsContainer())
		return;

	m_dragged_item = sel_item;

	wxTextDataObject* obj = new wxTextDataObject;
	obj->SetText("Some text");
	evt.SetDataObject(obj);
	evt.SetDragFlags(wxDrag_DefaultMove);
	// [EVENT][STATE][THREAD][UNITY] Record the dragged item so the drop handler can resolve the source even if the mouse moves outside the tree and
	// so Unity's DragAndDrop layer can track the origin without race conditions.
}

void AuxiliaryList::on_drop_possible(wxDataViewEvent& evt)
{
	evt.Allow();
	// [EVENT][UNITY] Always allow drops so Unity can highlight targets prior to commits and keep the drop hint overlay in sync.
}

void AuxiliaryList::on_drop(wxDataViewEvent& evt)
{
	m_auxiliary_model->MoveItem(evt.GetItem(), m_dragged_item);

	Expand(evt.GetItem());
	Select(m_dragged_item);
	m_dragged_item = wxDataViewItem(nullptr);
	// [EVENT][STATE][THREAD][UNITY][PORTING_HAZARD:P2] Clear the drag sentinel once the move completes so future drops start fresh, avoid stale references, and teach Unity to reset its drag state before the next drop.
}

void AuxiliaryList::on_editing_started(wxDataViewEvent& evt)
{
	// [EVENT] Placeholder hook for edit-start notifications so Unity can show inline text fields when renaming begins.
}

void AuxiliaryList::on_editing_done(wxDataViewEvent& evt)
{
	bool is_done = m_auxiliary_model->Rename(evt.GetItem(), evt.GetValue().GetString());
	if (!is_done)
		evt.Veto();
	// [STATE][UNITY] Accept or veto rename results to keep the UI tree synchronized; Unity should surface validation feedback before closing a text field.
}

void AuxiliaryList::on_left_dclick(wxMouseEvent& evt)
{
	wxDataViewItem sel_item = this->GetSelection();
	AuxiliaryModelNode* sel = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel != nullptr && !sel->IsContainer()) {
		wxLaunchDefaultApplication(sel->path, 0);
	}
	else {
		evt.Skip();
	}
}
// [EVENT][STATE][UNITY][PORTING_HAZARD:P3] Double-click launching relies on native shells; Unity should use a cross-platform helper (Process.Start
// or Application.OpenURL) on the main thread and guard non-file containers.
// [PORTING_HAZARD:P2] Double-click launching must confirm the selected node still maps to a file before invoking Process.Start in Unity.
// [PORTING_HAZARD:P2] Guard stale path references so the launcher doesn't throw when selection data changes mid-drag.
void AuxiliaryList::handle_key_event(wxKeyEvent& evt)
{
	if (evt.GetKeyCode() == WXK_DELETE || evt.GetKeyCode() == WXK_BACK)
		m_auxiliary_model->Delete(this->GetSelection());
	// [EVENT][STATE][THREAD][UNITY] Keyboard delete/backspace mirrors the toolbar Delete button so Unity can wire the same hotkeys into this helper
	// via the Input System command map while staying on the UI thread.
