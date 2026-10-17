#pragma once
#include <cmath>
#include <cstdint>
namespace tempos::design {
inline constexpr float padding = 20;
inline constexpr float slimPadding = 17;
inline constexpr float radius = 24;
inline constexpr float graphStroke = 2;
inline constexpr float iconSize = 20;
inline constexpr float iconStroke = 1.7f;
inline constexpr float headerTextX = 48;
inline constexpr float headerY = 17;
inline constexpr float titleSize = 16;
inline constexpr float compactTitleSize = 14;
inline constexpr float smallTextContrast = 4.5f;
// Display emphasis, not manufacturer thermal limits or a hardware fault verdict.
enum class Metric { Temperature, Load, Capacity };
inline constexpr uint32_t metricWhite = 0xffffff, metricOrange = 0xffcf8a, metricRed = 0xffa0a0;
inline uint32_t metricColor(Metric metric, double value, bool fresh = true) {
  if (!fresh || !std::isfinite(value) || value < 0 ||
      value > (metric == Metric::Temperature ? 150. : 100.))
    return metricWhite;
  const double high = metric == Metric::Load ? 95. : 90.;
  return value >= high ? metricRed : value >= 80 ? metricOrange : metricWhite;
}
inline double whiteContrast(uint32_t rgb) {
  auto linear = [](int value) {
    double c = value / 255.;
    return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
  };
  return 1.05 / (.2126 * linear((rgb >> 16) & 255) + .7152 * linear((rgb >> 8) & 255) +
                 .0722 * linear(rgb & 255) + .05);
}
inline uint32_t readableSurface(uint32_t rgb) {
  for (double factor = 1; factor > 0; factor -= .02) {
    auto adjusted = (uint32_t(((rgb >> 16) & 255) * factor) << 16) |
                    (uint32_t(((rgb >> 8) & 255) * factor) << 8) | uint32_t((rgb & 255) * factor);
    if (whiteContrast(adjusted) >= smallTextContrast)
      return adjusted;
  }
  return 0;
}
} // namespace tempos::design
