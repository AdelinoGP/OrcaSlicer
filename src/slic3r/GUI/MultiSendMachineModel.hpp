#ifndef slic3r_MultiSendMachineModel_hpp_
#define slic3r_MultiSendMachineModel_hpp_

#include "DeviceManager.hpp"
#include "wx/dataview.h"

namespace Slic3r { namespace GUI {

// [INTENT] MultiSendMachineModel serves as the data model for presenting machine information in a wxWidgets DataView control,
//          likely for a UI that allows sending print jobs to multiple machines. It manages the underlying data and
//          provides it to the wxDataView through the wxDataViewModel interface.
// [UNITY] This class would be refactored in Unity. The data it manages (machine objects) would become part of a
//          ScriptableObject or a dedicated C# data class. Its role as a data provider for a UI list would be
//          fulfilled by binding the C# data to a UI Toolkit ListView or a custom UI element managing a collection.
// [PORTING_HAZARD:P2] Direct inheritance from `wxDataViewModel` is a significant architectural dependency on wxWidgets.
//                     This will require a complete redesign of the data binding and presentation layer in Unity.
class MultiSendMachineModel : public wxDataViewModel
{
public:
    // [INTENT] Constructor for MultiSendMachineModel.
    MultiSendMachineModel();
    // [INTENT] Destructor for MultiSendMachineModel.
    ~MultiSendMachineModel();

    // [INTENT] Initializes the model, potentially fetching initial data or setting up internal states.
    void Init();

    // [INTENT] Adds a new machine object to the model.
    wxDataViewItem AddMachine(MachineObject* obj);

private:
};

}} // namespace Slic3r::GUI

#endif
