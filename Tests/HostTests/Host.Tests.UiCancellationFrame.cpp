#include "Host.Tests.UiCancellationFrame.h"

#include "../../RedPrism/UiCancellationFrame.h"

#include <cstdio>
#include <limits>

namespace
{
unsigned int gFailures = 0;
unsigned int gChecks = 0;

void Expect(bool passed, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s ui-cancellation-frame.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
        ++gFailures;
}

UiCancellationFrame::Acceptance AcceptedAt(std::uint64_t ordinal = 40) noexcept
{
    return {{7, 41}, 501, 29, ordinal, true, true};
}

UiCancellationFrame::Frame VisibleAt(std::uint64_t ordinal = 41) noexcept
{
    return {{7, 41}, 501,  29,    ordinal, UiCancellationFrame::PresentAttempt::SuccessfulVisible,
            true,    true, false, true,    true};
}

void FirstVisibleSuccessQualifies() noexcept
{
    UiCancellationFrame proof;
    const bool accepted = proof.Accept(AcceptedAt(40));
    const auto firstVisible = proof.Observe(VisibleAt(41));
    Expect(accepted && firstVisible == UiCancellationFrame::State::Qualified && proof.QualifiedOrdinal() == 41,
           "first-successful-visible-ordinal-proves-original-frame");
}

void StaleFirstFrameCannotBeReplacedByLaterRestore() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt(40));
    auto stale = VisibleAt(41);
    stale.previewSelected = true;
    const auto first = proof.Observe(stale);
    const auto later = proof.Observe(VisibleAt(42));
    Expect(first == UiCancellationFrame::State::Rejected && later == UiCancellationFrame::State::Rejected &&
               proof.QualifiedOrdinal() == 0,
           "later-restored-frame-cannot-hide-stale-first-success");
}

void ChromeOnlyFirstPresentPermanentlyRefuses() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt());
    auto chromeOnly = VisibleAt();
    chromeOnly.canvasDrawSucceeded = false;
    Expect(proof.Observe(chromeOnly) == UiCancellationFrame::State::Rejected,
           "chrome-only-present-cannot-prove-restored-source");
    Expect(proof.Observe(VisibleAt(42)) == UiCancellationFrame::State::Rejected && proof.QualifiedOrdinal() == 0,
           "later-canvas-success-cannot-replace-first-chrome-only-present");
}

void NoPresentAndOcclusionStayIncomplete() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt(40));
    auto noAttempt = VisibleAt(40);
    noAttempt.attempt = UiCancellationFrame::PresentAttempt::None;
    const auto afterNoAttempt = proof.Observe(noAttempt);
    auto occluded = VisibleAt(40);
    occluded.attempt = UiCancellationFrame::PresentAttempt::Occluded;
    const auto afterOcclusion = proof.Observe(occluded);
    Expect(afterNoAttempt == UiCancellationFrame::State::AwaitingFirstVisibleFrame &&
               afterOcclusion == UiCancellationFrame::State::AwaitingFirstVisibleFrame && proof.QualifiedOrdinal() == 0,
           "no-visible-success-and-occlusion-remain-incomplete");
}

void TestAndFailedPresentDoNotAdvance() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt(40));
    auto testOnly = VisibleAt(40);
    testOnly.attempt = UiCancellationFrame::PresentAttempt::TestOnly;
    auto failed = VisibleAt(40);
    failed.attempt = UiCancellationFrame::PresentAttempt::Failed;
    const auto afterTest = proof.Observe(testOnly);
    const auto afterFailure = proof.Observe(failed);
    Expect(afterTest == UiCancellationFrame::State::AwaitingFirstVisibleFrame &&
               afterFailure == UiCancellationFrame::State::AwaitingFirstVisibleFrame && proof.QualifiedOrdinal() == 0,
           "test-and-failed-present-do-not-consume-visible-ordinal");
}

void MissingIdentityAndRepeatedAcceptanceRefuse() noexcept
{
    UiCancellationFrame missing;
    auto noIdentity = AcceptedAt();
    noIdentity.original.generation = 0;
    const bool missingAccepted = missing.Accept(noIdentity);

    UiCancellationFrame repeated;
    const bool firstAccepted = repeated.Accept(AcceptedAt());
    const bool secondAccepted = repeated.Accept(AcceptedAt());
    Expect(!missingAccepted && missing.Current() == UiCancellationFrame::State::Rejected && firstAccepted &&
               !secondAccepted && repeated.Current() == UiCancellationFrame::State::Rejected,
           "missing-identity-and-repeated-acceptance-refuse");
}

void ChangedAcceptanceIdentityRefuses() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt());
    auto changed = VisibleAt();
    changed.original.generation = 42;
    Expect(proof.Observe(changed) == UiCancellationFrame::State::Rejected,
           "frame-from-different-accepted-escape-refuses");
}

void SkippedOrdinalRefuses() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt(40));
    Expect(proof.Observe(VisibleAt(42)) == UiCancellationFrame::State::Rejected,
           "missing-successful-ordinal-refuses-proof");
}

void WrongCommittedSourceOrPreviewRefuses() noexcept
{
    UiCancellationFrame wrongSource;
    (void)wrongSource.Accept(AcceptedAt());
    auto source = VisibleAt();
    source.originalStateCurrent = false;

    UiCancellationFrame stalePreview;
    (void)stalePreview.Accept(AcceptedAt());
    auto preview = VisibleAt();
    preview.committedSourceSelected = false;
    preview.previewSelected = true;

    Expect(wrongSource.Observe(source) == UiCancellationFrame::State::Rejected &&
               stalePreview.Observe(preview) == UiCancellationFrame::State::Rejected,
           "wrong-source-and-preview-selection-refuse");
}

void VisibleCardRefuses() noexcept
{
    UiCancellationFrame proof;
    (void)proof.Accept(AcceptedAt());
    auto visibleCard = VisibleAt();
    visibleCard.cardHidden = false;
    Expect(proof.Observe(visibleCard) == UiCancellationFrame::State::Rejected,
           "reopened-adjustment-card-refuses-frame");
}

void ChangedDocumentOrOriginalStateRefuses() noexcept
{
    UiCancellationFrame authority;
    (void)authority.Accept(AcceptedAt());
    auto changedAuthority = VisibleAt();
    changedAuthority.documentSerial = 502;

    UiCancellationFrame history;
    (void)history.Accept(AcceptedAt());
    auto changedHistory = VisibleAt();
    changedHistory.originalStateCurrent = false;

    Expect(authority.Observe(changedAuthority) == UiCancellationFrame::State::Rejected &&
               history.Observe(changedHistory) == UiCancellationFrame::State::Rejected,
           "changed-document-or-exact-original-state-refuses-frame");
}

void CounterReversalAndOverflowRefuse() noexcept
{
    UiCancellationFrame reversed;
    (void)reversed.Accept(AcceptedAt(40));
    auto backwards = VisibleAt(39);

    UiCancellationFrame overflow;
    auto lastOrdinal = AcceptedAt((std::numeric_limits<std::uint64_t>::max)());
    const bool overflowAccepted = overflow.Accept(lastOrdinal);

    Expect(reversed.Observe(backwards) == UiCancellationFrame::State::Rejected && !overflowAccepted &&
               overflow.Current() == UiCancellationFrame::State::Rejected,
           "backward-counter-and-unadvanceable-maximum-refuse");
}

void UnauthenticatedAttemptsRefuse() noexcept
{
    UiCancellationFrame unknown;
    (void)unknown.Accept(AcceptedAt());
    auto invalid = VisibleAt(40);
    invalid.attempt = static_cast<UiCancellationFrame::PresentAttempt>(255);
    Expect(unknown.Observe(invalid) == UiCancellationFrame::State::Rejected, "unknown-present-attempt-refuses");
    UiCancellationFrame unchanged;
    (void)unchanged.Accept(AcceptedAt());
    Expect(unchanged.Observe(VisibleAt(40)) == UiCancellationFrame::State::Rejected,
           "successful-present-without-ordinal-advance-refuses");
    UiCancellationFrame occluded;
    (void)occluded.Accept(AcceptedAt());
    auto impossible = VisibleAt(41);
    impossible.attempt = UiCancellationFrame::PresentAttempt::Occluded;
    Expect(occluded.Observe(impossible) == UiCancellationFrame::State::Rejected,
           "occluded-attempt-cannot-explain-successful-count-advance");
    UiCancellationFrame reopened;
    (void)reopened.Accept(AcceptedAt());
    auto newer = VisibleAt();
    newer.opening = 30;
    Expect(reopened.Observe(newer) == UiCancellationFrame::State::Rejected,
           "different-panel-opening-cannot-prove-original-escape");
}
} // namespace

bool RunUiCancellationFrameTests() noexcept
{
    gFailures = 0;
    gChecks = 0;
    FirstVisibleSuccessQualifies();
    StaleFirstFrameCannotBeReplacedByLaterRestore();
    ChromeOnlyFirstPresentPermanentlyRefuses();
    NoPresentAndOcclusionStayIncomplete();
    TestAndFailedPresentDoNotAdvance();
    MissingIdentityAndRepeatedAcceptanceRefuse();
    ChangedAcceptanceIdentityRefuses();
    SkippedOrdinalRefuses();
    WrongCommittedSourceOrPreviewRefuses();
    VisibleCardRefuses();
    ChangedDocumentOrOriginalStateRefuses();
    CounterReversalAndOverflowRefuse();
    UnauthenticatedAttemptsRefuse();
    const bool passed = gFailures == 0 && gChecks == 17;
    std::printf("%s UI cancellation frame reduction: %u checks, %u failures.\n", passed ? "PASS" : "FAIL", gChecks,
                gFailures);
    std::fflush(stdout);
    return passed;
}