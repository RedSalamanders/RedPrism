#pragma once

#include "Document.h"

#include <cstdint>

// Owner-created scalar cleanup capability. It can clear only one exact pending intent, never a draft,
// document, history transaction or a newer opening. Retain across a stopped/failed worker preparation.
class LassoIntent final
{
  public:
    LassoIntent(const LassoIntent&) noexcept = default;
    LassoIntent& operator=(const LassoIntent&) noexcept = default;

  private:
    friend class DocumentSession;
    LassoIntent(Guid session, std::uint64_t documentSerial, std::uint64_t serial, bool opening) noexcept
        : _session(session), _documentSerial(documentSerial), _serial(serial), _opening(opening)
    {
    }
    Guid _session;
    std::uint64_t _documentSerial, _serial;
    bool _opening;
};
