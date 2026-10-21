#include "Host.Tests.PolygonalInputJournal.h"
#include "HostPolygonalInputJournal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>

namespace
{
int failures = 0;
unsigned checks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s host-polygonal-journal.%s\n", condition ? "PASS" : "FAIL", name);
    ++checks;
    failures += condition ? 0 : 1;
}

void ExpectIndexed(bool condition, const char* name, std::uint64_t index) noexcept
{
    std::printf("%s host-polygonal-journal.%s.%llu\n", condition ? "PASS" : "FAIL", name,
                static_cast<unsigned long long>(index));
    failures += condition ? 0 : 1;
    ++checks;
}

using Journal = HostPolygonalInputJournal;
using Context = Journal::Context;
using Sample = Journal::Sample;
using Action = Journal::Action;
using Admission = Journal::Admission;

Guid MakeGuid(std::uint8_t seed) noexcept
{
    Guid result{};
    for (std::size_t i = 0; i < result.bytes.size(); ++i)
        result.bytes[i] = static_cast<std::uint8_t>(seed + i);
    return result;
}

Context MakeContext(std::uint64_t ordinal = 1, std::uint8_t identity = 1) noexcept
{
    Context result{};
    result.document.sessionIdentity = MakeGuid(identity);
    result.document.documentSerial = 17;
    result.document.generation = 23;
    result.document.restoreSerial = 29;
    result.document.canvasWidth = 800;
    result.document.canvasHeight = 600;
    result.ordinal = ordinal;
    result.tool = HostSelectionPressInput::Tool::Lasso;
    result.kind = LassoKind::Polygonal;
    result.mode = SelectionMode::New;
    return result;
}

Sample MakeSample(std::uint64_t ordinal, double x, double y) noexcept
{
    Sample result{};
    result.point = {x, y};
    result.modifiers = {ordinal % 2 != 0, ordinal % 3 != 0, ordinal % 5 != 0};
    result.ordinal = ordinal;
    result.viewScale = 1.0 + static_cast<double>(ordinal) / 10.0;
    result.antialiased = ordinal % 2 == 0;
    result.doubleClick = ordinal % 3 == 0;
    result.leftDown = ordinal % 4 != 0;
    result.documentOrigin = {static_cast<double>(ordinal) * 0.25, -static_cast<double>(ordinal) * 0.5};
    result.viewPoint = {result.documentOrigin.x + x * result.viewScale, result.documentOrigin.y + y * result.viewScale};
    result.dipScale = 1.0 + static_cast<double>(ordinal % 3) * 0.5;
    return result;
}

bool SameSample(const Sample& a, const Sample& b) noexcept
{
    return a.point == b.point && a.modifiers == b.modifiers && a.ordinal == b.ordinal && a.viewScale == b.viewScale &&
           a.antialiased == b.antialiased && a.doubleClick == b.doubleClick && a.leftDown == b.leftDown &&
           a.viewPoint == b.viewPoint && a.documentOrigin == b.documentOrigin && a.dipScale == b.dipScale;
}

void TestFifoAndContext() noexcept
{
    auto journal = std::make_unique<Journal>();
    auto first = MakeContext(1, 11);
    auto down = MakeSample(1, 10, 20);
    down.viewScale = 2.5;
    down.antialiased = false;
    down.doubleClick = true;
    down.leftDown = true;
    Expect(journal->Begin(4, first, down), "fifo_begin_records_first_down");

    auto move = MakeSample(3, 11, 21);
    Expect(journal->Record(4, Action::Move, move) == Admission::Recorded, "fifo_move_admitted");
    auto up = MakeSample(5, 12, 22);
    Expect(journal->Record(4, Action::Up, up) == Admission::Recorded, "fifo_up_admitted");

    auto later = MakeContext(7, 11);
    later.mode = SelectionMode::Add;
    later.mask = true;
    later.active = MakeGuid(100);
    later.crop = CropRect{1, 2, 300, 400};
    later.wand = {91, WandSampleSize::FiveByFive, false, true};
    later.document.generation = 31;
    later.document.restoreSerial = 37;
    auto laterDown = MakeSample(7, 30, 40);
    laterDown.modifiers = {false, true, true};
    laterDown.viewScale = 4.25;
    laterDown.antialiased = false;
    laterDown.doubleClick = true;
    laterDown.leftDown = true;
    Expect(journal->Down(4, later, laterDown) == Admission::Recorded, "fifo_later_down_admitted_after_up");
    Expect(!journal->Closed(), "fifo_up_does_not_close_producer");
    Expect(journal->PendingCount() == 4, "fifo_four_records_pending");

    const Action expectedActions[] = {Action::Down, Action::Move, Action::Up, Action::Down};
    const Sample expectedSamples[] = {down, move, up, laterDown};
    const Context expectedContexts[] = {first, Context{}, Context{}, later};
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto peeked = journal->Peek(4);
        Expect(peeked.has_value(), i == 0   ? "fifo_peek_first_present"
                                   : i == 1 ? "fifo_peek_second_present"
                                   : i == 2 ? "fifo_peek_third_present"
                                            : "fifo_peek_fourth_present");
        if (!peeked)
            break;
        Expect(peeked->action == expectedActions[i], i == 0   ? "fifo_first_action_down"
                                                     : i == 1 ? "fifo_second_action_move"
                                                     : i == 2 ? "fifo_third_action_up"
                                                              : "fifo_fourth_action_down");
        Expect(SameSample(peeked->sample, expectedSamples[i]), i == 0   ? "fifo_first_scalar_metadata"
                                                               : i == 1 ? "fifo_second_scalar_metadata"
                                                               : i == 2 ? "fifo_third_scalar_metadata"
                                                                        : "fifo_fourth_scalar_metadata");
        Expect(peeked->down.has_value() == (i == 0 || i == 3), i == 0   ? "fifo_first_context_present"
                                                               : i == 1 ? "fifo_second_context_absent"
                                                               : i == 2 ? "fifo_third_context_absent"
                                                                        : "fifo_fourth_context_present");
        if (peeked->down)
        {
            const auto& actual = peeked->down.value();
            const auto& expected = expectedContexts[i];
            Expect(actual.document == expected.document,
                   i == 0 ? "fifo_first_full_document_authority" : "fifo_fourth_full_document_authority");
            Expect(actual.active == expected.active,
                   i == 0 ? "fifo_first_target_identity" : "fifo_fourth_target_identity");
            Expect(actual.wand == expected.wand,
                   i == 0 ? "fifo_first_full_wand_context" : "fifo_fourth_full_wand_context");
            Expect(actual.crop.has_value() == expected.crop.has_value(),
                   i == 0 ? "fifo_first_crop_presence" : "fifo_fourth_crop_presence");
            if (actual.crop && expected.crop)
                Expect(actual.crop->x == expected.crop->x && actual.crop->y == expected.crop->y &&
                           actual.crop->width == expected.crop->width && actual.crop->height == expected.crop->height,
                       "fifo_fourth_crop_values");
            Expect(actual.tool == expected.tool && actual.kind == expected.kind,
                   i == 0 ? "fifo_first_tool_kind" : "fifo_fourth_tool_kind");
            Expect(peeked->down->ordinal == expectedContexts[i].ordinal,
                   i == 0 ? "fifo_first_context_ordinal" : "fifo_fourth_context_ordinal");
            Expect(peeked->down->mode == expectedContexts[i].mode,
                   i == 0 ? "fifo_first_context_mode" : "fifo_fourth_context_mode");
            Expect(peeked->down->mask == expectedContexts[i].mask,
                   i == 0 ? "fifo_first_context_mask" : "fifo_fourth_context_mask");
        }
        const auto secondPeek = journal->Peek(4);
        Expect(secondPeek.has_value() && secondPeek->action == expectedActions[i] &&
                   SameSample(secondPeek->sample, expectedSamples[i]),
               i == 0   ? "fifo_peek_nonconsuming_first"
               : i == 1 ? "fifo_peek_nonconsuming_second"
               : i == 2 ? "fifo_peek_nonconsuming_third"
                        : "fifo_peek_nonconsuming_fourth");
        const auto taken = journal->Take(4);
        Expect(taken.has_value() && taken->action == expectedActions[i] &&
                   SameSample(taken->sample, expectedSamples[i]),
               i == 0   ? "fifo_take_first"
               : i == 1 ? "fifo_take_second"
               : i == 2 ? "fifo_take_third"
                        : "fifo_take_fourth");
    }
    Expect(journal->PendingCount() == 0, "fifo_drained_exactly");
    Expect(!journal->Finish(4), "fifo_finish_requires_producer_close");
    Expect(journal->CloseInput(4), "fifo_explicit_producer_close");
    Expect(journal->Finish(4), "fifo_finish_after_close_and_drain");
}

void TestRejectedInputsPreservePrefix() noexcept
{
    auto journal = std::make_unique<Journal>();
    auto context = MakeContext(10, 21);
    Expect(journal->Begin(9, context, MakeSample(10, 1, 2)), "reject_begin");
    auto baseline = MakeSample(12, 3, 4);
    Expect(journal->Record(9, Action::Move, baseline) == Admission::Recorded, "reject_prefix_append");
    const std::size_t prefixCount = journal->PendingCount();

    Expect(journal->Record(8, Action::Move, MakeSample(13, 5, 6)) == Admission::Stale, "reject_foreign_stream_stale");
    auto badContext = MakeContext(14, 22);
    Expect(journal->Down(9, badContext, MakeSample(14, 5, 6)) == Admission::Stale, "reject_foreign_session_stale");
    badContext = context;
    ++badContext.document.documentSerial;
    badContext.ordinal = 14;
    Expect(journal->Down(9, badContext, MakeSample(14, 5, 6)) == Admission::Stale, "reject_foreign_document_stale");
    badContext = context;
    ++badContext.document.canvasWidth;
    badContext.ordinal = 14;
    Expect(journal->Down(9, badContext, MakeSample(14, 5, 6)) == Admission::Stale, "reject_changed_width_stale");
    badContext = context;
    ++badContext.document.canvasHeight;
    badContext.ordinal = 14;
    Expect(journal->Down(9, badContext, MakeSample(14, 5, 6)) == Admission::Stale, "reject_changed_height_stale");

    auto invalidContext = context;
    invalidContext.tool = static_cast<HostSelectionPressInput::Tool>(255);
    invalidContext.ordinal = 14;
    Expect(journal->Down(9, invalidContext, MakeSample(14, 5, 6)) == Admission::Invalid, "reject_invalid_tool_enum");
    invalidContext = context;
    invalidContext.kind = LassoKind::Rectangle;
    invalidContext.ordinal = 14;
    Expect(journal->Down(9, invalidContext, MakeSample(14, 5, 6)) == Admission::Invalid, "reject_invalid_lasso_kind");
    invalidContext = context;
    invalidContext.mode = static_cast<SelectionMode>(255);
    invalidContext.ordinal = 14;
    Expect(journal->Down(9, invalidContext, MakeSample(14, 5, 6)) == Admission::Invalid,
           "reject_invalid_selection_mode_enum");
    invalidContext = context;
    invalidContext.ordinal = 14;
    auto mismatched = MakeSample(15, 5, 6);
    Expect(journal->Down(9, invalidContext, mismatched) == Admission::Invalid,
           "reject_context_sample_ordinal_mismatch");
    invalidContext.ordinal = 15;
    auto zeroScale = MakeSample(15, 5, 6);
    zeroScale.viewScale = 0;
    Expect(journal->Down(9, invalidContext, zeroScale) == Admission::Invalid, "reject_zero_scale");
    auto negativeScale = MakeSample(15, 5, 6);
    negativeScale.viewScale = -1;
    Expect(journal->Down(9, invalidContext, negativeScale) == Admission::Invalid, "reject_negative_scale");
    auto nanScale = MakeSample(15, 5, 6);
    nanScale.viewScale = std::numeric_limits<double>::quiet_NaN();
    Expect(journal->Down(9, invalidContext, nanScale) == Admission::Invalid, "reject_nan_scale");
    auto infiniteScale = MakeSample(15, 5, 6);
    infiniteScale.viewScale = std::numeric_limits<double>::infinity();
    Expect(journal->Down(9, invalidContext, infiniteScale) == Admission::Invalid, "reject_infinite_scale");

    auto invalidView = MakeSample(15, 5, 6);
    invalidView.viewPoint.x = std::numeric_limits<double>::quiet_NaN();
    Expect(journal->Record(9, Action::Move, invalidView) == Admission::Invalid, "reject_nan_physical_view_point");
    invalidView = MakeSample(15, 5, 6);
    invalidView.documentOrigin.y = std::numeric_limits<double>::infinity();
    Expect(journal->Record(9, Action::Move, invalidView) == Admission::Invalid, "reject_infinite_document_origin");
    const double invalidDipScales[] = {0, -1, std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::infinity()};
    const char* dipNames[] = {"reject_zero_dip_scale", "reject_negative_dip_scale", "reject_nan_dip_scale",
                              "reject_infinite_dip_scale"};
    for (std::size_t i = 0; i < 4; ++i)
    {
        invalidView = MakeSample(15, 5, 6);
        invalidView.dipScale = invalidDipScales[i];
        Expect(journal->Record(9, Action::Move, invalidView) == Admission::Invalid, dipNames[i]);
    }

    const std::uint64_t badOrdinals[] = {0, 11, 12};
    const char* ordinalNames[] = {"reject_zero_ordinal", "reject_decreasing_ordinal", "reject_duplicate_ordinal"};
    for (std::size_t i = 0; i < 3; ++i)
        Expect(journal->Record(9, Action::Move, MakeSample(badOrdinals[i], 8, 9)) == Admission::Invalid,
               ordinalNames[i]);
    auto nanPoint = MakeSample(14, 8, 9);
    nanPoint.point.x = std::numeric_limits<double>::quiet_NaN();
    Expect(journal->Record(9, Action::Move, nanPoint) == Admission::Invalid, "reject_nan_x_coordinate");
    auto infPoint = MakeSample(14, 8, 9);
    infPoint.point.y = std::numeric_limits<double>::infinity();
    Expect(journal->Record(9, Action::Move, infPoint) == Admission::Invalid, "reject_infinite_y_coordinate");
    Expect(journal->Record(9, static_cast<Action>(255), MakeSample(14, 8, 9)) == Admission::Invalid,
           "reject_invalid_action_enum");
    Expect(journal->Record(9, Action::Down, MakeSample(14, 8, 9)) == Admission::Invalid,
           "reject_down_through_record_api");
    Expect(journal->PendingCount() == prefixCount, "reject_refusals_preserve_prefix_count");
    const auto first = journal->Take(9);
    Expect(first.has_value() && first->sample.ordinal == 10, "reject_prefix_first_record_unchanged");
    const auto second = journal->Take(9);
    Expect(second.has_value() && second->sample.ordinal == 12 && SameSample(second->sample, baseline),
           "reject_prefix_second_record_unchanged");
    Expect(journal->CloseInput(9), "reject_close_after_prefix");
    Expect(journal->Finish(9), "reject_finish_after_preserved_prefix_drain");
}

void TestCapacityAndEmergencyLoss() noexcept
{
    auto journal = std::make_unique<Journal>();
    auto context = MakeContext(1, 31);
    Expect(journal->Begin(12, context, MakeSample(1, 1, 1)), "capacity_begin");
    for (std::uint64_t ordinal = 2; ordinal <= Journal::kCapacity; ++ordinal)
        ExpectIndexed(journal->Record(12, Action::Move, MakeSample(ordinal, static_cast<double>(ordinal), 3)) ==
                          Admission::Recorded,
                      "capacity-accepted", ordinal);
    Expect(journal->PendingCount() == Journal::kCapacity, "capacity_exactly_512_preserved");
    auto overflowSample = MakeSample(Journal::kCapacity + 1, 999, 4);
    Expect(journal->Record(12, Action::Move, overflowSample) == Admission::Overflow,
           "capacity_next_append_reports_overflow");
    Expect(journal->Overflowed(), "capacity_overflow_flag_set");
    Expect(journal->Closed(), "capacity_overflow_closes_producer");
    Expect(journal->PendingCount() == Journal::kCapacity + 1, "capacity_loss_slot_is_additional");
    for (std::uint64_t ordinal = 1; ordinal <= Journal::kCapacity; ++ordinal)
    {
        const auto event = journal->Take(12);
        ExpectIndexed(event.has_value(), "capacity-prefix-present", ordinal);
        if (!event)
            break;
        ExpectIndexed(event->sample.ordinal == ordinal, "capacity-prefix-ordinal", ordinal);
        ExpectIndexed(event->action == (ordinal == 1 ? Action::Down : Action::Move), "capacity-prefix-action", ordinal);
    }
    const auto loss = journal->Peek(12);
    Expect(loss.has_value(), "capacity_emergency_loss_present_after_prefix");
    if (loss)
    {
        Expect(loss->action == Action::Lost, "capacity_emergency_loss_action");
        Expect(loss->emergency, "capacity_emergency_loss_marked");
        Expect(loss->sample.ordinal == Journal::kCapacity, "capacity_emergency_reuses_last_ordinal");
        Expect(loss->down == std::nullopt, "capacity_emergency_has_no_down_context");
    }
    const auto consumedLoss = journal->Take(12);
    Expect(consumedLoss.has_value() && consumedLoss->action == Action::Lost && consumedLoss->emergency,
           "capacity_emergency_loss_consumed_once");
    Expect(!journal->Peek(12).has_value(), "capacity_emergency_loss_not_repeated");
    Expect(journal->CloseInput(12) == false, "capacity_loss_already_closed_input");
    Expect(journal->Finish(12), "capacity_finish_after_prefix_and_loss_drain");
}

void TestExplicitLostAndRingWrap() noexcept
{
    auto journal = std::make_unique<Journal>();
    auto context = MakeContext(1, 41);
    Expect(journal->Begin(15, context, MakeSample(1, 1, 2)), "lost_begin");
    Expect(journal->Record(15, Action::Move, MakeSample(2, 2, 3)) == Admission::Recorded, "lost_prefix_move_admitted");
    Expect(journal->Record(15, Action::Lost, MakeSample(3, 3, 4)) == Admission::Recorded,
           "lost_explicit_record_admitted");
    Expect(journal->Closed(), "lost_explicit_record_closes");
    Expect(journal->Record(15, Action::Move, MakeSample(4, 4, 5)) == Admission::Closed,
           "lost_future_record_refused_closed");
    const auto down = journal->Take(15);
    Expect(down.has_value() && down->action == Action::Down && down->sample.ordinal == 1,
           "lost_explicit_prefix_down_preserved");
    const auto move = journal->Take(15);
    Expect(move.has_value() && move->action == Action::Move && move->sample.ordinal == 2,
           "lost_explicit_prefix_move_preserved");
    const auto lost = journal->Peek(15);
    Expect(lost.has_value() && lost->action == Action::Lost && !lost->emergency && lost->sample.ordinal == 3,
           "lost_explicit_terminal_is_normal_record");
    Expect(!journal->Finish(15), "lost_explicit_finish_refused_while_terminal_unread");
    Expect(journal->Take(15).has_value(), "lost_explicit_terminal_consumed");
    Expect(!journal->Take(15).has_value(), "lost_explicit_terminal_consumed_once");
    Expect(journal->CloseInput(15) == false, "lost_explicit_already_closed");
    Expect(journal->Finish(15), "lost_explicit_finish_after_drain");

    Expect(journal->Begin(16, MakeContext(20, 41), MakeSample(20, 20, 20)), "wrap_begin");
    for (std::uint64_t ordinal = 21; ordinal <= 600; ++ordinal)
    {
        ExpectIndexed(journal->Record(16, Action::Move, MakeSample(ordinal, static_cast<double>(ordinal), 9)) ==
                          Admission::Recorded,
                      "wrap-admitted", ordinal);
        const auto consumed = journal->Take(16);
        ExpectIndexed(consumed.has_value() && consumed->sample.ordinal == ordinal - 1, "wrap-consumed", ordinal);
    }
    Expect(journal->PendingCount() == 1, "wrap_one_row_remains_after_repeated_wraps");
    const auto tail = journal->Peek(16);
    Expect(tail.has_value() && tail->sample.ordinal == 600 && tail->sample.point.x == 600,
           "wrap_tail_scalar_data_survives");
    Expect(journal->CloseInput(16), "wrap_close_input");
    Expect(journal->Take(16).has_value(), "wrap_drain_tail");
    Expect(journal->Finish(16), "wrap_finish_after_tail_drain");
}

void TestLifecycleReuseAndMaximumOrdinal() noexcept
{
    auto journal = std::make_unique<Journal>();
    auto context = MakeContext(2, 51);
    Expect(journal->Begin(30, context, MakeSample(2, 2, 3)), "lifecycle_begin");
    Expect(!journal->Finish(30), "lifecycle_finish_refused_while_open_even_with_pending");
    Expect(!journal->Finish(29), "lifecycle_finish_refused_for_stale_stream");
    Expect(!journal->Finish(0), "lifecycle_finish_refused_for_zero_stream");
    Expect(!journal->CloseInput(29), "lifecycle_stale_close_refused");
    Expect(journal->Record(30, Action::Move, MakeSample(3, 3, 4)) == Admission::Recorded,
           "lifecycle_unread_row_admitted");
    Expect(journal->CloseInput(30), "lifecycle_close_producer");
    Expect(!journal->Finish(30), "lifecycle_finish_refused_with_unread_rows");
    Expect(!journal->Begin(30, MakeContext(10, 52), MakeSample(10, 1, 1)), "lifecycle_begin_refused_while_busy");
    Expect(!journal->Begin(29, MakeContext(10, 52), MakeSample(10, 1, 1)), "lifecycle_begin_refused_lower_stream");
    const auto first = journal->Take(30);
    Expect(first.has_value() && first->sample.ordinal == 2, "lifecycle_drain_original_down");
    Expect(!journal->Finish(30), "lifecycle_finish_refused_with_one_unread_row");
    const auto second = journal->Take(30);
    Expect(second.has_value() && second->sample.ordinal == 3, "lifecycle_drain_remaining_row");
    Expect(journal->Finish(30), "lifecycle_finish_after_close_and_full_drain");
    Expect(journal->Record(30, Action::Move, MakeSample(4, 4, 4)) == Admission::Stale,
           "lifecycle_old_stream_stale_after_finish");
    Expect(!journal->Begin(30, MakeContext(11, 52), MakeSample(11, 1, 1)), "lifecycle_reused_stream_refused");
    Expect(!journal->Begin(0, MakeContext(11, 52), MakeSample(11, 1, 1)), "lifecycle_zero_stream_refused");
    Expect(journal->Begin(31, MakeContext(11, 52), MakeSample(11, 1, 1)), "lifecycle_larger_stream_reuse_succeeds");
    Expect(journal->Stream() == 31, "lifecycle_new_stream_identity");
    Expect(journal->PendingCount() == 1, "lifecycle_new_stream_starts_with_fresh_down");
    Expect(journal->CloseInput(31), "lifecycle_close_new_stream");
    Expect(journal->Take(31).has_value(), "lifecycle_drain_new_stream_down");
    Expect(journal->Finish(31), "lifecycle_finish_new_stream");

    const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    Expect(journal->Begin(max, MakeContext(12, 53), MakeSample(12, 1, 1)), "lifecycle_max_stream_begins");
    Expect(journal->CloseInput(max), "lifecycle_close_max_stream");
    Expect(journal->Take(max).has_value(), "lifecycle_drain_max_stream");
    Expect(journal->Finish(max), "lifecycle_finish_max_stream");
    Expect(!journal->Begin(1, MakeContext(13, 54), MakeSample(13, 1, 1)), "lifecycle_stream_wrap_candidate_refused");

    journal = std::make_unique<Journal>();
    context = MakeContext(max - 1, 61);
    Expect(journal->Begin(40, context, MakeSample(max - 1, 10, 11)), "max_begin_near_limit");
    Expect(journal->Record(40, Action::Move, MakeSample(max, 12, 13)) == Admission::Recorded,
           "max_last_ordinal_recorded");
    Expect(journal->Record(40, Action::Move, MakeSample(1, 14, 15)) == Admission::Invalid,
           "max_ordinal_wrap_candidate_refused");
    Expect(journal->PendingCount() == 2, "max_wrap_refusal_preserves_two_record_prefix");
    Expect(journal->SealLost(40) == Admission::Recorded, "max_seal_lost_uses_emergency_slot");
    Expect(journal->PendingCount() == 3, "max_emergency_loss_keeps_prefix");
    Expect(journal->Closed(), "max_seal_lost_closes_input");
    Expect(journal->SealLost(40) == Admission::Closed, "max_second_seal_cannot_replace_loss");
    Expect(journal->Down(40, context, MakeSample(max, 14, 15)) == Admission::Closed, "max_down_refused_after_seal");
    Expect(journal->Record(40, Action::Move, MakeSample(max, 14, 15)) == Admission::Closed,
           "max_record_refused_after_seal");
    Expect(!journal->Finish(40), "max_finish_refused_with_prefix_and_loss");
    const auto down = journal->Take(40);
    Expect(down.has_value() && down->sample.ordinal == max - 1, "max_prefix_down_exact");
    const auto last = journal->Take(40);
    Expect(last.has_value() && last->sample.ordinal == max, "max_prefix_last_exact");
    Expect(!journal->Finish(40), "max_finish_refused_until_emergency_loss_consumed");
    const auto lost = journal->Take(40);
    Expect(lost.has_value() && lost->action == Action::Lost && lost->emergency && lost->sample.ordinal == max,
           "max_emergency_loss_reuses_max_without_wrap");
    Expect(!journal->Take(40).has_value(), "max_emergency_loss_once");
    Expect(journal->CloseInput(40) == false, "max_seal_lost_closed_producer");
    Expect(journal->Finish(40), "max_finish_after_loss_drain");
}
void TestCapturedViewAndLegacyCloseRadius() noexcept
{
    // EditorCanvas.lassoMouseDown: hypot(pointer - projected first point) <= 8 view points.
    // Windows view points are DIPs, so DPI 144 uses a literal 12-physical-pixel radius.
    auto sample = MakeSample(1, 0, 0);
    sample.viewScale = 0.125;
    sample.dipScale = 1.5;
    sample.documentOrigin = {1048576.25, -2097152.5};
    const Vec2 first{12, 20};
    sample.viewPoint = {1048589.75, -2097150.0};
    Expect(Journal::NearFirst(sample, first) == std::optional{true}, "view_144_dpi_12_physical_pixel_boundary");
    sample.viewPoint.x = std::nextafter(sample.viewPoint.x, std::numeric_limits<double>::infinity());
    Expect(Journal::NearFirst(sample, first) == std::optional{false}, "view_one_ulp_outside_physical_boundary");
    sample.viewScale = 0.001;
    sample.dipScale = 2;
    sample.documentOrigin = {};
    sample.viewPoint = {16, 0};
    Expect(Journal::NearFirst(sample, {}) == std::optional{true}, "view_minimum_zoom_192_dpi_boundary");
    sample.viewPoint.x = 16.25;
    Expect(Journal::NearFirst(sample, {}) == std::optional{false}, "view_minimum_zoom_outside_radius");
    auto invalid = sample;
    invalid.dipScale = 0;
    Expect(!Journal::NearFirst(invalid, {}).has_value(), "view_invalid_snapshot_refused");
    Expect(!Journal::NearFirst(sample, {std::numeric_limits<double>::quiet_NaN(), 0}).has_value(),
           "view_invalid_first_point_refused");

    // Literal large-pan counterexample: legacy physical distance is exactly 8, while reconstructing
    // it from the rounded document point produces 8.075 and would incorrectly refuse closure.
    sample.viewScale = 0.3;
    sample.dipScale = 1;
    sample.documentOrigin = {1125899906842624.25, 0};
    sample.viewPoint = {1125899906842636.0, 0};
    sample.point = {(sample.viewPoint.x - sample.documentOrigin.x) / sample.viewScale, 0};
    const Vec2 largeFirst{12.25, 0};
    Expect(Journal::NearFirst(sample, largeFirst) == std::optional{true},
           "view_large_pan_preserves_legacy_exact_boundary");
    Expect(std::fabs((sample.point.x - largeFirst.x) * sample.viewScale) > 8,
           "view_document_reconstruction_is_not_equivalent");
    auto journal = std::make_unique<Journal>();
    Expect(journal->Begin(80, MakeContext(1), sample), "view_snapshot_recorded");
    sample.viewPoint = {};
    sample.documentOrigin = {};
    sample.dipScale = 2;
    const auto captured = journal->Peek(80);
    Expect(captured && Journal::NearFirst(captured->sample, largeFirst) == std::optional{true} &&
               captured->sample.viewPoint == Vec2{1125899906842636.0, 0} && captured->sample.dipScale == 1,
           "view_later_view_changes_cannot_reinterpret_recorded_click");
    Expect(journal->CloseInput(80) && journal->Take(80) && journal->Finish(80), "view_snapshot_lifetime_drained");
}

} // namespace

int RunHostPolygonalInputJournalTests()
{
    failures = 0;
    checks = 0;
    TestFifoAndContext();
    TestRejectedInputsPreservePrefix();
    TestCapacityAndEmergencyLoss();
    TestExplicitLostAndRingWrap();
    TestLifecycleReuseAndMaximumOrdinal();
    TestCapturedViewAndLegacyCloseRadius();
    std::printf("%s host-polygonal-journal.summary checks=%u failures=%d\n", failures == 0 ? "PASS" : "FAIL", checks,
                failures);
    return failures;
}
