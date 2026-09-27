#include "WorkerPool.h"

struct ConcurrencyAnalysisProbe final
{
    SRWLOCK lock = SRWLOCK_INIT;
    _Guarded_by_(lock) int value = 0;

    void Safe() noexcept
    {
        WorkerResources::ExclusiveLock guard{lock};
        ++value;
    }

#ifdef REDPRISM_NEGATIVE_ANALYSIS_PROBE
    void Unsafe() noexcept
    {
        ++value;
    }
#endif
};

void RunConcurrencyAnalysisProbe(ConcurrencyAnalysisProbe& probe) noexcept
{
    probe.Safe();
#ifdef REDPRISM_NEGATIVE_ANALYSIS_PROBE
    probe.Unsafe();
#endif
}
