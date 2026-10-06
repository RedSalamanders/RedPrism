#pragma once

#include "DeferredRelease.h"
#include "Document.h"
#include "Jobs.h"
#include "PixelBuffer.h"

#include <memory>

struct CloneReadbackTestGate;
// The original readback admission stays charged through the live clone stroke and final worker retirement.
// Declare the lease first so the document/pixel owners die before its acknowledgement releases the charge.
#pragma warning(push)
#pragma warning(disable : 4324)
struct CloneReadbackState final : DeferredReleaseNode
{
    SharedRetainedMemoryLease lease;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster image;
    Raster composite;
    std::uint64_t opening = 0;
    std::shared_ptr<CloneReadbackTestGate> gate;
    std::uint32_t ownerThread = 0;

    CloneReadbackState() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<CloneReadbackState> owned(static_cast<CloneReadbackState*>(node)); };
    }
    ~CloneReadbackState();
    struct Retire final
    {
        void operator()(CloneReadbackState* state) const noexcept
        {
            if (state && !TryDeferRelease(*state, DeferredReleasePolicy::AlwaysOnOwner))
            {
                const std::unique_ptr<CloneReadbackState> owned(state);
            }
        }
    };
};
#pragma warning(pop)
