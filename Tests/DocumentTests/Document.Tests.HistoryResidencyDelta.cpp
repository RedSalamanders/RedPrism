#include "Document.Tests.HistoryResidencyDelta.h"
#include "Document.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <cstdio>
#include <optional>
#include <utility>

namespace
{
int failures = 0;
int controls = 0;
void Expect(bool value, const char* label) noexcept
{
    std::printf("%s history-residency-delta.%s\n", value ? "PASS" : "FAIL", label);
    ++controls;
    failures += value ? 0 : 1;
}
struct Visits
{
    std::size_t records = 0;
    std::size_t full = 0;
    CancelSource* cancel = nullptr;
};
void Visit(void* context, DocumentHistory::PrepareStage stage)
{
    auto& visits = *static_cast<Visits*>(context);
    if (stage == DocumentHistory::PrepareStage::StorageIndexResidencyRecord)
    {
        ++visits.records;
        if (visits.cancel)
            static_cast<void>(visits.cancel->Cancel());
    }
    if (stage == DocumentHistory::PrepareStage::StorageIndexResidencyFullScan)
        ++visits.full;
}
bool Publish(DocumentHistory& history, DocumentHistory::Publication& publication)
{
    DocumentHistory::ResidencyPermit permit;
    return permit.TryAcquire(publication) == DocumentHistory::ResidencyValidation::Ready &&
           history.Publish(publication);
}
Raster GrowOnePixel()
{
    return Raster::MakeSparse(1, 1, RasterFormat::Gray8, {}, 0, 0, {}, 37);
}
void TestHistoryDelta()
{
    const auto base = Raster::Make(8, 8, RasterFormat::Bgra8888);
    const auto sparse = Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    DocumentFixture fixture;
    fixture.width = 64;
    fixture.height = 64;
    for (int index = 0; index < 32; ++index)
    {
        const auto image = index ? Raster::Make(1, 1, RasterFormat::Bgra8888) : sparse;
        auto layer = ImageLayer::Blank(L"Journal reference",
                                       {static_cast<double>(image.Width()), static_cast<double>(image.Height())});
        layer.asset = RasterAsset{image, image, L"Journal reference"};
        layer.shapeImage = {};
        fixture.layers.push_back(std::move(layer));
    }
    std::optional<CanvasDocument> original{std::move(fixture).Seal()};
    auto changed = DocumentFixture::Copy(*original);
    changed.width = 65;
    changed.layers[0].asset = RasterAsset{base, base, L"Journal reference"};
    std::optional<CanvasDocument> current{std::move(changed).Seal()};
    DocumentHistory candidate(100, 1024 * 1024);
    DocumentHistory reference(100, 1024 * 1024);
    reference.Begin(L"Journal edit", original, {});
    reference.End(current, {});
    Visits initial;
    auto first = DocumentHistory::PrepareEdit(candidate.CaptureState(), original, {}, current, {}, L"Journal edit", {},
                                              {&Visit, &initial});
    const bool initialized = first && Publish(candidate, *first);
    Expect(initialized && initial.records == 0 && initial.full == 33,
           "bootstrap-scans-33-unique-raster-owners-on-worker");
    if (!initialized)
        return;
    Expect(candidate.RetainedBytes(current) == 0 &&
               candidate.RetainedBytes(current) == reference.RetainedBytes(current),
           "unmaterialized-retained-sparse-has-zero-literal-charge");
    const auto revision = candidate.CurrentRevision();
    const auto undo = candidate.UndoCount();
    Visits unchanged;
    auto maintenance =
        DocumentHistory::PrepareResidencyMaintenance(candidate.CaptureState(), current, {}, {&Visit, &unchanged});
    Expect(maintenance && unchanged.records == 0 && unchanged.full == 0,
           "unchanged-epoch-maintenance-scans-no-raster-records");
    if (!maintenance)
        return;
    Expect(sparse.Data() != nullptr && sparse.ByteSize() == 256,
           "late-retained-sparse-materialization-has-256-literal-bytes");
    DocumentHistory::ResidencyPermit stale;
    Expect(stale.TryAcquire(*maintenance) == DocumentHistory::ResidencyValidation::Refresh &&
               candidate.CurrentRevision() == revision && candidate.UndoCount() == undo,
           "stale-owner-witness-refuses-before-publication");
    Visits delta;
    const bool refreshed = DocumentHistory::RefreshPreparedResidency(*maintenance, current, {}, {&Visit, &delta});
    Expect(refreshed && delta.records == 1 && delta.full == 0,
           "one-growth-refresh-consumes-one-event-without-33-record-rescan");
    Expect(refreshed && Publish(candidate, *maintenance) && candidate.CurrentRevision() == revision &&
               candidate.UndoCount() == undo && candidate.RetainedBytes(current) == 256 &&
               candidate.RetainedBytes(current) == reference.RetainedBytes(current),
           "delta-maintenance-publishes-exact-reference-charge-without-user-edit");
    bool unrelatedGrew = false;
    {
        const auto unrelated = GrowOnePixel();
        const auto* pixels = unrelated.Data();
        unrelatedGrew = pixels && pixels[0] == 37;
    }
    Visits unrelated;
    auto ignored =
        DocumentHistory::PrepareResidencyMaintenance(candidate.CaptureState(), current, {}, {&Visit, &unrelated});
    Expect(unrelatedGrew && ignored && unrelated.records == 1 && unrelated.full == 0 && Publish(candidate, *ignored) &&
               candidate.RetainedBytes(current) == 256,
           "freed-unrelated-storage-event-costs-one-lookup-and-changes-no-charge");
    const auto another = GrowOnePixel();
    const bool anotherGrew = another.Data() != nullptr;
    CancelSource cancellation;
    Visits cancelled{0, 0, &cancellation};
    auto refused = DocumentHistory::PrepareResidencyMaintenance(candidate.CaptureState(), current, cancellation.Token(),
                                                                {&Visit, &cancelled});
    Expect(anotherGrew && !refused && cancelled.records == 1 && cancelled.full == 0 &&
               candidate.CurrentRevision() == revision && candidate.UndoCount() == undo &&
               candidate.RetainedBytes(current) == 256,
           "mid-delta-cancellation-preserves-published-history");
    Visits retry;
    auto complete =
        DocumentHistory::PrepareResidencyMaintenance(candidate.CaptureState(), current, {}, {&Visit, &retry});
    Expect(complete && retry.records == 1 && retry.full == 0 && Publish(candidate, *complete),
           "uncancelled-fresh-preparation-still-sees-the-original-event");
    bool wrapped = true;
    for (std::size_t index = 0; index <= Raster::kResidencyJournalCapacity; ++index)
    {
        const auto retired = GrowOnePixel();
        const auto* bytes = retired.Data();
        wrapped = wrapped && bytes && bytes[0] == 37;
    }
    Visits fallback;
    auto resampled =
        DocumentHistory::PrepareResidencyMaintenance(candidate.CaptureState(), current, {}, {&Visit, &fallback});
    Expect(wrapped && resampled && fallback.records == 0 && fallback.full == 33,
           "overwritten-4097-event-window-falls-back-to-33-record-worker-rescan");
    Expect(resampled && Publish(candidate, *resampled) && candidate.CurrentRevision() == revision &&
               candidate.UndoCount() == undo && candidate.RetainedBytes(current) == 256 &&
               candidate.RetainedBytes(current) == reference.RetainedBytes(current),
           "overflow-fallback-preserves-reference-bytes-revision-and-history");
}
} // namespace
int RunHistoryResidencyDeltaTests()
{
    failures = 0;
    controls = 0;
    TestHistoryDelta();
    std::printf("%s history-residency-delta.summary passed=%d failed=%d controls=%d\n", failures ? "FAIL" : "PASS",
                controls - failures, failures, controls);
    return failures;
}
