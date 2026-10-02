#pragma once

#include <atomic>
#include <memory>
#include <windows.h>

class BrushStroke;

// Borrowed harness-only evidence; its owner outlives Shutdown's final callback acknowledgement.
struct ApplicationShutdownTestProbe final
{
    DWORD owner = GetCurrentThreadId();
    std::weak_ptr<BrushStroke> draft;
    std::weak_ptr<void> liveDraft;
    bool liveDraftAliveAtDetach = false;
    std::atomic<bool> liveDraftRetired{false};
    unsigned int ownerCalls = 0;
    bool draftAliveAtDetach = false;
    bool graphicsDetachedOnOwner = false;
    bool reservationAndWakeAlive = false;
    void (*checkOwner)(void*) noexcept = nullptr;
    void* checkContext = nullptr;
    std::atomic<unsigned int> workerCalls{0};
    std::atomic<bool> mta{false}, offOwner{false}, draftRetired{false};
    std::atomic<bool> hostCpuRootsRetired{false};
};
