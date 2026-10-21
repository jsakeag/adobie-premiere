#include "mpp/title.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
// Static so it doesn't clash with the copy inside NanoGUI's NanoVG.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mpp {

namespace {

struct Font {
  std::vector<unsigned char> data;
  stbtt_fontinfo info{};
  int ascent = 0, descent = 0, gap = 0;
};

Font load_font(const char* file) {
  Font f;
  const std::string path = std::string(MPP_FONT_DIR) + "/" + file;
  std::ifstream in(path, std::ios::binary);
  f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (f.data.empty() ||
      !stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0))) {
    throw std::runtime_error("Could not load the title font " + path);
  }
  stbtt_GetFontVMetrics(&f.info, &f.ascent, &f.descent, &f.gap);
  return f;
}

const Font& font(TitleFont which) {
  static const Font regular = load_font("Roboto-Regular.ttf");
  static const Font bold = load_font("Roboto-Bold.ttf");
  return which == TitleFont::Bold ? bold : regular;
}

// UTF-8 to code points, split into lines. Malformed bytes become '?'.
std::vector<std::u32string> decode_lines(const std::string& text) {
  std::vector<std::u32string> lines(1);
  for (std::size_t i = 0; i < text.size();) {
    const auto b = static_cast<unsigned char>(text[i]);
    char32_t c = '?';
    int extra = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : -1;
    if (extra >= 0 && i + extra < text.size()) {
      c = extra == 0 ? b : b & (0x3F >> extra);
      for (int k = 1; k <= extra; ++k) c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
      i += extra + 1;
    } else {
      ++i;
    }
    if (c == '\n') lines.emplace_back();
    else lines.back().push_back(c);
  }
  return lines;
}

struct Layout {
  const Font* font = nullptr;
  float scale = 0;
  double line_height = 0;
  std::vector<std::u32string> lines;
  std::vector<double> widths;
  Rect box;  // the whole block in the frame
};

Layout layout(const TitleText& t, int width, int height) {
  Layout l;
  l.font = &font(t.font);
  const double px = std::max(1.0, t.size * height);
  l.scale = stbtt_ScaleForPixelHeight(&l.font->info, static_cast<float>(px));
  l.line_height = (l.font->ascent - l.font->descent + l.font->gap) * l.scale;
  l.lines = decode_lines(t.text);
  double widest = 0;
  for (const std::u32string& line : l.lines) {
    double w = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
      int advance = 0, bearing = 0;
      stbtt_GetCodepointHMetrics(&l.font->info, static_cast<int>(line[i]), &advance, &bearing);
      w += advance * l.scale;
      if (i + 1 < line.size()) {
        w += stbtt_GetCodepointKernAdvance(&l.font->info, static_cast<int>(line[i]),
                                           static_cast<int>(line[i + 1])) * l.scale;
      }
    }
    l.widths.push_back(w);
    widest = std::max(widest, w);
  }
  const double block_h = l.line_height * l.lines.size();
  const double anchor = t.x * width;
  const double left = t.align == TextAlign::Left    ? anchor
                      : t.align == TextAlign::Right ? anchor - widest
                                                    : anchor - widest / 2;
  const double top = t.y * height - block_h / 2;
  l.box = {static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
           static_cast<int>(std::ceil(widest)), static_cast<int>(std::ceil(block_h))};
  return l;
}

// Draws `colour` over the image wherever `coverage` (0..1, one per pixel)
// says, shifted by (dx, dy).
void paint(Image& image, const std::vector<float>& coverage, Color colour, double opacity, int dx,
           int dy) {
  const int w = image.width(), h = image.height();
  std::uint8_t* p = image.data();
  for (int y = 0; y < h; ++y) {
    const int sy = y - dy;
    if (sy < 0 || sy >= h) continue;
    for (int x = 0; x < w; ++x) {
      const int sx = x - dx;
      if (sx < 0 || sx >= w) continue;
      const double a = coverage[static_cast<std::size_t>(sy) * w + sx] * opacity * colour.a / 255.0;
      if (a <= 0) continue;
      std::uint8_t* px = p + (static_cast<std::size_t>(y) * w + x) * 4;
      const double below = px[3] / 255.0;
      const double out = a + below * (1 - a);
      const std::uint8_t c[3] = {colour.r, colour.g, colour.b};
      for (int k = 0; k < 3; ++k) {
        px[k] = static_cast<std::uint8_t>(std::lround((c[k] * a + px[k] * below * (1 - a)) / out));
      }
      px[3] = static_cast<std::uint8_t>(std::lround(out * 255));
    }
  }
}

std::vector<float> text_coverage(const TitleText& t, int width, int height) {
  std::vector<float> cover(static_cast<std::size_t>(width) * height, 0.0f);
  const Layout l = layout(t, width, height);
  const stbtt_fontinfo* info = &l.font->info;
  std::vector<unsigned char> glyph;
  for (std::size_t n = 0; n < l.lines.size(); ++n) {
    const std::u32string& line = l.lines[n];
    const double block_w = l.box.width;
    double pen = l.box.x + (t.align == TextAlign::Left    ? 0.0
                           : t.align == TextAlign::Right ? block_w - l.widths[n]
                                                         : (block_w - l.widths[n]) / 2);
    const int baseline =
        static_cast<int>(std::lround(l.box.y + n * l.line_height + l.font->ascent * l.scale));
    for (std::size_t i = 0; i < line.size(); ++i) {
      const int c = static_cast<int>(line[i]);
      const float shift = static_cast<float>(pen - std::floor(pen));
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      stbtt_GetCodepointBitmapBoxSubpixel(info, c, l.scale, l.scale, shift, 0, &x0, &y0, &x1, &y1);
      const int gw = x1 - x0, gh = y1 - y0;
      if (gw > 0 && gh > 0) {
        glyph.assign(static_cast<std::size_t>(gw) * gh, 0);
        stbtt_MakeCodepointBitmapSubpixel(info, glyph.data(), gw, gh, gw, l.scale, l.scale, shift, 0, c);
        const int ox = static_cast<int>(std::floor(pen)) + x0, oy = baseline + y0;
        for (int gy = 0; gy < gh; ++gy) {
          const int y = oy + gy;
          if (y < 0 || y >= height) continue;
          for (int gx = 0; gx < gw; ++gx) {
            const int x = ox + gx;
            if (x < 0 || x >= width) continue;
            float& cell = cover[static_cast<std::size_t>(y) * width + x];
            cell = std::max(cell, glyph[static_cast<std::size_t>(gy) * gw + gx] / 255.0f);
          }
        }
      }
      int advance = 0, bearing = 0;
      stbtt_GetCodepointHMetrics(info, c, &advance, &bearing);
      pen += advance * l.scale;
      if (i + 1 < line.size()) {
        pen += stbtt_GetCodepointKernAdvance(info, c, static_cast<int>(line[i + 1])) * l.scale;
      }
    }
  }
  return cover;
}

// Anti-aliased coverage of a shape: a rectangle by its signed distance, an
// oval by an estimate of it.
std::vector<float> shape_coverage(const TitleShape& s, int width, int height) {
  std::vector<float> cover(static_cast<std::size_t>(width) * height, 0.0f);
  const double cx = s.x * width, cy = s.y * height;
  const double hw = std::max(0.5, s.width * width / 2), hh = std::max(0.5, s.height * height / 2);
  const double half_line = std::max(0.5, s.line * height / 2);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      double d;  // negative inside
      if (s.kind == ShapeKind::Rectangle) {
        const double ox = std::abs(dx) - hw, oy = std::abs(dy) - hh;
        d = std::hypot(std::max(ox, 0.0), std::max(oy, 0.0)) + std::min(std::max(ox, oy), 0.0);
      } else {
        const double k = std::hypot(dx / hw, dy / hh);
        const double g = std::hypot(dx / (hw * hw), dy / (hh * hh));
        d = g > 0 ? (k - 1) * k / g : -std::min(hw, hh);
      }
      const double c = s.filled ? 0.5 - d : half_line + 0.5 - std::abs(d);
      cover[static_cast<std::size_t>(y) * width + x] = static_cast<float>(std::clamp(c, 0.0, 1.0));
    }
  }
  return cover;
}

std::string signature(const TitleSpec& t) {
  std::string key;
  char buf[160];
  const auto colour = [](Color c) { return (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a; };
  std::snprintf(buf, sizeof(buf), "|bg%u", static_cast<unsigned>(colour(t.background)));
  key += buf;
  for (const TitleShape& s : t.shapes) {
    std::snprintf(buf, sizeof(buf), "|s%d %.17g %.17g %.17g %.17g %u %d %.17g", static_cast<int>(s.kind),
                  s.x, s.y, s.width, s.height, static_cast<unsigned>(colour(s.color)), s.filled, s.line);
    key += buf;
  }
  for (const TitleText& x : t.texts) {
    std::snprintf(buf, sizeof(buf), "|t%d %.17g %u %.17g %.17g %d %d ", static_cast<int>(x.font),
                  x.size, static_cast<unsigned>(colour(x.color)), x.x, x.y,
                  static_cast<int>(x.align), x.shadow);
    key += buf;
    key += std::to_string(x.text.size()) + ":" + x.text;
  }
  return key;
}

std::string cache_key(const TitleSpec& t, int width, int height) {
  return std::to_string(width) + "x" + std::to_string(height) + signature(t);
}

Image draw(const TitleSpec& t, int width, int height) {
  Image out(width, height, t.background);
  for (const TitleShape& s : t.shapes) paint(out, shape_coverage(s, width, height), s.color, 1.0, 0, 0);
  for (const TitleText& x : t.texts) {
    if (x.text.empty()) continue;
    const std::vector<float> cover = text_coverage(x, width, height);
    if (x.shadow) {
      const int offset = std::max(1, static_cast<int>(std::lround(x.size * height * 0.06)));
      paint(out, cover, kBlack, 0.6, offset, offset);
    }
    paint(out, cover, x.color, 1.0, 0, 0);
  }
  return out;
}

}  // namespace

Image render_title(const TitleSpec& title, int width, int height) {
  static std::mutex mutex;
  static std::map<std::string, std::shared_ptr<const Image>> cache;
  const std::string key = cache_key(title, width, height);
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
  }
  auto image = std::make_shared<const Image>(draw(title, width, height));
  std::lock_guard<std::mutex> lock(mutex);
  if (cache.size() >= 24) cache.clear();
  cache.emplace(key, image);
  return *image;
}

std::string title_signature(const TitleSpec& title) { return signature(title); }

Rect text_bounds(const TitleText& text, int width, int height) {
  return layout(text, width, height).box;
}

Rect shape_bounds(const TitleShape& s, int width, int height) {
  const double w = s.width * width, h = s.height * height;
  return {static_cast<int>(std::floor(s.x * width - w / 2)), static_cast<int>(std::floor(s.y * height - h / 2)),
          static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h))};
}

}  // namespace mpp
