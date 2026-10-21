#pragma once

#include "../../Common/Document.h"

#include <exception>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// Test construction only. The owned mutable records may describe intentionally invalid documents.
// Explicit sealing preserves those values; production validation remains under test, not in this helper.
// There is no conversion or mutable borrow from a published CanvasDocument.
struct DocumentFixture final
{
    Guid id;
    int width = 1;
    int height = 1;
    double resolution = 72;
    std::vector<ImageLayer> layers;
    std::optional<DocumentSelection> selection;

    [[nodiscard]] static DocumentFixture Copy(const CanvasDocument& source)
    {
        DocumentFixture result;
        result.id = source.id;
        result.width = source.width;
        result.height = source.height;
        result.resolution = source.resolution;
        result.layers.assign(source.layers.begin(), source.layers.end());
        result.selection = source.selection;
        return result;
    }

    [[nodiscard]] CanvasDocument Seal() const&
    {
        DocumentFixture copy = *this;
        return std::move(copy).Seal();
    }

    [[nodiscard]] CanvasDocument Seal() &&
    {
        const auto capacity = layers.capacity();
        auto builder = RecordRoot<ImageLayer>::Builder::Adopt(std::move(layers), capacity);
        if (!builder)
            std::terminate();
        auto root = builder.value().Seal();
        if (!root)
            std::terminate();
        CanvasDocument result;
        result.id = id;
        result.width = width;
        result.height = height;
        result.resolution = resolution;
        result.layers = std::move(root.value());
        result.selection = std::move(selection);
        return result;
    }
};

template <typename Edit> void EditLayerRecordsForTest(DocumentSession& session, Edit&& edit)
{
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            auto fixture = DocumentFixture::Copy(document);
            std::invoke(std::forward<Edit>(edit), fixture);
            document = std::move(fixture).Seal();
        });
}

template <typename Edit> void EditDocumentFixture(CanvasDocument& document, Edit&& edit)
{
    auto fixture = DocumentFixture::Copy(document);
    std::invoke(std::forward<Edit>(edit), fixture);
    document = std::move(fixture).Seal();
}
