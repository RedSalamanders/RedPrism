#pragma once

#include "Jobs.h"
#include "ProjectStore.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct ProjectSaveTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 0x4B5;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool afterPublication = false;
    bool throwAllocationFailure = false;
    bool throwAdmissionFailure = false;
    bool traceOnly = false; // collect actual asynchronous save stages without holding the worker
    struct Observation final
    {
        ProjectIoStage stage;
        unsigned int value;
    };
    std::array<Observation, 32> observations{}; // worker writes; harness reads only after acknowledgement
    std::size_t observationCount = 0;
    bool traceTruncated = false;
    // Immutable harness request before submission. Worker-only writes; read after the original acknowledgement.
    ProjectSaveFaultStage faultStageForTest = ProjectSaveFaultStage::Count;
    std::uint32_t faultErrorForTest = ERROR_WRITE_FAULT;
    ProjectSaveFaultStage precedingFaultStageForTest = ProjectSaveFaultStage::Count;
    unsigned int faultAtVisitForTest = 1; // Immutable harness request; repeated cleanup has distinct boundaries.
    std::array<unsigned int, static_cast<std::size_t>(ProjectSaveFaultStage::Count)> faultVisits{};
    bool faultTraceTruncated = false;
    bool createAssetCollisionForTest = false; // Immutable, exclusive-fixture request; no failure is synthesized.
    std::wstring assetStagingPatternForTest, assetStagingParentForTest, assetLeafForTest;
    unsigned int assetStagingMatchesForTest = 0;
    bool assetCollisionCreatedForTest = false;
    std::uint32_t assetCollisionErrorForTest = ERROR_SUCCESS;
    bool holdAtFaultForTest = false; // Immutable request; original worker gate releases after WM_ENDSESSION returns.
    JobResult heldFaultResultForTest;
    bool heldFaultCancelledForTest = false;
    // Immutable request/path; the worker owns the real child reader and writes facts before acknowledgement.
    bool holdRecoveryReaderForTest = false;
    std::wstring recoveryManifestForTest;
    bool recoveryReaderOpenedForTest = false;
    std::uint32_t recoveryReaderErrorForTest = ERROR_SUCCESS;
    bool recoveryDeleteAccessDeniedWhileHeldForTest = false;
    std::uint32_t recoveryDeleteAccessErrorWhileHeldForTest = ERROR_SUCCESS;
    bool recoveryReaderRetiredForTest = false;
    ProjectIoStatus saveStatus = ProjectIoStatus::Encode;
    JobResult result;
    std::atomic<unsigned int> visits{0};
};
