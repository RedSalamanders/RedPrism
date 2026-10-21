#include "Application.h"

#include "ChromeHost.h"

#include <cstdint>
#include <cstdio>

bool Application::RunSamplerIntentHostSelfTest() noexcept
{
    if (_chrome == nullptr)
    {
        SelfTestFailure(L"The sampler picker intent self-test needs the chrome.\n");
        return false;
    }

    const std::uint32_t originalForeground = _session.ForegroundColor();
    const std::uint32_t originalBackground = _session.BackgroundColor();
    const Guid originalHistoryRevision = _session.history.CurrentRevision();
    const std::uint64_t originalGeneration = _session.Generation();
    const int originalUndoCount = _session.history.UndoCount();

    const auto sessionUnchanged = [this, originalForeground, originalBackground, originalHistoryRevision,
                                   originalGeneration, originalUndoCount]() noexcept
    {
        return _session.ForegroundColor() == originalForeground && _session.BackgroundColor() == originalBackground &&
               _session.history.CurrentRevision() == originalHistoryRevision &&
               _session.Generation() == originalGeneration && _session.history.UndoCount() == originalUndoCount;
    };
    const auto fail = [this, originalForeground, originalBackground](const wchar_t* message) noexcept
    {
        if (_chrome != nullptr && _chrome->ColorPickerOpen())
        {
            _chrome->CloseColorPicker(false);
        }
        if (_session.ForegroundColor() != originalForeground)
        {
            _session.SetForegroundColor(originalForeground);
        }
        if (_session.BackgroundColor() != originalBackground)
        {
            _session.SetBackgroundColor(originalBackground);
        }
        SelfTestFailure(message);
        return false;
    };
    const auto check = [&fail](bool condition, const wchar_t* message, const char* control) noexcept
    {
        if (!condition)
        {
            return fail(message);
        }
        std::printf("PASS %s\n", control);
        return true;
    };

    if (!check(!_chrome->ColorPickerOpen(), L"The sampler picker intent self-test began with an open picker.\n",
               "sampler-intent.picker.starts-closed"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Foreground, originalForeground);
    const auto foregroundIntent = _chrome->CapturePickerIntentAuthority();
    const auto foregroundOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(_chrome->ColorPickerOpen() && foregroundIntent.has_value() && foregroundOpening.has_value() &&
                   foregroundIntent.value().target == PickerTarget::Foreground &&
                   foregroundOpening.value().target == PickerTarget::Foreground,
               L"The foreground picker did not expose both intent and publication authority.\n",
               "sampler-intent.picker.foreground-authorities-captured"))
    {
        return false;
    }
    const auto originalForegroundIntent = foregroundIntent.value();
    const auto originalForegroundOpening = foregroundOpening.value();
    auto foregroundPublication = originalForegroundOpening;
    if (!check(_chrome->ApplySampledPickerColor(foregroundPublication, 0x00112233u) &&
                   foregroundPublication != originalForegroundOpening &&
                   _chrome->IsPickerIntentCurrent(originalForegroundIntent) && _chrome->PickerColor() == 0xFF112233u &&
                   sessionUnchanged(),
               L"An owned foreground sample did not renew publication authority while preserving manual intent.\n",
               "sampler-intent.picker.foreground-own-sample-renews-publication-keeps-intent"))
    {
        return false;
    }
    const auto firstForegroundPublication = foregroundPublication;
    if (!check(_chrome->ApplySampledPickerColor(foregroundPublication, 0x00445566u) &&
                   foregroundPublication != firstForegroundPublication &&
                   _chrome->IsPickerIntentCurrent(originalForegroundIntent) && _chrome->PickerColor() == 0xFF445566u &&
                   sessionUnchanged(),
               L"A second foreground sample did not renew the same caller authority.\n",
               "sampler-intent.picker.foreground-successive-own-samples-renew-full-authority"))
    {
        return false;
    }
    auto staleForegroundPublication = originalForegroundOpening;
    if (!check(!_chrome->ApplySampledPickerColor(staleForegroundPublication, 0xFF778899u) &&
                   staleForegroundPublication == originalForegroundOpening && _chrome->PickerColor() == 0xFF445566u &&
                   sessionUnchanged(),
               L"The original foreground publication authority was not stale after renewal.\n",
               "sampler-intent.picker.foreground-original-publication-refused"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalForeground && sessionUnchanged(),
               L"Foreground picker cancellation changed its opening color or the session palette.\n",
               "sampler-intent.picker.foreground-cancel-restores-opening-and-session"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Background, originalBackground);
    const auto backgroundIntent = _chrome->CapturePickerIntentAuthority();
    const auto backgroundOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(_chrome->ColorPickerOpen() && backgroundIntent.has_value() && backgroundOpening.has_value() &&
                   backgroundIntent.value().target == PickerTarget::Background &&
                   backgroundOpening.value().target == PickerTarget::Background,
               L"The background picker did not expose both intent and publication authority.\n",
               "sampler-intent.picker.background-authorities-captured"))
    {
        return false;
    }
    const auto originalBackgroundIntent = backgroundIntent.value();
    const auto originalBackgroundOpening = backgroundOpening.value();
    auto backgroundPublication = originalBackgroundOpening;
    if (!check(_chrome->ApplySampledPickerColor(backgroundPublication, 0x00778899u) &&
                   backgroundPublication != originalBackgroundOpening &&
                   _chrome->IsPickerIntentCurrent(originalBackgroundIntent) && _chrome->PickerColor() == 0xFF778899u &&
                   sessionUnchanged(),
               L"An owned background sample did not renew publication authority while preserving manual intent.\n",
               "sampler-intent.picker.background-own-sample-renews-publication-keeps-intent"))
    {
        return false;
    }
    const auto firstBackgroundPublication = backgroundPublication;
    if (!check(_chrome->ApplySampledPickerColor(backgroundPublication, 0x00AABBCCu) &&
                   backgroundPublication != firstBackgroundPublication &&
                   _chrome->IsPickerIntentCurrent(originalBackgroundIntent) && _chrome->PickerColor() == 0xFFAABBCCu &&
                   sessionUnchanged(),
               L"A second background sample did not renew the same caller authority.\n",
               "sampler-intent.picker.background-successive-own-samples-renew-full-authority"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalBackground && sessionUnchanged(),
               L"Background picker cancellation changed its opening color or the session palette.\n",
               "sampler-intent.picker.background-cancel-restores-opening-and-session"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Foreground, originalForeground);
    const auto equalIntent = _chrome->CapturePickerIntentAuthority();
    const auto equalOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(equalIntent.has_value() && equalOpening.has_value(),
               L"The equal-input picker authorities were unavailable.\n",
               "sampler-intent.picker.equal-input-authorities-captured"))
    {
        return false;
    }
    const auto equalManualBefore = equalIntent.value();
    auto equalPublicationBefore = equalOpening.value();
    _chrome->SampleColor(originalForeground);
    if (!check(!_chrome->IsPickerIntentCurrent(equalManualBefore) &&
                   !_chrome->ApplySampledPickerColor(equalPublicationBefore, 0xFF010203u) &&
                   equalPublicationBefore == equalOpening.value() && _chrome->PickerColor() == originalForeground &&
                   sessionUnchanged(),
               L"An equal public SampleColor input did not invalidate both picker authorities.\n",
               "sampler-intent.picker.equal-public-sample-invalidates-manual-and-publication"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalForeground && sessionUnchanged(),
               L"Cancelling equal picker input changed its opening color or the palette.\n",
               "sampler-intent.picker.equal-input-cancel-restores-opening"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Background, originalBackground);
    const auto abaIntent = _chrome->CapturePickerIntentAuthority();
    const auto abaOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(abaIntent.has_value() && abaOpening.has_value(), L"The A-B-A picker authorities were unavailable.\n",
               "sampler-intent.picker.aba-authorities-captured"))
    {
        return false;
    }
    const auto abaManualBefore = abaIntent.value();
    auto abaPublicationBefore = abaOpening.value();
    _chrome->SampleColor(0xFF102030u);
    _chrome->SampleColor(0xFF405060u);
    _chrome->SampleColor(0xFF102030u);
    if (!check(!_chrome->IsPickerIntentCurrent(abaManualBefore) &&
                   !_chrome->ApplySampledPickerColor(abaPublicationBefore, 0xFF708090u) &&
                   abaPublicationBefore == abaOpening.value() && _chrome->PickerColor() == 0xFF102030u &&
                   sessionUnchanged(),
               L"Picker A-B-A input rebound an older intent or publication.\n",
               "sampler-intent.picker.manual-a-b-a-invalidates-old-authorities"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalBackground && sessionUnchanged(),
               L"Cancelling picker A-B-A changed its opening color or the palette.\n",
               "sampler-intent.picker.aba-cancel-restores-opening"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Background, originalBackground);
    const auto reopenIntent = _chrome->CapturePickerIntentAuthority();
    auto reopenOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(reopenIntent.has_value() && reopenOpening.has_value() &&
                   _chrome->ApplySampledPickerColor(reopenOpening.value(), 0x0013579Bu),
               L"The picker could not publish a sample before cancellation and reopening.\n",
               "sampler-intent.picker.reopen-source-sample-published"))
    {
        return false;
    }
    const auto closedIntent = reopenIntent.value();
    const auto closedPublication = reopenOpening.value();
    _chrome->CloseColorPicker(false);
    _chrome->OpenColorPicker(PickerTarget::Background, originalBackground);
    auto closedPublicationCopy = closedPublication;
    if (!check(_chrome->ColorPickerOpen() && !_chrome->IsPickerIntentCurrent(closedIntent) &&
                   !_chrome->ApplySampledPickerColor(closedPublicationCopy, 0xFF2468ACu) &&
                   closedPublicationCopy == closedPublication && _chrome->PickerColor() == originalBackground &&
                   sessionUnchanged(),
               L"Equal-target picker reopening accepted an authority from the cancelled opening.\n",
               "sampler-intent.picker.cancel-and-equal-reopen-refuses-old-authorities"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalBackground && sessionUnchanged(),
               L"Cancelling the reopened picker changed its opening color or the palette.\n",
               "sampler-intent.picker.reopened-cancel-restores-opening"))
    {
        return false;
    }

    _chrome->OpenColorPicker(PickerTarget::Foreground, originalForeground);
    const auto hostIntent = _chrome->CapturePickerIntentAuthority();
    const auto hostOpening = _chrome->CapturePickerSampleAuthority();
    if (!check(hostIntent.has_value() && hostOpening.has_value(),
               L"The foreign-host picker authorities were unavailable.\n",
               "sampler-intent.picker.foreign-authorities-captured"))
    {
        return false;
    }
    const auto expectedHostIntent = hostIntent.value();
    const auto expectedHostOpening = hostOpening.value();
    auto foreignIntent = expectedHostIntent;
    foreignIntent.host = Guid::Generate();
    auto wrongTargetIntent = expectedHostIntent;
    wrongTargetIntent.target = PickerTarget::Background;
    auto foreignPublication = expectedHostOpening;
    foreignPublication.host = Guid::Generate();
    auto wrongTargetPublication = expectedHostOpening;
    wrongTargetPublication.target = PickerTarget::Background;
    const auto foreignPublicationBefore = foreignPublication;
    const auto wrongTargetPublicationBefore = wrongTargetPublication;
    if (!check(!_chrome->IsPickerIntentCurrent(foreignIntent) && !_chrome->IsPickerIntentCurrent(wrongTargetIntent) &&
                   !_chrome->ApplySampledPickerColor(foreignPublication, 0xFFABCDEFu) &&
                   !_chrome->ApplySampledPickerColor(wrongTargetPublication, 0xFF987654u) &&
                   foreignPublication == foreignPublicationBefore &&
                   wrongTargetPublication == wrongTargetPublicationBefore &&
                   _chrome->PickerColor() == originalForeground && sessionUnchanged(),
               L"Foreign-host or wrong-target picker intent/publication authority was accepted.\n",
               "sampler-intent.picker.foreign-host-and-wrong-target-refused"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == originalForeground && sessionUnchanged(),
               L"Cancelling the authority-target fixture changed the opening color or palette.\n",
               "sampler-intent.picker.foreign-target-cancel-restores-opening"))
    {
        return false;
    }

    constexpr std::uint32_t openingBaseline = 0xFF13579Bu;
    constexpr std::uint32_t ownPreview = 0xFF2468ACu;
    _chrome->OpenColorPicker(PickerTarget::Foreground, openingBaseline);
    const auto openingManual = _chrome->CapturePickerIntentAuthority();
    const auto openingPublication = _chrome->CapturePickerSampleAuthority();
    if (!check(openingManual.has_value() && openingPublication.has_value(),
               L"The picker opening authorities were unavailable.\n",
               "sampler-intent.picker.opening-authorities-captured"))
    {
        return false;
    }
    const auto originalOpeningIntent = openingManual.value();
    const auto originalOpeningPublication = openingPublication.value();
    auto openingPreviewPublication = originalOpeningPublication;
    if (!check(_chrome->ApplySampledPickerColor(openingPreviewPublication, ownPreview) &&
                   _chrome->IsPickerOpeningCurrent(originalOpeningIntent) &&
                   _chrome->IsPickerIntentCurrent(originalOpeningIntent) && _chrome->PickerColor() == ownPreview &&
                   sessionUnchanged(),
               L"An owned preview invalidated its opening or manual intent.\n",
               "sampler-intent.picker.own-preview-keeps-opening-and-manual-intent"))
    {
        return false;
    }
    _chrome->SampleColor(openingBaseline);
    const auto acceptedManual = _chrome->CapturePickerIntentAuthority();
    if (!check(!_chrome->IsPickerIntentCurrent(originalOpeningIntent) &&
                   _chrome->IsPickerOpeningCurrent(originalOpeningIntent) && acceptedManual.has_value() &&
                   acceptedManual.value().opening == originalOpeningIntent.opening &&
                   _chrome->IsPickerOpeningCurrent(acceptedManual.value()) &&
                   _chrome->IsPickerIntentCurrent(acceptedManual.value()) && sessionUnchanged(),
               L"Equal manual input did not renew intent within the still-current picker opening.\n",
               "sampler-intent.picker.equal-input-keeps-opening-and-new-intent-valid"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == openingBaseline && sessionUnchanged(),
               L"Cancel did not restore the sampled picker opening.\n",
               "sampler-intent.picker.opening-preview-cancel-restores-baseline"))
    {
        return false;
    }

    constexpr std::uint32_t reopenedBaseline = 0xFF369ACBu;
    _chrome->OpenColorPicker(PickerTarget::Background, reopenedBaseline);
    const auto priorOpening = _chrome->CapturePickerIntentAuthority();
    if (!check(priorOpening.has_value(), L"The prior picker opening authority was unavailable.\n",
               "sampler-intent.picker.reopen-original-opening-captured"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    _chrome->OpenColorPicker(PickerTarget::Background, reopenedBaseline);
    const auto currentOpening = _chrome->CapturePickerIntentAuthority();
    if (!check(currentOpening.has_value(), L"The reopened picker authority was unavailable.\n",
               "sampler-intent.picker.reopen-current-opening-captured"))
    {
        return false;
    }
    auto foreignOpening = currentOpening.value();
    foreignOpening.host = Guid::Generate();
    auto wrongTargetOpening = currentOpening.value();
    wrongTargetOpening.target = PickerTarget::Foreground;
    auto wrongOpeningSerial = currentOpening.value();
    ++wrongOpeningSerial.opening;
    if (!check(currentOpening.value().opening != priorOpening.value().opening &&
                   !_chrome->IsPickerOpeningCurrent(priorOpening.value()) &&
                   !_chrome->IsPickerIntentCurrent(priorOpening.value()) &&
                   _chrome->IsPickerOpeningCurrent(currentOpening.value()) &&
                   !_chrome->IsPickerOpeningCurrent(foreignOpening) &&
                   !_chrome->IsPickerOpeningCurrent(wrongTargetOpening) &&
                   !_chrome->IsPickerOpeningCurrent(wrongOpeningSerial) && _chrome->PickerColor() == reopenedBaseline &&
                   sessionUnchanged(),
               L"Equal target and color reopening accepted a prior or mutated picker opening.\n",
               "sampler-intent.picker.equal-reopen-refuses-prior-and-mutated-opening-authorities"))
    {
        return false;
    }
    _chrome->CloseColorPicker(false);
    if (!check(!_chrome->ColorPickerOpen() && _chrome->PickerColor() == reopenedBaseline && sessionUnchanged(),
               L"Cancel did not restore the equal-reopened picker baseline.\n",
               "sampler-intent.picker.equal-reopen-cancel-restores-baseline"))
    {
        return false;
    }

    return check(!_chrome->ColorPickerOpen() && sessionUnchanged(),
                 L"The picker intent self-test did not restore the closed picker and original palette.\n",
                 "sampler-intent.picker.finishes-closed-with-original-palette-and-history");
}
