#include "model.h"
#include <random>
using namespace tempos;
extern void check(bool, const char *);

void placementTests() {
  auto equal = [](const std::optional<Box> &a, const std::optional<Box> &b) {
    return bool(a) == bool(b) && (!a || (a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h));
  };
  check(!nearestPosition({100, 100}, {16, 16, 100, 100}, {}), "oversized drop rejected");
  check(!nearestPosition({100, 100}, {NAN, 16, 16, 16}, {}), "nonfinite drop rejected");
  check(!nearestPosition({100, 100}, {16, 16, 16, 16}, {{0, 0, 100, 100}}), "full screen rejects drop");
  check(gridStep == 8 && snap(20) == 24 && snap(27) == 24, "visible and placement grid uses eight DIP");
  check(extent(Size::Slim, 150).height == 112 && extent(Size::XL, 150).height == 1264,
        "fractional cell heights expand to complete grid cells");
  {
    Monitor screen{L"primary", L"", {0, 0, 1920, 1040}, 96, true};
    Widget first; first.size = Size::Slim; first.scale = 150; first.monitor = screen.id;
    first.x = 800; first.y = 204;
    check(validateWidget(first), "legacy Slim widget remains valid on the new grid");
    Widget second = first; second.id = L"legacy-neighbor"; second.y = 328;
    auto arranged = arrangeWidgets({first, second}, {screen});
    check(arranged[1].visible && arranged[1].box.x == 800 && arranged[1].box.y == 336,
          "legacy four-DIP Slim layout expands locally instead of jumping to the screen origin");
    Widget edge; edge.monitor = screen.id; edge.x = 1584; edge.y = 800;
    auto restored = arrangeWidgets({edge}, {screen});
    check(restored[0].visible && restored[0].box.x == 1568 && restored[0].box.y == 800,
          "restoring beyond a connected screen edge chooses the nearest legal grid position");
  }
  std::mt19937 random(0x54454d50);
  for (int trial = 0; trial < 2000; ++trial) {
    Extent area{float(64 + random() % 160), float(64 + random() % 160)};
    Box desired{float(int(random() % 320) - 48), float(int(random() % 320) - 48),
                float(8 + random() % 48), float(8 + random() % 48)};
    std::vector<Box> occupied;
    for (unsigned i = 0, count = random() % 8; i < count; ++i)
      occupied.push_back({float(random() % 200), float(random() % 200),
                          float(8 + random() % 48), float(8 + random() % 48)});
    // Independent exhaustive oracle: every legal grid point, including ties.
    std::optional<Box> expected;
    double distance = INFINITY;
    for (float y = 16; y + desired.h <= area.height - 16; y += 8)
      for (float x = 16; x + desired.w <= area.width - 16; x += 8) {
        Box b{x, y, desired.w, desired.h};
        if (std::any_of(occupied.begin(), occupied.end(), [&](auto &o) { return overlaps(b, o); })) continue;
        double dx = double(x) - desired.x, dy = double(y) - desired.y;
        if (dx * dx + dy * dy < distance) {
          expected = b; distance = dx * dx + dy * dy;
        }
      }
    auto actual = nearestPosition(area, desired, occupied);
    check(equal(actual, expected), "nearest drop matches exhaustive oracle");
    if (actual) check(equal(nearestPosition(area, *actual, occupied), actual), "placement is idempotent");
    std::optional<Box> first;
    for (float x = 16; !first && x + desired.w <= area.width - 16; x += 8)
      for (float y = 16; y + desired.h <= area.height - 16; y += 8) {
        Box b{x, y, desired.w, desired.h};
        if (std::none_of(occupied.begin(), occupied.end(), [&](auto &o) { return overlaps(b, o); })) {
          first = b; break;
        }
      }
    check(equal(freePosition(area, {desired.w, desired.h}, occupied), first),
          "automatic placement preserves exhaustive column-first result");
  }
  for (UINT dpi : {96u, 120u, 144u, 168u, 192u})
    for (int scale : {100, 150, 200})
      for (Size size : {Size::S, Size::Slim, Size::M, Size::L, Size::XL}) {
        auto s = extent(size, scale);
        check(std::fmod(s.width, 8) == 0 && std::fmod(s.height, 8) == 0,
              "every supported size and scale fills complete eight-DIP cells");
        Extent area{2560.f * 96 / dpi, 1440.f * 96 / dpi};
        for (float x : {-200.f, 16.f, area.width}) for (float y : {-200.f, 16.f, area.height}) {
          auto p = nearestPosition(area, {x, y, s.width, s.height}, {});
          bool fits = s.width + 32 <= area.width && s.height + 32 <= area.height;
          check(bool(p) == fits, "DPI size edge drop capacity");
          if (p) check(p->x >= 16 && p->y >= 16 && p->x + p->w <= area.width - 16 &&
                       p->y + p->h <= area.height - 16 && snap(p->x) == p->x && snap(p->y) == p->y,
                       "DPI size edge drop stays on legal grid");
        }
      }
  for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
    Monitor primary{L"primary", L"", {0, 0, 1920, 1040}, 96, true};
    Monitor secondary{L"secondary", L"", {-2560, -1440, 0, 0}, dpi, false};
    std::vector<Widget> widgets;
    for (int i = 0; i < 16; ++i) {
      Widget w; w.id = L"matrix-" + std::to_wstring(i); w.size = Size::S;
      w.monitor = secondary.id; w.x = 16.f + (i / 4) * 176; w.y = 16.f + (i % 4) * 176;
      widgets.push_back(w);
    }
    auto original = widgets;
    for (auto screens : {std::vector<Monitor>{primary, secondary}, std::vector<Monitor>{primary},
                         std::vector<Monitor>{secondary, primary}}) {
      auto placed = arrangeWidgets(widgets, screens);
      for (size_t i = 0; i < placed.size(); ++i) {
        const auto &p = placed[i];
        if (!p.visible) continue;
        auto monitor = std::find_if(screens.begin(), screens.end(), [&](const auto &m) { return m.id == p.monitor; });
        check(monitor != screens.end(), "monitor lifecycle placement targets connected screen");
        float width = (monitor->work.right - monitor->work.left) * 96.f / monitor->dpi;
        float height = (monitor->work.bottom - monitor->work.top) * 96.f / monitor->dpi;
        check(p.box.x >= 16 && p.box.y >= 16 && p.box.x + p.box.w <= width - 16 && p.box.y + p.box.h <= height - 16,
              "mixed DPI negative monitor origin stays inside work area");
        for (size_t j = 0; j < i; ++j)
          if (placed[j].visible && placed[j].monitor == p.monitor)
            check(!overlaps(p.box, placed[j].box), "monitor removal/reordering preserves collision-free placement");
      }
    }
    check(std::equal(widgets.begin(), widgets.end(), original.begin(), [](const auto &a, const auto &b) {
      return a.id == b.id && a.monitor == b.monitor && a.x == b.x && a.y == b.y;
    }), "temporary monitor relocation never mutates saved originals");
  }
}
