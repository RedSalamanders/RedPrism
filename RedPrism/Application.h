#pragma once

#include "BrushCompute.h"
#include "CanvasRenderer.h"
#include "CanvasSize.h"
#include "CanvasViewport.h"
#include "ChromeEvents.h"
#include "ColorEditWork.h"
#include "CommandBindings.h"
#include "ContinuationRequests.h"
#include "Crop.h"
#include "Document.h"
#include "EditorTool.h"
#include "GpuWork.h"
#include "HistoryWork.h"
#include "ImageImportWork.h"
#include "JobActivity.h"
#include "Jobs.h"
#include "PathClassificationWork.h"
#include "PreviewMailbox.h"
#include "ProjectStore.h"
#include "Prompts.h"
#include "RasterEditWork.h"
#include "Renderer.h"
#include "SettingsStore.h"
#include "TransformEdit.h"
#include "TransformOverlay.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;
struct UiBenchmarkState;
struct ColorWorkTestGate;
struct UtilityWorkTestGate;
struct SizingWorkTestGate;
struct HistoryRestoreTestGate;
struct FileWorkTestGate;
struct ProjectSaveTestGate;
struct ProjectOpenTestGate;
struct ImageImportTestGate;
struct RasterEditTestGate;
struct GradientWorkTestGate;
struct PixelMoveWorkTestGate;
struct ExportWorkTestGate;
struct TransformInspectorState;
enum class SizeCardKind : std::uint8_t;
namespace DxUi
{
struct PointerEvent;
}

// Application*.cpp self-test diagnostics (Application.cpp): prints to the debugger and, when the parent redirected
// it, to stderr.
void SelfTestFailure(const wchar_t* message) noexcept;
inline void SelfTestFailure(const std::wstring& message) noexcept
{
    SelfTestFailure(message.c_str());
}

// A solid premultiplied BGRA raster, and a painted layer added on top as one Import Image history entry then made
// active (Application.cpp): shared by the self-tests that need a real pixel layer to act on.
[[nodiscard]] Raster SolidRaster(int width, int height, std::uint32_t argb);
Guid InsertPaintedLayer(DocumentSession& session, const wchar_t* name, int width, int height, Vec2 origin,
                        double rotation, std::uint32_t argb);

// The chrome's events (ChromeEvents) land on the handlers below, marked `override`.
class Application final : public ChromeEvents
{
  public:
    Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath, uint32_t screenshotDpi,
                const wchar_t* demo = nullptr, const wchar_t* bench = nullptr) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int Run(int showCommand) noexcept;
    int RunSelfTest() noexcept;
    // `--bench brush`: the legacy 4K stroke benchmark on this device; prints BRUSH BENCH lines to stdout.
    int RunBenchmark() noexcept;
    [[nodiscard]] HRESULT InitializeProcessing() noexcept;
    [[nodiscard]] bool OpenCommandLinePaths(std::span<const std::wstring_view> paths) noexcept;
    void Shutdown() noexcept;
    // Settings (Specs/Core/Core_Settings.md): the file to read, watch and write; empty for a run without one.
    void UseSettings(std::wstring path, const AppSettings& settings) noexcept;

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedPrism.Window";
    static constexpr UINT_PTR kScreenshotTimerId = 1;
    static constexpr UINT_PTR kJobIndicatorTimerId = 8;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static void RequestFrameThunk(void* context) noexcept;
    static void DispatchMessageThunk(void* context, void* message) noexcept;
    int RunJobShutdownSelfTest() noexcept;
    static void JobNoticeThunk(void* context, JobNotice notice) noexcept;
    void SyncJobActivity(std::uint64_t nowQpc) noexcept;
    void OnJobIndicatorTimer() noexcept;
    int RunJobActivitySelfTest() noexcept;
    int RunUiBenchmark() noexcept;
    HRESULT InitializeUiBenchmark() noexcept;
    void AdvanceUiBenchmark() noexcept;
    void ExecuteUiBenchmarkStep() noexcept;
    void PrepareUiBenchmarkStep() noexcept;
    void FailUiBenchmark(const char* reason) noexcept;
    static constexpr UINT kUiBenchmarkMessage = WM_APP + 10;
    static constexpr UINT_PTR kUiBenchmarkTimerId = 9;

    HRESULT RegisterWindowClass() noexcept;
    HRESULT CreateMainWindow(bool visible) noexcept;
    HRESULT RegisterDisplayPowerNotification(HWND window) noexcept;
    HRESULT InitializeRenderer() noexcept;
    HRESULT InitializeChrome() noexcept;
    HRESULT InitializeDocument() noexcept;
    // ApplicationSettings.cpp: the settings file, its event-driven reload on the UI thread, the theme and the C14
    // gates.
    void ApplySettings(const AppSettings& settings) noexcept;
    void RefreshTheme() noexcept;
    bool StartSettingsWatch() noexcept;
    void StopSettingsWatch() noexcept;
    void NoteSettingsFileState() noexcept;
    void OnSettingsDirectoryChanged(bool immediate) noexcept;
    bool ReloadSettingsIfChanged() noexcept;
    void SaveSettings() noexcept;
    [[nodiscard]] HANDLE SettingsWatchHandle() const noexcept;
    int RunProductHardeningSelfTest() noexcept;
    static constexpr UINT_PTR kSettingsTimerId = 7;
    // The chrome mirrors the session (RF-1001): SyncChrome after anything that may have changed it, each region
    // writing only what changed; SyncView after a pan or a zoom, which changes only the status bar's zoom and what the
    // overlay draws over the canvas.
    void SyncChrome() noexcept;
    void SyncView() noexcept;
    void SyncStatus() noexcept;
    [[nodiscard]] TransformInspectorState TransformInspector() const;
    // The Move tool's box in view pixels (nullopt when it is not shown), and the box and snap guides on the overlay.
    [[nodiscard]] std::optional<TransformOverlayGeometry> TransformBox() const;
    void SyncTransformOverlay() noexcept;
    // The selection outline and the lasso draft over the canvas; `shown` null keeps the selection shown last.
    void SyncSelectionOverlay(const std::optional<DocumentSelection>* shown) noexcept;
    [[nodiscard]] float ChromeDpi() const noexcept;
    void DocumentChanged() noexcept;
    void BindCompositionDevice() noexcept;
    void CloseMainWindow() noexcept;
    bool WaitUntilMessage(DWORD timeout = INFINITE) noexcept;
    bool WaitForFrameLatency() noexcept;
    void CheckDeviceAdapter() noexcept;
    // The frame loop (Rendering_Composition.md "Failures"): one frame with device-loss recovery, the canvas failure
    // notice, the stop that asks to close through the save prompt, surfaces dropped while unseen, the exit code.
    void RenderFrame() noexcept;
    void ReportCanvasFailure(HRESULT failure) noexcept;
    void OnFrameLoopFailure(HRESULT failure) noexcept;
    void ReleaseSurfacesWhileUnseen() noexcept;
    [[nodiscard]] int ExitCode(int quitCode) const noexcept;
    int RunRenderingSelfTest() noexcept;
    void OnPointer(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    // The pointer's one interaction (RF-904, `_drag` below): a press on the canvas with nothing in flight begins the
    // one its tool (or Space) picks; the moves and the release go to the interaction in flight whatever the tool or
    // Space say by then; the pointer leaving the window without a capture, or lost to a dialog, a menu or another
    // app (Specs/UI/UI_Windowing.md "Pointer loss"), ends it without a release.
    enum class PointerEnd : std::uint8_t
    {
        Left, // the pointer left the window and no capture holds it: a drag ends as its release would
        Lost, // capture, focus or activation went elsewhere: a Move-tool drag goes back and a shape draft drops
    };
    void PressCanvas(const DxUi::PointerEvent& event, UINT message, Vec2 view, POINT client) noexcept;
    [[nodiscard]] bool DragPointer(const DxUi::PointerEvent& event, Vec2 view) noexcept;
    void HoverCanvas(const DxUi::PointerEvent& event, Vec2 view, bool inHole) noexcept;
    void EndPointerDrag(PointerEnd end) noexcept;
    [[nodiscard]] bool PointerBusy() const noexcept;
    // A key, a command or a header button ended what a `Drag` drags: the drag ends with it.
    template <class Drag> void DropDrag() noexcept
    {
        if (std::holds_alternative<Drag>(_drag))
        {
            _drag = PointerDrag{};
        }
    }
    void OnCommand(int command) noexcept override;
    void OnTool(EditorTool tool) noexcept;
    // The chrome's layers panel, color picker and layout events (next to InitializeChrome).
    void OnLayerSelected(Guid id) noexcept override;
    // RF-907: the layers panel's Mask badge and the Layer menu target a layer's mask or its pixels (legacy
    // selectLayerTarget); `toggleMask` (a Shift press on the badge) also turns the mask off or on.
    void TargetLayer(Guid id, bool mask, bool toggleMask) noexcept;
    void OnLayerExpanded(Guid id, bool expanded) noexcept override;
    void OnLayerReordered(Guid source, Guid target, std::optional<Guid> parent,
                          DxUi::TreeDropPlace place) noexcept override;
    void OnPickerColor(PickerTarget target, ChromeEditPhase phase, uint32_t argb) noexcept override;
    void OnChromeLayout() noexcept override;
    // Move tool (C8, Specs/Document/Document_Tools.md): header actions, pointer drags, cursor and the demo document.
    void OnTransformAction(TransformAction action, double value) noexcept override;
    void ChangeTransform(const std::function<void(LayerTransform&)>& update) noexcept;
    void BeginTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void ContinueTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void EndTransformDrag() noexcept;
    // What a Move-tool press picks: the session's part of the rule (the pending edit, the active layer and whether it
    // can transform alone, the group box, auto-select), then the pointer's (RF-1002: the cursor keeps one rule per
    // `_cursorRevision`, as building it walks every layer).
    struct TransformPressRule
    {
        bool document = false;
        bool picks = false; // no pending edit: a press may pick another layer
        bool autoSelect = false;
        std::optional<Guid> active;
        bool group = false; // the selected layers or the active folder transform together
        std::optional<LayerTransform> groupBox;
        std::optional<LayerTransform> activeBox; // the active layer's box, when it can transform alone
    };
    [[nodiscard]] TransformPressRule CurrentTransformPressRule() const;
    [[nodiscard]] std::optional<std::pair<Guid, bool>> TransformPressLayer(const TransformPressRule& rule, Vec2 pixel,
                                                                           bool control) const;
    [[nodiscard]] std::optional<std::pair<Guid, bool>> TransformPressLayer(Vec2 pixel, bool control) const
    {
        return TransformPressLayer(CurrentTransformPressRule(), pixel, control);
    }
    // The top-most visible pixel layer at a document point.
    [[nodiscard]] std::optional<Guid> LayerUnderPointer(Vec2 pixel) const;
    [[nodiscard]] bool ApplyCanvasCursor() noexcept;
    // The canvas's cursor at a client point (RF-1002): nullopt off the canvas (the chrome decides); a null id hides
    // the cursor (the brush tip is the cursor). The parts that depend on the pixel under the pointer are decided once
    // per pointer position and session state: WM_SETCURSOR comes with every pointer message.
    [[nodiscard]] std::optional<LPCWSTR> CanvasCursorAt(POINT client) noexcept;
    [[nodiscard]] bool CursorPressesLayer(Vec2 pixel, bool control) noexcept;
    [[nodiscard]] bool CursorOverSelection(Vec2 pixel) noexcept;
    void ApplyDemo() noexcept;
    // Selection tools (C9, Specs/Document/Document_Tools.md): header actions, pointer rules, keys and the wand's
    // composite sample. Outline and draft changes touch chrome only; the canvas never re-composites for them.
    void OnSelectionAction(SelectionAction action, double value) noexcept override;
    void SelectionPress(Vec2 view, UINT modifiers, POINT client) noexcept;
    [[nodiscard]] bool SelectionPointerMove(Vec2 view, UINT modifiers) noexcept;
    void SelectionRelease(Vec2 view) noexcept;
    void EndSelectionInteraction(bool keepDraft) noexcept;
    void UpdateSelectionModifiers() noexcept;
    void SelectionChanged() noexcept;
    void RunMagicWand(Vec2 pixel, SelectionMode mode) noexcept;
    [[nodiscard]] std::optional<Raster> WandComposite() noexcept;
    // Pixel edits (C9b, Specs/Document/Document_Tools.md): the Edit menu's fills, clear, invert and clipboard,
    // Ctrl-drag and Ctrl-arrow pixel moves (shown through the stroke preview), Ctrl+T on a selection, the Delete
    // key and masks from the selection. A copy mirrors to the system clipboard as PNG and DIB; Paste brings another
    // app's image in when the system clipboard changed since that copy.
    void BeginPixelDrag(Vec2 pixel, bool duplicate) noexcept;
    void DragPixels(Vec2 pixel, bool constrain) noexcept;
    void FinishPixelDrag() noexcept;
    void CancelPixelDrag() noexcept;
    void MirrorClipboard() noexcept;
    void PasteFromClipboard() noexcept;
    // Color adjustments and filters (C12, ApplicationAdjust.cpp): the Image and Filter menus, the adjustment panel's
    // actions, adjustment layers edited over the composite beneath them, and the eyedroppers on the canvas.
    bool BeginColorEdit(int command) noexcept;
    void AddAdjustmentLayer(int command) noexcept;
    void EditActiveAdjustment() noexcept;
    void OpenAdjustmentEditor() noexcept;
    void ApplyColorEdit() noexcept;
    using HostOutcome = ContinuationOutcome;
    class ColorCommitJob;
    [[nodiscard]] HostOutcome StartColorCommit(std::uint64_t ordinal) noexcept;
    [[nodiscard]] bool ColorCommitBusy() const noexcept
    {
        return _colorCommitStamp.has_value();
    }
    [[nodiscard]] bool ColorControlsFrozen() const noexcept
    {
        return ColorCommitBusy() || (_pendingPanelOpening && _session.PendingColorEdit() &&
                                     _pendingPanelOpening.value() == _session.PendingColorEdit()->openingSequence);
    }
    void CancelColorCommit() noexcept;
    void CompleteColorCommit(HistoryWorkStamp stamp, std::uint64_t opening, ColorEditJobResult& result,
                             JobResult completion, PreparedColorEdit* publication = nullptr,
                             std::int32_t captureError = 0) noexcept;
    int RunColorCommitSelfTest() noexcept;
    using HostRequestQueue = ContinuationQueue<HostRequest>;
    class ContinuationAction final : public UiAction
    {
      public:
        explicit ContinuationAction(Application& owner) noexcept : _owner(owner) {}
        void Run() noexcept override;

      private:
        Application& _owner;
    };
    [[nodiscard]] bool InitializeHostContinuations() noexcept;
    [[nodiscard]] ContinuationReceipt QueuePanelCommit(std::uint64_t opening) noexcept;
    [[nodiscard]] ContinuationReceipt QueueHostInput(HostRequestInput input) noexcept;
    [[nodiscard]] ContinuationReceipt QueueHostRequest(HostRequest request) noexcept;
    [[nodiscard]] ContinuationReceipt QueueHostPaths(std::span<const std::wstring_view> paths, HostPathAction action,
                                                     std::optional<Vec2> point = {}) noexcept;
    [[nodiscard]] ContinuationReceipt QueueHostWrite(HostWriteFormat format, bool saveAs) noexcept;
    void ReportHostAdmission(const ContinuationReceipt& receipt) noexcept;
    void RunHostContinuation() noexcept;
    void FinishHostContinuation(ContinuationReceipt::Result result) noexcept;
    // Every asynchronous history finisher calls this after apply/drop and HistoryWork::Finish.
    void ResumeHostHistory(HistoryWorkStamp stamp, HostOutcome outcome, std::int32_t error) noexcept;
    void CancelHostContinuations() noexcept;
    void StopHostContinuations() noexcept;
    static void RetireHostContinuation(HostRequestQueue::Entry&& entry, ContinuationRetirement reason) noexcept;
    int RunHostContinuationSelfTest() noexcept;
    int RunHostFlowSelfTest() noexcept;
    class PathClassificationJob;
    [[nodiscard]] HRESULT StartPathClassification(const HostPathsInput& input) noexcept;
    [[nodiscard]] bool HostPathIsProject(std::size_t index) const noexcept;
    int RunPathClassificationSelfTest() noexcept;
    class ProjectOpenJob;
    [[nodiscard]] HRESULT StartProjectOpen(const std::wstring& path) noexcept;
    [[nodiscard]] HRESULT SubmitProjectOpen(HistoryWorkStamp stamp, std::uint64_t generation, std::wstring path,
                                            std::optional<ProjectLoadLimits> limits) noexcept;
    int RunProjectOpenSelfTest() noexcept;
    [[nodiscard]] Prompts::Answer AskDiscardChanges() noexcept;
    [[nodiscard]] HRESULT SelectHostOutput(HostWriteFormat format, bool saveAs) noexcept;
    [[nodiscard]] HRESULT WriteHostOutput(HostWriteFormat format) noexcept;
    class ProjectSaveJob;
    [[nodiscard]] HRESULT StartProjectSave() noexcept;
    int RunProjectSaveSelfTest() noexcept;
    class ExportReadbackJob;
    [[nodiscard]] HRESULT StartExportReadback(HostWriteFormat format) noexcept;
    int RunExportReadbackSelfTest() noexcept;
    [[nodiscard]] bool ExecuteHostInput() noexcept;
    class ImageImportJob;
    [[nodiscard]] HRESULT StartImageImport(std::span<const std::wstring_view> paths, bool newProject,
                                           std::optional<Vec2> center) noexcept;
    [[nodiscard]] HRESULT SubmitImageImport(HistoryWorkStamp stamp, std::uint64_t generation,
                                            std::vector<std::wstring> paths,
                                            std::optional<std::vector<ImageImportFilePlan>> files, bool newProject,
                                            std::optional<Vec2> center) noexcept;
    int RunImageImportSelfTest() noexcept;
    class ColorPreviewJob;
    class ColorHistogramJob;
    class ColorFailureAction;
    class UtilityReadbackJob;
    class MergeReadbackJob;
    class MergePlanProbeJob;
    struct MergeProbePayload;
    [[nodiscard]] HRESULT StartMergeReadback() noexcept;
    [[nodiscard]] HRESULT StartPreparedMergeReadback(HistoryWorkStamp stamp, std::uint64_t generation,
                                                     std::shared_ptr<MergeProbePayload> source) noexcept;
    void FinishMergeWork(HistoryWorkStamp stamp, HostOutcome outcome, std::int32_t error,
                         bool budgetRefusal = false) noexcept;
    enum class UtilityReadbackKind
    {
        Merge,
        AdjustmentOpening
    };
    [[nodiscard]] HRESULT PrepareUtilityDevice(HRESULT reason) noexcept;
    [[nodiscard]] HRESULT StartUtilityReadback(CanvasDocument document, UtilityReadbackKind kind,
                                               std::optional<DocumentSession::MergePlan> merge = std::nullopt,
                                               std::optional<Guid> adjustment = std::nullopt) noexcept;
    int RunUtilityReadbackSelfTest() noexcept;
    void RefreshColorJobs() noexcept;
    void ConfigureColorLookupPreview(ColorEditJobInput& input) const;
    void SyncColorLookupPreview() noexcept;
    void CancelColorJobs() noexcept;
    void LaunchColorPreview() noexcept;
    [[nodiscard]] std::int32_t OfferColorPreview() noexcept;
    [[nodiscard]] std::int32_t SubmitColorPreview(PreviewMailbox<ColorEditJobInput>::Launch launch) noexcept;
    [[nodiscard]] std::int32_t SubmitColorHistogram() noexcept;
    void CompleteColorPreview(PreviewStamp stamp, ColorEditPreviewResult& result, JobResult completion) noexcept;
    void CompleteColorHistogram(ColorEditHistogramResult& result, JobResult completion) noexcept;
    void QueueColorFailure() noexcept;
    int RunColorJobsSelfTest() noexcept;
    void CancelColorEdit(bool cancelQueued = true) noexcept;
    void OnAdjustAction(AdjustAction action, double value) noexcept override;
    void OnCurveChanged(std::vector<CurvePoint> points) noexcept override;
    void OnBandChanged(int index, double degrees) noexcept override;
    void OnAdjustColor(ChromeEditPhase phase, uint32_t argb, bool highlights) noexcept;
    void SyncAdjustPanel() noexcept;
    // The panel's live DxUi controls (review fixes wave 1b, RF-1107): typed decimals, the Gradient Map picker's
    // lifetime, the spectrum's handle strip and the drawn editors in the UI Automation tree.
    int RunAdjustSelfTest() noexcept;
    int RunAdjustBusySelfTest() noexcept;
    // ApplicationSize.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Size cards"): the New Canvas, Canvas Size and
    // Image Size cards edit a draft the Application owns; Apply resizes as one entry.
    [[nodiscard]] bool SizeCardOpen() const noexcept;
    void OpenSizeCard(SizeCardKind kind) noexcept;
    void CancelSizeCard() noexcept;
    void ApplySizeCard() noexcept;
    void OnSizeAction(SizeAction action, double value) noexcept override;
    void OnSizeColor(ChromeEditPhase phase, uint32_t argb) noexcept;
    void SyncSizeCard() noexcept;
    int RunSizeSelfTest() noexcept;
    int RunSizingWorkSelfTest() noexcept;
    class SizingJob;
    class HistoryRestoreJob;
    [[nodiscard]] HRESULT StartHistoryRestore(bool redo) noexcept;
    int RunHistoryRestoreSelfTest() noexcept;
    [[nodiscard]] HRESULT StartSizing(std::variant<CanvasSizeOptions, ImageSizeOptions> options) noexcept;
    class RasterEditJob;
    [[nodiscard]] HRESULT StartRasterEdit(RasterEditKind kind, FillSource fill = FillSource::Background) noexcept;
    void ExecuteRasterEditCommand(RasterEditKind kind, FillSource fill = FillSource::Background) noexcept;
    int RunRasterEditSelfTest() noexcept;
    struct GradientRequest final
    {
        Guid target;
        std::uint64_t opening = 0;
        std::uint64_t sequence = 0;
        bool mask = false;
        Vec2 start;
        Vec2 end;
        GradientShape shape = GradientShape::Linear;
        std::array<double, 4> color{};
        std::array<double, 4> endColor{};
        double opacity = 1;
    };
    class GradientJob;
    [[nodiscard]] GradientRequest CaptureGradientRequest() const noexcept;
    void RefreshGradientJobs() noexcept;
    void CancelGradientJobs() noexcept;
    void LaunchGradientPreview() noexcept;
    [[nodiscard]] HRESULT SubmitGradientJob(GradientRequest request, std::optional<PreviewStamp> preview,
                                            std::optional<HistoryWorkStamp> commit) noexcept;
    [[nodiscard]] HRESULT StartGradientCommit(std::uint64_t ordinal) noexcept;
    int RunGradientWorkSelfTest() noexcept;
    struct PixelMoveRequest final
    {
        Guid target;
        std::uint64_t opening = 0;
        std::uint64_t sequence = 0;
        Vec2 offset;
        bool duplicate = false;
    };
    class PixelMoveJob;
    [[nodiscard]] PixelMoveRequest CapturePixelMoveRequest() const noexcept;
    void RefreshPixelMoveJobs() noexcept;
    void CancelPixelMoveJobs(bool keepOpening = false) noexcept;
    void LaunchPixelMovePreview() noexcept;
    [[nodiscard]] HRESULT SubmitPixelMoveJob(PixelMoveRequest request, std::optional<PreviewStamp> preview,
                                             std::optional<HistoryWorkStamp> commit) noexcept;
    [[nodiscard]] HRESULT StartPixelMoveCommit(std::uint64_t ordinal) noexcept;
    void ExecutePixelNudge(Vec2 offset) noexcept;
    int RunPixelMoveWorkSelfTest() noexcept;
    [[nodiscard]] bool AdjustPointer(const DxUi::PointerEvent& event, Vec2 view, bool inHole) noexcept;
    // Brush and Eraser (C10, Specs/Rendering/Rendering_Brush.md): header actions, strokes through the compute
    // engine, the live preview in the composition, keys and the tip circle.
    void OnBrushAction(BrushAction action, double value) noexcept override;
    void BeginBrushStroke(Vec2 view, UINT modifiers) noexcept;
    void ContinueBrushStroke(Vec2 view) noexcept;
    void FinishBrushStroke() noexcept;
    void CancelBrushStroke() noexcept;
    // The composition's stroke preview shows the one raster edit under way (RF-904): a stroke and a warp begin their
    // own; SyncStrokePreview shows a pending gradient or lifted pixels, and is the one place a preview ends once no
    // raster edit is under way; PushPreviewTiles uploads what the edit under way changed since the last push.
    void SyncStrokePreview() noexcept;
    void PushPreviewTiles() noexcept;
    // Ends the composition's stroke preview; its working texture becomes `commit`'s only when the commit landed on
    // the preview's own layer and target (RF-407).
    void EndStrokePreview(const std::optional<BrushCommitInfo>& commit) noexcept;
    void SyncBrushCursor() noexcept;
    void DropBrushEngine() noexcept;
    // Retouch tools (C11): Spot Healing, Clone Stamp and Blur go through the brush pipeline with a kind; Liquify
    // and Smudge through the warp pipeline (a document-size working copy shown through the stroke preview); the
    // Gradient's pending raster shows through the preview until Apply; the Shape's draft and the Eyedropper's ring
    // draw on the overlay.
    [[nodiscard]] BrushKind BrushKindFor() const noexcept;
    [[nodiscard]] bool WarpsWithSmear() const noexcept;
    void BeginWarpStroke(Vec2 view) noexcept;
    void ContinueWarpStroke(Vec2 view) noexcept;
    void FinishWarpStroke() noexcept;
    void CancelWarpStroke() noexcept;
    void OnGradientAction(GradientAction action, double value) noexcept override;
    void BeginGradientDrag(Vec2 view) noexcept;
    void DragGradient(Vec2 view, UINT modifiers) noexcept;
    void EndGradientDrag() noexcept;
    void CommitGradient() noexcept;
    void CancelGradient() noexcept;
    void OnShapeAction(ShapeAction action, double value) noexcept override;
    void CancelShapeDraft() noexcept;
    void FinishShapeDraft() noexcept;
    // ApplicationCrop.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Crop tool"): the frame's drags, the header's
    // actions, Enter / Escape, the cursor and the self-test.
    void BeginCropDrag(Vec2 view) noexcept;
    void ContinueCropDrag(Vec2 view) noexcept;
    void EndCropDrag() noexcept;
    void ApplyCrop() noexcept;
    void CancelCropFrame() noexcept;
    void OnCropAction(CropAction action, double value) noexcept override;
    void SyncCropChrome() noexcept;
    [[nodiscard]] LPCWSTR CropCursor(Vec2 view) const noexcept;
    int RunCropSelfTest() noexcept;
    // ApplicationLayerEdits.cpp (C16): the Rename Layer card and Merge Down through the composition.
    void RenameActiveLayer() noexcept;
    void OnLayerInvoked(Guid id) noexcept override;
    void OnRename(std::optional<std::wstring_view> name) noexcept override;
    // The card closes without renaming: its layer or the document went away, or an interaction settled.
    void CloseRenameCard() noexcept;
    void MergeLayersCommand() noexcept;
    // The layers panel's opacity stepper: a typed edit previews on the layer it started on and records one entry
    // when it commits, or when anything else (a command, a tool, a canvas press, another layer, a save) closes it.
    void OnLayerOpacity(double opacity, ChromeEditPhase phase) noexcept override;
    void CloseOpacityEdit() noexcept;
    [[nodiscard]] const ImageLayer* OpacityEditLayer() noexcept;
    int RunLayerEditsSelfTest() noexcept;
    [[nodiscard]] bool SamplesColor() const noexcept;
    void SampleColorAt(Vec2 view) noexcept;
    void EndColorSampling() noexcept;
    void SyncToolOverlays() noexcept;
    // ApplicationKeyboard.cpp (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators"): a key down goes to the
    // gestures of an edit in progress, then the effective binding table (`_bindings`), then the chrome; the menu
    // bar takes a tapped Alt, F10 and Alt+letter.
    LRESULT OnKeyDown(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnKeyUp(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnCharacter(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    // The message loop's step for one queued message: a key down of the window is dispatched before it is translated,
    // and translated only when the key down left its character (TranslateAccelerator's order).
    void DispatchQueuedMessage(MSG& message) noexcept;
    // A replacement chord in the catalog's form for the active layout: a `vk` naming a key the layout places on a
    // position key becomes that position (CommandBindings::Build compares chords in that form).
    [[nodiscard]] static KeyChord NormalizeLayoutChord(KeyChord chord) noexcept;
    // WM_INPUTLANGCHANGE: the binding table follows the new layout, and the chrome retitles its chords.
    void OnKeyboardLayoutChanged() noexcept;
    void OnKeyMenu(wchar_t character) noexcept;
    bool HandleKey(const KeyPress& press) noexcept;
    bool HandleEditGesture(const KeyPress& press) noexcept;
    bool HandleCanvasKey(const KeyPress& press) noexcept;
    [[nodiscard]] KeyState CurrentKeyState() const noexcept;
    // Self-test driver: one key press through the window procedure as the message loop delivers it — the key down
    // (a system key with Alt), the character TranslateMessage posts, the key up — with the thread's key state
    // holding the modifiers (MK_CONTROL, MK_SHIFT, kKeyAlt, kKeyAltGr) and a real scan code (the US OEM bracket and
    // number-row keys name their physical position). True when the key path took the key down.
    static constexpr UINT kKeyAlt = 0x0100;
    static constexpr UINT kKeyAltGr = 0x0200; // AltGr: the right Alt, with the left Ctrl Windows adds to it
    bool HandleAccelerator(UINT virtualKey, UINT modifiers, bool repeat = false) noexcept;
    int RunKeyboardSelfTest() noexcept;
    void NewDocument() noexcept;
    void OpenDocument() noexcept;
    void SaveDocument(bool saveAs) noexcept;
    void ExportFlattened(bool jpeg) noexcept;
    void OpenPath(const wchar_t* path) noexcept;
    // ApplicationFiles.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Files" and "Unimplemented functions"): images
    // opened as a layer or a new project, the save prompt, Import Images, Close Project and the notice for a
    // function that has no implementation yet.
    using ImageOpenChoice = HostImageOpenChoice;
    static constexpr UINT kEndSessionMessage = WM_APP + 8; // the save prompt a refused session end posts
    static constexpr UINT kNoticeMessage = WM_APP + 9;     // a notice card to show (wParam: its string id)
    [[nodiscard]] ImageOpenChoice PromptImageOpen(const std::wstring& firstName, std::size_t count) noexcept;
    // Modified history, or a pending edit that Save would record (a live stroke or warp, a pending gradient, a
    // changed transform, a typed opacity).
    [[nodiscard]] bool HasUnsavedWork() const noexcept;
    // Before Save and Export (Decision D2): the typed opacity records its entry, a live stroke, warp or pending
    // gradient commits, a pending transform applies, and an open panel applies as its OK would.
    [[nodiscard]] bool SettleForWrite() noexcept;
    // Session end (Specs/UI/UI_Windowing.md): a modified document refuses with a block reason and asks through the
    // posted prompt; the settings are saved when the session ends.
    [[nodiscard]] bool OnQueryEndSession(LPARAM reasons) noexcept;
    void OnEndSession(bool ending) noexcept;
    void RunEndSessionPrompt() noexcept;
    // A close or the session-end prompt waits while a message it arrived inside (a modal loop, a chrome dispatch) is
    // still on the stack; the outermost window-procedure return posts it again.
    [[nodiscard]] bool CloseMustWait() const noexcept;
    void ReleaseDeferredClose() noexcept;
    // Capture or focus went elsewhere (a dialog, a menu, another app): every pointer interaction ends.
    void EndPointerInteractions() noexcept;
    // Quiet runs drive the pointer synthetically from a hidden window: they honor only the losses their own thread
    // causes, never a capture or focus change another thread or process sends.
    [[nodiscard]] bool HonorsPointerLoss() const noexcept;
    // `dropPoint`: where a drop landed on the canvas, in document pixels; images added as layers center on it.
    void OpenPaths(const std::vector<std::wstring>& paths, std::optional<Vec2> dropPoint = std::nullopt) noexcept;
    void ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active, std::wstring path) noexcept;
    void FinishPreparedDocumentReplacement() noexcept;
    void ImportImages() noexcept;
    void CloseProject() noexcept;
    // Ends every interaction in flight before a tool switch (panels apply) or before the document goes (they cancel).
    void SettleInteractions(bool applyPanels) noexcept;
    // The edits under way, in the one order Escape, Enter, Undo and a settle consult them (RF-904): the size card and
    // an open panel first (a card comes before the canvas), then the canvas's drags, then what stays pending between
    // drags. Outside the cards at most one of them is under way at a time.
    enum class CanvasEdit : std::uint8_t
    {
        SizeCard,
        Panel,
        PixelMove,
        Warp,
        Brush,
        Shape,
        OutlineMove,
        Lasso,
        Gradient,
        Transform,
        Crop,
    };
    // What ends an edit under way: Escape cancels it and Enter applies it; Undo or Redo drops it and history stays put
    // (an open panel keeps history where it is until OK or Cancel, while a lasso outline and the crop frame go and
    // history moves); a tool switch settles it with a panel applying, a document on its way out with a panel
    // cancelling; a write (Decision D2) records what Save would and leaves the rest.
    enum class EditEnd : std::uint8_t
    {
        Escape,
        Enter,
        Undo,
        ToolSwitch,
        DocumentGoes,
        Write,
    };
    [[nodiscard]] bool EditUnderWay(CanvasEdit edit) const noexcept;
    // Ends `edit` when it is under way; true when Escape, Enter or Undo stops there.
    bool EndCanvasEdit(CanvasEdit edit, EditEnd end) noexcept;
    // Escape, Enter or Undo go to the first edit under way that takes them; true when one did.
    bool EndFirstCanvasEdit(EditEnd end) noexcept;
    // A settle or a write ends every edit under way, in order.
    void EndCanvasEdits(EditEnd end) noexcept;
    void ShowUnavailable(UINT captionId) noexcept;
    int RunFilesSelfTest() noexcept;
    int RunSessionSelfTest() noexcept;
    int RunClipboardSelfTest() noexcept;
    // Refusals the host reports (review fixes wave 1b): Load Mask / Layer Selection past the outline limit, and edits
    // the document's pixel budget refused (Decision D7).
    int RunRefusalSelfTest() noexcept;
    // ApplicationChromeSync.cpp (review fixes RF-1001, RF-1002): what a pointer move costs the chrome and the cursor.
    int RunChromeSyncSelfTest() noexcept;
    void ZoomBy(double factor) noexcept;
    void ShowError(UINT stringId) noexcept;
    // An edit the document's 100 M pixel budget refused (Decision D7, `DocumentSession::TakeBudgetRefusal`) shows
    // IDS_ERROR_TOO_LARGE in the notice card, from the message loop; SyncChrome reports it once, whichever path ran
    // the edit.
    void ReportBudgetRefusal() noexcept;
    [[nodiscard]] Vec2 DocumentSize() const noexcept;
    // The document (or a caller-built variant of it) flattened at full resolution, never the canvas's preview
    // composite (RF-402): binds the composition device, reads back through CanvasRenderer::ComposeReadback and
    // wraps the bytes in a Raster. nullopt only when the compose itself failed; the caller still checks
    // Raster::HasStorage() when an empty allocation matters to it.
    [[nodiscard]] std::optional<Raster> ComposeDocumentReadback(const CanvasDocument& document) noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, const RECT* suggestedBounds) noexcept;
    void OnGetMinMaxInfo(HWND window, MINMAXINFO* info) const noexcept;

    HINSTANCE _instance = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hpowernotify _displayPowerNotification;
    Renderer _renderer;
    std::unique_ptr<ChromeHost> _chrome;
    // Declared before the session: a pending stroke tells its engine when the session drops it.
    GpuBrushCoverage _brushEngine;
    DocumentSession _session;
    JobScheduler _jobs;
    GpuWorkQueue _gpuWork;
    bool _gpuWorkInitialized = false;
    HRESULT _gpuBindingError = E_PENDING;
    HistoryWork _historyWork;
    ContinuationAction _continuationAction{*this}; // stable borrowed source; unregister before joining jobs
    HostInputBudget _hostInputBudget;              // outlives queued and active request reservations
    std::optional<HostRequest> _startupPaths;      // owned until Run initializes the document and can bind its serial
    HostRequestQueue _hostRequests;
    std::optional<HostRequestQueue::Entry> _activeHostRequest;
    std::optional<HistoryWorkStamp> _hostCommitStamp;
    std::optional<HistoryWorkStamp> _hostFinishedStamp;
    std::optional<HistoryWorkStamp> _utilityStamp;
    std::optional<JobTicket> _utilityTicket;
    std::shared_ptr<UtilityWorkTestGate> _utilityWorkTestGate;
    std::optional<HistoryWorkStamp> _sizingStamp;
    std::optional<HistoryWorkStamp> _historyRestoreStamp;
    std::optional<JobTicket> _historyRestoreTicket;
    std::shared_ptr<HistoryRestoreTestGate> _historyRestoreTestGate;
    std::optional<JobTicket> _sizingTicket;
    std::shared_ptr<SizingWorkTestGate> _sizingWorkTestGate;
    std::optional<HistoryWorkStamp> _rasterEditStamp;
    std::optional<JobTicket> _rasterEditTicket;
    std::shared_ptr<RasterEditTestGate> _rasterEditTestGate;
    std::optional<JobTicket> _pathClassificationTicket;
    PathClassificationBatch _pathClassifications;
    bool _pathClassificationCancelled = false;
    std::shared_ptr<FileWorkTestGate> _fileWorkTestGate;
    std::optional<HistoryWorkStamp> _projectOpenStamp;
    std::optional<JobTicket> _projectOpenTicket;
    std::shared_ptr<ProjectOpenTestGate> _projectOpenTestGate;
    std::optional<HistoryWorkStamp> _imageImportStamp;
    std::optional<JobTicket> _imageImportTicket;
    std::shared_ptr<ImageImportTestGate> _imageImportTestGate;
    std::optional<HistoryWorkStamp> _projectSaveStamp;
    std::optional<JobTicket> _projectSaveTicket;
    std::shared_ptr<ProjectSaveTestGate> _projectSaveTestGate;
    std::optional<HistoryWorkStamp> _exportStamp;
    std::optional<JobTicket> _exportTicket;
    std::shared_ptr<ExportWorkTestGate> _exportWorkTestGate;
    ContinuationReceipt::Result _hostPrerequisite{HostOutcome::Pending, 0};
    bool _hostSourceRegistered = false;
    bool _hostAdmissionAllocationFailureForTest = false;
    enum class HostStage : std::uint8_t
    {
        Begin,
        PathProject,
        PathImages,
        Confirm,
        Output,
        Settle,
        GradientSettle,
        PixelMoveSettle,
        Write,
        Execute,
        FileComplete,
        Complete
    };
    HostStage _hostStage = HostStage::Begin;
    HostStage _hostAfterPrerequisite = HostStage::Execute;
    HostWriteFormat _hostWriteFormat = HostWriteFormat::Project;
    std::size_t _hostPathIndex = 0;
    bool _hostSawProject = false;
    bool _hostImportNewProject = false;
    bool _hostSaveBeforeAction = false;
    bool _hostWriteInFlight = false;
    bool _hostBatchFailed = false;
    std::int32_t _hostBatchError = S_OK;
    bool _hostExecuting = false; // only the selected runner can enter an already approved action
    HRESULT _hostExecutionError = S_OK;
    std::uint64_t _sizeInvocation = 0;
    std::uint64_t _endSessionGeneration = 0;
    std::optional<Prompts::Answer> _forcedDiscardAnswer;
    std::optional<HistoryWorkStamp> _colorCommitStamp;
    std::optional<std::uint64_t> _pendingPanelOpening;
    std::optional<JobTicket> _colorCommitTicket;
    HostOutcome _colorCommitOutcome = HostOutcome::Succeeded;
    std::int32_t _colorCommitError = 0;
    PreviewMailbox<ColorEditJobInput> _colorPreviews;
    PreviewMailbox<GradientRequest> _gradientPreviews;
    bool _gradientMailboxOpen = false;
    bool _gradientCompleting = false;
    std::uint64_t _gradientPreviewSerial = 0;
    std::uint64_t _gradientPreviewOpening = 0;
    std::uint64_t _gradientSeenSequence = 0;
    std::optional<JobTicket> _gradientPreviewTicket;
    std::optional<JobTicket> _gradientCommitTicket;
    std::optional<HistoryWorkStamp> _gradientCommitStamp;
    std::shared_ptr<GradientWorkTestGate> _gradientWorkTestGate;
    PreviewMailbox<PixelMoveRequest> _pixelMovePreviews;
    bool _pixelMoveMailboxOpen = false;
    bool _pixelMoveCompleting = false;
    std::uint64_t _pixelMovePreviewSerial = 0;
    std::uint64_t _pixelMovePreviewOpening = 0;
    std::uint64_t _pixelMoveSeenSequence = 0;
    std::optional<JobTicket> _pixelMovePreviewTicket;
    std::optional<JobTicket> _pixelMoveCommitTicket;
    std::optional<HistoryWorkStamp> _pixelMoveCommitStamp;
    std::shared_ptr<PixelMoveWorkTestGate> _pixelMoveWorkTestGate;
    std::shared_ptr<const PixelMoveOpening> _pixelMoveOpening;
    std::uint64_t _pixelMoveOpeningSerial = 0;
    std::uint64_t _pixelMoveOpeningId = 0;
    std::uint64_t _pixelMoveOpeningGeneration = 0;
    bool _colorMailboxOpen = false;
    std::uint64_t _colorDocumentSerial = 0;
    std::uint64_t _colorOpening = 0;
    std::uint64_t _colorSeenSequence = 0;
    bool _colorHistogramAttempted = false;
    std::optional<JobTicket> _colorPreviewTicket;
    std::optional<JobTicket> _colorHistogramTicket;
    std::shared_ptr<ColorWorkTestGate> _colorWorkTestGate;
    JobActivity _jobActivity;
    std::uint64_t _jobQpcFrequency = 0;
    std::optional<JobTicket> _jobIndicatorTimer;
    CanvasViewport _viewport;
    CanvasRenderer _composition;
    std::wstring _screenshotPath;
    std::wstring _documentPath;
    uint32_t _screenshotDpi = 0;
    EditorTool _tool = EditorTool::Move;
    // The Move tool's and the selection tools' header choices: host state, never the document session's (RF-810).
    // The eyedropper's comparison ring follows the settings (`_settings.sampleRing`).
    bool _locksTransformRatio = true;
    bool _transformAutoSelect = false;
    bool _showsTransformControls = true;
    int _selectionExpandAmount = 1; // 1…DocumentSession::kSelectionModifyLimit
    int _selectionContractAmount = 1;
    bool _spacePan = false;
    bool _acceleratorShift = false;
    CommandBindings _bindings;        // the effective binding table: catalog defaults and the settings' replacements
    bool _suppressCharacter = false;  // the key down ran a command or a gesture: its WM_CHAR / WM_SYSCHAR types nothing
    bool _lastKeyHandled = false;     // the last key down went to a command or a gesture (read by the self-test driver)
    std::uint16_t _keyMovedFocus = 0; // a held key whose press moved the keyboard: its repeats go nowhere
    bool _releasingCapture = false;   // a button-up releases the capture: not a capture loss
    // The typed opacity edit: the layer it started on, that layer's opacity before the first preview (what history
    // holds) and the value previewed. Previews change the document outside history; one entry records the edit.
    struct OpacityEdit
    {
        Guid layer;
        double start = 1.0;
        double shown = 1.0;
    };
    std::optional<OpacityEdit> _opacityEdit;
    // The value an edit closed from outside recorded while the field kept its text: the field's late commit of that
    // same value is already in history and changes nothing (not even another document's layer).
    std::optional<double> _opacityRecorded;
    std::optional<Guid> _renameLayer; // the layer the open Rename card renames
    int _messageDepth = 0;            // window-procedure calls on the stack
    bool _closeDeferred = false;      // a WM_CLOSE waits for the stack to unwind
    bool _jobCloseRequested = false;  // a close message during a completion/action waits for the outer loop
    bool _shutdownDeferred = false;   // an already-confirmed close waits for a completion/action to return
    bool _shutdownStarted = false;
    bool _shutdownComplete = false;
    bool _closeConfirmed = false;    // the session-end prompt already asked: the next WM_CLOSE does not
    bool _endSessionPending = false; // a session end was refused and its prompt has not answered yet
    bool _endSessionPromptDeferred = false;
    bool _layoutDirty = false;
    // The pointer's one interaction in flight (RF-904). A brush stroke, a warp and a shape draft are the session's own
    // pending edits (PendingBrush, PendingWarp, PendingShape) and keep no state here.
    struct PanDrag
    {
        Vec2 last{}; // the Hand, or Space held at the press: the view point the canvas was dragged to last
    };
    struct ZoomDrag
    {
        Vec2 anchor{}; // the Zoom tool's press (view pixels), the zoom then, and whether the drag passed the click slop
        double zoom = 1;
        bool moved = false;
    };
    struct MoveToolDrag
    {
        TransformDrag drag;
        bool duplicateOnMove = false; // an Alt-drag duplicates the layer once the pointer moves
    };
    struct OutlineDrag
    {
        Vec2 start{};  // document point where the selection outline was grabbed
        Vec2 offset{}; // the whole-pixel offset it has been dragged by
    };
    struct DraftDrag
    {
        std::optional<Vec2> marqueePixel; // the Marquee's last point, redrawn when Shift changes without a move
        bool constrainArmed = true;       // Shift squares the box, except a Shift held at the press until let go
    };
    struct PixelDrag
    {
        Vec2 start{}; // document point where a Ctrl-drag lifted the selected pixels
    };
    struct GradientDrag
    {
        int handle = 1; // 0 the start, 1 the end of the pending line
    };
    struct ColorSampling
    {
        uint32_t original = 0xFF000000u; // the color before the press (the sample ring's lower half)
        std::optional<Raster> composite; // the canvas read at the press
        std::optional<Vec2> point;       // the document point sampled last
    };
    struct HueTargetDrag
    {
        double startX = 0; // view x where a targeted Hue/Saturation drag began
    };
    using PointerDrag = std::variant<std::monostate, PanDrag, ZoomDrag, MoveToolDrag, CropDrag, OutlineDrag, DraftDrag,
                                     PixelDrag, GradientDrag, ColorSampling, HueTargetDrag>;
    PointerDrag _drag;
    std::optional<TransformOverlayGeometry> _overlayGeometry;
    GradientMapSettings _adjustColorOriginal; // the Gradient Map ends when its color picker opened
    DWORD _clipboardSequence = 0; // the system clipboard's sequence number right after the last copy was offered to it
    ULONGLONG _lastPressTick = 0;
    POINT _lastPressPoint{};
    std::optional<DocumentSelection> _overlaySelection;
    std::optional<Vec2> _brushHover; // document point under the pointer while a brush tool is active
    std::wstring _bench;
    std::unique_ptr<UiBenchmarkState> _uiBenchmark;
    Vec2 _overlayOrigin{};
    double _overlayScale = 0.0;
    double _overlayBacking = 0.0;
    bool _marchingAnts = false;     // a non-empty selection outline shows (SyncChrome); the frame loop steps its dash
    std::uint64_t _chromeSyncs = 0; // SyncChrome runs (the self-test counts what a pointer move costs)
    std::vector<float> _guideXs;    // the snap guides in view pixels, reused between syncs
    std::vector<float> _guideYs;
    // What the status bar's texts were formatted from (SyncStatus).
    struct StatusKey
    {
        int percent = 0;
        bool document = false;
        int width = 0;
        int height = 0;
        EditorTool tool = EditorTool::Move;
        std::uint64_t chrome = 0; // ChromeHost::DisplayRevision: rebuilt controls or new chords
        [[nodiscard]] bool operator==(const StatusKey&) const noexcept = default;
    };
    std::optional<StatusKey> _statusKey;
    // The cursor's decisions that depend on the pixel under the pointer (RF-1002): each holds for one document pixel
    // (and Ctrl, for the layer) until SyncChrome, through which every session change passes, moves `_cursorRevision`.
    struct CursorProbe
    {
        Vec2 pixel{};
        bool control = false;
        std::uint64_t revision = 0;
        bool result = false;
        bool valid = false;
    };
    std::uint64_t _cursorRevision = 1;
    TransformPressRule _pressRule; // the cursor's, as of `_pressRuleRevision`
    std::uint64_t _pressRuleRevision = 0;
    CursorProbe _layerProbe;
    CursorProbe _selectionProbe;
    std::optional<SelectionBounds> _selectionReach; // the selection's bounds: nothing outside them is inside it
    std::uint64_t _selectionReachRevision = 0;
    // LayerUnderPointer's visible pixel layers top-first (index into the document's layers, id), per `_cursorRevision`.
    mutable std::vector<std::pair<std::size_t, Guid>> _pointerLayers;
    mutable std::uint64_t _pointerLayersRevision = 0;
    mutable std::uint64_t _pointerLayerScans = 0; // LayerUnderPointer runs (the self-test counts them)
    std::uint64_t _cursorSelectionTests = 0;      // CanMoveSelection calls the cursor made (the self-test counts them)
    // The gradient or pixel-move raster the composition's stroke preview was begun for (never dereferenced: a new
    // raster, or an expired one, restarts the preview).
    std::weak_ptr<const BrushStroke> _previewSource;
    std::wstring _demo;
    bool _quietUi = false;
    // Real UI by default; the self-test, screenshot and benchmark modes point this at QuietPrompts() wherever they
    // set `_quietUi = true`. Both are stateless singletons (Prompts.h), so this never owns anything.
    const Prompts* _prompts = &RealPrompts();
    int _errorsShown = 0;                            // ShowError calls; quiet runs count instead of showing them
    UINT _lastErrorId = 0;                           // the string the last ShowError named
    int _budgetNotices = 0;                          // budget refusals reported (ReportBudgetRefusal)
    std::optional<ImageOpenChoice> _forcedImageOpen; // the self-test's answer to the image-open prompt
    std::optional<bool> _forcedDiscard;              // the self-test's answer to the save prompt (true: Don't Save)
    using SizeDialog = HostSizeDraft;
    SizeDialog _sizeDialog;
    bool _forceWarp = false;
    std::wstring _settingsPath;
    AppSettings _settings;
    wil::unique_hfind_change _settingsWatch;
    FILETIME _settingsSeenWrite{};
    std::uint64_t _settingsSeenSize = 0;
    bool _restoreMaximized = false;
    bool _classRegistered = false;
    bool _rendererReady = false;
    bool _windowVisible = false;
    bool _displayPoweredOn = true;
    bool _occlusionStatusChanged = false;
    bool _frameInvalidated = true;
    bool _mouseTracked = false;
    bool _screenshotArmed = false;
    HRESULT _runtimeFailure = S_OK;
    HRESULT _screenshotResult = S_OK;
    HRESULT _frameLoopFailure = S_OK; // drawing stopped; the close it asked for prompts to save (RF-401)
    HRESULT _lastFrameFailure = S_OK;
    int _failedFrames = 0;
    bool _canvasFailureShown = false;
    int _canvasFailureNotices = 0; // quiet runs count them
    std::uint64_t _boundDeviceGeneration = 0;
};
