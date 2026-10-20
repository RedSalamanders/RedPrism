#pragma once

#include "DeferredRelease.h"
#include "DocumentSizeHistory.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <memory>

// Borrowed callbacks outlive this nested stage through its Completed or joined-Close hook. The original
// parent is the sole owner; its terminal notification can retire that context immediately afterwards.
// Current/wait/residency/applied/completed run on the owner. Refresh/prepared run on the original worker.
// Callbacks cannot dispatch messages, reserve a job or replace the immutable final source.
struct FinalPublicationCallbacks final
{
    void* context = nullptr;
    bool (*currentOwner)(void*) noexcept = nullptr;
    void (*waitingOwner)(void*, bool) noexcept = nullptr;
    void (*residencyTryOwner)(void*) noexcept = nullptr;
    void (*refreshForWork)(void*) noexcept = nullptr;
    void (*preparedForWork)(void*, void* shutdownEvent) noexcept = nullptr;
    void (*appliedOwner)(void*) noexcept = nullptr;
    void (*completedOwner)(void*, bool applied, JobResult) noexcept = nullptr;
};

struct DestroyFinalParentForWork final
{
    void operator()(DeferredReleaseNode* parent) const noexcept
    {
        if (parent)
            parent->destroy(parent);
    }
};

// Construct/move/destroy only on the original worker. The caller admits the complete CPU/output overlap
// and both accounting owners before capture. A parent payload retains all old source/history/result
// aliases for worker retirement, including construction failure, cancellation and joined shutdown.
struct SizeFinalPublicationWork final
{
    std::shared_ptr<SharedPublicationMemory> finalMemory;
    std::shared_ptr<SharedPublicationMemory> temporaryMemory;
    PreparedDocumentSize publication;
    std::unique_ptr<DeferredReleaseNode, DestroyFinalParentForWork> parent;
    std::uint64_t remainingCpuBytes = 0, finalOutputBytes = 0;
    std::uint32_t ownerThread = 0;
    std::uint64_t displayCpuBytes = 0, displayGpuBytes = 0;
};
