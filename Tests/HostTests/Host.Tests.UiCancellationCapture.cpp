#include "Host.Tests.UiCancellationCapture.h"

#include "UiCancellationCapture.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kWaitMs = 10000;
unsigned int gFailures = 0;
unsigned int gChecks = 0;

void Expect(bool passed, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s ui-cancellation-capture.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
    {
        ++gFailures;
    }
    std::fflush(stdout);
}

wil::unique_handle Event(bool manualReset = true)
{
    wil::unique_handle event{CreateEventW(nullptr, manualReset ? TRUE : FALSE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL ui-cancellation-capture.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

bool WaitOne(HANDLE event) noexcept
{
    return WaitForSingleObject(event, kWaitMs) == WAIT_OBJECT_0;
}

bool SameNotice(const JobExecutionNotice& left, const JobExecutionNotice& right) noexcept
{
    if (left.boundary != right.boundary || left.ticket != right.ticket || left.kind != right.kind ||
        left.lane != right.lane || left.result.has_value() != right.result.has_value())
    {
        return false;
    }
    return !left.result || (left.result->status == right.result->status && left.result->error == right.result->error);
}

void TestEmptyCaptureAndRawSamples()
{
    auto empty = std::make_unique<UiCancellationCapture<4>>();
    UiCancellationCapture<4>::Sample sample{};
    Expect(empty->Read(0, sample) == UiCancellationCapture<4>::ReadResult::Exhausted && !empty->Overflowed(),
           "empty-read-is-exhausted");

    const JobExecutionNotice entered{JobExecutionBoundary::BodyEntered, JobTicket{41, 0x123456789ABCDEF0ull},
                                     JobClass::Selection, JobExecutionLane::GpuRecording, std::nullopt};
    const JobExecutionNotice exited{JobExecutionBoundary::BodyExited, JobTicket{42, 0xFEDCBA9876543210ull},
                                    JobClass::Commit, JobExecutionLane::Compute,
                                    JobResult{JobStatus::Failed, static_cast<std::int32_t>(-0x1234)}};
    auto capture = std::make_unique<UiCancellationCapture<4>>();
    const bool published = capture->Publish(entered, 19) && capture->Publish(exited, 987654321);
    UiCancellationCapture<4>::Sample first{};
    UiCancellationCapture<4>::Sample second{};
    const bool firstRead = capture->Read(0, first) == UiCancellationCapture<4>::ReadResult::Published;
    const bool secondRead = capture->Read(1, second) == UiCancellationCapture<4>::ReadResult::Published;
    Expect(published && firstRead && SameNotice(first.notice, entered) && first.ticks == 19,
           "entered-preserves-boundary-ticket-class-lane-and-absent-result");
    Expect(secondRead && SameNotice(second.notice, exited) && second.ticks == 987654321,
           "exited-preserves-boundary-ticket-class-lane-and-raw-failure-result");

    const JobExecutionNotice finished{JobExecutionBoundary::BodyExited, JobTicket{5, 77}, JobClass::Refinement,
                                      JobExecutionLane::Compute, JobResult{JobStatus::Finished, 0}};
    UiCancellationCapture<4>::Sample finishedSample{};
    Expect(capture->Publish(finished, 1000000000) &&
               capture->Read(2, finishedSample) == UiCancellationCapture<4>::ReadResult::Published &&
               SameNotice(finishedSample.notice, finished) && finishedSample.ticks == 1000000000,
           "raw-success-result-and-clock-are-preserved");

    const JobExecutionNotice negativeClockNotice{JobExecutionBoundary::BodySkipped, JobTicket{7, 0xAABBCCDDEEFF0011ull},
                                                 JobClass::Io, JobExecutionLane::GpuRecording,
                                                 JobResult{JobStatus::Cancelled, 0}};
    auto negativeCapture = std::make_unique<UiCancellationCapture<2>>();
    UiCancellationCapture<2>::Sample negativeSample{};
    const bool negativePublished = negativeCapture->Publish(negativeClockNotice, -73);
    const bool negativeRead =
        negativeCapture->Read(0, negativeSample) == UiCancellationCapture<2>::ReadResult::Published;
    Expect(negativePublished && negativeRead && SameNotice(negativeSample.notice, negativeClockNotice) &&
               negativeSample.ticks == -73,
           "negative-clock-value-is-retained-with-complete-notice");
}

void TestExactCapacityAndOverflowRefusal()
{
    constexpr std::size_t kCapacity = 8;
    auto capture = std::make_unique<UiCancellationCapture<kCapacity>>();
    bool acceptedAll = true;
    for (std::size_t index = 0; index < kCapacity; ++index)
    {
        const JobExecutionNotice notice{JobExecutionBoundary::BodyEntered,
                                        JobTicket{index, static_cast<std::uint64_t>(1000 + index)},
                                        JobClass::Background, JobExecutionLane::Compute, std::nullopt};
        acceptedAll = capture->Publish(notice, static_cast<std::int64_t>(index)) && acceptedAll;
    }
    const JobExecutionNotice extra{JobExecutionBoundary::BodyEntered, JobTicket{99, 9999}, JobClass::Stroke,
                                   JobExecutionLane::GpuRecording, std::nullopt};
    const bool refusedAtCapacity = !capture->Publish(extra, 100);
    const bool overflowed = capture->Overflowed();
    UiCancellationCapture<kCapacity>::Sample sample{};
    const bool readsFailClosed = capture->Read(0, sample) == UiCancellationCapture<kCapacity>::ReadResult::Failed;
    const bool laterPublicationRefused = !capture->Publish(extra, 101);
    Expect(acceptedAll, "exact-capacity-publications-are-accepted");
    Expect(refusedAtCapacity && overflowed && laterPublicationRefused,
           "first-overflow-and-every-later-publication-are-refused");
    Expect(readsFailClosed, "overflow-makes-read-fail-closed");
}

constexpr std::size_t kProducerCount = 4;
constexpr std::size_t kRecordsPerProducer = 16;
constexpr std::size_t kConcurrentRecordCount = kProducerCount * kRecordsPerProducer;
constexpr std::uint64_t kGenerationBase = 0xD00D000000000000ull;

JobExecutionNotice ExpectedConcurrentNotice(std::size_t producer, std::size_t ordinal) noexcept
{
    const std::size_t identity = producer * kRecordsPerProducer + ordinal;
    const auto kindValue = static_cast<unsigned int>(identity % static_cast<unsigned int>(JobClass::Count));
    const auto boundaryValue = static_cast<std::uint8_t>(identity % 3);
    const auto lane = producer % 2 == 0 ? JobExecutionLane::Compute : JobExecutionLane::GpuRecording;
    std::optional<JobResult> result;
    if (boundaryValue == static_cast<std::uint8_t>(JobExecutionBoundary::BodyExited))
    {
        const auto resultKind = (identity / 3) % 3;
        if (resultKind == 0)
        {
            result = JobResult{JobStatus::Finished, 0};
        }
        else if (resultKind == 1)
        {
            result = JobResult{JobStatus::Failed, -static_cast<std::int32_t>(100 + identity)};
        }
        else
        {
            result = JobResult{JobStatus::Cancelled, 0};
        }
    }
    else if (boundaryValue == static_cast<std::uint8_t>(JobExecutionBoundary::BodySkipped))
    {
        result = JobResult{JobStatus::Cancelled, 0};
    }
    return JobExecutionNotice{static_cast<JobExecutionBoundary>(boundaryValue),
                              JobTicket{identity, kGenerationBase + identity}, static_cast<JobClass>(kindValue), lane,
                              result};
}

struct ProducerContext final
{
    UiCancellationCapture<kConcurrentRecordCount>* capture = nullptr;
    HANDLE readyEvent = nullptr;
    HANDLE startEvent = nullptr;
    HANDLE doneEvent = nullptr;
    std::atomic<unsigned int>* readyCount = nullptr;
    std::atomic<bool>* publishFailed = nullptr;
    std::size_t producer = 0;
};

void PublishFixedRecords(ProducerContext context) noexcept
{
    if (context.readyCount->fetch_add(1, std::memory_order_acq_rel) + 1 == kProducerCount)
    {
        (void)SetEvent(context.readyEvent);
    }
    if (WaitForSingleObject(context.startEvent, kWaitMs) != WAIT_OBJECT_0)
    {
        context.publishFailed->store(true, std::memory_order_release);
        (void)SetEvent(context.doneEvent);
        return;
    }

    for (std::size_t ordinal = 0; ordinal < kRecordsPerProducer; ++ordinal)
    {
        const std::size_t identity = context.producer * kRecordsPerProducer + ordinal;
        if (!context.capture->Publish(ExpectedConcurrentNotice(context.producer, ordinal),
                                      static_cast<std::int64_t>(5000 + identity)))
        {
            context.publishFailed->store(true, std::memory_order_release);
        }
    }
    (void)SetEvent(context.doneEvent);
}

void TestConcurrentComputeAndRecordingPublication()
{
    auto capture = std::make_unique<UiCancellationCapture<kConcurrentRecordCount>>();
    auto allReady = Event();
    auto start = Event();
    std::array<wil::unique_handle, kProducerCount> doneEvents{};
    std::array<std::thread, kProducerCount> producers{};
    std::atomic<unsigned int> readyCount{0};
    std::atomic<bool> publishFailed{false};

    for (auto& done : doneEvents)
    {
        done = Event();
    }
    for (std::size_t producer = 0; producer < kProducerCount; ++producer)
    {
        const ProducerContext context{capture.get(), allReady.get(), start.get(), doneEvents[producer].get(),
                                      &readyCount,   &publishFailed, producer};
        producers[producer] = std::thread(PublishFixedRecords, context);
    }

    const bool producersReady = WaitOne(allReady.get());
    (void)SetEvent(start.get());
    bool allDone = true;
    for (const auto& done : doneEvents)
    {
        allDone = WaitOne(done.get()) && allDone;
    }
    for (auto& producer : producers)
    {
        if (producer.joinable())
        {
            producer.join();
        }
    }

    Expect(producersReady && readyCount.load(std::memory_order_acquire) == kProducerCount,
           "all-compute-and-recording-producers-reach-event-gate");
    Expect(allDone && !publishFailed.load(std::memory_order_acquire) && !capture->Overflowed(),
           "fixed-concurrent-producer-loops-publish-without-refusal");

    std::array<unsigned int, kConcurrentRecordCount> identityMultiplicity{};
    bool recordsMatch = true;
    for (std::size_t index = 0; index < kConcurrentRecordCount; ++index)
    {
        UiCancellationCapture<kConcurrentRecordCount>::Sample sample{};
        if (capture->Read(index, sample) != UiCancellationCapture<kConcurrentRecordCount>::ReadResult::Published)
        {
            recordsMatch = false;
            continue;
        }
        const std::size_t identity = sample.notice.ticket.slot;
        if (identity >= kConcurrentRecordCount || sample.notice.ticket.generation != kGenerationBase + identity)
        {
            recordsMatch = false;
            continue;
        }
        ++identityMultiplicity[identity];
        const std::size_t producer = identity / kRecordsPerProducer;
        const std::size_t ordinal = identity % kRecordsPerProducer;
        recordsMatch = recordsMatch && SameNotice(sample.notice, ExpectedConcurrentNotice(producer, ordinal)) &&
                       sample.ticks == static_cast<std::int64_t>(5000 + identity);
    }
    for (const unsigned int multiplicity : identityMultiplicity)
    {
        recordsMatch = recordsMatch && multiplicity == 1;
    }
    Expect(recordsMatch, "compute-and-recording-identities-appear-exactly-once-with-raw-fields");
    UiCancellationCapture<kConcurrentRecordCount>::Sample tail{};
    Expect(capture->Read(kConcurrentRecordCount, tail) ==
               UiCancellationCapture<kConcurrentRecordCount>::ReadResult::Exhausted,
           "concurrent-log-ends-at-exact-record-count");
}
void TestPublishedReaderWhileProducerAlive()
{
    auto capture = std::make_unique<UiCancellationCapture<2>>();
    auto published = Event();
    auto continueProducer = Event();
    std::atomic<bool> producerOk{false};
    const JobExecutionNotice first{JobExecutionBoundary::BodyEntered, JobTicket{3, 91}, JobClass::Commit,
                                   JobExecutionLane::Compute, std::nullopt};
    const JobExecutionNotice second{JobExecutionBoundary::BodyExited, JobTicket{3, 91}, JobClass::Commit,
                                    JobExecutionLane::Compute, JobResult{JobStatus::Cancelled, 0}};
    std::thread producer(
        [&]() noexcept
        {
            const bool firstOk = capture->Publish(first, 10);
            const bool signaled = SetEvent(published.get()) != FALSE;
            const bool resumed = WaitOne(continueProducer.get());
            producerOk.store(firstOk && signaled && resumed && capture->Publish(second, 20), std::memory_order_release);
        });
    const bool observed = WaitOne(published.get());
    UiCancellationCapture<2>::Sample sample{};
    const bool earlyRead = observed && capture->Read(0, sample) == UiCancellationCapture<2>::ReadResult::Published &&
                           SameNotice(sample.notice, first) && sample.ticks == 10;
    Expect(earlyRead, "release-published-sample-readable-before-producer-join");
    // Release even after a failed expectation; a correctness failure cannot strand the original producer.
    (void)SetEvent(continueProducer.get());
    producer.join();
    UiCancellationCapture<2>::Sample original{};
    UiCancellationCapture<2>::Sample terminal{};
    Expect(producerOk.load(std::memory_order_acquire) &&
               capture->Read(0, original) == UiCancellationCapture<2>::ReadResult::Published &&
               SameNotice(original.notice, first) && original.ticks == 10 &&
               capture->Read(1, terminal) == UiCancellationCapture<2>::ReadResult::Published &&
               SameNotice(terminal.notice, second) && terminal.ticks == 20,
           "later-publication-keeps-already-read-original-cell-immutable");
}

} // namespace

int RunUiCancellationCaptureTests()
{
    gFailures = 0;
    gChecks = 0;
    TestEmptyCaptureAndRawSamples();
    TestExactCapacityAndOverflowRefusal();
    TestConcurrentComputeAndRecordingPublication();
    TestPublishedReaderWhileProducerAlive();
    std::printf("%s ui-cancellation-capture.summary checks=%u failed=%u\n",
                gFailures == 0 && gChecks == 14 ? "PASS" : "FAIL", gChecks, gFailures);
    std::fflush(stdout);
    return static_cast<int>(gFailures + (gChecks != 14 ? 1 : 0));
}
