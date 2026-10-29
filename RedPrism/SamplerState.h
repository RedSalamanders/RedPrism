#pragma once

#include "ChromeEvents.h"
#include "ContinuationReceipt.h"
#include "Document.h"
#include "Jobs.h"
#include "SamplerIntentInput.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct SamplerPickerBinding final
{
    Guid host;
    std::uint64_t opening = 0;
    PickerTarget target = PickerTarget::Foreground;
};

// A younger input resolves this exact older prefix, even if a subsequent manual edit suppresses UI publication.
struct SamplerColorDependency final
{
    std::uint64_t opening = 0;
    DocumentSession::PaletteIntentAuthority intent;
    std::uint32_t fallbackArgb = 0xFF000000u;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct SamplerOpening final
{
    // Owner-only fixed metadata. CPU document/pixel owners belong solely to the admitted job's release aggregate.
    SamplerIntentInput points;
    std::uint64_t opening = 0;
    DocumentReadAuthority source;
    std::optional<SamplerPickerBinding> picker;
    std::optional<SamplerPointIntent> latestAcceptedIntent;
    std::optional<SamplerPointIntent> resolvedIntent;
    std::optional<JobTicket> ticket;
    ContinuationReceipt receipt;
    std::uint32_t originalArgb = 0xFF000000u;
    std::uint32_t resolvedArgb = 0xFF000000u;
    std::uint32_t consumers = 0;
    bool jobAlive = false;
    bool terminal = false;
    bool overflow = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct SamplerState final
{
    struct PalettePrefix final
    {
        Guid session;
        std::uint64_t documentSerial = 0;
        std::uint64_t intent = 0;
        std::uint32_t argb = 0xFF000000u;
    };
    // One selected input and eight pending host inputs. Allocate this fixed storage once at initialization.
    static constexpr std::size_t kOpeningCapacity = 9;
    std::array<SamplerOpening, kOpeningCapacity> openings;
    std::uint64_t nextOpening = 0;
    // Ordered foreground openings share their accepted opaque prefix while explicit intent is unchanged.
    // This value owns no document or pixel storage and never adopts a later live manual palette edit.
    std::optional<PalettePrefix> palettePrefix;
};
#pragma warning(pop)
