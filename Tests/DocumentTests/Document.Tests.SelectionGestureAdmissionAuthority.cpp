#include "Document.Tests.SelectionGestureAdmissionAuthority.h"
#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/Support.Tests.DocumentFixture.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <windows.h>

namespace
{
unsigned checks = 0, failures = 0;
const char* current = "setup";
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s selection-gesture-admission-authority.%s.%s\n", value ? "PASS" : "FAIL", current, label);
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL selection-gesture-admission-authority.setup.%s\n", label);
    std::_Exit(1);
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("event-settlement");
}
bool Memory(void*, MemorySnapshot& result) noexcept
{
    constexpr auto available = kMemoryAdmissionHeadroomBytes + 512ull * 1024 * 1024;
    result = {available, available};
    return true;
}
enum class Change
{
    Crop,
    Active,
    Mask,
    History
};
void Run(Change change, const char* name)
{
    current = name;
    auto session = std::make_unique<DocumentSession>();
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    document.selection = DocumentSelection{false, SelectionLoops{{{1, 2}, {7, 2}, {7, 6}, {1, 6}}}};
    auto layer = ImageLayer::Blank(L"Admission authority", {8, 8});
    layer.asset = RasterAsset{Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, layer.name};
    const std::uint8_t white = 255;
    auto mask = Raster::Make(1, 1, RasterFormat::Gray8, &white, 1);
    layer.mask = LayerMask{RasterAsset{mask, mask, L"Mask"}};
    const auto active = layer.id;
    document.layers.push_back(std::move(layer));
    session->ReplaceDocument(std::move(document).Seal(), active);
    const auto description = DescribeSelectionOutlineGestureMemory(*session);
    if (!description)
        SetupFailure("description");
    const auto generation = session->Generation();
    const auto revision = session->history.CurrentRevision();
    auto prefix = std::make_unique<HostSelectionPressInput>();
    HostSelectionPressInput::Context arrival;
    arrival.document = description->source.document;
    arrival.active = active;
    arrival.ordinal = 1;
    if (!prefix->Begin(23, arrival, {2, 3}, {}, false) ||
        prefix->Move(23, {4, 6}, {}, false, 2) != HostSelectionPressInput::Admission::Recorded ||
        prefix->Finish(23, false, 3) != HostSelectionPressInput::Admission::Recorded)
        SetupFailure("accepted-prefix");
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Memory, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("scheduler");
    auto state = std::make_unique<SelectionOutlineGestureState>(scheduler, *session, *prefix, 23, description->limits);
    state->admissionSource = description->source;
    std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
    if (scheduler.Submit(job,
                         {.kind = JobClass::Selection,
                          .serialKey = session->DocumentSerial(),
                          .memoryBytes = description->limits.totalBytes,
                          .deferredRetirement = true,
                          .allowMemoryWaiting = false,
                          .reserveRetainedMemory = true,
                          .continuingOutputLimit = description->limits.continuingOutputBytes},
                         state->original) != S_OK)
        SetupFailure("original-admission");
    // Allocate may finish, but no owner drain/capture has occurred. These reference operations change
    // editor/history state without changing the document generation or revision.
    switch (change)
    {
    case Change::Crop:
        session->SetCropFrame(CropRect{0, 0, 4, 4});
        break;
    case Change::Active:
        session->SelectLayer(std::nullopt);
        break;
    case Change::Mask:
        session->SelectLayerTarget(active, true);
        break;
    case Change::History:
        session->history.Begin(L"Foreign Begin", session->Document(), session->ActiveLayerID());
        break;
    }
    Settle(scheduler);
    Expect(state->completed && state->outcome.status == JobStatus::Failed && state->outcome.error == E_ABORT &&
               state->runs == 1 && state->captures == 0 && state->hits == 0 && !state->opened && !state->closed &&
               !state->aborted && state->inputAcknowledged && !prefix->Busy(),
           "metadata-change-refuses-before-retained-source-and-preparation");
    const bool foreign = change == Change::Crop     ? session->CropFrame() == std::optional<CropRect>{{0, 0, 4, 4}}
                         : change == Change::Active ? !session->ActiveLayerID()
                         : change == Change::Mask   ? session->IsMaskSelected()
                                                    : session->history.CaptureState().Depth() == 1 &&
                                                          !session->history.IsCurrent(description->source.history);
    Expect(foreign && session->Generation() == generation && session->history.CurrentRevision() == revision &&
               session->Selection() && !session->Selection()->antialiased &&
               session->Selection()->Bounds() == std::optional<SelectionBounds>{{1, 2, 7, 6}} &&
               session->history.CaptureState().Depth() == (change == Change::History ? 1 : 0),
           "refusal-preserves-foreign-metadata-literal-selection-generation-and-history");
    state.reset();
    session.reset();
    Settle(scheduler);
    Expect(scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0,
           "all-original-metadata-and-source-reservations-refund");
    scheduler.Close();
}
} // namespace
int RunDocumentSelectionGestureAdmissionAuthorityTests()
{
    Run(Change::Crop, "crop");
    Run(Change::Active, "active");
    Run(Change::Mask, "mask");
    Run(Change::History, "history-begin");
    std::printf("SUMMARY selection-gesture-admission-authority checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
