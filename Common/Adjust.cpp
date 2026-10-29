#include "Adjust.h"
#include <cstddef>

#include "BrushStroke.h"
#include "ImageFilters.h"
#include "Kernels.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double Clamp(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::min(high, std::max(low, value)) : fallback;
}

[[nodiscard]] double Wrap360(double value) noexcept
{
    const double remainder = std::fmod(value, 360.0);
    return remainder < 0 ? remainder + 360.0 : remainder;
}

// A value in −1…1 for an integer lattice point, fixed by the point and the seed. Two uniform halves summed give a
// triangular spread, closer to film grain than flat noise.
// Uniform in [0, 1).
[[nodiscard]] std::array<double, 3> ToHsl(double red, double green, double blue) noexcept
{
    const double high = std::max({red, green, blue});
    const double low = std::min({red, green, blue});
    const double lightness = (high + low) / 2;
    const double delta = high - low;
    if (!(delta > 0))
    {
        return {0, 0, lightness};
    }
    const double saturation = delta / (1 - std::fabs(2 * lightness - 1));
    double hue = 0;
    if (high == red)
    {
        hue = (green - blue) / delta;
    }
    else if (high == green)
    {
        hue = (blue - red) / delta + 2;
    }
    else
    {
        hue = (red - green) / delta + 4;
    }
    hue *= 60;
    if (hue < 0)
    {
        hue += 360;
    }
    return {hue, std::min(1.0, saturation), lightness};
}

[[nodiscard]] std::array<double, 3> ToRgb(double hue, double saturation, double lightness) noexcept
{
    if (!(saturation > 0))
    {
        return {lightness, lightness, lightness};
    }
    const double chroma = (1 - std::fabs(2 * lightness - 1)) * saturation;
    const double sector = hue / 60;
    const double second = chroma * (1 - std::fabs(std::fmod(sector, 2.0) - 1));
    const double base = lightness - chroma / 2;
    double red = 0;
    double green = 0;
    double blue = 0;
    switch (static_cast<int>(sector))
    {
    case 0:
        red = chroma;
        green = second;
        break;
    case 1:
        red = second;
        green = chroma;
        break;
    case 2:
        green = chroma;
        blue = second;
        break;
    case 3:
        green = second;
        blue = chroma;
        break;
    case 4:
        red = second;
        blue = chroma;
        break;
    default:
        red = chroma;
        blue = second;
        break;
    }
    return {std::min(1.0, std::max(0.0, red + base)), std::min(1.0, std::max(0.0, green + base)),
            std::min(1.0, std::max(0.0, blue + base))};
}

// A mutable copy of an image's pixels; false when memory runs out.
[[nodiscard]] bool CopyPixels(const Raster& image, PixelBuffer& bytes, const CancelToken& cancellation) noexcept
{
    bytes = CopyRasterPixelsForWork(image, cancellation);
    return static_cast<bool>(bytes);
}

// PDF non-separable helpers on straight colors.
[[nodiscard]] double Lum(const std::array<double, 3>& c) noexcept
{
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
}

[[nodiscard]] std::array<double, 3> ClipColor(std::array<double, 3> c) noexcept
{
    const double l = Lum(c);
    const double n = std::min({c[0], c[1], c[2]});
    const double x = std::max({c[0], c[1], c[2]});
    if (n < 0)
    {
        for (double& channel : c)
        {
            channel = l + (channel - l) * l / (l - n);
        }
    }
    if (x > 1)
    {
        for (double& channel : c)
        {
            channel = l + (channel - l) * (1 - l) / (x - l);
        }
    }
    return c;
}

[[nodiscard]] std::array<double, 3> SetLum(std::array<double, 3> c, double l) noexcept
{
    const double d = l - Lum(c);
    for (double& channel : c)
    {
        channel += d;
    }
    return ClipColor(c);
}

[[nodiscard]] double Sat(const std::array<double, 3>& c) noexcept
{
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
}

[[nodiscard]] std::array<double, 3> SetSat(std::array<double, 3> c, double s) noexcept
{
    int maxIndex = 0;
    int minIndex = 0;
    for (int i = 1; i < 3; ++i)
    {
        if (c[static_cast<std::size_t>(i)] > c[static_cast<std::size_t>(maxIndex)])
        {
            maxIndex = i;
        }
        if (c[static_cast<std::size_t>(i)] < c[static_cast<std::size_t>(minIndex)])
        {
            minIndex = i;
        }
    }
    if (maxIndex == minIndex)
    {
        return {0, 0, 0};
    }
    const int midIndex = 3 - maxIndex - minIndex;
    const double cmax = c[static_cast<std::size_t>(maxIndex)];
    const double cmid = c[static_cast<std::size_t>(midIndex)];
    const double cmin = c[static_cast<std::size_t>(minIndex)];
    std::array<double, 3> result{0, 0, 0};
    if (cmax > cmin)
    {
        result[static_cast<std::size_t>(midIndex)] = (cmid - cmin) * s / (cmax - cmin);
        result[static_cast<std::size_t>(maxIndex)] = s;
    }
    return result;
}

[[nodiscard]] double Dodge(double backdrop, double source) noexcept
{
    if (source >= 0.999)
    {
        return 1.0;
    }
    if (backdrop <= 1e-5)
    {
        return 0.0;
    }
    return std::min(1.0, backdrop / (1.0 - source));
}

[[nodiscard]] double Burn(double backdrop, double source) noexcept
{
    if (source <= 1e-5)
    {
        return 0.0;
    }
    if (backdrop >= 0.999)
    {
        return 1.0;
    }
    return 1.0 - std::min(1.0, (1.0 - backdrop) / source);
}

// Bilinear premultiplied sample with transparent outside; `x`, `y` in pixel-center coordinates.
void SampleBilinear(const std::uint8_t* bgra, int width, int height, int stride, double x, double y,
                    std::array<double, 4>& out) noexcept
{
    out = {0, 0, 0, 0};
    const double fx0 = std::floor(x);
    const double fy0 = std::floor(y);
    const double fx = x - fx0;
    const double fy = y - fy0;
    const long x0 = static_cast<long>(fx0);
    const long y0 = static_cast<long>(fy0);
    for (int j = 0; j < 2; ++j)
    {
        const long row = y0 + j;
        if (row < 0 || row >= height)
        {
            continue;
        }
        const double wy = j ? fy : 1 - fy;
        if (wy == 0)
        {
            continue;
        }
        const std::uint8_t* line = bgra + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
        for (int i = 0; i < 2; ++i)
        {
            const long column = x0 + i;
            if (column < 0 || column >= width)
            {
                continue;
            }
            const double weight = wy * (i ? fx : 1 - fx);
            if (weight == 0)
            {
                continue;
            }
            const std::uint8_t* p = line + static_cast<std::size_t>(column) * 4u;
            for (int c = 0; c < 4; ++c)
            {
                out[static_cast<std::size_t>(c)] += weight * p[c];
            }
        }
    }
}

// The Image menu's color adjustments and the adjustment layer kinds they edit.
constexpr std::array<std::pair<FilterKind, AdjustmentKind>, 6> kImageAdjustments{{
    {FilterKind::HueSaturation, AdjustmentKind::Hsv},
    {FilterKind::Levels, AdjustmentKind::Levels},
    {FilterKind::Curves, AdjustmentKind::Curves},
    {FilterKind::Exposure, AdjustmentKind::Exposure},
    {FilterKind::GradientMap, AdjustmentKind::GradientMap},
    {FilterKind::Grain, AdjustmentKind::Grain},
}};
} // namespace

std::optional<AdjustmentKind> FilterAdjustmentKind(FilterKind kind) noexcept
{
    for (const auto& [filter, adjustment] : kImageAdjustments)
    {
        if (filter == kind)
        {
            return adjustment;
        }
    }
    return std::nullopt;
}

FilterKind AdjustmentFilterKind(AdjustmentKind kind) noexcept
{
    for (const auto& [filter, adjustment] : kImageAdjustments)
    {
        if (adjustment == kind)
        {
            return filter;
        }
    }
    return FilterKind::Curves;
}

const wchar_t* FilterKindName(FilterKind kind) noexcept
{
    switch (kind)
    {
    case FilterKind::GaussianBlur:
        return L"Gaussian Blur";
    case FilterKind::MotionBlur:
        return L"Motion Blur";
    case FilterKind::AddNoise:
        return L"Add Noise";
    case FilterKind::LensCorrection:
        return L"Lens Correction";
    case FilterKind::ContentAwareFill:
        return L"Content-Aware Fill";
    case FilterKind::Curves:
        return L"Curves";
    case FilterKind::Exposure:
        return L"Exposure";
    case FilterKind::GradientMap:
        return L"Gradient Map";
    case FilterKind::Grain:
        return L"Grain";
    case FilterKind::Levels:
        return L"Levels";
    case FilterKind::HueSaturation:
        return L"Hue/Saturation";
    }
    return L"";
}

FilterSettings FilterSettings::Normalized() const
{
    FilterSettings result = *this;
    result.radius = Clamp(radius, 0.1, 250, 1);
    result.angle = Clamp(angle, -90, 90, 0);
    result.distance = Clamp(distance, 1, 2000, 10);
    result.amount = Clamp(amount, 0.1, 400, 10);
    result.distortion = Clamp(distortion, -100, 100, 0);
    result.exposure.exposure = Clamp(exposure.exposure, -20, 20, 0);
    result.exposure.offset = Clamp(exposure.offset, -0.5, 0.5, 0);
    result.exposure.gamma = Clamp(exposure.gamma, 0.01, 9.99, 1);
    for (AdjustmentColor* color : {&result.gradientMap.shadows, &result.gradientMap.highlights})
    {
        color->red = Clamp(color->red, 0, 1, 0);
        color->green = Clamp(color->green, 0, 1, 0);
        color->blue = Clamp(color->blue, 0, 1, 0);
    }
    result.grain.amount = Clamp(grain.amount, 0, 100, 25);
    result.grain.size = Clamp(grain.size, 0.5, 20, 1.5);
    result.grain.roughness = Clamp(grain.roughness, 0, 100, 50);
    return result;
}

namespace Adjust
{
// ── Levels ────────────────────────────────────────────────────────────────

double LevelRangeApply(const LevelRange& range, double value) noexcept
{
    const LevelRange s = range.Normalized();
    const double input = std::min(1.0, std::max(0.0, (value * 255 - s.black) / (s.white - s.black)));
    return (s.outputBlack + std::pow(input, 1 / s.gamma) * (s.outputWhite - s.outputBlack)) / 255;
}

double LevelsApply(const LevelsSettings& settings, double value, LevelsChannel channel) noexcept
{
    return LevelRangeApply(settings.ranges[0],
                           LevelRangeApply(settings.ranges[static_cast<std::size_t>(channel)], value));
}

bool LevelsIsIdentity(const LevelsSettings& settings) noexcept
{
    for (const LevelRange& range : settings.ranges)
    {
        if (!(range.Normalized() == LevelRange{}))
        {
            return false;
        }
    }
    return true;
}

LevelRange& LevelsCurrent(LevelsSettings& settings) noexcept
{
    return settings.ranges[static_cast<std::size_t>(settings.channel)];
}

const LevelRange& LevelsCurrent(const LevelsSettings& settings) noexcept
{
    return settings.ranges[static_cast<std::size_t>(settings.channel)];
}

void SetLevelsCurrent(LevelsSettings& settings, const LevelRange& range) noexcept
{
    settings.ranges[static_cast<std::size_t>(settings.channel)] = range.Normalized();
}

double HistogramDisplayScale(const std::array<double, 256>& bins) noexcept
{
    double peak = 0;
    for (const double bin : bins)
    {
        if (std::isfinite(bin) && bin > 0)
        {
            peak = std::max(peak, bin);
        }
    }
    if (!(peak > 0))
    {
        return 0;
    }
    std::vector<double> interior;
    for (std::size_t index = 1; index + 1 < bins.size(); ++index)
    {
        if (std::isfinite(bins[index]) && bins[index] > 0)
        {
            interior.push_back(bins[index]);
        }
    }
    if (interior.empty())
    {
        return peak;
    }
    std::sort(interior.begin(), interior.end());
    const double typicalPeak = interior[static_cast<std::size_t>(static_cast<double>(interior.size() - 1) * 0.95)];
    return std::min(peak, typicalPeak * 4);
}

LevelsSettings AutoLevels(LevelsAuto mode, const LevelsHistogram& histogram) noexcept
{
    LevelsSettings result;
    const auto endpoints = [](const std::array<double, 256>& bins) -> std::optional<std::pair<double, double>>
    {
        double total = 0;
        for (const double bin : bins)
        {
            total += bin;
        }
        if (!(total > 0))
        {
            return std::nullopt;
        }
        double sum = 0;
        int low = 0;
        int high = 255;
        for (int i = 0; i < 256; ++i)
        {
            sum += bins[static_cast<std::size_t>(i)];
            if (sum > total * 0.001)
            {
                low = i;
                break;
            }
        }
        sum = 0;
        for (int i = 255; i >= 0; --i)
        {
            sum += bins[static_cast<std::size_t>(i)];
            if (sum > total * 0.001)
            {
                high = i;
                break;
            }
        }
        if (low < high)
        {
            return std::make_pair(static_cast<double>(low), static_cast<double>(high));
        }
        return std::nullopt;
    };
    if (mode == LevelsAuto::Contrast)
    {
        // A shared interval preserves channel relationships.
        std::optional<double> low;
        std::optional<double> high;
        for (std::size_t c = 1; c < 4; ++c)
        {
            const std::optional<std::pair<double, double>> limits = endpoints(histogram[c]);
            if (!limits.has_value())
            {
                continue;
            }
            low = low.has_value() ? std::min(low.value(), limits->first) : limits->first;
            high = high.has_value() ? std::max(high.value(), limits->second) : limits->second;
        }
        if (low.has_value() && high.has_value() && low.value() < high.value())
        {
            LevelRange range;
            range.black = low.value();
            range.white = high.value();
            result.ranges[0] = range;
        }
        return result;
    }
    for (std::size_t c = 1; c < 4; ++c)
    {
        const std::optional<std::pair<double, double>> limits = endpoints(histogram[c]);
        if (!limits.has_value())
        {
            continue;
        }
        LevelRange range;
        range.black = limits->first;
        range.white = limits->second;
        if (mode == LevelsAuto::Neutral)
        {
            double total = 0;
            double weighted = 0;
            for (std::size_t i = 0; i < 256; ++i)
            {
                total += histogram[c][i];
                weighted += LevelRangeApply(range, static_cast<double>(i) / 255) * histogram[c][i];
            }
            const double mean = weighted / total;
            if (mean > 0 && mean < 1)
            {
                range.gamma = std::min(9.99, std::max(0.1, std::log(mean) / std::log(0.5)));
            }
        }
        result.ranges[c] = range;
    }
    return result;
}

LevelsSettings SampledLevels(const LevelsSettings& settings, const std::array<double, 3>& rgb,
                             LevelsSample mode) noexcept
{
    LevelsSettings result = settings;
    result.ranges[0] = LevelRange{};
    for (std::size_t c = 1; c < 4; ++c)
    {
        LevelRange range = result.ranges[c];
        const double v = rgb[c - 1] * 255;
        switch (mode)
        {
        case LevelsSample::Black:
            range.black = std::min(range.white - 1, std::max(0.0, v));
            break;
        case LevelsSample::White:
            range.white = std::max(range.black + 1, std::min(255.0, v));
            break;
        case LevelsSample::Gray:
        {
            const double fraction = (v - range.black) / (range.white - range.black);
            if (!(fraction > 0 && fraction < 1))
            {
                continue;
            }
            range.gamma = std::log(fraction) / std::log(0.5);
            break;
        }
        }
        range.outputBlack = 0;
        range.outputWhite = 255;
        result.ranges[c] = range.Normalized();
    }
    return result;
}

// ── Lookup tables ─────────────────────────────────────────────────────────

ChannelTables LevelsTables(const LevelsSettings& settings) noexcept
{
    ChannelTables tables;
    const LevelsChannel channels[] = {LevelsChannel::Red, LevelsChannel::Green, LevelsChannel::Blue};
    for (std::size_t c = 0; c < 3; ++c)
    {
        for (std::size_t i = 0; i < 256; ++i)
        {
            tables.values[c * 256 + i] =
                static_cast<float>(LevelsApply(settings, static_cast<double>(i) / 255, channels[c]));
        }
    }
    return tables;
}

double CurveValue(const CurvesSettings& settings, double x, int channel) noexcept
{
    const std::vector<CurvePoint>& p = settings.channels[static_cast<std::size_t>(channel)];
    if (p.size() < 2)
    {
        return std::min(255.0, std::max(0.0, x));
    }
    std::size_t i = 0;
    for (std::size_t index = 0; index < p.size(); ++index)
    {
        if (p[index].x <= x)
        {
            i = index;
        }
    }
    i = std::min(p.size() - 2, i);
    // Only the two endpoint slopes of this interval are needed. Keep the same secant arithmetic without
    // allocating a vector inside this noexcept lookup (called 1536 times while building one RGB table).
    const auto delta = [&](std::size_t index) noexcept
    { return (p[index + 1].y - p[index].y) / (p[index + 1].x - p[index].x); };
    const auto slope = [&](std::size_t j) noexcept -> double
    {
        if (j == 0)
        {
            return delta(0);
        }
        if (j == p.size() - 1)
        {
            return delta(p.size() - 2);
        }
        const double previous = delta(j - 1);
        const double next = delta(j);
        if (previous * next <= 0)
        {
            return 0;
        }
        return 2 / (1 / previous + 1 / next);
    };
    const double h = p[i + 1].x - p[i].x;
    const double t = std::min(1.0, std::max(0.0, (x - p[i].x) / h));
    const double y = (2 * t * t * t - 3 * t * t + 1) * p[i].y + (t * t * t - 2 * t * t + t) * h * slope(i) +
                     (-2 * t * t * t + 3 * t * t) * p[i + 1].y + (t * t * t - t * t) * h * slope(i + 1);
    return std::min(255.0, std::max(0.0, y));
}

ChannelTables CurvesTables(const CurvesSettings& settings) noexcept
{
    ChannelTables tables;
    for (int channel = 1; channel <= 3; ++channel)
    {
        for (std::size_t i = 0; i < 256; ++i)
        {
            tables.values[static_cast<std::size_t>(channel - 1) * 256 + i] = static_cast<float>(
                CurveValue(settings, CurveValue(settings, static_cast<double>(i), channel), 0) / 255);
        }
    }
    return tables;
}

std::array<float, 256> ExposureTable(const ExposureSettings& settings) noexcept
{
    std::array<float, 256> table{};
    const double scale = std::pow(2.0, settings.exposure);
    for (std::size_t index = 0; index < 256; ++index)
    {
        const double encoded = static_cast<double>(index) / 255;
        double linear = encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
        linear = std::pow(std::max(0.0, linear * scale + settings.offset), 1 / settings.gamma);
        const double output = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
        table[index] = static_cast<float>(std::min(1.0, std::max(0.0, output)));
    }
    return table;
}

ChannelTables ExposureTables(const ExposureSettings& settings) noexcept
{
    ChannelTables tables;
    const std::array<float, 256> table = ExposureTable(settings);
    for (std::size_t c = 0; c < 3; ++c)
    {
        std::copy(table.begin(), table.end(), tables.values.begin() + static_cast<std::ptrdiff_t>(c * 256));
    }
    return tables;
}

std::array<std::uint8_t, 768> GradientMapTable(const GradientMapSettings& settings) noexcept
{
    const AdjustmentColor dark = settings.reversed ? settings.highlights : settings.shadows;
    const AdjustmentColor light = settings.reversed ? settings.shadows : settings.highlights;
    std::array<std::uint8_t, 768> table{};
    for (std::size_t index = 0; index < 256; ++index)
    {
        const double t = static_cast<double>(index) / 255;
        const double channels[3] = {dark.red + (light.red - dark.red) * t, dark.green + (light.green - dark.green) * t,
                                    dark.blue + (light.blue - dark.blue) * t};
        for (std::size_t k = 0; k < 3; ++k)
        {
            table[index * 3 + k] =
                static_cast<std::uint8_t>(std::min(255.0, std::max(0.0, std::round(channels[k] * 255))));
        }
    }
    return table;
}

// ── Kernels ───────────────────────────────────────────────────────────────

void ApplyTables(std::uint8_t* bgra, int width, int height, int stride, const ChannelTables& tables,
                 const CancelToken& cancellation) noexcept
{
    ActiveKernels().applyTables(bgra, width, height, stride, tables, cancellation);
}

void ApplyGradientMap(std::uint8_t* bgra, int width, int height, int stride, const std::array<std::uint8_t, 768>& table,
                      const CancelToken& cancellation) noexcept
{
    ActiveKernels().applyGradientMap(bgra, width, height, stride, table, cancellation);
}

void ApplyGrain(std::uint8_t* bgra, int width, int height, int stride, const GrainSettings& grain, double originX,
                double originY, double unitsPerPixel, const CancelToken& cancellation) noexcept
{
    ActiveKernels().applyGrain(bgra, width, height, stride, grain, originX, originY, unitsPerPixel, cancellation);
}

void AddNoise(std::uint8_t* bgra, int width, int height, int stride, float amount, bool gaussian, bool monochromatic,
              std::uint32_t seed, const CancelToken& cancellation) noexcept
{
    ActiveKernels().addNoise(bgra, width, height, stride, amount, gaussian, monochromatic, seed, cancellation);
}

void LensDistort(const std::uint8_t* source, std::uint8_t* destination, int width, int height, int stride, double k,
                 const CancelToken& cancellation) noexcept
{
    ActiveKernels().lensDistort(source, destination, width, height, stride, k, cancellation);
}

// ── Hue / Saturation ──────────────────────────────────────────────────────

double HueForward(double from, double to) noexcept
{
    const double delta = std::fmod(to - from, 360.0);
    return delta < 0 ? delta + 360 : delta;
}

double BandWeight(const HueBand& band, double hue) noexcept
{
    const double span = HueForward(band.falloffStart, band.falloffEnd);
    if (!(span > 0))
    {
        return 1; // Master covers everything.
    }
    const double position = HueForward(band.falloffStart, hue);
    if (position > span)
    {
        return 0;
    }
    const double rampIn = HueForward(band.falloffStart, band.rangeStart);
    const double plateauEnd = HueForward(band.falloffStart, band.rangeEnd);
    if (position < rampIn)
    {
        return rampIn > 0 ? position / rampIn : 1;
    }
    if (position <= plateauEnd)
    {
        return 1;
    }
    const double rampOut = span - plateauEnd;
    return rampOut > 0 ? (span - position) / rampOut : 1;
}

HueBand BandCentered(const HueBand& band, double hue) noexcept
{
    const double core = HueForward(band.rangeStart, band.rangeEnd);
    const double leading = HueForward(band.falloffStart, band.rangeStart);
    const double trailing = HueForward(band.rangeEnd, band.falloffEnd);
    const double start = Wrap360(hue - core / 2);
    return HueBand{Wrap360(start - leading), start, Wrap360(start + core), Wrap360(start + core + trailing)};
}

namespace
{
// Keeps all four handles in 0…360 and the band under a full circle.
void NormalizeBand(HueBand& band) noexcept
{
    band.falloffStart = Wrap360(band.falloffStart);
    band.rangeStart = Wrap360(band.rangeStart);
    band.rangeEnd = Wrap360(band.rangeEnd);
    band.falloffEnd = Wrap360(band.falloffEnd);
    if (HueForward(band.falloffStart, band.falloffEnd) > 350)
    {
        band.falloffEnd = Wrap360(band.falloffStart + 350);
    }
}
} // namespace

void BandInclude(HueBand& band, double hue) noexcept
{
    if (!(BandWeight(band, hue) < 1))
    {
        return;
    }
    const double shoulderIn = HueForward(band.falloffStart, band.rangeStart);
    const double shoulderOut = HueForward(band.rangeEnd, band.falloffEnd);
    const double beforeStart = HueForward(hue, band.rangeStart);
    const double afterEnd = HueForward(band.rangeEnd, hue);
    if (beforeStart <= afterEnd)
    {
        band.rangeStart = hue;
        band.falloffStart = hue - shoulderIn;
    }
    else
    {
        band.rangeEnd = hue;
        band.falloffEnd = hue + shoulderOut;
    }
    NormalizeBand(band);
}

void BandExclude(HueBand& band, double hue) noexcept
{
    if (!(BandWeight(band, hue) > 0))
    {
        return;
    }
    const double shoulderIn = HueForward(band.falloffStart, band.rangeStart);
    const double shoulderOut = HueForward(band.rangeEnd, band.falloffEnd);
    const double fromStart = HueForward(band.falloffStart, hue);
    const double toEnd = HueForward(hue, band.falloffEnd);
    if (fromStart <= toEnd)
    {
        band.falloffStart = hue + 1;
        band.rangeStart = hue + 1 + shoulderIn;
    }
    else
    {
        band.falloffEnd = hue - 1;
        band.rangeEnd = hue - 1 - shoulderOut;
    }
    NormalizeBand(band);
}

bool BandSetHandle(HueBand& band, int index, double degrees) noexcept
{
    HueBand updated = band;
    const double value = Wrap360(degrees);
    switch (index)
    {
    case 0:
        updated.falloffStart = value;
        break;
    case 1:
        updated.rangeStart = value;
        break;
    case 2:
        updated.rangeEnd = value;
        break;
    default:
        updated.falloffEnd = value;
        break;
    }
    const double span = HueForward(updated.falloffStart, updated.falloffEnd);
    const double toStart = HueForward(updated.falloffStart, updated.rangeStart);
    const double toEnd = HueForward(updated.falloffStart, updated.rangeEnd);
    if (!(span > 1) || span > 350 || toStart > toEnd || toEnd > span)
    {
        return false;
    }
    band = updated;
    return true;
}

HueBand DefaultBand(ColorRange range) noexcept
{
    return HueSaturationSettings::DefaultBands()[static_cast<std::size_t>(range)];
}

RangeAdjustment& RangeOf(HueSaturationSettings& settings) noexcept
{
    return settings.adjustments[static_cast<std::size_t>(settings.range)];
}

const RangeAdjustment& RangeOf(const HueSaturationSettings& settings) noexcept
{
    return settings.adjustments[static_cast<std::size_t>(settings.range)];
}

HueBand& BandOf(HueSaturationSettings& settings) noexcept
{
    return settings.bands[static_cast<std::size_t>(settings.range)];
}

const HueBand& BandOf(const HueSaturationSettings& settings) noexcept
{
    return settings.bands[static_cast<std::size_t>(settings.range)];
}

double RangeWeight(const HueSaturationSettings& settings, ColorRange range, double hue) noexcept
{
    if (range == ColorRange::Master)
    {
        return 1;
    }
    const double weight = BandWeight(settings.bands[static_cast<std::size_t>(range)], hue);
    return settings.invertRange && range == settings.range ? 1 - weight : weight;
}

bool HsvIsIdentity(const HueSaturationSettings& settings) noexcept
{
    if (settings.colorize)
    {
        return false;
    }
    for (const RangeAdjustment& adjustment : settings.adjustments)
    {
        if (!(adjustment == RangeAdjustment{}))
        {
            return false;
        }
    }
    return true;
}

HueSaturationSettings HsvSettings(double hue, double saturation, double lightness, bool colorize,
                                  ColorRange range) noexcept
{
    HueSaturationSettings settings;
    settings.range = range;
    settings.colorize = colorize;
    settings.adjustments[static_cast<std::size_t>(range)] = RangeAdjustment{hue, saturation, lightness};
    return settings;
}

HueSaturationSettings ColorizeStart() noexcept
{
    return HsvSettings(0, 25, 0, true);
}

std::array<HueResponse, 361> HueResponses(const HueSaturationSettings& settings) noexcept
{
    std::array<HueResponse, 361> response{};
    for (int degree = 0; degree <= 360; ++degree)
    {
        HueResponse& sum = response[static_cast<std::size_t>(degree)];
        for (std::size_t index = 0; index < settings.adjustments.size(); ++index)
        {
            const RangeAdjustment& adjustment = settings.adjustments[index];
            if (adjustment == RangeAdjustment{})
            {
                continue;
            }
            const double weight = RangeWeight(settings, static_cast<ColorRange>(index), static_cast<double>(degree));
            if (!(weight > 0))
            {
                continue;
            }
            sum.shift += adjustment.hue * weight;
            sum.saturation += adjustment.saturation * weight;
            sum.lightness += adjustment.lightness * weight;
        }
    }
    return response;
}

std::array<double, 3> AdjustColor(double red, double green, double blue, const HueSaturationSettings& settings,
                                  const std::array<HueResponse, 361>* responses) noexcept
{
    std::array<double, 3> hsl = ToHsl(red, green, blue);
    double hue = hsl[0];
    double saturation = hsl[1];
    double lightness = hsl[2];
    double lightnessAmount = 0;
    if (settings.colorize)
    {
        const RangeAdjustment& range = RangeOf(settings);
        hue = std::fmod(range.hue, 360.0);
        saturation = std::min(1.0, std::max(0.0, range.saturation / 100));
        lightnessAmount = range.lightness / 100;
    }
    else
    {
        // Every range contributes, weighted by how strongly it claims the original hue.
        std::array<HueResponse, 361> own{};
        if (responses == nullptr)
        {
            own = HueResponses(settings);
            responses = &own;
        }
        const std::size_t index =
            static_cast<std::size_t>(std::min(360, std::max(0, static_cast<int>(std::round(hue)))));
        const HueResponse& sampled = (*responses)[index];
        lightnessAmount = sampled.lightness / 100;
        hue = std::fmod(hue + sampled.shift, 360.0);
        if (hue < 0)
        {
            hue += 360;
        }
        // Multiplicative, so neutral grays stay neutral.
        saturation = std::min(1.0, std::max(0.0, saturation * (1 + sampled.saturation / 100)));
    }
    // Lightness pulls toward white above 0 and toward black below, reaching either at ±100.
    const double amount = std::min(1.0, std::max(-1.0, lightnessAmount));
    lightness = amount >= 0 ? lightness + (1 - lightness) * amount : lightness * (1 + amount);
    return ToRgb(hue, saturation, std::min(1.0, std::max(0.0, lightness)));
}

double ShiftedHue(double hue, const HueSaturationSettings& settings) noexcept
{
    double shift = 0;
    for (std::size_t index = 0; index < settings.adjustments.size(); ++index)
    {
        if (settings.adjustments[index].hue != 0)
        {
            shift += settings.adjustments[index].hue * RangeWeight(settings, static_cast<ColorRange>(index), hue);
        }
    }
    return Wrap360(hue + shift);
}

std::vector<float> ColorCube(const HueSaturationSettings& settings, const CancelToken& cancellation)
{
    if (cancellation.IsCancelled())
    {
        return {};
    }
    const std::array<HueResponse, 361> response = HueResponses(settings);
    std::vector<float> values(static_cast<std::size_t>(kCubeDimension) * kCubeDimension * kCubeDimension * 4, 0.0f);
    std::size_t index = 0;
    const double step = kCubeDimension - 1;
    for (int blue = 0; blue < kCubeDimension; ++blue)
    {
        for (int green = 0; green < kCubeDimension; ++green)
        {
            if (cancellation.IsCancelled())
            {
                return {};
            }
            for (int red = 0; red < kCubeDimension; ++red)
            {
                const std::array<double, 3> color =
                    AdjustColor(red / step, green / step, blue / step, settings, &response);
                values[index] = static_cast<float>(color[0]);
                values[index + 1] = static_cast<float>(color[1]);
                values[index + 2] = static_cast<float>(color[2]);
                values[index + 3] = 1.0f;
                index += 4;
            }
        }
    }
    return values;
}

void ApplyCube(std::uint8_t* bgra, int width, int height, int stride, const std::vector<float>& cube,
               const CancelToken& cancellation) noexcept
{
    ActiveKernels().applyCube(bgra, width, height, stride, cube, cancellation);
}

// ── Straight-color blends ─────────────────────────────────────────────────

std::array<double, 3> BlendStraight(const std::array<double, 3>& backdrop, const std::array<double, 3>& source,
                                    LayerBlendMode mode) noexcept
{
    std::array<double, 3> result = source;
    switch (mode)
    {
    case LayerBlendMode::Normal:
        break;
    case LayerBlendMode::Multiply:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = backdrop[i] * source[i];
        }
        break;
    case LayerBlendMode::Screen:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = 1 - (1 - backdrop[i]) * (1 - source[i]);
        }
        break;
    case LayerBlendMode::Overlay:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = backdrop[i] <= 0.5 ? 2 * backdrop[i] * source[i] : 1 - 2 * (1 - backdrop[i]) * (1 - source[i]);
        }
        break;
    case LayerBlendMode::Darken:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = std::min(backdrop[i], source[i]);
        }
        break;
    case LayerBlendMode::Lighten:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = std::max(backdrop[i], source[i]);
        }
        break;
    case LayerBlendMode::Difference:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = std::fabs(backdrop[i] - source[i]);
        }
        break;
    case LayerBlendMode::ColorDodge:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = Dodge(backdrop[i], source[i]);
        }
        break;
    case LayerBlendMode::ColorBurn:
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = Burn(backdrop[i], source[i]);
        }
        break;
    case LayerBlendMode::Hue:
        result = SetLum(SetSat(source, Sat(backdrop)), Lum(backdrop));
        break;
    case LayerBlendMode::Saturation:
        result = SetLum(SetSat(backdrop, Sat(source)), Lum(backdrop));
        break;
    case LayerBlendMode::Color:
        result = SetLum(source, Lum(backdrop));
        break;
    case LayerBlendMode::Luminosity:
        result = SetLum(backdrop, Lum(source));
        break;
    }
    for (double& channel : result)
    {
        channel = std::min(1.0, std::max(0.0, channel));
    }
    return result;
}

// ── Whole rasters ─────────────────────────────────────────────────────────

Raster ApplyAdjustment(const LayerAdjustment& adjustment, const Raster& image, double originX, double originY,
                       double unitsPerPixel, const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || !adjustment.IsValid() || !image.HasStorage() ||
        image.Format() != RasterFormat::Bgra8888)
    {
        return {};
    }
    const int width = image.Width();
    const int height = image.Height();
    PixelBuffer bytes;
    switch (adjustment.kind)
    {
    case AdjustmentKind::Hsv:
    {
        const HueSaturationSettings settings = adjustment.ResolvedHsv();
        if (HsvIsIdentity(settings))
        {
            return image;
        }
        const std::vector<float> cube = ColorCube(settings, cancellation);
        if (cube.empty())
        {
            return {};
        }
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyCube(bytes.Data(), width, height, width * 4, cube, cancellation);
        break;
    }
    case AdjustmentKind::Levels:
        if (LevelsIsIdentity(adjustment.levels))
        {
            return image;
        }
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyTables(bytes.Data(), width, height, width * 4, LevelsTables(adjustment.levels), cancellation);
        break;
    case AdjustmentKind::Curves:
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyTables(bytes.Data(), width, height, width * 4, CurvesTables(adjustment.curves), cancellation);
        break;
    case AdjustmentKind::Exposure:
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyTables(bytes.Data(), width, height, width * 4, ExposureTables(adjustment.Exposure()), cancellation);
        break;
    case AdjustmentKind::GradientMap:
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyGradientMap(bytes.Data(), width, height, width * 4, GradientMapTable(adjustment.GradientMap()),
                         cancellation);
        break;
    case AdjustmentKind::Grain:
    {
        const GrainSettings grain = adjustment.Grain();
        if (!(grain.amount > 0) || !std::isfinite(unitsPerPixel) || !(unitsPerPixel > 0))
        {
            return image;
        }
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        ApplyGrain(bytes.Data(), width, height, width * 4, grain, originX, originY, unitsPerPixel, cancellation);
        break;
    }
    }
    return cancellation.IsCancelled() ? Raster{}
                                      : Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(bytes));
}

FilterSettings AdjustmentSettings(const LayerAdjustment& adjustment)
{
    FilterSettings settings;
    settings.curves = adjustment.curves;
    settings.exposure = adjustment.Exposure();
    settings.gradientMap = adjustment.GradientMap();
    settings.grain = adjustment.Grain();
    settings.levels = adjustment.levels;
    settings.hsv = adjustment.ResolvedHsv();
    return settings;
}

LayerAdjustment AdjustmentWithSettings(LayerAdjustment adjustment, const FilterSettings& settings)
{
    switch (adjustment.kind)
    {
    case AdjustmentKind::Hsv:
        adjustment.hsvSettings = settings.hsv;
        break;
    case AdjustmentKind::Levels:
        adjustment.levels = settings.levels;
        break;
    case AdjustmentKind::Curves:
        adjustment.curves = settings.curves;
        break;
    case AdjustmentKind::Exposure:
        adjustment.exposureSettings = settings.exposure;
        break;
    case AdjustmentKind::GradientMap:
        adjustment.gradientMapSettings = settings.gradientMap;
        break;
    case AdjustmentKind::Grain:
        adjustment.grainSettings = settings.grain;
        break;
    }
    return adjustment;
}

std::shared_ptr<const AdjustmentLookup> BuildAdjustmentLookup(const LayerAdjustment& adjustment,
                                                              const CancelToken& cancellation)
{
    if (!adjustment.IsValid() || cancellation.IsCancelled())
        return {};
    auto lookup = std::make_shared<AdjustmentLookup>();
    lookup->adjustment = adjustment;
    if (adjustment.kind == AdjustmentKind::Hsv)
    {
        lookup->cube = ColorCube(adjustment.ResolvedHsv(), cancellation);
        if (lookup->cube.size() != static_cast<std::size_t>(kCubeDimension * kCubeDimension * kCubeDimension * 4))
            return {};
    }
    else if (adjustment.kind == AdjustmentKind::GradientMap)
    {
        const auto colors = GradientMapTable(adjustment.GradientMap());
        for (std::size_t i = 0; i < 256; ++i)
        {
            for (std::size_t c = 0; c < 3; ++c)
                lookup->table[i * 4 + c] = colors[i * 3 + c] / 255.0f;
            lookup->table[i * 4 + 3] = 1;
        }
    }
    else if (adjustment.kind != AdjustmentKind::Grain)
    {
        const ChannelTables tables = adjustment.kind == AdjustmentKind::Levels ? LevelsTables(adjustment.levels)
                                     : adjustment.kind == AdjustmentKind::Curves
                                         ? CurvesTables(adjustment.curves)
                                         : ExposureTables(adjustment.Exposure());
        for (std::size_t i = 0; i < 256; ++i)
        {
            for (std::size_t c = 0; c < 3; ++c)
                lookup->table[i * 4 + c] = tables.values[c * 256 + i];
            lookup->table[i * 4 + 3] = 1;
        }
    }
    return cancellation.IsCancelled() ? std::shared_ptr<const AdjustmentLookup>{} : std::move(lookup);
}

LevelsHistogram Histogram(const Raster& image, const Raster* coverage, const CancelToken& cancellation) noexcept
{
    LevelsHistogram bins{};
    if (cancellation.IsCancelled() || !image.HasStorage() || image.Format() != RasterFormat::Bgra8888)
    {
        return {};
    }
    const PixelBuffer privatePixels =
        image.IsMaterialized() ? PixelBuffer{} : CopyRasterPixelsForWork(image, cancellation);
    const std::uint8_t* pixels = image.IsMaterialized() ? image.Data() : privatePixels.Data();
    if (pixels == nullptr)
    {
        return bins;
    }
    const std::uint8_t* mask = nullptr;
    int maskStride = 0;
    PixelBuffer privateMask;
    if (coverage != nullptr)
    {
        if (coverage->Format() != RasterFormat::Gray8)
        {
            return {};
        }
        privateMask = coverage->IsMaterialized() ? PixelBuffer{} : CopyRasterPixelsForWork(*coverage, cancellation);
        mask = coverage->IsMaterialized() ? coverage->Data() : privateMask.Data();
        maskStride = coverage->Stride();
        if (mask == nullptr || coverage->Width() != image.Width() || coverage->Height() != image.Height())
        {
            return bins; // An empty selection counts nothing.
        }
    }
    for (int y = 0; y < image.Height(); ++y)
    {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride());
        for (int x = 0; x < image.Width(); ++x)
        {
            if ((x & 255) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4u;
            if (p[3] == 0)
            {
                continue;
            }
            const double weight =
                p[3] / 255.0 *
                (mask != nullptr ? mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(maskStride) +
                                        static_cast<std::size_t>(x)] /
                                       255.0
                                 : 1.0);
            for (int channel = 0; channel < 3; ++channel)
            {
                const int value = static_cast<int>(std::min(255.0, std::round(p[2 - channel] * 255.0 / p[3])));
                bins[static_cast<std::size_t>(channel + 1)][static_cast<std::size_t>(value)] += weight;
                bins[0][static_cast<std::size_t>(value)] += weight / 3.0;
            }
        }
    }
    return cancellation.IsCancelled() ? LevelsHistogram{} : bins;
}

Raster GaussianBlurred(const Raster& image, double sigma, const CancelToken& cancellation)
{
    return ImageFilters::GaussianBlur(image, sigma, cancellation);
}

Raster MotionBlurred(const Raster& image, double angleDegrees, double distance, const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || !std::isfinite(angleDegrees) || !std::isfinite(distance) || distance < 0)
    {
        return {};
    }
    const Raster prepared = ContiguousRasterForWork(image, cancellation);
    const std::uint8_t* source = prepared.Data();
    if (source == nullptr || image.Format() != RasterFormat::Bgra8888)
    {
        return {};
    }
    const int width = image.Width();
    const int height = image.Height();
    const int stride = image.Stride();
    // Photoshop smears evenly along the whole distance; the streak is sampled at most every pixel, up to 96 taps.
    const int samples = std::max(1, static_cast<int>(std::lround(std::min(96.0, distance))));
    if (samples == 1)
    {
        return image;
    }
    PixelBuffer bytes =
        PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    if (!bytes)
    {
        return {};
    }
    // Counterclockwise from horizontal on screen (y down): right is +x, up is −y.
    const double radians = angleDegrees * kPi / 180.0;
    const double dx = std::cos(radians);
    const double dy = -std::sin(radians);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if ((x & 7) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            std::array<double, 4> sum{};
            for (int i = 0; i < samples; ++i)
            {
                const double offset = (static_cast<double>(i) / (samples - 1) - 0.5) * distance;
                std::array<double, 4> sample{};
                SampleBilinear(source, width, height, stride, x + dx * offset, y + dy * offset, sample);
                for (std::size_t c = 0; c < 4; ++c)
                {
                    sum[c] += sample[c];
                }
            }
            std::uint8_t* out =
                bytes.Data() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t c = 0; c < 4; ++c)
            {
                out[c] = static_cast<std::uint8_t>(std::lround(std::min(255.0, sum[c] / samples)));
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{}
                                      : Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(bytes));
}

Raster RunFilter(FilterKind kind, const Raster& image, const FilterSettings& given, double scale, std::uint32_t seed,
                 const Raster* coverage, bool* noSource, const CancelToken& cancellation)
{
    if (noSource != nullptr)
    {
        *noSource = false;
    }
    if (cancellation.IsCancelled() || !image.HasStorage() || image.Format() != RasterFormat::Bgra8888 ||
        !std::isfinite(scale) || !(scale > 0))
    {
        return {};
    }
    FilterSettings settings = given.Normalized();
    if (const std::optional<AdjustmentKind> adjustment = FilterAdjustmentKind(kind); adjustment.has_value())
    {
        // Grain sits in layer pixels; the job's seed gives each application its own pattern.
        settings.grain.seed = seed;
        LayerAdjustment record;
        record.kind = adjustment.value();
        return ApplyAdjustment(AdjustmentWithSettings(std::move(record), settings), image, 0.0, 0.0, 1.0 / scale,
                               cancellation);
    }
    const int width = image.Width();
    const int height = image.Height();
    PixelBuffer bytes;
    switch (kind)
    {
    case FilterKind::Curves:
    case FilterKind::Exposure:
    case FilterKind::GradientMap:
    case FilterKind::Grain:
    case FilterKind::Levels:
    case FilterKind::HueSaturation:
        return {}; // returned above
    case FilterKind::GaussianBlur:
        return GaussianBlurred(image, settings.radius * scale, cancellation);
    case FilterKind::MotionBlur:
        return MotionBlurred(image, settings.angle, settings.distance * scale, cancellation);
    case FilterKind::AddNoise:
        if (!CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        AddNoise(bytes.Data(), width, height, width * 4, static_cast<float>(settings.amount), settings.gaussian,
                 settings.monochromatic, seed, cancellation);
        break;
    case FilterKind::ContentAwareFill:
    {
        if (coverage == nullptr || coverage->Format() != RasterFormat::Gray8 || coverage->Width() != width ||
            coverage->Height() != height || !CopyPixels(image, bytes, cancellation))
        {
            return {};
        }
        const Raster preparedMask = ContiguousRasterForWork(*coverage, cancellation);
        if (!preparedMask.HasStorage())
        {
            return {};
        }
        const int filled = ContentFill(bytes.Data(), width, height, width * 4, preparedMask.Data(),
                                       preparedMask.Stride(), cancellation);
        if (filled != 1)
        {
            if (noSource != nullptr)
            {
                *noSource = filled == 0;
            }
            return {};
        }
        break;
    }
    case FilterKind::LensCorrection:
    {
        PixelBuffer source;
        if (!CopyPixels(image, source, cancellation))
        {
            return {};
        }
        bytes = PixelBuffer::TryAllocate(source.Size());
        if (!bytes)
        {
            return {};
        }
        // The warp is relative to the image's own size, so a downscaled preview bends the same way.
        LensDistort(source.Data(), bytes.Data(), width, height, width * 4, settings.distortion / 100 * kLensStrength,
                    cancellation);
        break;
    }
    }
    return cancellation.IsCancelled() ? Raster{}
                                      : Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(bytes));
}

Raster BlendThrough(const Raster& adjusted, const Raster& original, const Raster& coverage,
                    const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || !adjusted.HasStorage() || !original.HasStorage() ||
        adjusted.Width() != original.Width() || adjusted.Height() != original.Height() || !coverage.HasStorage() ||
        coverage.Width() != adjusted.Width() || coverage.Height() != adjusted.Height() ||
        adjusted.Format() != RasterFormat::Bgra8888 || original.Format() != RasterFormat::Bgra8888 ||
        coverage.Format() != RasterFormat::Gray8)
    {
        return {};
    }
    PixelBuffer top;
    const Raster base = ContiguousRasterForWork(original, cancellation);
    const Raster preparedMask = ContiguousRasterForWork(coverage, cancellation);
    if (!base.HasStorage() || !preparedMask.HasStorage() || !CopyPixels(adjusted, top, cancellation))
    {
        return {};
    }
    const int width = adjusted.Width();
    const int height = adjusted.Height();
    const std::uint8_t* mask = preparedMask.Data();
    const std::uint8_t* originalBytes = base.Data();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if ((x & 255) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const double c = mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                                  static_cast<std::size_t>(x)] /
                             255.0;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                top[offset + k] =
                    static_cast<std::uint8_t>(std::lround(top[offset + k] * c + originalBytes[offset + k] * (1.0 - c)));
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(top));
}

Raster Downscaled(const Raster& image, int maxSide, const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || !image.HasStorage() || maxSide < 1)
    {
        return {};
    }
    const int width = image.Width();
    const int height = image.Height();
    const int largest = std::max(width, height);
    if (largest <= maxSide)
    {
        return image;
    }
    const double factor = static_cast<double>(maxSide) / largest;
    const int outWidth = std::max(1, static_cast<int>(std::lround(width * factor)));
    const int outHeight = std::max(1, static_cast<int>(std::lround(height * factor)));
    const int channels = image.Channels();
    PixelBuffer source;
    if (!CopyPixels(image, source, cancellation))
    {
        return {};
    }
    PixelBuffer out = PixelBuffer::TryAllocate(
        static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * static_cast<std::size_t>(channels));
    if (!out)
    {
        return {};
    }
    for (int y = 0; y < outHeight; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const int y0 = (y * height) / outHeight;
        const int y1 = std::max(y0 + 1, ((y + 1) * height) / outHeight);
        for (int x = 0; x < outWidth; ++x)
        {
            if ((x & 31) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const int x0 = (x * width) / outWidth;
            const int x1 = std::max(x0 + 1, ((x + 1) * width) / outWidth);
            for (int c = 0; c < channels; ++c)
            {
                double sum = 0;
                for (int sy = y0; sy < y1; ++sy)
                {
                    for (int sx = x0; sx < x1; ++sx)
                    {
                        if ((sx & 255) == 0 && cancellation.IsCancelled())
                        {
                            return {};
                        }
                        sum += source[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(sx)) *
                                          static_cast<std::size_t>(channels) +
                                      static_cast<std::size_t>(c)];
                    }
                }
                out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(x)) *
                        static_cast<std::size_t>(channels) +
                    static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(std::lround(sum / ((y1 - y0) * (x1 - x0))));
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(outWidth, outHeight, image.Format(), std::move(out));
}

Trimmed TrimmedToAlpha(const Raster& image, const LayerTransform& placed, const CancelToken& cancellation,
                       const std::shared_ptr<const void>& outputLifetime)
{
    const Raster prepared = ContiguousRasterForWork(image, cancellation);
    if (!prepared.HasStorage() || image.Format() != RasterFormat::Bgra8888)
    {
        return {};
    }
    Trimmed result{image, placed};
    const int width = image.Width();
    const int height = image.Height();
    int left = width;
    int right = 0;
    int top = height;
    int bottom = 0;
    const std::uint8_t* data = prepared.Data();
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const std::uint8_t* row = data + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride());
        int first = 0;
        while (first < width && row[static_cast<std::size_t>(first) * 4u + 3u] == 0)
        {
            if ((first & 255) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            ++first;
        }
        if (first == width)
        {
            continue;
        }
        int last = width;
        while (last > first && row[static_cast<std::size_t>(last - 1) * 4u + 3u] == 0)
        {
            if ((last & 255) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            --last;
        }
        left = std::min(left, first);
        right = std::max(right, last);
        top = std::min(top, y);
        bottom = y + 1;
    }
    if (cancellation.IsCancelled())
    {
        return {};
    }
    const GridRect crop{left, top, right - left, bottom - top};
    if (crop.Empty() || (crop.x == 0 && crop.y == 0 && crop.width == width && crop.height == height))
    {
        return result;
    }
    result.image = CropRasterForWork(prepared, crop, 0, cancellation, outputLifetime);
    if (!result.image.HasStorage())
    {
        return {};
    }
    result.transform = PlacementOfRect(placed, width, height, crop);
    result.cropX = crop.x;
    result.cropY = crop.y;
    return result;
}

double BlurMargin(FilterKind kind, const FilterSettings& settings) noexcept
{
    switch (kind)
    {
    case FilterKind::GaussianBlur:
        return settings.radius * 3 + 2;
    case FilterKind::MotionBlur:
        return settings.distance / 2 + 2;
    case ::FilterKind::AddNoise:
    case ::FilterKind::ContentAwareFill:
    case ::FilterKind::Curves:
    case ::FilterKind::Exposure:
    case ::FilterKind::GradientMap:
    case ::FilterKind::Grain:
    case ::FilterKind::HueSaturation:
    case ::FilterKind::LensCorrection:
    case ::FilterKind::Levels:
    default:
        return 0;
    }
}
} // namespace Adjust

namespace Adjust
{
namespace
{
[[nodiscard]] std::uint32_t NextRandom(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return state;
}

// The mean squared difference of the known pixels around p and the pixels around q, over every channel.
[[nodiscard]] double PatchMatch(const std::uint8_t* pixels, int stride, const std::uint8_t* known, int w, int h, int p,
                                int q, int radius) noexcept
{
    const int px = p % w;
    const int py = p / w;
    const int qx = q % w;
    const int qy = q / w;
    int count = 0;
    double sum = 0;
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int x = px + dx;
            const int y = py + dy;
            const int sx = qx + dx;
            const int sy = qy + dy;
            if (x < 0 || y < 0 || x >= w || y >= h || sx < 0 || sy < 0 || sx >= w || sy >= h ||
                !known[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)])
            {
                continue;
            }
            const std::uint8_t* a = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                                    static_cast<std::size_t>(x) * 4u;
            const std::uint8_t* b = pixels + static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                                    static_cast<std::size_t>(sx) * 4u;
            for (int c = 0; c < 4; ++c)
            {
                const int d = a[c] - b[c];
                sum += d * d;
            }
            ++count;
        }
    }
    return count ? sum / count : std::numeric_limits<double>::max();
}
} // namespace

namespace
{
int ContentFillUnchecked(std::uint8_t* pixels, int w, int h, int stride, const std::uint8_t* mask, int maskStride,
                         const CancelToken& cancellation)
{
    if (cancellation.IsCancelled())
    {
        return -2;
    }
    if (pixels == nullptr || mask == nullptr || w < 1 || h < 1 || w > kMaxCanvasSide || h > kMaxCanvasSide ||
        stride < w * 4 || maskStride < w)
    {
        return 0;
    }
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const auto known = std::make_unique_for_overwrite<std::uint8_t[]>(n);
    const auto target = std::make_unique_for_overwrite<std::uint8_t[]>(n);
    const auto valid = std::make_unique_for_overwrite<std::uint8_t[]>(n);
    const auto queued = std::make_unique_for_overwrite<std::uint8_t[]>(n);
    const auto donors = std::make_unique_for_overwrite<int[]>(n);
    const auto queue = std::make_unique_for_overwrite<int[]>(n);
    const auto chosen = std::make_unique_for_overwrite<int[]>(n);
    // known/target are filled on the first pass; queue/donors are read only below their written counts.
    for (std::size_t start = 0; start < n; start += 4096)
    {
        if (cancellation.IsCancelled())
        {
            return -2;
        }
        const std::size_t count = std::min(std::size_t{4096}, n - start);
        std::fill_n(valid.get() + start, count, std::uint8_t{0});
        std::fill_n(queued.get() + start, count, std::uint8_t{0});
        std::fill_n(chosen.get() + start, count, -1);
    }
    const int radius = (w >= 5 && h >= 5) ? 2 : 0;
    std::size_t missing = 0;
    std::size_t donorCount = 0;
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t scan = 0;
    // Selected pixels are filled. Unselected opaque pixels are the image to match and copy from; unselected
    // transparent ones are neither: nothing to match against, and left as they are.
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if ((x & 31) == 0 && cancellation.IsCancelled())
            {
                return -2;
            }
            const std::size_t p =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            target[p] = mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(maskStride) +
                             static_cast<std::size_t>(x)] != 0;
            known[p] = !target[p] && pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                                            static_cast<std::size_t>(x) * 4u + 3] == 255;
            if (target[p])
            {
                ++missing;
            }
        }
    }
    if (missing == 0)
    {
        return cancellation.IsCancelled() ? -2 : 1;
    }
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if ((x & 31) == 0 && cancellation.IsCancelled())
            {
                return -2;
            }
            const std::size_t p =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            if (!known[p])
            {
                continue;
            }
            bool ok = true;
            for (int dy = -radius; dy <= radius && ok; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    const int sx = x + dx;
                    const int sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= h ||
                        !known[static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(sx)])
                    {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok)
            {
                valid[p] = 1;
                donors[donorCount++] = static_cast<int>(p);
            }
        }
    }
    if (donorCount == 0)
    {
        return 0;
    }
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if ((x & 31) == 0 && cancellation.IsCancelled())
            {
                return -2;
            }
            const std::size_t p =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            if (target[p] &&
                ((x && known[p - 1]) || (x + 1 < w && known[p + 1]) || (y && known[p - static_cast<std::size_t>(w)]) ||
                 (y + 1 < h && known[p + static_cast<std::size_t>(w)])))
            {
                queue[tail++] = static_cast<int>(p);
                queued[p] = 1;
            }
        }
    }
    std::uint32_t seed = 0x6d2b79f5u;
    for (;;)
    {
        while (head < tail)
        {
            if (cancellation.IsCancelled())
            {
                return -2;
            }
            const int p = queue[head++];
            const int x = p % w;
            const int y = p / w;
            int best = -1;
            double score = std::numeric_limits<double>::max();
            const int neighbors[4] = {x ? p - 1 : -1, x + 1 < w ? p + 1 : -1, y ? p - w : -1, y + 1 < h ? p + w : -1};
            // Propagate coherent source offsets, then refine with a randomized patch search.
            for (int k = 0; k < 28; ++k)
            {
                int q = -1;
                if (k < 4)
                {
                    const int t = neighbors[k];
                    if (t >= 0)
                    {
                        q = (chosen[static_cast<std::size_t>(t)] >= 0 ? chosen[static_cast<std::size_t>(t)] : t) +
                            (p - t);
                    }
                }
                else
                {
                    q = donors[NextRandom(seed) % donorCount];
                }
                if (q < 0 || static_cast<std::size_t>(q) >= n || !valid[static_cast<std::size_t>(q)])
                {
                    continue;
                }
                const double s = PatchMatch(pixels, stride, known.get(), w, h, p, q, radius);
                if (best < 0 || s < score)
                {
                    score = s;
                    best = q;
                }
            }
            if (best < 0)
            {
                best = donors[0];
            }
            for (int r = 64; r >= 1; r /= 2)
            {
                const int qx =
                    best % w + static_cast<int>(NextRandom(seed) % static_cast<std::uint32_t>(2 * r + 1)) - r;
                const int qy =
                    best / w + static_cast<int>(NextRandom(seed) % static_cast<std::uint32_t>(2 * r + 1)) - r;
                if (qx < 0 || qy < 0 || qx >= w || qy >= h ||
                    !valid[static_cast<std::size_t>(qy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(qx)])
                {
                    continue;
                }
                const int q = qy * w + qx;
                const double s = PatchMatch(pixels, stride, known.get(), w, h, p, q, radius);
                if (s < score)
                {
                    score = s;
                    best = q;
                }
            }
            std::memcpy(pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                            static_cast<std::size_t>(x) * 4u,
                        pixels + static_cast<std::size_t>(best / w) * static_cast<std::size_t>(stride) +
                            static_cast<std::size_t>(best % w) * 4u,
                        4);
            known[static_cast<std::size_t>(p)] = 1;
            chosen[static_cast<std::size_t>(p)] = best;
            for (int k = 0; k < 4; ++k)
            {
                const int q = neighbors[k];
                if (q >= 0 && target[static_cast<std::size_t>(q)] && !known[static_cast<std::size_t>(q)] &&
                    !queued[static_cast<std::size_t>(q)])
                {
                    queued[static_cast<std::size_t>(q)] = 1;
                    queue[tail++] = q;
                }
            }
        }
        // A selected area that only transparency touches starts from the best random donor, then spreads.
        while (scan < n && (!target[scan] || known[scan]))
        {
            if ((scan & 255u) == 0 && cancellation.IsCancelled())
            {
                return -2;
            }
            ++scan;
        }
        if (scan >= n)
        {
            break;
        }
        queue[tail++] = static_cast<int>(scan);
        queued[scan] = 1;
    }
    return cancellation.IsCancelled() ? -2 : 1;
}
std::int32_t TryContentFill(std::uint8_t* pixels, int w, int h, int stride, const std::uint8_t* mask, int maskStride,
                            const CancelToken& cancellation, int& result) noexcept
{
    try
    {
        result = ContentFillUnchecked(pixels, w, h, stride, mask, maskStride, cancellation);
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<std::int32_t>(0x8007000eU); // E_OUTOFMEMORY; caller fails the complete operation.
    }
}
} // namespace

int ContentFill(std::uint8_t* pixels, int w, int h, int stride, const std::uint8_t* mask, int maskStride,
                const CancelToken& cancellation) noexcept
{
    int result = -1;
    return TryContentFill(pixels, w, h, stride, mask, maskStride, cancellation, result) < 0 ? -1 : result;
}
} // namespace Adjust
