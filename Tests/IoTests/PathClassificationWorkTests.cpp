#include "PathClassificationWorkTests.h"

#include "PathClassificationWork.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
unsigned long long gScratchSequence = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s pathwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

class ScratchDirectory final
{
  public:
    ScratchDirectory()
    {
        std::array<wchar_t, 32768> tempPath{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
        if (length == 0 || static_cast<std::size_t>(length) >= tempPath.size())
        {
            return;
        }

        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            _path.assign(tempPath.data(), length);
            _path.append(L"RedPrism.PathClassification.");
            _path.append(std::to_wstring(GetCurrentProcessId()));
            _path.push_back(L'.');
            _path.append(std::to_wstring(++gScratchSequence));
            if (CreateDirectoryW(_path.c_str(), nullptr))
            {
                _created = true;
                return;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                return;
            }
        }
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory()
    {
        if (_created)
        {
            std::error_code error;
            std::filesystem::remove_all(_path, error);
        }
    }

    [[nodiscard]] bool IsCreated() const noexcept
    {
        return _created;
    }

    [[nodiscard]] const std::wstring& Path() const noexcept
    {
        return _path;
    }

    [[nodiscard]] std::wstring Child(std::wstring_view name) const
    {
        std::wstring path = _path;
        path.push_back(L'\\');
        path.append(name);
        return path;
    }

  private:
    std::wstring _path;
    bool _created = false;
};

[[nodiscard]] bool WriteBytes(const std::wstring& path, std::span<const std::uint8_t> bytes) noexcept
{
    wil::unique_hfile file{
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        return false;
    }
    DWORD written = 0;
    return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
           static_cast<std::size_t>(written) == bytes.size();
}

[[nodiscard]] bool WriteSignature(const std::wstring& path, std::array<std::uint8_t, 4> signature) noexcept
{
    return WriteBytes(path, signature);
}

[[nodiscard]] bool HasOnlyStatus(const PathClassificationBatch& result, PathClassificationStatus status) noexcept
{
    return result.status == status && result.count == 0;
}

struct ScratchFactoryContext final
{
    unsigned int calls = 0;
    CancelSource* cancellation = nullptr;
    bool refuse = false;
};

[[nodiscard]] std::unique_ptr<PathClassificationScratch> CreateTestScratch(void* rawContext) noexcept
{
    auto& context = *static_cast<ScratchFactoryContext*>(rawContext);
    ++context.calls;
    if (context.cancellation)
    {
        (void)context.cancellation->Cancel();
    }
    if (context.refuse)
    {
        return nullptr;
    }
    return std::unique_ptr<PathClassificationScratch>(new (std::nothrow) PathClassificationScratch{});
}

void ClassificationRules()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "scratch.created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring folder = scratch.Child(L"exploded");
    const std::wstring png = scratch.Child(L"ordinary.png");
    const std::wstring zipLocal = scratch.Child(L"local.zip");
    const std::wstring zipEmpty = scratch.Child(L"empty.zip");
    const std::wstring zipSpanned = scratch.Child(L"spanned.zip");
    const std::wstring shortFile = scratch.Child(L"short.bin");
    const std::array<std::uint8_t, 4> pngHeader{'P', 'N', 'G', '!'};
    const std::array<std::uint8_t, 2> shortBytes{'P', 'K'};
    Expect(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "fixture.folder-created");
    Expect(WriteBytes(png, pngHeader), "fixture.png-created");
    Expect(WriteSignature(zipLocal, {'P', 'K', 3, 4}), "fixture.zip-local-created");
    Expect(WriteSignature(zipEmpty, {'P', 'K', 5, 6}), "fixture.zip-empty-created");
    Expect(WriteSignature(zipSpanned, {'P', 'K', 7, 8}), "fixture.zip-spanned-created");
    Expect(WriteBytes(shortFile, shortBytes), "fixture.short-created");

    const std::wstring missingProject = scratch.Child(L"missing.PRISM");
    const std::wstring missingImage = scratch.Child(L"missing.png");
    const std::array<std::wstring, 10> ownedPaths{folder,
                                                  missingProject,
                                                  png,
                                                  zipLocal,
                                                  zipEmpty,
                                                  zipSpanned,
                                                  shortFile,
                                                  missingImage,
                                                  scratch.Child(L"UPPER.PrIsM"),
                                                  scratch.Child(L"missing.prism")};
    const std::array<std::wstring_view, 10> views{ownedPaths[0], ownedPaths[1], ownedPaths[2], ownedPaths[3],
                                                  ownedPaths[4], ownedPaths[5], ownedPaths[6], ownedPaths[7],
                                                  ownedPaths[8], ownedPaths[9]};
    const PathClassificationBatch result = ClassifyPathBatch(views);
    Expect(result.status == PathClassificationStatus::Ok && result.count == views.size(), "rules.batch-completed");
    if (result.status == PathClassificationStatus::Ok && result.count == views.size())
    {
        Expect(result.classifications[0] == PathClassification::Project, "rules.exploded-folder");
        Expect(result.classifications[1] == PathClassification::Project, "rules.missing-uppercase-comp");
        Expect(result.classifications[2] == PathClassification::Image, "rules.nonproject-png");
        Expect(result.classifications[3] == PathClassification::Project, "rules.zip-local-signature");
        Expect(result.classifications[4] == PathClassification::Project, "rules.zip-empty-signature");
        Expect(result.classifications[5] == PathClassification::Project, "rules.zip-spanned-signature");
        Expect(result.classifications[6] == PathClassification::Image, "rules.short-file");
        Expect(result.classifications[7] == PathClassification::Image, "rules.missing-non-comp-file");
        Expect(result.classifications[8] == PathClassification::Project, "rules.mixed-case-comp");
        Expect(result.classifications[9] == PathClassification::Project, "rules.missing-lowercase-comp");
    }

    const std::wstring unreadable = scratch.Child(L"unreadable.bin");
    Expect(WriteBytes(unreadable, pngHeader), "fixture.unreadable-created");
    {
        wil::unique_hfile exclusive{
            CreateFileW(unreadable.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        Expect(static_cast<bool>(exclusive), "fixture.unreadable-locked");
        const std::array<std::wstring_view, 1> unreadableView{unreadable};
        const PathClassificationBatch unreadableResult = ClassifyPathBatch(unreadableView);
        Expect(unreadableResult.status == PathClassificationStatus::Ok && unreadableResult.count == 1 &&
                   unreadableResult.classifications[0] == PathClassification::Image,
               "rules.existing-but-unreadable-is-image");
    }
}

void InvalidAndCancelledBatches()
{
    ScratchDirectory scratch;
    if (!scratch.IsCreated())
    {
        Expect(false, "invalid.scratch-created");
        return;
    }

    const std::wstring unchanged = scratch.Child(L"source-is-not-mutated.prism");
    const std::array<std::wstring, 1> owned{unchanged};
    const std::array<std::wstring_view, 1> views{owned[0]};
    CancelSource cancelled;
    (void)cancelled.Cancel();
    const PathClassificationBatch result = ClassifyPathBatch(views, cancelled.Token());
    Expect(HasOnlyStatus(result, PathClassificationStatus::Cancelled), "cancel.pre-cancel-publishes-no-result");
    Expect(owned[0] == unchanged && views[0] == unchanged, "cancel.pre-cancel-preserves-source");

    const std::wstring tooLong(kMaxPathClassificationLength + 1, L'x');
    const std::array<std::wstring_view, 1> overlong{tooLong};
    const PathClassificationBatch overlongResult = ClassifyPathBatch(overlong);
    Expect(HasOnlyStatus(overlongResult, PathClassificationStatus::InvalidInput), "invalid.overlong-path");

    std::array<std::wstring, kMaxPathClassificationBatch + 1> manyOwned{};
    std::array<std::wstring_view, kMaxPathClassificationBatch + 1> manyViews{};
    for (std::size_t index = 0; index < manyOwned.size(); ++index)
    {
        manyOwned[index] = unchanged;
        manyViews[index] = manyOwned[index];
    }
    const PathClassificationBatch tooManyResult = ClassifyPathBatch(manyViews);
    Expect(HasOnlyStatus(tooManyResult, PathClassificationStatus::InvalidInput), "invalid.path-count-over-256");

    const std::array<std::wstring_view, 1> emptyPath{std::wstring_view{}};
    const PathClassificationBatch emptyPathResult = ClassifyPathBatch(emptyPath);
    Expect(HasOnlyStatus(emptyPathResult, PathClassificationStatus::InvalidInput), "invalid.empty-path");

    const std::array<wchar_t, 3> embeddedNullPath{L'a', L'\0', L'b'};
    const std::array<std::wstring_view, 1> embeddedNull{
        std::wstring_view{embeddedNullPath.data(), embeddedNullPath.size()}};
    const PathClassificationBatch embeddedNullResult = ClassifyPathBatch(embeddedNull);
    Expect(HasOnlyStatus(embeddedNullResult, PathClassificationStatus::InvalidInput), "invalid.embedded-null");
}

void EmptyBatchesAndBoundedViews()
{
    const PathClassificationBatch empty = ClassifyPathBatch(std::span<const std::wstring_view>{});
    Expect(empty.status == PathClassificationStatus::Ok && empty.count == 0, "empty.batch-succeeds-with-zero-count");

    CancelSource cancelled;
    (void)cancelled.Cancel();
    const PathClassificationBatch cancelledEmpty =
        ClassifyPathBatch(std::span<const std::wstring_view>{}, cancelled.Token());
    Expect(HasOnlyStatus(cancelledEmpty, PathClassificationStatus::Cancelled),
           "empty.pre-cancelled-batch-is-cancelled");

    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "bounded.scratch-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring original = scratch.Child(L"bounded.PRISM");
    std::wstring backing = original;
    backing.append(L".suffix");
    const std::wstring backingBefore = backing;
    const std::wstring_view nonTerminatedView{backing.data(), original.size()};
    const std::array<std::wstring_view, 1> views{nonTerminatedView};
    const PathClassificationBatch bounded = ClassifyPathBatch(views);
    Expect(bounded.status == PathClassificationStatus::Ok && bounded.count == 1 &&
               bounded.classifications[0] == PathClassification::Project,
           "bounded.counted-uppercase-comp-subview-is-project");
    Expect(backing == backingBefore && nonTerminatedView == original, "bounded.classification-preserves-input-source");
}

void ScratchAllocationAndCancellation()
{
    constexpr std::wstring_view requested = L"counted.PRISM";
    const std::wstring source = std::wstring(requested) + L".trailing-text";
    const std::wstring sourceBefore = source;
    const std::array<std::wstring_view, 1> views{std::wstring_view{source.data(), requested.size()}};

    ScratchFactoryContext refused{0, nullptr, true};
    const PathClassificationBatch outOfMemory =
        ClassifyPathBatch(views, {}, PathClassificationScratchSource{&refused, CreateTestScratch});
    Expect(outOfMemory.status == PathClassificationStatus::OutOfMemory && outOfMemory.count == 0,
           "scratch.refusal-is-out-of-memory");
    Expect(std::all_of(outOfMemory.classifications.begin(), outOfMemory.classifications.end(),
                       [](PathClassification value) { return value == PathClassification{}; }),
           "scratch.refusal-publishes-default-classifications");
    Expect(source == sourceBefore && views[0] == requested, "scratch.refusal-preserves-source");
    Expect(refused.calls == 1, "scratch.refusal-calls-factory-once");

    CancelSource allocationCancelled;
    ScratchFactoryContext cancelAtAllocation{0, &allocationCancelled, true};
    const PathClassificationBatch cancelled = ClassifyPathBatch(
        views, allocationCancelled.Token(), PathClassificationScratchSource{&cancelAtAllocation, CreateTestScratch});
    Expect(HasOnlyStatus(cancelled, PathClassificationStatus::Cancelled), "scratch.allocation-cancellation-wins");
    Expect(cancelAtAllocation.calls == 1, "scratch.cancel-calls-factory-once");

    ScratchFactoryContext emptyFactory{};
    const PathClassificationBatch empty = ClassifyPathBatch(
        std::span<const std::wstring_view>{}, {}, PathClassificationScratchSource{&emptyFactory, CreateTestScratch});
    Expect(empty.status == PathClassificationStatus::Ok && empty.count == 0 && emptyFactory.calls == 0,
           "scratch.empty-batch-skips-factory");

    const std::array<std::wstring_view, 1> invalidPath{std::wstring_view{}};
    ScratchFactoryContext invalidFactory{};
    const PathClassificationBatch invalid =
        ClassifyPathBatch(invalidPath, {}, PathClassificationScratchSource{&invalidFactory, CreateTestScratch});
    Expect(HasOnlyStatus(invalid, PathClassificationStatus::InvalidInput) && invalidFactory.calls == 0,
           "scratch.invalid-input-skips-factory");

    CancelSource preCancelled;
    (void)preCancelled.Cancel();
    ScratchFactoryContext preCancelledFactory{};
    const PathClassificationBatch alreadyCancelled = ClassifyPathBatch(
        views, preCancelled.Token(), PathClassificationScratchSource{&preCancelledFactory, CreateTestScratch});
    Expect(HasOnlyStatus(alreadyCancelled, PathClassificationStatus::Cancelled) && preCancelledFactory.calls == 0,
           "scratch.precancelled-batch-skips-factory");

    ScratchFactoryContext successful{};
    const PathClassificationBatch customScratch =
        ClassifyPathBatch(views, {}, PathClassificationScratchSource{&successful, CreateTestScratch});
    Expect(customScratch.status == PathClassificationStatus::Ok && customScratch.count == 1 &&
               customScratch.classifications[0] == PathClassification::Project,
           "scratch.custom-buffer-classifies-counted-comp-view");
    Expect(source == sourceBefore && views[0] == requested, "scratch.custom-buffer-preserves-source");
    Expect(successful.calls == 1, "scratch.success-calls-factory-once");
}
} // namespace

int RunPathClassificationWorkTests()
{
    gFailures = 0;
    ClassificationRules();
    InvalidAndCancelledBatches();
    EmptyBatchesAndBoundedViews();
    ScratchAllocationAndCancellation();
    return gFailures;
}
