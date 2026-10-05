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

struct SamplerState final
{
    // One selected input and eight pending host inputs. Allocate this fixed storage once at initialization.
    static constexpr std::size_t kOpeningCapacity = 9;
    std::array<SamplerOpening, kOpeningCapacity> openings;
    std::uint64_t nextOpening = 0;
};
