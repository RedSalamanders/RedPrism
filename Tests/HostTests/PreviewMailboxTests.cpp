#include "PreviewMailboxTests.h"

#include "../../Common/PreviewMailbox.h"

#include <cstdio>
#include <memory>

namespace
{
struct Lifetime final
{
    int destroyed = 0;
};

struct CountedDelete final
{
    Lifetime* lifetime = nullptr;

    void operator()(int* value) const noexcept
    {
        if (value != nullptr)
        {
            ++lifetime->destroyed;
            delete value;
        }
    }
};

using Payload = std::unique_ptr<int, CountedDelete>;

struct Request final
{
    Payload payload;

    Request(Lifetime& lifetime, int value) : payload(new int(value), CountedDelete{&lifetime}) {}

    Request(Request&&) noexcept = default;
    Request& operator=(Request&&) noexcept = default;
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
};

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s preview-mailbox.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}
} // namespace

int RunPreviewMailboxTests()
{
    g_failures = 0;

    Lifetime lifetime;
    PreviewMailbox<Request> mailbox;

    Request rejected{lifetime, 90};
    const int* const rejectedAddress = rejected.payload.get();
    Expect(!mailbox.Offer(rejected).accepted && rejected.payload && *rejected.payload == 90 &&
               rejected.payload.get() == rejectedAddress && lifetime.destroyed == 0,
           "closed-offer-retains-caller-request");

    Expect(mailbox.Begin(), "begin-first-epoch");
    // archived behavioral reference and Filters.swift:340-365 keep
    // the running preview alive and coalesce later settings into the newest pending job.
    Request first{lifetime, 1};
    const PreviewOffer offer1 = mailbox.Offer(first);
    auto launch1 = mailbox.TakeNext();
    Expect(offer1.accepted && !first.payload && launch1 && launch1->stamp.sequence == 1 && *launch1->input.payload == 1,
           "offer-1-launches-with-moved-payload");
    if (!launch1)
    {
        return g_failures;
    }
    const PreviewStamp stamp1 = launch1->stamp;

    Request second{lifetime, 2};
    const PreviewOffer offer2 = mailbox.Offer(second);
    Request third{lifetime, 3};
    const PreviewOffer offer3 = mailbox.Offer(third);
    Expect(offer2.accepted && offer3.accepted && !second.payload && !third.payload && lifetime.destroyed == 1,
           "offer-3-destroys-superseded-pending-2");
    Expect(mailbox.CanPublish(stamp1), "pending-offers-preserve-active-1-publish-right");
    Expect(!mailbox.TakeNext(), "pending-3-waits-until-active-finishes");
    Expect(mailbox.Finish(stamp1, true), "finish-active-1-success");
    Expect(!mailbox.Finish(stamp1, true), "duplicate-finish-1-refused");

    auto launch3 = mailbox.TakeNext();
    Expect(launch3 && launch3->stamp.sequence == 3 && *launch3->input.payload == 3,
           "only-latest-pending-3-launches-after-finish-1");
    if (!launch3)
    {
        return g_failures;
    }
    const PreviewStamp stamp3 = launch3->stamp;
    Expect(!mailbox.Finish(stamp1, false) && mailbox.CanPublish(stamp3), "stale-finish-1-cannot-finish-active-3");
    Expect(mailbox.Finish(stamp3, true), "finish-active-3-success");

    auto refinement3 = mailbox.TakeRefinement();
    Expect(refinement3 && *refinement3 == stamp3 && mailbox.CanPublishRefinement(stamp3),
           "latest-settled-success-allows-refinement");
    Expect(!mailbox.TakeRefinement(), "one-refinement-attempt-per-sequence");

    Request fourth{lifetime, 4};
    const PreviewOffer offer4 = mailbox.Offer(fourth);
    Expect(offer4.accepted && offer4.supersededRefinement == refinement3 && !mailbox.CanPublishRefinement(stamp3),
           "new-offer-invalidates-refinement-and-returns-cancel-stamp");
    auto launch4 = mailbox.TakeNext();
    Expect(launch4 && launch4->stamp.sequence == 4 && *launch4->input.payload == 4 &&
               mailbox.CanPublish(launch4->stamp),
           "interactive-4-launches-after-refinement-invalidation");
    if (!launch4)
    {
        return g_failures;
    }
    const PreviewStamp stamp4 = launch4->stamp;
    Expect(!mailbox.FinishRefinement(stamp3), "invalidated-refinement-finish-refused");
    Request fifth{lifetime, 5};
    const PreviewOffer offer5 = mailbox.Offer(fifth);
    Request sixth{lifetime, 6};
    const PreviewOffer offer6 = mailbox.Offer(sixth);
    Expect(offer5.accepted && offer6.accepted && lifetime.destroyed == 2, "offer-6-destroys-superseded-pending-5");
    Expect(mailbox.Finish(stamp4, false), "failed-submission-finishes-active-4");
    Expect(!mailbox.TakeRefinement(), "failed-preview-with-pending-work-does-not-refine");
    auto launch6 = mailbox.TakeNext();
    Expect(launch6 && launch6->stamp.sequence == 6 && *launch6->input.payload == 6,
           "failed-submission-allows-latest-pending-6-through");
    if (!launch6)
    {
        return g_failures;
    }
    const PreviewStamp stamp6 = launch6->stamp;
    Expect(mailbox.Finish(stamp6, true), "finish-active-6-success");
    auto refinement6 = mailbox.TakeRefinement();
    Expect(refinement6 && *refinement6 == stamp6, "latest-successful-6-allows-refinement");
    Expect(mailbox.FinishRefinement(stamp6), "finish-refinement-6");
    Expect(!mailbox.TakeRefinement(), "finished-refinement-6-is-not-retried");

    Request seventh{lifetime, 7};
    const PreviewOffer offer7 = mailbox.Offer(seventh);
    auto launch7 = mailbox.TakeNext();
    Expect(offer7.accepted && launch7 && launch7->stamp.sequence == 7, "launch-7-before-close");
    if (!launch7)
    {
        return g_failures;
    }
    Request eighth{lifetime, 8};
    const PreviewOffer offer8 = mailbox.Offer(eighth);
    Expect(offer8.accepted, "offer-8-pending-before-close");
    const InvalidatedPreviews closedWithInteractive = mailbox.Close();
    Expect(closedWithInteractive.interactive == launch7->stamp && !closedWithInteractive.refinement &&
               !mailbox.CanPublish(launch7->stamp) && !mailbox.Finish(launch7->stamp, true),
           "close-invalidates-active-and-returns-interactive-stamp");
    Expect(lifetime.destroyed == 3 && launch7->input.payload && *launch7->input.payload == 7,
           "close-destroys-pending-8-but-launch-7-retains-in-flight-input");
    launch7.reset();
    Expect(lifetime.destroyed == 4, "in-flight-input-dies-with-launch-owner");

    Expect(mailbox.Begin(), "reopen-second-epoch");
    // retained numeric golden cases covers the stale preview that
    // must not return after preview-off followed by reopening the edit.
    Request reopened{lifetime, 9};
    const PreviewOffer reopenOffer = mailbox.Offer(reopened);
    auto launch9 = mailbox.TakeNext();
    Expect(reopenOffer.accepted && launch9 && launch9->stamp.epoch != stamp1.epoch && !mailbox.CanPublish(stamp1) &&
               mailbox.CanPublish(launch9->stamp),
           "older-epoch-stamp-cannot-publish-after-reopen");
    if (!launch9)
    {
        return g_failures;
    }

    Lifetime otherLifetime;
    PreviewMailbox<Request> otherMailbox;
    Expect(otherMailbox.Begin(), "begin-second-mailbox");
    Request otherRequest{otherLifetime, 10};
    const PreviewOffer otherOffer = otherMailbox.Offer(otherRequest);
    auto otherLaunch = otherMailbox.TakeNext();
    Expect(otherOffer.accepted && otherLaunch && !otherMailbox.CanPublish(launch9->stamp) &&
               !otherMailbox.Finish(launch9->stamp, false) && otherMailbox.CanPublish(otherLaunch->stamp),
           "cross-mailbox-stamp-cannot-publish-or-finish");
    if (!otherLaunch)
    {
        return g_failures;
    }

    Expect(mailbox.Finish(launch9->stamp, true), "finish-active-9-success");
    const auto closeRefinement = mailbox.TakeRefinement();
    Expect(closeRefinement && mailbox.CanPublishRefinement(*closeRefinement), "refinement-active-before-close");
    if (!closeRefinement)
    {
        return g_failures;
    }
    const InvalidatedPreviews closedWithRefinement = mailbox.Close();
    Expect(closedWithRefinement.refinement == closeRefinement && !closedWithRefinement.interactive &&
               !mailbox.CanPublishRefinement(*closeRefinement) && !mailbox.FinishRefinement(*closeRefinement),
           "close-returns-and-invalidates-refinement-stamp");
    Expect(otherMailbox.Finish(otherLaunch->stamp, false), "finish-other-mailbox-request");
    const InvalidatedPreviews otherClosed = otherMailbox.Close();
    Expect(!otherClosed.interactive && !otherClosed.refinement, "close-finished-other-mailbox");

    return g_failures;
}
