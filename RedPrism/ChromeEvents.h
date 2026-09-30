#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The chrome's vocabulary (defined in ChromeHost.h; opaque here so the Application can implement ChromeEvents without
// including the DxUi headers).
enum class ChromeEditPhase : std::uint8_t;
enum class TransformAction : std::uint8_t;
enum class SelectionAction : std::uint8_t;
enum class BrushAction : std::uint8_t;
enum class GradientAction : std::uint8_t;
enum class ShapeAction : std::uint8_t;
enum class CropAction : std::uint8_t;
enum class AdjustAction : std::uint8_t;
enum class SizeAction : std::uint8_t;
namespace DxUi
{
enum class TreeDropPlace : std::uint8_t;
}

// The color the color picker card edits (ChromeHost::OpenColorPicker): a palette well, an end of the Gradient Map in
// the adjustment panel, or the Canvas Size card's custom extension color.
enum class PickerTarget : std::uint8_t
{
    Foreground,
    Background,
    GradientShadows,
    GradientHighlights,
    SizeExtension,
};

// What the chrome reports to its host, on the UI thread, often from inside a DxUi callback (the host syncs the chrome
// on the next frame there rather than re-entering it). ChromeHost holds the reference it is constructed with, so
// every event has its receiver.
class ChromeEvents
{
  public:
    // A menu row, a rail tool, a palette hot zone or a Command List row.
    virtual void OnCommand(int command) noexcept = 0;
    // The layers panel: a row selected, a folder expanded or collapsed, a row double-clicked (the host renames it), a
    // row dragged (`parent` is the target's parent for Before and After and is ignored for Inside, where the target is
    // the folder; the document move is PlaceLayer).
    virtual void OnLayerSelected(Guid id) noexcept = 0;
    virtual void OnLayerExpanded(Guid id, bool expanded) noexcept = 0;
    virtual void OnLayerInvoked(Guid id) noexcept = 0;
    virtual void OnLayerReordered(Guid source, Guid target, std::optional<Guid> parent,
                                  DxUi::TreeDropPlace place) noexcept = 0;
    // The Rename Layer card closed: the committed name, nullopt on cancel.
    virtual void OnRename(std::optional<std::wstring_view> name) noexcept = 0;
    // The active layer's opacity in 0…1 from the layers panel's stepper: typing previews, Enter, focus loss and the
    // buttons commit, Escape cancels.
    virtual void OnLayerOpacity(double opacity, ChromeEditPhase phase) noexcept = 0;
    // The color picker on `target` (an opaque color): the working color previews, OK commits it, Cancel restores the
    // color the picker opened on.
    virtual void OnPickerColor(PickerTarget target, ChromeEditPhase phase, std::uint32_t argb) noexcept = 0;
    // The canvas hole changed from inside the chrome (a splitter drag or its keys): the host re-syncs the viewport.
    virtual void OnChromeLayout() noexcept = 0;
    // The tool headers, the adjustment panel and the size cards: an action with one value (a typed number, a chosen
    // index, 0 or 1 for a flag).
    virtual void OnTransformAction(TransformAction action, double value) noexcept = 0;
    virtual void OnSelectionAction(SelectionAction action, double value) noexcept = 0;
    virtual void OnBrushAction(BrushAction action, double value) noexcept = 0;
    virtual void OnGradientAction(GradientAction action, double value) noexcept = 0;
    virtual void OnShapeAction(ShapeAction action, double value) noexcept = 0;
    virtual void OnCropAction(CropAction action, double value) noexcept = 0;
    virtual void OnAdjustAction(AdjustAction action, double value) noexcept = 0;
    virtual void OnSizeAction(SizeAction action, double value) noexcept = 0;
    // The adjustment panel's drawn editors: the curve's points after an edit, a spectrum band handle at `degrees`.
    virtual void OnCurveChanged(std::vector<CurvePoint> points) noexcept = 0;
    virtual void OnBandChanged(int index, double degrees) noexcept = 0;

  protected:
    ChromeEvents() = default;
    ChromeEvents(const ChromeEvents&) = default;
    ChromeEvents& operator=(const ChromeEvents&) = default;
    ~ChromeEvents() = default;
};
