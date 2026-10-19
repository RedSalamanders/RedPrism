#include "PendingDisplaySource.h"

#include "ColorEditWork.h"
#include "PreparedRasterPreview.h"
#include "SharedPublicationMemory.h"

bool DocumentSession::DescribePendingDisplaySource(PendingDisplaySource& output) const noexcept
{
    const auto authority = CaptureDocumentReadAuthority();
    if (!authority || _brush || HasPendingWarp() || _shapeDraft || _selectionMoveOrigin || _lassoDraft)
        return false;
    PendingDisplaySource result;
    result.authority = authority.value();
    if (_gradient || _pixelMove)
    {
        const auto* gradient = _gradient ? &_gradient.value() : nullptr;
        const auto* move = _pixelMove ? &_pixelMove.value() : nullptr;
        if (!gradient && move && move->proceduralOpening)
        {
            if (!move->deferred || !move->HasOffset() || move->openingGeneration != Generation() ||
                !move->proceduralMemory || !move->proceduralMemory->IsBound() || move->proceduralWidth < 1 ||
                move->proceduralHeight < 1 || !move->proceduralOwnershipBytes)
                return false;
            result.kind = PendingDisplaySource::Kind::PixelMove;
            result.opening = move->opening;
            result.publication = move->sequence;
            result.target = move->target;
            result.pixelOpening = &move->proceduralOpening;
            result.pixelMemory = &move->proceduralMemory;
            result.pixelOffset = move->offset;
            result.pixelDuplicate = move->duplicate;
            result.pixelWidth = move->proceduralWidth;
            result.pixelHeight = move->proceduralHeight;
            result.pixelOwnershipBytes = move->proceduralOwnershipBytes;
        }
        else
        {
            bool mask = false;
            if (gradient)
            {
                if (!gradient->deferred || !gradient->HasLine() || !gradient->raster || !gradient->publishedSequence ||
                    gradient->publishedGeneration != Generation())
                    return false;
                result.kind = PendingDisplaySource::Kind::Gradient;
                result.opening = gradient->opening;
                result.publication = gradient->publishedSequence;
                result.target = gradient->target;
                result.rasterPreview = &gradient->display;
                mask = gradient->mask;
            }
            else
            {
                if (!move || !move->deferred || !move->HasOffset() || !move->raster || !move->publishedSequence ||
                    move->openingGeneration != Generation() || move->publishedGeneration != Generation())
                    return false;
                result.kind = PendingDisplaySource::Kind::PixelMove;
                result.opening = move->opening;
                result.publication = move->publishedSequence;
                result.target = move->target;
                result.rasterPreview = &move->display;
            }
            const auto& preview = *result.rasterPreview;
            if (!preview || !preview->memory || !preview->memory->IsBound() || !preview->ownershipBytes ||
                preview->target != result.target || preview->mask != mask)
                return false;
        }
        result.document = &_document.value();
    }
    else if (_transformEdit)
    {
        if (!_transformEdit->deferred || !_displayCopy || _displayTransformOpening != _transformEdit->opening ||
            _displaySourceGeneration != Generation())
            return false;
        result.kind = PendingDisplaySource::Kind::Transform;
        result.opening = _transformEdit->opening;
        result.publication = _displayRevision;
        result.document = &_displayCopy.value();
    }
    else if (_colorDisplayJobsEnabled && _colorEdit && _colorEdit->deferred && _adjustmentEditingID)
    {
        const auto& edit = _colorEdit.value();
        if (!_adjustmentOriginal || !edit.preview || !edit.previewSequence || !edit.previewLookup)
            return false;
        result.kind = PendingDisplaySource::Kind::Adjustment;
        result.opening = edit.openingSequence;
        result.publication = edit.previewSequence;
        result.document = &_document.value();
        result.target = edit.layerID;
        result.lookup = &edit.previewLookup;
        result.original = &_adjustmentOriginal.value();
    }
    else if (_colorDisplayJobsEnabled && _colorEdit && _colorEdit->deferred)
    {
        // This guarded DisplayDocument branch can only borrow the completed CPU cache or committed root.
        // No fallback PreviewedLayer/clone/materialization is reachable from this descriptor.
        const auto* document = DisplayDocument();
        if (!document || document == &_document.value())
            return false;
        const auto& edit = _colorEdit.value();
        result.kind = PendingDisplaySource::Kind::Color;
        result.opening = edit.openingSequence;
        result.publication = _displayColorSequence;
        result.document = document;
        if (ColorLookupPreviewReady())
        {
            result.target = edit.layerID;
            result.adjustmentSource = &edit.previewSource;
            result.adjustmentCoverage = &edit.previewCoverage;
            result.lookup = &edit.previewLookup;
            result.previewScale = edit.previewScale;
        }
    }
    else
        return false;
    result.sourceIdentity = result.document->layers.Identity();
    output = result;
    return true;
}
