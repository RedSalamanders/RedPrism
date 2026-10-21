#pragma once

#include "Jobs.h"
#include "PixelMoveClosureWork.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <memory>

struct SelectionOutlineGestureState;

// A worker constructs this optional stage object inside its already admitted original gesture.
// It is never submitted to the scheduler. The parent forwards its existing ticket/context and retains
// the same FIFO position, cancellation, descriptor slot and accounting endpoint through final retirement.
struct SelectionPixelFinalWork final
{
    PreparedPixelMoveClosure closure;
    std::shared_ptr<SharedPublicationMemory> finalMemory;
    std::shared_ptr<SharedPublicationMemory> temporaryMemory;
    std::uint64_t remainingCpuBytes;
    std::uint64_t finalOutputBytes;
    std::uint32_t ownerThread = 0; // Owner-captured scalar, never a worker HWND/session query.
    std::uint64_t displayCpuBytes = 0, displayGpuBytes = 0;
};

// The factory runs on the worker and may only own its moved immutable work and store borrowed host/result
// pointers. Host/session/device capture is owner-only after original-stage acknowledgement/admission.
// Its first RetireWorkerStage can follow factory construction before any delegated Run; the initial
// stage must acknowledge this without inventing a producer. Cancellation may skip that first owner hook.
// A null return retires all moved work on the worker and refuses; it never selects the CPU fallback.
// The returned stage object's Completed writes the state's scalar result without notifying any observer.
// Its owner-side destructor must not access that state: the parent's terminal observer may retire it first.
using SelectionPixelFinalFactory = std::unique_ptr<Job> (*)(void* host, SelectionOutlineGestureState& result,
                                                            SelectionPixelFinalWork work);
