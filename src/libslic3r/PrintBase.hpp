// [INTENT] PrintBase.hpp — Abstract base layer for all print pipelines (FFF and SLA).
// Defines the cancellation mechanism, step-state machine, warning propagation, and
// status callback protocol shared between Print (FFF) and SLAPrint (SLA).
//
// [STATE] Two orthogonal state systems:
//   1. PrintStateBase / PrintState<> — per-step INVALID/STARTED/DONE finite state machine
//      with monotonically-increasing timestamps used to detect stale UI caches.
//   2. PrintBase::m_cancel_status — std::atomic<CancelStatus> signalling the worker
//      thread that it should abort via throw_if_canceled().
//
// [COUPLING] PrintBase owns the Model copy (m_model), the PlaceholderParser
//   (m_placeholder_parser), and the full merged DynamicPrintConfig (m_full_print_config).
//   PrintObjectBase holds a raw ModelObject* into m_model — pointer lifetime is
//   governed by the Print subclass (Print / SLAPrint).
//
// [CONCURRENCY] Two threads share this header's machinery:
//   - Worker thread  : calls set_started(), set_done(), throw_if_canceled(),
//                      active_step_add_warning() — all under m_state_mutex.
//   - UI thread      : calls apply(), invalidate_step*(), cancel() — also locks
//                      m_state_mutex before bumping timestamps.
//   m_cancel_status is atomic; all other mutable state must be guarded by m_state_mutex.
//
// [HAZARD-H816] PrintStateBase::g_last_timestamp is a raw static size_t (not atomic,
//   no mutex) shared across ALL Print and SLAPrint instances. Concurrent multi-plate
//   slicing (distinct Print objects on separate TBB threads) creates unsynchronized
//   read-modify-write races on this counter. The upstream FIXME comment (line 101–104)
//   acknowledges this. Refactoring must replace with an atomic or per-instance counter.

#ifndef slic3r_PrintBase_hpp_
#define slic3r_PrintBase_hpp_

#include "libslic3r.h"
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>

#include "ObjectID.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

// [INTENT] Typed error codes for structured exceptions that carry an object reference
// and optional parameter list alongside a human-readable message.
// Used by Print::validate() to return machine-readable reasons to the UI layer so it
// can display localized tooltips and highlight the offending object.
// [COUPLING] The UI layer must switch on StringExceptionType values to select the
// correct tooltip or dialog template — adding a new value here requires a matching
// UI handler, otherwise the default (NOT_DEFINED) tooltip will appear silently.
enum StringExceptionType {
    STRING_EXCEPT_NOT_DEFINED                     = 0,
    STRING_EXCEPT_FILAMENT_NOT_MATCH_BED_TYPE     = 1,
    STRING_EXCEPT_FILAMENTS_DIFFERENT_TEMP        = 2,
    STRING_EXCEPT_OBJECT_COLLISION_IN_SEQ_PRINT   = 3,
    STRING_EXCEPT_OBJECT_COLLISION_IN_LAYER_PRINT = 4,
    STRING_EXCEPT_LAYER_HEIGHT_EXCEEDS_LIMIT      = 5,
    STRING_EXCEPT_COUNT
};

// [INTENT] BBS: error with object
// Carrier struct for validation errors and warnings returned from Print::validate().
// Unifies a human-readable message, a typed code (StringExceptionType), an optional
// ObjectBase pointer for UI highlighting, an option key for the offending config entry,
// and variadic params for parameterized tooltip formatting.
// [COUPLING] Callers must not hold raw pointers past the lifetime of the Print object.
// [HAZARD-H817] 'object' is a raw non-owning pointer into the Print's internal
// PrintObject/ModelObject list. If the Print is cleared or apply() rebuilds the list
// while the UI holds a StringObjectException, the pointer dangles. Refactoring should
// replace with ObjectID and a lookup function.
struct StringObjectException
{
    std::string              string;
    ObjectBase const*        object = nullptr;
    std::string              opt_key;
    StringExceptionType      type; // warning type for tips
    bool                     is_warning = false;
    std::vector<std::string> params; // warning params for tips
};

// [INTENT] Thrown by throw_if_canceled() when the worker thread detects cancellation.
// Propagates up through TBB task bodies, Print::process(), and SLAPrint::process()
// to the top-level background processing loop, which catches it and sets the UI state.
// [COUPLING] Must NOT be caught inside individual slicing steps — it must escape to
// the outermost process() call. Any catch(...) block inside a step that does not
// re-throw will silently swallow the cancellation and stall the UI.
class CanceledException : public std::exception
{
public:
    const char* what() const throw() { return "Background processing has been canceled"; }
};

// [INTENT] Non-template base for the step-state machine. Owns the global timestamp
// counter and all value types (State enum, Warning struct, StateWithWarnings) that
// the template PrintState<> specialises over. Separated so non-template translation
// units can reference Warning/WarningLevel without knowing the StepType enum.
//
// [STATE] Three-value FSM per step: INVALID → STARTED → DONE → INVALID (cycle).
// Timestamps are monotonically increasing size_t values — the UI caches them to
// detect which steps changed since the last UI refresh.
//
// [MEMORY] StateWithWarnings holds a vector<Warning> per step. Warnings are
// deduplicated by message or message_id. Non-current warnings are pruned at set_done().
//
// [HAZARD-H816] g_last_timestamp: see file-level annotation.
class PrintStateBase
{
public:
    enum State {
        INVALID,
        STARTED,
        DONE,
    };

    enum class WarningLevel { NON_CRITICAL, CRITICAL };

    // [INTENT] Typed notification codes carried alongside status-update callbacks.
    // The UI switches on these values to display different notification widgets
    // (e.g. SlicingReplaceInitEmptyLayers shows a "replace empty layers" banner).
    // [COUPLING] Adding values here requires matching UI handling in
    // GUI_App / NotificationManager, otherwise the event is silently dropped.
    enum SlicingNotificationType {
        SlicingDefaultNotification = 0, // normal status update, called by set_status
        SlicingReplaceInitEmptyLayers,
        SlicingNeedSupportOn,
        SlicingEmptyGcodeLayers,
        SlicingGcodeOverlap
    };

    typedef size_t TimeStamp;

    // [INTENT] Bundles a step's FSM state with the timestamp when it last changed.
    // Callers snapshot this atomically (under m_state_mutex) and compare timestamps
    // to detect incremental invalidations without re-reading full state.
    struct StateWithTimeStamp
    {
        StateWithTimeStamp() : state(INVALID), timestamp(0) {}
        State     state;
        TimeStamp timestamp;
    };

    // [INTENT] A single deduplication entry in a step's warning list.
    // [STATE] 'current' is reset to false when the step is invalidated via
    // mark_warnings_non_current(). It is set back to true if the same warning is
    // re-emitted during the next STARTED phase. Non-current warnings are discarded
    // at set_done(). This lets the UI fade out stale warnings that were not reproduced.
    struct Warning
    {
        // Critical warnings will be displayed on G-code export in a modal dialog, so that the user cannot miss them.
        WarningLevel level;
        // If the warning is not current, then it is in an unknown state. It may or may not be valid.
        // A current warning will become non-current if its milestone gets invalidated.
        // A non-current warning will either become current or it will be removed at the end of a milestone.
        bool current;
        // Message to be shown to the user, UTF8, localized.
        std::string message;
        // If message_id == 0, then the message is expected to identify the warning uniquely.
        // Otherwise message_id identifies the message. For example, if the message contains a varying number, then
        // it cannot itself identify the message type.
        int message_id;
    };

    struct StateWithWarnings : public StateWithTimeStamp
    {
        void mark_warnings_non_current()
        {
            for (auto& w : warnings)
                w.current = false;
        }
        std::vector<Warning> warnings;
    };

protected:
    // FIXME last timestamp is shared between Print & SLAPrint,
    //  and if multiple Print or SLAPrint instances are executed in parallel, modification of g_last_timestamp
    //  is not synchronized!
    static size_t g_last_timestamp;
};

// [INTENT] Concrete step-state machine template instantiated over PrintStep (FFF)
// and PrintObjectStep (FFF per-object) or SLAPrintStep / SLAPrintObjectStep (SLA).
// Manages m_state[COUNT] array and the m_step_active cursor. All methods that
// modify m_state take an external std::mutex reference (PrintBase::m_state_mutex)
// so that the Print and all its PrintObjects share a single lock.
//
// [CONCURRENCY] Callers must hold m_state_mutex before calling any unguarded variant.
// The guarded variants (state_with_timestamp, set_started, set_done, active_step_add_warning)
// lock internally. invalidate / invalidate_multiple / invalidate_all are called with the
// mutex already held by the UI thread (via Print::apply()).
//
// [HAZARD-H818] set_started() does NOT reset m_step_active to -1 when
// throw_if_canceled() fires — the debug asserts checking this are explicitly commented
// out (see lines ~206-208). If throw_if_canceled() fires inside set_started() after
// m_step_active is set but before the worker enters the step body, m_step_active is
// stale. This is deemed acceptable because the worker thread will not call
// active_step_add_warning() after a throw, but refactoring must account for it.
//
// [HAZARD-H819] invalidate_multiple() sets ALL states to INVALID and bumps timestamps
// BEFORE calling cancel(). Any thread reading timestamps between the first state flip
// and the cancel() call will see INVALID state with a new timestamp — it may
// incorrectly infer that the step has not yet started when it has. This window is
// inherent to the design; refactoring should atomically swap state and fire cancel together.
template<class StepType, size_t COUNT> class PrintState : public PrintStateBase
{
public:
    PrintState() {}

    StateWithTimeStamp state_with_timestamp(StepType step, std::mutex& mtx) const
    {
        std::scoped_lock<std::mutex> lock(mtx);
        StateWithTimeStamp           state = m_state[step];
        return state;
    }

    StateWithWarnings state_with_warnings(StepType step, std::mutex& mtx) const
    {
        std::scoped_lock<std::mutex> lock(mtx);
        StateWithWarnings            state = m_state[step];
        return state;
    }

    bool is_started(StepType step, std::mutex& mtx) const { return this->state_with_timestamp(step, mtx).state == STARTED; }

    bool is_done(StepType step, std::mutex& mtx) const { return this->state_with_timestamp(step, mtx).state == DONE; }

    StateWithTimeStamp state_with_timestamp_unguarded(StepType step) const { return m_state[step]; }

    bool is_started_unguarded(StepType step) const { return this->state_with_timestamp_unguarded(step).state == STARTED; }

    bool is_done_unguarded(StepType step) const { return this->state_with_timestamp_unguarded(step).state == DONE; }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply() updates its state, which may
    // influence the processing step being entered.
    template<typename ThrowIfCanceled> bool set_started(StepType step, std::mutex& mtx, ThrowIfCanceled throw_if_canceled)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
#ifndef NDEBUG
// The following test is not necessarily valid after the background processing thread
// is stopped with throw_if_canceled(), as the CanceledException is not being catched
// by the Print or PrintObject to update m_step_active or m_state[...].state.
// This should not be a problem as long as the caller calls set_started() / set_done() /
// active_step_add_warning() consistently. From the robustness point of view it would be
// be better to catch CanceledException and do the updates. From the performance point of view,
// the current implementation is optimal.
//
//        assert(m_step_active == -1);
//        for (int i = 0; i < int(COUNT); ++ i)
//            assert(m_state[i].state != STARTED);
#endif // NDEBUG
        if (m_state[step].state == DONE)
            return false;
        PrintStateBase::StateWithWarnings& state = m_state[step];
        state.state                              = STARTED;
        state.timestamp                          = ++g_last_timestamp;
        state.mark_warnings_non_current();
        m_step_active = static_cast<int>(step);
        return true;
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // Return value:
    // 		Timestamp when this stepentered the DONE state.
    // 		bool indicates whether the UI has to update the slicing warnings of this step or not.
    template<typename ThrowIfCanceled>
    std::pair<TimeStamp, bool> set_done(StepType step, std::mutex& mtx, ThrowIfCanceled throw_if_canceled)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
        assert(m_state[step].state == STARTED);
        assert(m_step_active == static_cast<int>(step));
        PrintStateBase::StateWithWarnings& state = m_state[step];
        state.state                              = DONE;
        state.timestamp                          = ++g_last_timestamp;
        m_step_active                            = -1;
        // Remove all non-current warnings.
        auto it                = std::remove_if(state.warnings.begin(), state.warnings.end(), [](const auto& w) { return !w.current; });
        bool update_warning_ui = false;
        if (it != state.warnings.end()) {
            state.warnings.erase(it, state.warnings.end());
            update_warning_ui = true;
        }
        return std::make_pair(state.timestamp, update_warning_ui);
    }

    // Make the step invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case the step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback> bool invalidate(StepType step, CancelationCallback cancel)
    {
        bool invalidated = m_state[step].state != INVALID;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            PrintStateBase::StateWithWarnings& state = m_state[step];
            state.state                              = INVALID;
            state.timestamp                          = ++g_last_timestamp;
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify it.
            state.mark_warnings_non_current();
            m_step_active = -1;
        }
        return invalidated;
    }

    template<typename CancelationCallback, typename StepTypeIterator>
    bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end, CancelationCallback cancel)
    {
        bool invalidated = false;
        for (StepTypeIterator it = step_begin; it != step_end; ++it) {
            StateWithTimeStamp& state = m_state[*it];
            if (state.state != INVALID) {
                invalidated     = true;
                state.state     = INVALID;
                state.timestamp = ++g_last_timestamp;
            }
        }
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread to proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify the warnings.
            for (StepTypeIterator it = step_begin; it != step_end; ++it)
                m_state[*it].mark_warnings_non_current();
            m_step_active = -1;
        }
        return invalidated;
    }

    // Make all steps invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case any step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback> bool invalidate_all(CancelationCallback cancel)
    {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++i) {
            StateWithTimeStamp& state = m_state[i];
            if (state.state != INVALID) {
                invalidated     = true;
                state.state     = INVALID;
                state.timestamp = ++g_last_timestamp;
            }
        }
        if (invalidated) {
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to modify the warnings.
            for (size_t i = 0; i < COUNT; ++i)
                m_state[i].mark_warnings_non_current();
            m_step_active = -1;
        }
        return invalidated;
    }

    // Update list of warnings of the current milestone with a new warning.
    // The warning may already exist in the list, marked as current or not current.
    // If it already exists, mark it as current.
    // Return value:
    // 		Current milestone (StepType).
    // 		bool indicates whether the UI has to be updated or not.
    std::pair<StepType, bool> active_step_add_warning(PrintStateBase::WarningLevel warning_level,
                                                      const std::string&           message,
                                                      int                          message_id,
                                                      std::mutex&                  mtx)
    {
        std::scoped_lock<std::mutex> lock(mtx);
        assert(m_step_active != -1);
        StateWithWarnings& state = m_state[m_step_active];
        assert(state.state == STARTED);
        std::pair<StepType, bool> retval(static_cast<StepType>(m_step_active), true);
        // Does a warning of the same level and message or message_id exist already?
        auto it = (message_id == 0) ? std::find_if(state.warnings.begin(), state.warnings.end(),
                                                   [&message](const auto& w) { return w.message_id == 0 && w.message == message; }) :
                                      std::find_if(state.warnings.begin(), state.warnings.end(),
                                                   [message_id](const auto& w) { return w.message_id == message_id; });
        if (it == state.warnings.end())
            // No, create a new warning and update UI.
            state.warnings.emplace_back(PrintStateBase::Warning{warning_level, true, message, message_id});
        else if (it->message != message || it->level != warning_level) {
            // Yes, however it needs an update.
            it->message = message;
            it->level   = warning_level;
            it->current = true;
        } else if (it->current)
            // Yes, and it is current. Don't update UI.
            retval.second = false;
        else
            // Yes, but it is not current. Mark it as current.
            it->current = true;
        return retval;
    }

private:
    StateWithWarnings m_state[COUNT];
    // Active class StepType or -1 if none is active.
    // If the background processing is canceled, m_step_active may not be resetted
    // to -1, see the comment in this->set_started().
    int m_step_active = -1;
};

class PrintBase;

// [INTENT] Abstract base for per-object print representations (PrintObject for FFF,
// SLAPrintObject for SLA). Holds a raw pointer to the corresponding ModelObject inside
// PrintBase::m_model. Exposes static helpers so PrintObjectBaseWithState<> can reach
// PrintBase's protected mutex and cancel_callback without requiring full friendship.
// [COUPLING] m_model_object must remain valid for the lifetime of this PrintObjectBase.
// It is owned by the Model inside PrintBase::m_model; if PrintBase::clear() is called
// while a PrintObjectBase* is held externally, the pointer will dangle.
class PrintObjectBase : public ObjectBase
{
public:
    const ModelObject* model_object() const { return m_model_object; }
    ModelObject*       model_object() { return m_model_object; }

protected:
    PrintObjectBase(ModelObject* model_object) : m_model_object(model_object) {}
    virtual ~PrintObjectBase() {}
    // Declared here to allow access from PrintBase through friendship.
    static std::mutex&           state_mutex(PrintBase* print);
    static std::function<void()> cancel_callback(PrintBase* print);
    // Notify UI about a new warning of a milestone "step" on this PrintObjectBase.
    // The UI will be notified by calling a status callback registered on print.
    // If no status callback is registered, the message is printed to console.
    void status_update_warnings(PrintBase*                              print,
                                int                                     step,
                                PrintStateBase::WarningLevel            warning_level,
                                const std::string&                      message,
                                PrintStateBase::SlicingNotificationType message_id = PrintStateBase::SlicingDefaultNotification);
    void emptylayer_update_msg(PrintBase* print, int type, const std::string& message, bool overwrite);

    ModelObject* m_model_object;
};

// [INTENT] Capability object returned by PrintBase::make_try_cancel(). Wraps
// throw_if_canceled() so it can be passed as a callable to non-friend code (e.g.
// TBB parallel_for bodies, support generators) without exposing PrintBase internals.
// [COUPLING] Lifetime must not exceed the PrintBase that created it.
// [CONCURRENCY] operator() reads m_cancel_status atomically — safe from any thread.
class PrintTryCancel
{
public:
    // calls print.throw_if_canceled().
    void operator()();

private:
    friend PrintBase;
    PrintTryCancel() = delete;
    PrintTryCancel(const PrintBase* print) : m_print(print) {}
    const PrintBase* m_print;
};

/**
 * @brief Printing involves slicing and export of device dependent instructions.
 *
 * Every technology has a potentially different set of requirements for
 * slicing, support structures and output print instructions. The pipeline
 * however remains roughly the same:
 *      slice -> convert to instructions -> send to printer
 *
 * The PrintBase class will abstract this flow for different technologies.
 *
 * [INTENT] Owns the canonical Model copy, the merged DynamicPrintConfig, the
 * PlaceholderParser, the m_cancel_status atomic, the m_state_mutex, and the two
 * callbacks (m_status_callback, m_cancel_callback). All Print and SLAPrint instances
 * derive from PrintBase. The two callbacks decouple slicing logic from UI threading.
 *
 * [CONCURRENCY] m_cancel_status is std::atomic<CancelStatus> — correct for
 * multi-thread read/write. m_cancel_callback and m_status_callback are std::function
 * objects modified only from the UI thread before process() is called; they are safe
 * to read from the worker thread provided no UI thread modifies them concurrently.
 * m_state_mutex guards all step-state reads/writes.
 *
 * [STATE] Default constructor calls restart() which sets m_cancel_status =
 * NOT_CANCELED. m_cancel_callback is initialised to a no-op lambda — never nullptr.
 */
class PrintBase : public ObjectBase
{
public:
    PrintBase() : m_placeholder_parser(&m_full_print_config) { this->restart(); }
    inline virtual ~PrintBase() {}

    virtual PrinterTechnology technology() const noexcept = 0;

    // Reset the print status including the copy of the Model / ModelObject hierarchy.
    virtual void clear() = 0;
    // The Print is empty either after clear() or after apply() over an empty model,
    // or after apply() over a model, where no object is printable (all outside the print volume).
    virtual bool empty() const = 0;
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    virtual std::vector<ObjectID> print_object_ids() const = 0;

    // Validate the print, return empty string if valid, return error if process() cannot (or should not) be started.
    // BBS: add more paremeters to validate
    virtual StringObjectException validate(StringObjectException*                  warning           = nullptr,
                                           Polygons*                               collison_polygons = nullptr,
                                           std::vector<std::pair<Polygon, float>>* height_polygons   = nullptr) const
    {
        return {};
    }

    enum ApplyStatus {
        // No change after the Print::apply() call.
        APPLY_STATUS_UNCHANGED,
        // Some of the Print / PrintObject / PrintObjectInstance data was changed,
        // but no result was invalidated (only data influencing not yet calculated results were changed).
        APPLY_STATUS_CHANGED,
        // Some data was changed, which in turn invalidated already calculated steps.
        APPLY_STATUS_INVALIDATED,
    };
    virtual ApplyStatus apply(const Model& model, DynamicPrintConfig config) = 0;
    const Model&        model() const { return m_model; }

    // [INTENT] Limits process() to a subset of objects or steps for incremental preview.
    // set_task() must be called after apply() and before process(). finalize() reverts
    // these limits so subsequent apply()+process() cycles are not restricted.
    struct TaskParams
    {
        TaskParams() : single_model_object(0), single_model_instance_only(false), to_object_step(-1), to_print_step(-1) {}
        // If non-empty, limit the processing to this ModelObject.
        ObjectID single_model_object;
        // If set, only process single_model_object. Otherwise process everything, but single_model_object first.
        bool single_model_instance_only;
        // If non-negative, stop processing at the successive object step.
        int to_object_step;
        // If non-negative, stop processing at the successive print step.
        int to_print_step;
    };
    // After calling the apply() function, call set_task() to limit the task to be processed by process().
    virtual void set_task(const TaskParams& params) {}
    // Perform the calculation. This is the only method that is to be called at a worker thread.
    virtual void process(long long* time_cost_with_cache = nullptr, bool use_cache = false) = 0;
    virtual int  export_cached_data(const std::string& dir_path, bool with_space = false) { return 0; }
    virtual int  load_cached_data(const std::string& directory) { return 0; }
    // Clean up after process() finished, either with success, error or if canceled.
    // The adjustments on the Print / PrintObject data due to set_task() are to be reverted here.
    virtual void finalize() {}

    // [INTENT] Progress and warning carrier passed to m_status_callback. The FlagBits
    // field tells the UI which kind of update this is: pure progress text, scene reload,
    // or step-warning update. UPDATE_PRINT_STEP_WARNINGS and
    // UPDATE_PRINT_OBJECT_STEP_WARNINGS are mutually exclusive; warning_object_id
    // identifies the owning Print or PrintObject via ObjectID lookup.
    // [COUPLING] The UI callback must check flags before dereferencing warning_object_id,
    // because the object may have been destroyed by the time the callback runs if the
    // UI thread processes the callback asynchronously.
    struct SlicingStatus
    {
        SlicingStatus(int                                     percent,
                      const std::string&                      text,
                      unsigned int                            flags         = 0,
                      int                                     warning_step  = -1,
                      PrintStateBase::SlicingNotificationType msg_type      = PrintStateBase::SlicingDefaultNotification,
                      PrintStateBase::WarningLevel            warning_level = PrintStateBase::WarningLevel::NON_CRITICAL)
            : percent(percent), text(text), flags(flags), warning_step(warning_step), message_type(msg_type), warning_level(warning_level)
        {}
        SlicingStatus(const PrintBase&                        print,
                      int                                     warning_step,
                      const std::string&                      text,
                      PrintStateBase::SlicingNotificationType msg_type      = PrintStateBase::SlicingDefaultNotification,
                      PrintStateBase::WarningLevel            warning_level = PrintStateBase::WarningLevel::NON_CRITICAL)
            : flags(UPDATE_PRINT_STEP_WARNINGS)
            , warning_object_id(print.id())
            , text(text)
            , warning_step(warning_step)
            , message_type(msg_type)
            , warning_level(warning_level)
        {}
        SlicingStatus(const PrintObjectBase&                  print_object,
                      int                                     warning_step,
                      const std::string&                      text,
                      PrintStateBase::SlicingNotificationType msg_type      = PrintStateBase::SlicingDefaultNotification,
                      PrintStateBase::WarningLevel            warning_level = PrintStateBase::WarningLevel::NON_CRITICAL)
            : flags(UPDATE_PRINT_OBJECT_STEP_WARNINGS)
            , warning_object_id(print_object.id())
            , text(text)
            , warning_step(warning_step)
            , message_type(msg_type)
            , warning_level(warning_level)
        {}
        int         percent{-1};
        std::string text;
        // Bitmap of flags.
        enum FlagBits {
            DEFAULT                   = 0,
            RELOAD_SCENE              = 1 << 1,
            RELOAD_SLA_SUPPORT_POINTS = 1 << 2,
            RELOAD_SLA_PREVIEW        = 1 << 3,
            // UPDATE_PRINT_STEP_WARNINGS is mutually exclusive with UPDATE_PRINT_OBJECT_STEP_WARNINGS.
            UPDATE_PRINT_STEP_WARNINGS        = 1 << 4,
            UPDATE_PRINT_OBJECT_STEP_WARNINGS = 1 << 5
        };
        // Bitmap of FlagBits
        unsigned int flags;
        // set to an ObjectID of a Print or a PrintObject based on flags
        // (whether UPDATE_PRINT_STEP_WARNINGS or UPDATE_PRINT_OBJECT_STEP_WARNINGS is set).
        ObjectID warning_object_id;
        // For which Print or PrintObject step a new warning is being issued?
        int warning_step{-1};

        PrintStateBase::SlicingNotificationType message_type{PrintStateBase::SlicingDefaultNotification};
        PrintStateBase::WarningLevel            warning_level{PrintStateBase::WarningLevel::NON_CRITICAL};
    };
    typedef std::function<void(const SlicingStatus&)> status_callback_type;
    // Default status console print out in the form of percent => message.
    void set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void set_status_silent()
    {
        m_status_callback = [](const SlicingStatus&) {};
    }
    // Register a custom status callback.
    void set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void set_status(int percent, const std::string& message, unsigned int flags = SlicingStatus::DEFAULT, int warning_step = -1) const;

    typedef std::function<void()> cancel_callback_type;
    // Various methods will call this callback to stop the background processing (the Print::process() call)
    // in case a successive change of the Print / PrintObject / PrintRegion instances changed
    // the state of the finished or running calculations.
    void set_cancel_callback(cancel_callback_type cancel_callback) { m_cancel_callback = cancel_callback; }
    // Has the calculation been canceled?
    // [INTENT] Three-value cancellation signal. Distinguishes user-initiated cancel
    // (UI button/close) from internal cancel (triggered by PrintBase::invalidate_step*
    // when apply() detects a config change). Both cause throw_if_canceled() to throw,
    // but the UI layer can inspect which reason fired for post-cancel diagnostics.
    // [CONCURRENCY] m_cancel_status is std::atomic — read/write is always safe from
    // any thread. The acquire/release ordering on load/store ensures the cancellation
    // is visible to worker threads immediately after the store.
    enum CancelStatus {
        // No cancelation, background processing should run.
        NOT_CANCELED = 0,
        // Canceled by user from the user interface (user pressed the "Cancel" button or user closed the application).
        CANCELED_BY_USER = 1,
        // Canceled internally from Print::apply() through the Print/PrintObject::invalidate_step() or ::invalidate_all_steps().
        CANCELED_INTERNAL = 2
    };
    CancelStatus cancel_status() const { return m_cancel_status.load(std::memory_order_acquire); }
    // Has the calculation been canceled?
    bool canceled() const { return m_cancel_status.load(std::memory_order_acquire) != NOT_CANCELED; }
    // Cancel the running computation. Stop execution of all the background threads.
    void cancel() { m_cancel_status = CANCELED_BY_USER; }
    void cancel_internal() { m_cancel_status = CANCELED_INTERNAL; }
    // Cancel the running computation. Stop execution of all the background threads.
    void restart() { m_cancel_status = NOT_CANCELED; }
    // Returns true if the last step was finished with success.
    virtual bool finished() const = 0;

    const PlaceholderParser&  placeholder_parser() const { return m_placeholder_parser; }
    const DynamicPrintConfig& full_print_config() const { return m_full_print_config; }

    virtual std::string output_filename(const std::string& filename_base = std::string()) const = 0;
    // If the filename_base is set, it is used as the input for the template processing. In that case the path is expected to be the directory
    // (may be empty). If filename_set is empty, than the path may be a file or directory. If it is a file, then the macro will not be processed.
    std::string output_filepath(const std::string& path, const std::string& filename_base = std::string()) const;

    // BBS: get/set plate id
    int  get_plate_index() const { return m_plate_index; }
    void set_plate_index(int index) { m_plate_index = index; }
    bool get_no_check_flag() const { return m_no_check; }
    void set_no_check_flag(bool no_check) { m_no_check = no_check; }

    // SoftFever plate name
    std::string get_plate_name() const { return m_plate_name; }
    void        set_plate_name(const std::string& name) { m_plate_name = name; }

protected:
    friend class PrintObjectBase;
    friend class BackgroundSlicingProcess;

    std::mutex&           state_mutex() const { return m_state_mutex; }
    std::function<void()> cancel_callback() { return m_cancel_callback; }
    void                  call_cancel_callback() { m_cancel_callback(); }
    // Notify UI about a new warning of a milestone "step" on this PrintBase.
    // The UI will be notified by calling a status callback.
    // If no status callback is registered, the message is printed to console.
    void status_update_warnings(int                                     step,
                                PrintStateBase::WarningLevel            warning_level,
                                const std::string&                      message,
                                const PrintObjectBase*                  print_object = nullptr,
                                PrintStateBase::SlicingNotificationType message_id   = PrintStateBase::SlicingDefaultNotification);
    // BBS: add api to update printobject's warnings
    void status_update_warnings(int                                     step,
                                PrintStateBase::WarningLevel            warning_level,
                                const std::string&                      message,
                                PrintObjectBase&                        object,
                                PrintStateBase::SlicingNotificationType message_id = PrintStateBase::SlicingDefaultNotification);

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void throw_if_canceled() const
    {
        if (m_cancel_status.load(std::memory_order_acquire))
            throw CanceledException();
    }
    // Wrapper around this->throw_if_canceled(), so that throw_if_canceled() may be passed to a function without making throw_if_canceled() public.
    PrintTryCancel make_try_cancel() const { return PrintTryCancel(this); }

    // To be called by this->output_filename() with the format string pulled from the configuration layer.
    std::string output_filename(const std::string&   format,
                                const std::string&   default_ext,
                                const std::string&   filename_base,
                                const DynamicConfig* config_override = nullptr) const;
    // Update "scale", "input_filename", "input_filename_base" placeholders from the current printable ModelObjects.
    void update_object_placeholders(DynamicConfig& config, const std::string& default_ext) const;

    Model              m_model;
    DynamicPrintConfig m_full_print_config;
    DynamicPrintConfig m_ori_full_print_config; // original full print config without extruder applied
    PlaceholderParser  m_placeholder_parser;

    // BBS: add plate id into print base
    int  m_plate_index{0};
    bool m_no_check = false;

    // SoftFever: current plate name
    std::string m_plate_name;

    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type m_status_callback;

private:
    std::atomic<CancelStatus> m_cancel_status;

    // Callback to be evoked to stop the background processing before a state is updated.
    cancel_callback_type m_cancel_callback = []() {};

    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    mutable std::mutex m_state_mutex;

    friend PrintTryCancel;
};

// [INTENT] CRTP mixin that adds typed step-state management (PrintState<PrintStepEnum,
// COUNT>) to PrintBase. Concrete classes (Print, SLAPrint) inherit from this template.
// Provides typed set_started / set_done / invalidate_step* wrappers that forward to
// PrintState<> while supplying the shared m_state_mutex and cancel_callback.
// [COUPLING] PrintStepEnum must be a contiguous enum starting at 0 with COUNT values.
// Changing the enum ordering or count without recompiling all TUs will silently corrupt
// the m_state array (enum values are raw indices into m_state[COUNT]).
template<typename PrintStepEnum, const size_t COUNT> class PrintBaseWithState : public PrintBase
{
public:
    bool                               is_step_done(PrintStepEnum step) const { return m_state.is_done(step, this->state_mutex()); }
    PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintStepEnum step) const
    {
        return m_state.state_with_timestamp(step, this->state_mutex());
    }
    PrintStateBase::StateWithWarnings step_state_with_warnings(PrintStepEnum step) const
    {
        return m_state.state_with_warnings(step, this->state_mutex());
    }
    // Add a slicing warning to the active Print step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void active_step_add_warning(PrintStateBase::WarningLevel            warning_level,
                                 const std::string&                      message,
                                 PrintStateBase::SlicingNotificationType message_id = PrintStateBase::SlicingDefaultNotification)
    {
        std::pair<PrintStepEnum, bool> active_step = m_state.active_step_add_warning(warning_level, message, (int) message_id,
                                                                                     this->state_mutex());
        if (active_step.second)
            // Update UI.
            this->status_update_warnings(static_cast<int>(active_step.first), warning_level, message, nullptr, message_id);
    }

protected:
    bool set_started(PrintStepEnum step)
    {
        return m_state.set_started(step, this->state_mutex(), [this]() { this->throw_if_canceled(); });
    }
    PrintStateBase::TimeStamp set_done(PrintStepEnum step)
    {
        std::pair<PrintStateBase::TimeStamp, bool> status = m_state.set_done(step, this->state_mutex(),
                                                                             [this]() { this->throw_if_canceled(); });
        if (status.second)
            this->status_update_warnings(static_cast<int>(step), PrintStateBase::WarningLevel::NON_CRITICAL, std::string());
        return status.first;
    }
    bool invalidate_step(PrintStepEnum step) { return m_state.invalidate(step, this->cancel_callback()); }
    template<typename StepTypeIterator> bool invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end)
    {
        return m_state.invalidate_multiple(step_begin, step_end, this->cancel_callback());
    }
    bool invalidate_steps(std::initializer_list<PrintStepEnum> il)
    {
        return m_state.invalidate_multiple(il.begin(), il.end(), this->cancel_callback());
    }
    bool invalidate_all_steps() { return m_state.invalidate_all(this->cancel_callback()); }

    bool is_step_started_unguarded(PrintStepEnum step) const { return m_state.is_started_unguarded(step); }
    bool is_step_done_unguarded(PrintStepEnum step) const { return m_state.is_done_unguarded(step); }

private:
    PrintState<PrintStepEnum, COUNT> m_state;
};

// [INTENT] CRTP mixin that adds typed step-state management for per-object steps to
// PrintObjectBase. Concrete classes (PrintObject, SLAPrintObject) inherit from this.
// Provides typed set_started / set_done / invalidate_step* wrappers that delegate to
// the PrintState<> stored here, using the owning PrintType's shared mutex/cancel_callback.
// [COUPLING] PrintType must be a PrintBase subclass; m_print is a raw pointer held for
// the object's lifetime. PrintType::state_mutex() and cancel_callback() are called via
// the PrintObjectBase static helpers to avoid circular friend declarations.
// [CONCURRENCY] throw_if_canceled() here reads m_print->canceled() — a public atomic
// accessor; safe from any thread.
// [HAZARD-H818] Same m_step_active stale-on-cancel issue as PrintState::set_started().
template<typename PrintType, typename PrintObjectStepEnum, const size_t COUNT> class PrintObjectBaseWithState : public PrintObjectBase
{
public:
    PrintType*       print() { return m_print; }
    const PrintType* print() const { return m_print; }

    typedef PrintState<PrintObjectStepEnum, COUNT> PrintObjectState;
    bool is_step_done(PrintObjectStepEnum step) const { return m_state.is_done(step, PrintObjectBase::state_mutex(m_print)); }
    PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintObjectStepEnum step) const
    {
        return m_state.state_with_timestamp(step, PrintObjectBase::state_mutex(m_print));
    }
    PrintStateBase::StateWithWarnings step_state_with_warnings(PrintObjectStepEnum step) const
    {
        return m_state.state_with_warnings(step, PrintObjectBase::state_mutex(m_print));
    }

protected:
    PrintObjectBaseWithState(PrintType* print, ModelObject* model_object) : PrintObjectBase(model_object), m_print(print) {}

    bool set_started(PrintObjectStepEnum step)
    {
        return m_state.set_started(step, PrintObjectBase::state_mutex(m_print), [this]() { this->throw_if_canceled(); });
    }
    PrintStateBase::TimeStamp set_done(PrintObjectStepEnum step)
    {
        std::pair<PrintStateBase::TimeStamp, bool> status = m_state.set_done(step, PrintObjectBase::state_mutex(m_print),
                                                                             [this]() { this->throw_if_canceled(); });
        if (status.second)
            this->status_update_warnings(m_print, static_cast<int>(step), PrintStateBase::WarningLevel::NON_CRITICAL, std::string());
        return status.first;
    }

    bool invalidate_step(PrintObjectStepEnum step) { return m_state.invalidate(step, PrintObjectBase::cancel_callback(m_print)); }
    template<typename StepTypeIterator> bool invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end)
    {
        return m_state.invalidate_multiple(step_begin, step_end, PrintObjectBase::cancel_callback(m_print));
    }
    bool invalidate_steps(std::initializer_list<PrintObjectStepEnum> il)
    {
        return m_state.invalidate_multiple(il.begin(), il.end(), PrintObjectBase::cancel_callback(m_print));
    }
    bool invalidate_all_steps() { return m_state.invalidate_all(PrintObjectBase::cancel_callback(m_print)); }
    bool invalidate_all_steps_without_cancel()
    {
        return m_state.invalidate_all([]() {});
    }

    bool is_step_started_unguarded(PrintObjectStepEnum step) const { return m_state.is_started_unguarded(step); }
    bool is_step_done_unguarded(PrintObjectStepEnum step) const { return m_state.is_done_unguarded(step); }

    // Add a slicing warning to the active PrintObject step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void active_step_add_warning(PrintStateBase::WarningLevel            warning_level,
                                 const std::string&                      message,
                                 PrintStateBase::SlicingNotificationType message_id = PrintStateBase::SlicingDefaultNotification)
    {
        std::pair<PrintObjectStepEnum, bool> active_step = m_state.active_step_add_warning(warning_level, message, (int) message_id,
                                                                                           PrintObjectBase::state_mutex(m_print));
        if (active_step.second)
            this->status_update_warnings(m_print, static_cast<int>(active_step.first), warning_level, message, message_id);
    }

protected:
    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void throw_if_canceled()
    {
        if (m_print->canceled())
            throw CanceledException();
    }

    friend PrintType;
    PrintType* m_print;

private:
    PrintState<PrintObjectStepEnum, COUNT> m_state;
};

} // namespace Slic3r

#endif /* slic3r_PrintBase_hpp_ */
