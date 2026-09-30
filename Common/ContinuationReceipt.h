#pragma once

#include <cstdint>
#include <memory>
#include <utility>

enum class ContinuationOutcome
{
    Pending,
    Succeeded,
    Cancelled,
    Failed,
    Stale
};

class ContinuationCompletion;

// Owning observer value. Reads and publication stay on the UI owner thread; neither workers nor futures access
// this state. A receipt survives queue removal and publisher teardown, without borrowing the host or document.
class ContinuationReceipt final
{
  public:
    struct Result final
    {
        ContinuationOutcome outcome = ContinuationOutcome::Failed;
        std::int32_t error = 0;
        [[nodiscard]] bool operator==(const Result&) const noexcept = default;
    };

    ContinuationReceipt() noexcept = default;

    [[nodiscard]] Result Read() const noexcept
    {
        return _state ? _state->result : _immediate;
    }

    // Admission can fail before a shared state exists. Report that terminal result without allocating again.
    [[nodiscard]] static ContinuationReceipt Failure(std::int32_t error) noexcept
    {
        ContinuationReceipt receipt;
        receipt._immediate.error = error;
        return receipt;
    }

  private:
    struct State final
    {
        Result result{ContinuationOutcome::Pending, 0};
    };

    explicit ContinuationReceipt(std::shared_ptr<const State> state) noexcept : _state(std::move(state)) {}

    std::shared_ptr<const State> _state;
    Result _immediate;
    friend class ContinuationCompletion;
};

// Exactly one publisher. Dropping/overwriting unfinished ownership cancels it, so a retained observer can never
// stay Pending after that owner disappears. Explicit retirement publishes Stale/Failed before release instead.
class ContinuationCompletion final
{
  public:
    ContinuationCompletion() noexcept = default;
    ~ContinuationCompletion() noexcept
    {
        Abandon();
    }
    ContinuationCompletion(const ContinuationCompletion&) = delete;
    ContinuationCompletion& operator=(const ContinuationCompletion&) = delete;
    ContinuationCompletion(ContinuationCompletion&& other) noexcept : _state(std::move(other._state)) {}
    ContinuationCompletion& operator=(ContinuationCompletion&& other) noexcept
    {
        if (this != &other)
        {
            Abandon();
            _state = std::move(other._state);
        }
        return *this;
    }

    // One shared allocation at owning request construction. The host must call this inside its construction/
    // admission bad_alloc boundary and fail the whole request on failure, returning Receipt::Failure instead.
    [[nodiscard]] static ContinuationCompletion Create()
    {
        return ContinuationCompletion(std::make_shared<ContinuationReceipt::State>());
    }

    [[nodiscard]] ContinuationReceipt Receipt() const noexcept
    {
        return ContinuationReceipt(_state);
    }

    [[nodiscard]] bool Publish(ContinuationOutcome outcome, std::int32_t error = 0) noexcept
    {
        if (!_state || _state->result.outcome != ContinuationOutcome::Pending ||
            (outcome != ContinuationOutcome::Succeeded && outcome != ContinuationOutcome::Cancelled &&
             outcome != ContinuationOutcome::Failed && outcome != ContinuationOutcome::Stale))
        {
            return false;
        }
        _state->result = {outcome, error};
        return true;
    }

  private:
    explicit ContinuationCompletion(std::shared_ptr<ContinuationReceipt::State> state) noexcept
        : _state(std::move(state))
    {
    }
    void Abandon() noexcept
    {
        (void)Publish(ContinuationOutcome::Cancelled);
    }

    std::shared_ptr<ContinuationReceipt::State> _state;
};
