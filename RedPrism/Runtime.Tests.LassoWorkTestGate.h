#pragma once

#include "Jobs.h"
#include "SharedPublicationMemory.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Explicit fixture controls only. Configure before admission and keep this owner through joined Close.
struct LassoWorkTestGate final
{
    enum class Stage : std::uint8_t
    {
        OpeningAllocated,
        OpeningPrepared,
        FinishAllocated,
        FinishPrepared
    };
    static constexpr std::uint32_t kMessage = WM_APP + 0x6c8;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::optional<Stage> failAt;
    std::optional<Stage> holdAt;
    std::atomic<unsigned int> openingCaptures{0}, finishCaptures{0};
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> retired{false}, retiredOffOwner{false};
    std::weak_ptr<const SharedPublicationMemory> openingMemory, finishMemory, finishOutputMemory;
    [[nodiscard]] JobResult Reach(Stage stage, std::uint32_t ownerThread, JobContext& context)
    {
        if (failAt == stage)
            throw std::bad_alloc{}; // Reached original runner boundary; no translated partial publication.
        if (holdAt != stage)
            return {};
        visits.fetch_add(1, std::memory_order_release);
        if (!PostThreadMessageW(ownerThread, kMessage, static_cast<WPARAM>(stage), 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE waits[]{release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, waits, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
};
