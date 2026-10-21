#include "Host.Tests.PathClassificationRead.h"

#include "../../Common/Executor.h"
#include "../../Common/PathClassificationRead.h"
#include "../../Common/PathClassificationWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
std::size_t gAssertions = 0;
std::size_t gCurrentCaseAssertionCount = 0;
const char* gCurrentCase = nullptr;

struct ExpectedAssertion final
{
    const char* caseName;
    const char* assertionName;
};

struct ExpectedCase final
{
    const char* name;
    std::size_t first;
    std::size_t count;
};

constexpr std::array kExpectedAssertions{
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "filesystem-control-fixtures-created"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "actual-directory-is-project-without-opening"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "case-insensitive-comp-is-project-without-opening"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "missing-path-is-image-without-opening"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "regular-file-fixture-written"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection",
                      "empty-embedded-nul-and-overlength-refuse-before-open"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "stop-before-prepare-does-not-open-file"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection", "regular-file-prepares-for-read"},
    ExpectedAssertion{"filesystem-controls-and-input-rejection",
                      "stop-before-issue-closes-file-and-publishes-no-io-cycle"},
    ExpectedAssertion{"explicit-retirement-of-prepared-unissued-cycle", "unissued-retirement-fixtures-created"},
    ExpectedAssertion{"explicit-retirement-of-prepared-unissued-cycle",
                      "unissued-retirement-starts-with-prepared-owned-file"},
    ExpectedAssertion{"explicit-retirement-of-prepared-unissued-cycle",
                      "explicit-unissued-retirement-closes-file-and-signals-no-io-cycle"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "signature-fixtures-created"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "real-file-pk0304-is-project"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "reset-event-before-pk0506-cycle"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "real-file-pk0506-is-project"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "reset-event-before-pk0708-cycle"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "real-file-pk0708-is-project"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "reset-event-before-non-zip-cycle"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "real-file-other-bytes-are-image-after-reuse"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "reset-event-before-short-cycle"},
    ExpectedAssertion{"real-file-signatures-and-reuse", "real-file-short-header-is-image-without-stale-bytes"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "connected-overlapped-pipe-created"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "empty-connected-pipe-produces-genuine-pending-read"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "premature-event-signalled"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "premature-event-is-not-native-io-acknowledgement"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "reset-event-after-premature-signal"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge", "literal-pk0506-written-to-pipe"},
    ExpectedAssertion{"premature-signal-does-not-acknowledge",
                      "actual-pipe-completion-preserves-bytes-and-classifies-project"},
    ExpectedAssertion{"pending-cancellation-and-acknowledgement", "cancellation-pipe-created"},
    ExpectedAssertion{"pending-cancellation-and-acknowledgement", "cancellation-test-has-real-pending-read"},
    ExpectedAssertion{"pending-cancellation-and-acknowledgement",
                      "cancel-may-be-pending-then-requires-real-cancelled-completion"},
    ExpectedAssertion{"forced-retirement-without-token-cancellation", "forced-retirement-pipe-created"},
    ExpectedAssertion{"forced-retirement-without-token-cancellation", "forced-retirement-has-real-pending-read"},
    ExpectedAssertion{"forced-retirement-without-token-cancellation",
                      "forced-cancel-retirement-keeps-token-unmodified-and-publishes-after-native-ack"},
    ExpectedAssertion{"shutdown-joins-cancelled-native-read", "shutdown-pipe-created"},
    ExpectedAssertion{"shutdown-joins-cancelled-native-read", "shutdown-test-starts-real-pending-read"},
    ExpectedAssertion{"shutdown-joins-cancelled-native-read",
                      "shutdown-join-returns-only-after-cancelled-read-retirement"},
    ExpectedAssertion{"native-open-and-read-refusal", "native-refusal-fixtures-created"},
    ExpectedAssertion{"native-open-and-read-refusal", "unreadable-file-is-best-effort-image-without-an-io-cycle"},
    ExpectedAssertion{"native-open-and-read-refusal", "write-only-owned-handle-prepares-the-real-read-api"},
    ExpectedAssertion{"native-open-and-read-refusal", "actual-readfile-access-refusal-is-best-effort-image"},
    ExpectedAssertion{"native-open-and-read-refusal", "failed-issue-consumes-its-registered-no-io-cycle"},
    ExpectedAssertion{"native-open-and-read-refusal", "failed-issue-has-no-pending-native-buffer"},
};
constexpr std::array kExpectedCases{
    ExpectedCase{"filesystem-controls-and-input-rejection", 0, 9},
    ExpectedCase{"explicit-retirement-of-prepared-unissued-cycle", 9, 3},
    ExpectedCase{"real-file-signatures-and-reuse", 12, 10},
    ExpectedCase{"premature-signal-does-not-acknowledge", 22, 7},
    ExpectedCase{"pending-cancellation-and-acknowledgement", 29, 3},
    ExpectedCase{"forced-retirement-without-token-cancellation", 32, 3},
    ExpectedCase{"shutdown-joins-cancelled-native-read", 35, 3},
    ExpectedCase{"native-open-and-read-refusal", 38, 6},
};

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    const std::size_t index = gCurrentCaseAssertionCount++;
    const ExpectedCase* selected = nullptr;
    for (const ExpectedCase& item : kExpectedCases)
    {
        if (gCurrentCase && std::string_view{gCurrentCase} == item.name)
        {
            selected = &item;
            break;
        }
    }
    const ExpectedAssertion* expected =
        selected && index < selected->count ? &kExpectedAssertions[selected->first + index] : nullptr;
    const bool inOrder = expected && std::string_view{expected->caseName} == gCurrentCase &&
                         std::string_view{expected->assertionName} == name;
    if (!inOrder)
    {
        std::printf("FAIL path-classification-read.assertion-registry expected=%s.%s actual=%s.%s\n",
                    expected ? expected->caseName : (gCurrentCase ? gCurrentCase : "<no-case>"),
                    expected ? expected->assertionName : "<end-of-case>", gCurrentCase ? gCurrentCase : "<no-case>",
                    name);
    }
    condition = condition && inOrder;
    std::printf("%s path-classification-read.%s.%s\n", condition ? "PASS" : "FAIL", gCurrentCase, name);
    if (!condition)
    {
        ++gFailures;
    }
}

class TemporaryFile final
{
  public:
    TemporaryFile() noexcept
    {
        std::array<wchar_t, MAX_PATH> directory{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
        if (length == 0 || length >= directory.size() ||
            GetTempFileNameW(directory.data(), L"PCR", 0, _path.data()) == 0)
        {
            return;
        }
        _valid = true;
    }
    ~TemporaryFile()
    {
        if (_valid)
        {
            (void)DeleteFileW(_path.data());
        }
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    [[nodiscard]] bool Valid() const noexcept
    {
        return _valid;
    }
    [[nodiscard]] const wchar_t* Path() const noexcept
    {
        return _path.data();
    }
    [[nodiscard]] bool Write(std::span<const std::uint8_t> bytes) const noexcept
    {
        wil::unique_hfile file{
            CreateFileW(_path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            return false;
        }
        DWORD written = 0;
        return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
               written == bytes.size();
    }

  private:
    std::array<wchar_t, MAX_PATH> _path{};
    bool _valid = false;
};

class TemporaryDirectory final
{
  public:
    TemporaryDirectory() noexcept
    {
        std::array<wchar_t, MAX_PATH> directory{};
        std::array<wchar_t, MAX_PATH> candidate{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
        if (length == 0 || length >= directory.size() ||
            GetTempFileNameW(directory.data(), L"PCD", 0, candidate.data()) == 0)
        {
            return;
        }
        _path = candidate;
        _candidateFile = true;
        if (!DeleteFileW(_path.data()))
        {
            return;
        }
        _candidateFile = false;
        if (!CreateDirectoryW(_path.data(), nullptr))
        {
            return;
        }
        _valid = true;
    }
    ~TemporaryDirectory()
    {
        if (_valid)
        {
            (void)RemoveDirectoryW(_path.data());
        }
        else if (_candidateFile)
        {
            (void)DeleteFileW(_path.data());
        }
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] bool Valid() const noexcept
    {
        return _valid;
    }
    [[nodiscard]] const wchar_t* Path() const noexcept
    {
        return _path.data();
    }

  private:
    std::array<wchar_t, MAX_PATH> _path{};
    bool _valid = false;
    bool _candidateFile = false;
};

struct OpenContext final
{
    unsigned int calls = 0;
};

void* OpenOverlappedFile(void* context, const wchar_t* path) noexcept
{
    auto& state = *static_cast<OpenContext*>(context);
    ++state.calls;
    return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
}

struct PipeOpenContext final
{
    wil::unique_hfile* server = nullptr;
    unsigned int calls = 0;
};

void* OpenOverlappedNamedPipe(void* context, const wchar_t*) noexcept
{
    auto& state = *static_cast<PipeOpenContext*>(context);
    ++state.calls;
    return state.server->release();
}

class ReaderFixture final
{
  public:
    ReaderFixture() noexcept : _event{CreateEventW(nullptr, TRUE, FALSE, nullptr)} {}
    [[nodiscard]] bool Valid() const noexcept
    {
        return static_cast<bool>(_event);
    }
    [[nodiscard]] HANDLE Event() const noexcept
    {
        return _event.get();
    }
    void Initialize(PathClassificationRead& reader, PathClassificationReadSource source = {})
    {
        reader.Initialize(_event.get(), source);
    }

  private:
    wil::unique_handle _event;
};

bool WaitForCompletion(HANDLE event) noexcept
{
    return WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
}

bool ResetCycleEvent(HANDLE event, const char* name) noexcept
{
    const bool reset = ResetEvent(event) != FALSE;
    Expect(reset, name);
    return reset;
}

bool ReadAndClassify(PathClassificationRead& reader, HANDLE event, std::wstring_view path, PathClassification expected,
                     OpenContext& source) noexcept
{
    const auto previousCalls = source.calls;
    const auto prepared = reader.Prepare(path, {});
    if (prepared != PathClassificationReadStep::ReadyToIssue || !reader.ReadyToIssue())
    {
        return false;
    }
    const auto issued = reader.Issue({});
    if (issued == PathClassificationReadStep::Pending)
    {
        if (!WaitForCompletion(event))
        {
            return false;
        }
        if (reader.Acknowledge({}) != PathClassificationReadStep::Complete)
        {
            return false;
        }
    }
    else if (issued != PathClassificationReadStep::Complete)
    {
        return false;
    }
    return reader.Classification() == expected && source.calls == previousCalls + 1 && !reader.HasPendingRead();
}

bool CreateConnectedPipe(wil::unique_hfile& server, wil::unique_hfile& client) noexcept
{
    static unsigned int nextPipe = 0;
    std::array<wchar_t, 128> pipeName{};
    if (_snwprintf_s(pipeName.data(), pipeName.size(), _TRUNCATE,
                     L"\\\\.\\pipe\\RedPrism.PathClassificationRead.%lu.%u", GetCurrentProcessId(), ++nextPipe) < 0)
        return false;
    wil::unique_handle connectEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!connectEvent)
    {
        return false;
    }
    server.reset(CreateNamedPipeW(pipeName.data(),
                                  PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4, 4, 0, nullptr));
    if (!server)
    {
        return false;
    }
    OVERLAPPED connectOperation{};
    connectOperation.hEvent = connectEvent.get();
    const BOOL connected = ConnectNamedPipe(server.get(), &connectOperation);
    const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
    if (!connected && connectError != ERROR_IO_PENDING)
    {
        return false;
    }
    bool connectionPending = connectError == ERROR_IO_PENDING;
    const auto joinConnection = wil::scope_exit(
        [&]() noexcept
        {
            if (connectionPending)
            {
                (void)CancelIoEx(server.get(), &connectOperation);
                DWORD ignored = 0;
                (void)GetOverlappedResult(server.get(), &connectOperation, &ignored, TRUE);
            }
        });
    client.reset(
        CreateFileW(pipeName.data(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!client)
    {
        return false;
    }
    if (connectError == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(connectEvent.get(), 5000) != WAIT_OBJECT_0)
        {
            return false;
        }
        DWORD ignored = 0;
        if (!GetOverlappedResult(server.get(), &connectOperation, &ignored, FALSE))
        {
            return false;
        }
        connectionPending = false;
    }
    return true;
}

bool WritePipeBytes(HANDLE client, std::span<const std::uint8_t> bytes) noexcept
{
    DWORD written = 0;
    return WriteFile(client, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
           written == bytes.size();
}

void TestFilesystemControlsAndInputRejection()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    OpenContext source;
    TemporaryDirectory directory;
    TemporaryFile file;
    Expect(fixture.Valid() && directory.Valid() && file.Valid(), "filesystem-control-fixtures-created");
    if (!fixture.Valid() || !directory.Valid() || !file.Valid())
    {
        return;
    }

    fixture.Initialize(reader, {&source, OpenOverlappedFile});
    const auto directoryResult = reader.Prepare(directory.Path(), {});
    Expect(directoryResult == PathClassificationReadStep::Complete &&
               reader.Classification() == PathClassification::Project && source.calls == 0,
           "actual-directory-is-project-without-opening");
    const auto compResult = reader.Prepare(L"missing-case-insensitive.PRISM", {});
    Expect(compResult == PathClassificationReadStep::Complete &&
               reader.Classification() == PathClassification::Project && source.calls == 0,
           "case-insensitive-comp-is-project-without-opening");
    const auto missingResult = reader.Prepare(L"Z:\\RedPrism-path-read-no-such-file-8A7C2B.png", {});
    Expect(missingResult == PathClassificationReadStep::Complete &&
               reader.Classification() == PathClassification::Image && source.calls == 0,
           "missing-path-is-image-without-opening");

    const std::array<std::uint8_t, 4> plainBytes{'d', 'a', 't', 'a'};
    Expect(file.Write(plainBytes), "regular-file-fixture-written");
    const std::wstring_view filePath{file.Path()};
    const auto overlengthPath = std::make_unique<std::array<wchar_t, kMaxPathClassificationLength + 1>>();
    Expect(reader.Prepare({}, {}) == PathClassificationReadStep::InvalidInput &&
               reader.Prepare(std::wstring_view{L"bad\0path", 8}, {}) == PathClassificationReadStep::InvalidInput &&
               reader.Prepare(std::wstring_view{overlengthPath->data(), overlengthPath->size()}, {}) ==
                   PathClassificationReadStep::InvalidInput &&
               source.calls == 0,
           "empty-embedded-nul-and-overlength-refuse-before-open");

    CancelSource prePrepareCancellation;
    prePrepareCancellation.Cancel();
    Expect(reader.Prepare(filePath, prePrepareCancellation.Token()) == PathClassificationReadStep::Cancelled &&
               source.calls == 0,
           "stop-before-prepare-does-not-open-file");

    Expect(reader.Prepare(filePath, {}) == PathClassificationReadStep::ReadyToIssue && reader.ReadyToIssue(),
           "regular-file-prepares-for-read");
    CancelSource preIssueCancellation;
    preIssueCancellation.Cancel();
    const auto stoppedBeforeIssue = reader.Issue(preIssueCancellation.Token());
    wil::unique_hfile exclusiveReopen{
        CreateFileW(file.Path(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    Expect(stoppedBeforeIssue == PathClassificationReadStep::Cancelled && !reader.HasPendingRead() &&
               !reader.ReadyToIssue() && WaitForSingleObject(fixture.Event(), 0) == WAIT_OBJECT_0 &&
               static_cast<bool>(exclusiveReopen),
           "stop-before-issue-closes-file-and-publishes-no-io-cycle");
}

void TestExplicitRetirementOfPreparedUnissuedCycle()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    OpenContext source;
    TemporaryFile file;
    Expect(fixture.Valid() && file.Valid(), "unissued-retirement-fixtures-created");
    if (!fixture.Valid() || !file.Valid())
    {
        return;
    }
    reader.Initialize(fixture.Event(), {&source, OpenOverlappedFile});
    const auto prepared = reader.Prepare(file.Path(), {});
    Expect(prepared == PathClassificationReadStep::ReadyToIssue && reader.ReadyToIssue() && !reader.HasPendingRead() &&
               source.calls == 1,
           "unissued-retirement-starts-with-prepared-owned-file");
    if (prepared != PathClassificationReadStep::ReadyToIssue)
    {
        return;
    }

    reader.RetireUnissuedCycleWorker();
    wil::unique_hfile exclusiveReopen{
        CreateFileW(file.Path(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    Expect(!reader.ReadyToIssue() && !reader.HasPendingRead() &&
               WaitForSingleObject(fixture.Event(), 0) == WAIT_OBJECT_0 && static_cast<bool>(exclusiveReopen) &&
               reader.Classification() == PathClassification::Image,
           "explicit-unissued-retirement-closes-file-and-signals-no-io-cycle");
}

void TestRealFileSignaturesAndReuse()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    OpenContext source;
    TemporaryFile file;
    Expect(fixture.Valid() && file.Valid(), "signature-fixtures-created");
    if (!fixture.Valid() || !file.Valid())
    {
        return;
    }
    fixture.Initialize(reader, {&source, OpenOverlappedFile});
    const std::wstring_view path{file.Path()};

    const std::array<std::uint8_t, 4> pk0304{'P', 'K', 3, 4};
    Expect(file.Write(pk0304) && ReadAndClassify(reader, fixture.Event(), path, PathClassification::Project, source),
           "real-file-pk0304-is-project");
    if (!ResetCycleEvent(fixture.Event(), "reset-event-before-pk0506-cycle"))
    {
        return;
    }
    const std::array<std::uint8_t, 4> pk0506{'P', 'K', 5, 6};
    Expect(file.Write(pk0506) && ReadAndClassify(reader, fixture.Event(), path, PathClassification::Project, source),
           "real-file-pk0506-is-project");
    if (!ResetCycleEvent(fixture.Event(), "reset-event-before-pk0708-cycle"))
    {
        return;
    }
    const std::array<std::uint8_t, 4> pk0708{'P', 'K', 7, 8};
    Expect(file.Write(pk0708) && ReadAndClassify(reader, fixture.Event(), path, PathClassification::Project, source),
           "real-file-pk0708-is-project");
    if (!ResetCycleEvent(fixture.Event(), "reset-event-before-non-zip-cycle"))
    {
        return;
    }
    const std::array<std::uint8_t, 4> other{'P', 'K', 9, 9};
    Expect(file.Write(other) && ReadAndClassify(reader, fixture.Event(), path, PathClassification::Image, source),
           "real-file-other-bytes-are-image-after-reuse");
    if (!ResetCycleEvent(fixture.Event(), "reset-event-before-short-cycle"))
        return;
    const std::array<std::uint8_t, 2> shortBytes{'P', 'K'};
    Expect(file.Write(shortBytes) && ReadAndClassify(reader, fixture.Event(), path, PathClassification::Image, source),
           "real-file-short-header-is-image-without-stale-bytes");
}

void TestPrematureSignalDoesNotAcknowledgeRead()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    wil::unique_hfile server;
    wil::unique_hfile client;
    PipeOpenContext source{std::addressof(server)};
    TemporaryFile placeholder;
    const bool connected = CreateConnectedPipe(server, client);
    Expect(fixture.Valid() && placeholder.Valid() && connected, "connected-overlapped-pipe-created");
    if (!fixture.Valid() || !placeholder.Valid() || !connected)
    {
        return;
    }
    reader.Initialize(fixture.Event(), {&source, OpenOverlappedNamedPipe});
    const auto prepared = reader.Prepare(placeholder.Path(), {});
    const auto issued = prepared == PathClassificationReadStep::ReadyToIssue ? reader.Issue({})
                                                                             : PathClassificationReadStep::InvalidInput;
    Expect(prepared == PathClassificationReadStep::ReadyToIssue && issued == PathClassificationReadStep::Pending &&
               reader.HasPendingRead() && source.calls == 1,
           "empty-connected-pipe-produces-genuine-pending-read");
    if (issued != PathClassificationReadStep::Pending)
    {
        return;
    }

    const bool prematureSignal = SetEvent(fixture.Event()) != FALSE;
    Expect(prematureSignal, "premature-event-signalled");
    if (!prematureSignal)
    {
        return;
    }
    const auto premature = reader.Acknowledge({});
    Expect(premature == PathClassificationReadStep::Pending && reader.HasPendingRead(),
           "premature-event-is-not-native-io-acknowledgement");
    if (!ResetCycleEvent(fixture.Event(), "reset-event-after-premature-signal"))
    {
        return;
    }
    const std::array<std::uint8_t, 4> bytes{'P', 'K', 5, 6};
    Expect(WritePipeBytes(client.get(), bytes), "literal-pk0506-written-to-pipe");
    const bool completed = WaitForCompletion(fixture.Event());
    const auto acknowledged = completed ? reader.Acknowledge({}) : PathClassificationReadStep::Pending;
    Expect(completed && acknowledged == PathClassificationReadStep::Complete &&
               reader.Classification() == PathClassification::Project && !reader.HasPendingRead(),
           "actual-pipe-completion-preserves-bytes-and-classifies-project");
}

void TestPendingCancellationAndAcknowledgement()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    wil::unique_hfile server;
    wil::unique_hfile client;
    PipeOpenContext source{std::addressof(server)};
    TemporaryFile placeholder;
    const bool connected = CreateConnectedPipe(server, client);
    Expect(fixture.Valid() && placeholder.Valid() && connected, "cancellation-pipe-created");
    if (!fixture.Valid() || !placeholder.Valid() || !connected)
    {
        return;
    }
    reader.Initialize(fixture.Event(), {&source, OpenOverlappedNamedPipe});
    const auto prepared = reader.Prepare(placeholder.Path(), {});
    const auto issued = prepared == PathClassificationReadStep::ReadyToIssue ? reader.Issue({})
                                                                             : PathClassificationReadStep::InvalidInput;
    Expect(issued == PathClassificationReadStep::Pending && reader.HasPendingRead() && source.calls == 1,
           "cancellation-test-has-real-pending-read");
    if (issued != PathClassificationReadStep::Pending)
    {
        return;
    }

    CancelSource cancellation;
    cancellation.Cancel();
    const auto firstAck = reader.Acknowledge(cancellation.Token());
    const bool eventReady = WaitForCompletion(fixture.Event());
    const auto finalAck = eventReady ? reader.Acknowledge(cancellation.Token()) : firstAck;
    Expect((firstAck == PathClassificationReadStep::Pending || firstAck == PathClassificationReadStep::Cancelled) &&
               eventReady && finalAck == PathClassificationReadStep::Cancelled && !reader.HasPendingRead() &&
               reader.Classification() == PathClassification::Image,
           "cancel-may-be-pending-then-requires-real-cancelled-completion");
}

void TestForcedRetirementOfPendingReadWithoutTokenCancellation()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    wil::unique_hfile server;
    wil::unique_hfile client;
    PipeOpenContext source{std::addressof(server)};
    TemporaryFile placeholder;
    const bool connected = CreateConnectedPipe(server, client);
    Expect(fixture.Valid() && placeholder.Valid() && connected, "forced-retirement-pipe-created");
    if (!fixture.Valid() || !placeholder.Valid() || !connected)
    {
        return;
    }
    reader.Initialize(fixture.Event(), {&source, OpenOverlappedNamedPipe});
    const auto prepared = reader.Prepare(placeholder.Path(), {});
    const auto issued = prepared == PathClassificationReadStep::ReadyToIssue ? reader.Issue({})
                                                                             : PathClassificationReadStep::InvalidInput;
    Expect(issued == PathClassificationReadStep::Pending && reader.HasPendingRead() && source.calls == 1,
           "forced-retirement-has-real-pending-read");
    if (issued != PathClassificationReadStep::Pending)
    {
        return;
    }

    const CancelToken uncancelled;
    const auto firstAck = reader.Acknowledge(uncancelled, true);
    const bool eventReady = WaitForCompletion(fixture.Event());
    const auto finalAck = eventReady ? reader.Acknowledge(uncancelled) : firstAck;
    Expect(!uncancelled.IsCancelled() &&
               (firstAck == PathClassificationReadStep::Pending || firstAck == PathClassificationReadStep::Complete) &&
               eventReady && finalAck == PathClassificationReadStep::Complete && !reader.HasPendingRead() &&
               reader.Classification() == PathClassification::Image,
           "forced-cancel-retirement-keeps-token-unmodified-and-publishes-after-native-ack");
}

void TestShutdownJoinsCancelledNativeRead()
{
    ReaderFixture fixture;
    PathClassificationRead reader;
    wil::unique_hfile server;
    wil::unique_hfile client;
    PipeOpenContext source{std::addressof(server)};
    TemporaryFile placeholder;
    const bool connected = CreateConnectedPipe(server, client);
    Expect(fixture.Valid() && placeholder.Valid() && connected, "shutdown-pipe-created");
    if (!fixture.Valid() || !placeholder.Valid() || !connected)
    {
        return;
    }
    reader.Initialize(fixture.Event(), {&source, OpenOverlappedNamedPipe});
    const auto prepared = reader.Prepare(placeholder.Path(), {});
    const auto issued = prepared == PathClassificationReadStep::ReadyToIssue ? reader.Issue({})
                                                                             : PathClassificationReadStep::InvalidInput;
    Expect(issued == PathClassificationReadStep::Pending && reader.HasPendingRead() && source.calls == 1,
           "shutdown-test-starts-real-pending-read");
    if (issued != PathClassificationReadStep::Pending)
    {
        return;
    }
    reader.CancelAndJoinForShutdownWorker();
    Expect(!reader.HasPendingRead() && WaitForSingleObject(fixture.Event(), 0) == WAIT_OBJECT_0,
           "shutdown-join-returns-only-after-cancelled-read-retirement");
}

void TestNativeOpenAndReadRefusal()
{
    TemporaryFile file;
    ReaderFixture fixture;
    OpenContext source;
    PathClassificationRead unavailable;
    PathClassificationRead writeOnly;
    Expect(file.Valid() && file.Write(std::array<std::uint8_t, 4>{'P', 'K', 3, 4}) && fixture.Valid(),
           "native-refusal-fixtures-created");
    if (!file.Valid() || !fixture.Valid())
        return;
    unavailable.Initialize(fixture.Event(), {&source, [](void* context, const wchar_t*) noexcept -> void*
                                             {
                                                 ++static_cast<OpenContext*>(context)->calls;
                                                 return INVALID_HANDLE_VALUE;
                                             }});
    Expect(unavailable.Prepare(file.Path(), {}) == PathClassificationReadStep::Complete &&
               unavailable.Classification() == PathClassification::Image && !unavailable.ReadyToIssue() &&
               !unavailable.HasPendingRead() && source.calls == 1 &&
               WaitForSingleObject(fixture.Event(), 0) == WAIT_TIMEOUT,
           "unreadable-file-is-best-effort-image-without-an-io-cycle");
    writeOnly.Initialize(fixture.Event(),
                         {&source, [](void* context, const wchar_t* path) noexcept -> void*
                          {
                              ++static_cast<OpenContext*>(context)->calls;
                              return CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
                          }});
    Expect(writeOnly.Prepare(file.Path(), {}) == PathClassificationReadStep::ReadyToIssue && writeOnly.ReadyToIssue() &&
               source.calls == 2,
           "write-only-owned-handle-prepares-the-real-read-api");
    Expect(writeOnly.Issue({}) == PathClassificationReadStep::Complete &&
               writeOnly.Classification() == PathClassification::Image,
           "actual-readfile-access-refusal-is-best-effort-image");
    Expect(WaitForSingleObject(fixture.Event(), 0) == WAIT_OBJECT_0,
           "failed-issue-consumes-its-registered-no-io-cycle");
    Expect(!writeOnly.HasPendingRead() && !writeOnly.ReadyToIssue() &&
               writeOnly.Acknowledge({}) == PathClassificationReadStep::Complete,
           "failed-issue-has-no-pending-native-buffer");
}

struct RegisteredTest final
{
    const char* name;
    void (*run)();
    std::size_t expectedCaseIndex;
};

constexpr std::array kTests{
    RegisteredTest{"filesystem-controls-and-input-rejection", TestFilesystemControlsAndInputRejection, 0},
    RegisteredTest{"explicit-retirement-of-prepared-unissued-cycle", TestExplicitRetirementOfPreparedUnissuedCycle, 1},
    RegisteredTest{"real-file-signatures-and-reuse", TestRealFileSignaturesAndReuse, 2},
    RegisteredTest{"premature-signal-does-not-acknowledge", TestPrematureSignalDoesNotAcknowledgeRead, 3},
    RegisteredTest{"pending-cancellation-and-acknowledgement", TestPendingCancellationAndAcknowledgement, 4},
    RegisteredTest{"forced-retirement-without-token-cancellation",
                   TestForcedRetirementOfPendingReadWithoutTokenCancellation, 5},
    RegisteredTest{"shutdown-joins-cancelled-native-read", TestShutdownJoinsCancelledNativeRead, 6},
    RegisteredTest{"native-open-and-read-refusal", TestNativeOpenAndReadRefusal, 7},
};
} // namespace

int RunHostPathClassificationReadTests()
{
    gFailures = 0;
    gAssertions = 0;
    gCurrentCaseAssertionCount = 0;
    gCurrentCase = nullptr;
    for (const RegisteredTest& test : kTests)
    {
        const int failuresBefore = gFailures;
        const ExpectedCase& expectedCase = kExpectedCases[test.expectedCaseIndex];
        if (std::string_view{test.name} != expectedCase.name)
        {
            std::printf("FAIL path-classification-read.case-registry expected=%s actual=%s\n", expectedCase.name,
                        test.name);
            ++gFailures;
        }
        gCurrentCase = test.name;
        gCurrentCaseAssertionCount = 0;
        try
        {
            test.run();
        }
        catch (const std::bad_alloc&)
        {
            Expect(false, "allocation-failure-during-test-setup");
        }
        while (gCurrentCaseAssertionCount < expectedCase.count)
        {
            const ExpectedAssertion& missing = kExpectedAssertions[expectedCase.first + gCurrentCaseAssertionCount++];
            std::printf("FAIL path-classification-read.missing-assertion %s.%s\n", missing.caseName,
                        missing.assertionName);
            ++gFailures;
        }
        if (gFailures != failuresBefore)
        {
            std::printf("FAIL path-classification-read.case.%s\n", test.name);
        }
    }
    if (kTests.size() != kExpectedCases.size())
    {
        std::printf("FAIL path-classification-read.case-count expected=%zu actual=%zu\n", kExpectedCases.size(),
                    kTests.size());
        ++gFailures;
    }
    std::printf("SUMMARY path-classification-read registered=%zu expectedAssertions=%zu actualAssertions=%zu "
                "failures=%d\n",
                kTests.size(), kExpectedAssertions.size(), gAssertions, gFailures);
    return gFailures;
}
