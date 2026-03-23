#ifndef slic3r_GUI_AuxiliaryDataViewModel_hpp_
#define slic3r_GUI_AuxiliaryDataViewModel_hpp_

#include "wx/wxprec.h"
#include "wx/dataview.h"
#include "wx/hashmap.h"
#include "wx/vector.h"

#include "I18N.hpp"

#include <boost/filesystem.hpp>

class AuxiliaryModelNode;
WX_DEFINE_ARRAY_PTR(AuxiliaryModelNode*, AuxiliaryModelNodePtrArray);

namespace fs = boost::filesystem;
// [PORTING_HAZARD:P2] Synchronous `boost::filesystem` calls drive every folder/import/delete helper, so Unity needs to run these on
// background workers and marshal the results back to the main dispatcher.

// [INTENT][UNITY] Leaf and container nodes for the auxiliary tree that drive the `wxDataViewModel`; Unity should mirror this with a
// ScriptableObject tree exposed through a UI Toolkit TreeView/TreeViewController combo.
class AuxiliaryModelNode
{
public:
    AuxiliaryModelNode()
    {
        m_parent    = NULL;
        name        = "";
        m_container = true;
        m_root      = true;
    }
    // [STATE] Invisible root nodes keep `m_container` true and `m_root` true so the ListView always has a stable parent even when the user
    // empties the folder set.

    AuxiliaryModelNode(AuxiliaryModelNode* parent, const wxString& abs_path, bool is_container)
    {
        m_parent    = parent;
        m_container = is_container;
        m_root      = false;
        path        = abs_path;
        boost::filesystem::path path_obj(path.ToStdWstring());
        name = path_obj.filename().generic_wstring();

        parent->Append(this);
    }
    // [EVENT] Nodes attach themselves to their parent immediately so the DataViewModel sees folder/file changes in a single update; Unity
    // should register the ScriptableObject node with the TreeView data source here.

    ~AuxiliaryModelNode()
    {
        // free all our children nodes
        size_t count = m_children.GetCount();
        for (size_t i = 0; i < count; i++) {
            AuxiliaryModelNode* child = m_children[i];
            delete child;
        }
    }
    // [THREAD] Destruction walks the tree on the caller thread, so Unity should rely on managed objects and let the GC reclaim entries
    // instead of manual delete loops.

    bool IsContainer() const { return m_container; }

    AuxiliaryModelNode* GetParent() { return m_parent; }

    void SetParent(AuxiliaryModelNode* parent) { m_parent = parent; }

    AuxiliaryModelNodePtrArray& GetChildren() { return m_children; }
    AuxiliaryModelNode*         GetNthChild(unsigned int n) { return m_children.Item(n); }
    void                        Insert(AuxiliaryModelNode* child, unsigned int n) { m_children.Insert(child, n); }
    void                        Append(AuxiliaryModelNode* child) { m_children.Add(child); }
    unsigned int                GetChildCount() const { return m_children.GetCount(); }

public:
    wxString name;
    wxString path;

private:
    AuxiliaryModelNode*        m_parent;
    AuxiliaryModelNodePtrArray m_children;
    bool                       m_container;
    bool                       m_root;
};

// [INTENT][UNITY] Provides the wxDataViewModel backing for the Auxiliary tab so Unity ports can replace it with a UI Toolkit VisualElement
// tree/list driven by an ObservableCollection or ScriptableObject cache.
class AuxiliaryModel : public wxDataViewModel
{
public:
    AuxiliaryModel();
    ~AuxiliaryModel();

    // helper methods to change the model
    // [INTENT][EVENT][THREAD] CRUD helpers mutate the on-disk cache and guarantee `wxDataViewCtrl` gets the matching notifications; Unity
    // should wrap them in async tasks + dispatcher updates.
    wxDataViewItem      CreateFolder(wxString name = wxEmptyString);
    wxDataViewItemArray ImportFile(AuxiliaryModelNode* sel, wxArrayString file_paths);
    void                Delete(const wxDataViewItem& item);
    void                MoveItem(const wxDataViewItem& dropped_item, const wxDataViewItem& dragged_item);
    bool                IsOrphan(const wxDataViewItem& item);
    bool                Rename(const wxDataViewItem& item, const wxString& name);
    AuxiliaryModelNode* GetParent(AuxiliaryModelNode* node) const;
    void                Reparent(AuxiliaryModelNode* node, AuxiliaryModelNode* new_parent);

    void Init(wxString aux_path);
    // [STATE] `Init` seeds `m_root`/`m_root_dir` plus the default folders so the UI always has predictable folder nodes before user imports.
    void Reload(wxString aux_path);
    // [PORTING_HAZARD:P2] Reload blows away the directory directly via `fs::remove_all`, which blocks the UI; Unity needs to queue the
    // cleanup on a worker and notify the TreeView when the background pass finishes.

    // override sorting to always sort branches ascendingly

    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2, unsigned int column, bool ascending) const wxOVERRIDE;
    // [STATE] Always sort container rows alphabetically to keep folder groups predictable for copy/move commands.

    // implementation of base class virtuals to define model

    virtual unsigned int GetColumnCount() const wxOVERRIDE
    {
        // [STATE] The tree exposes one string column so the wxDataViewCtrl knows how to size and render the names.
        return 1;
    }

    virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE
    {
        // [STATE] Downstream code assumes a string column (folder/file names), which the Unity TreeView should mirror via string bindings.
        return "string";
    }

    virtual void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const wxOVERRIDE;
    // [STATE] `GetValue` reads the cached `name` from each node so VisualElements can display the label without re-scanning the disk.
    virtual bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) wxOVERRIDE;
    // [EVENT] Editing row labels updates the cached `name`; the Unity port should trigger the corresponding ScriptableObject edit and
    // dispatcher update.

    virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const wxOVERRIDE;

    virtual wxDataViewItem GetParent(const wxDataViewItem& item) const wxOVERRIDE;
    // [UNITY] Map parent queries to the TreeView data source so Unity knows which branch an item belongs to when handling context menu commands.
    virtual bool IsContainer(const wxDataViewItem& item) const wxOVERRIDE;
    // [STATE] Containers signal folders in the Unity TreeView so it can hide file icons while showing folder badges.
    virtual unsigned int GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const wxOVERRIDE;
    // [STATE] Controls how many children the TreeView requests per folder; Unity should cache counts the same way to keep virtualization consistent.

private:
    AuxiliaryModelNode* m_root;
    // [STATE] `m_root` references the invisible tree root owned by the model so `wxDataViewCtrl` always sees a valid parent even when
    // nothing is imported.
    wxString m_root_dir;
    // [STATE] Path to the temp auxiliary directory used for copy/import/delete; Unity copies should target `Application.temporaryCachePath`
    // or similar.
};

#endif // slic3r_GUI_AuxiliaryDataViewModel_hpp_
