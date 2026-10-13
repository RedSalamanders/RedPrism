#include "Document.Tests.PersistentHistory.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int failures = 0;
int controls = 0;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s persistent-history.%s\n", condition ? "PASS" : "FAIL", name);
    ++controls;
    if (!condition)
        ++failures;
}
bool Publish(DocumentHistory& history, DocumentHistory::Publication& publication)
{
    DocumentHistory::ResidencyPermit permit;
    return permit.TryAcquire(publication) == DocumentHistory::ResidencyValidation::Ready &&
           history.Publish(publication);
}
SelectionLoops Loops(int step)
{
    std::vector<SelectionLoop> loops;
    loops.reserve(static_cast<std::size_t>(step % 7 + 2));
    loops.emplace_back();
    loops.back().reserve(static_cast<std::size_t>(step % 11 + 5));
    const auto x = static_cast<double>(step);
    loops.back() = {{x, 0}, {x + 4, 0}, {x + 4, 4}, {x, 4}};
    return SelectionLoops{std::move(loops)};
}
void Compare(const DocumentHistory& candidate, const DocumentHistory& reference,
             const std::optional<CanvasDocument>& current)
{
    Expect(candidate.RetainedBytes(current) == reference.RetainedBytes(current),
           "exact-unique-retained-bytes-match-reference-traversal");
    Expect(candidate.UndoCount() == reference.UndoCount() &&
               candidate.CaptureState().FutureCount() == reference.CaptureState().FutureCount(),
           "oldest-first-trim-and-stack-counts-match-reference");
    Expect(candidate.CanUndo() == reference.CanUndo() && candidate.CanRedo() == reference.CanRedo() &&
               candidate.UndoName() == reference.UndoName() && candidate.RedoName() == reference.RedoName(),
           "undo-redo-availability-and-names-match-reference");
}
bool RestorePair(DocumentHistory& candidate, DocumentHistory& reference, std::optional<CanvasDocument>& current,
                 int& pairs)
{
    const bool available = reference.CanUndo();
    auto originalUndo = reference.Undo();
    auto preparedUndo = DocumentHistory::PrepareRestore(candidate.CaptureState(), false);
    if (available != originalUndo.has_value() || available != preparedUndo.has_value())
        return false;
    if (!available)
        return true;
    if (!Publish(candidate, *preparedUndo))
        return false;
    auto restored = preparedUndo->TakeRestoreSnapshot();
    if (!restored || restored->document != originalUndo->document)
        return false;
    current = restored->document;
    if (candidate.RetainedBytes(current) != reference.RetainedBytes(current) ||
        candidate.UndoCount() != reference.UndoCount() ||
        candidate.CaptureState().FutureCount() != reference.CaptureState().FutureCount())
        return false;
    auto originalRedo = reference.Redo();
    auto preparedRedo = DocumentHistory::PrepareRestore(candidate.CaptureState(), true);
    if (originalRedo.has_value() != preparedRedo.has_value())
        return false;
    if (originalRedo)
    {
        if (!Publish(candidate, *preparedRedo))
            return false;
        auto redone = preparedRedo->TakeRestoreSnapshot();
        if (!redone || redone->document != originalRedo->document)
            return false;
        current = redone->document;
    }
    ++pairs;
    return true;
}
void RunBudget(int entries, std::uint64_t bytes)
{
    DocumentHistory reference(entries, bytes);
    DocumentHistory candidate(entries, bytes);
    const auto base = Raster::Make(8, 8, RasterFormat::Bgra8888);
    const auto patch = Raster::Make(1, 1, RasterFormat::Bgra8888);
    const std::array images{
        base, Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, base, 0, 0, {{0, 0, patch, 0, 0, 1, 1}}, 0),
        Raster::Make(8, 8, RasterFormat::Bgra8888)};
    const std::array masks{Raster::Make(8, 8, RasterFormat::Gray8), Raster::Make(8, 8, RasterFormat::Gray8)};
    DocumentFixture fixture;
    fixture.width = 32;
    fixture.height = 32;
    auto layer = ImageLayer::Blank(L"Shared", {8, 8});
    layer.asset = RasterAsset{base, base, L"Shared"};
    layer.shapeImage = base;
    fixture.layers.push_back(layer);
    layer.id = Guid::Generate();
    fixture.layers.push_back(std::move(layer));
    std::optional<CanvasDocument> current{std::move(fixture).Seal()};
    int restorePairs = 0;
    for (int step = 0; step < 32; ++step)
    {
        auto changed = DocumentFixture::Copy(*current);
        changed.width = 33 + step;
        changed.layers[0].asset =
            RasterAsset{images[static_cast<std::size_t>(step / 3) % images.size()], base, L"Changed"};
        changed.layers[0].name.assign(static_cast<std::size_t>(step % 13 + 1), L'A');
        const auto mask = masks[static_cast<std::size_t>(step) % masks.size()];
        changed.layers[1].mask = LayerMask{RasterAsset{mask, mask, L"Mask"}};
        if (step % 4 != 0)
            changed.selection = DocumentSelection{true, Loops(step)};
        else
            changed.selection.reset();
        std::optional<CanvasDocument> after{std::move(changed).Seal()};
        reference.Begin(L"Persistent edit", current, {});
        reference.End(after, {});
        auto publication =
            DocumentHistory::PrepareEdit(candidate.CaptureState(), current, {}, after, {}, L"Persistent edit");
        const bool published = publication && Publish(candidate, *publication);
        Expect(published, "changed-images-masks-selections-and-metadata-publish");
        if (!published)
            return;
        current = std::move(after);
        Compare(candidate, reference, current);
        if (step == 9)
        {
            Expect(images[1].Data() != nullptr, "late-sparse-materialization-publishes-literal-cache");
            Compare(candidate, reference, current);
        }
        if (step == 11 || step == 23)
        {
            const bool same = RestorePair(candidate, reference, current, restorePairs);
            Expect(same, "undo-redo-presence-permit-payload-and-charges-match-reference");
            if (!same)
                return;
            Compare(candidate, reference, current);
        }
    }
    Expect((bytes != 1024 * 1024 || restorePairs == 2) && (bytes != 1 || restorePairs == 0),
           "large-budget-restores-both-pairs-and-one-byte-budget-retains-none");
    const auto originalRevision = candidate.CurrentRevision();
    const auto originalUndoCount = candidate.UndoCount();
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    auto after = *current;
    after.width += 1;
    auto cancelled = DocumentHistory::PrepareEdit(candidate.CaptureState(), current, {}, after, {}, L"Cancelled",
                                                  cancellation.Token());
    Expect(!cancelled && candidate.CurrentRevision() == originalRevision && candidate.UndoCount() == originalUndoCount,
           "pre-cancel-refusal-preserves-published-history");
    Compare(candidate, reference, current);
}
} // namespace

int RunPersistentHistoryTests()
{
    failures = 0;
    controls = 0;
    RunBudget(7, 1024 * 1024);
    RunBudget(31, 512);
    RunBudget(100, 1);
    std::printf("%s persistent-history.summary passed=%d failed=%d controls=%d\n", failures ? "FAIL" : "PASS",
                controls - failures, failures, controls);
    return failures;
}
