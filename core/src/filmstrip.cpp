#include "mpp/filmstrip.h"

#include <stdexcept>

namespace mpp {

namespace {

constexpr Color kBand{40, 40, 44, 255};
constexpr Color kInk{220, 220, 200, 255};
constexpr char kMagic[4] = {'M', 'P', 'F', 'S'};
constexpr std::uint8_t kVersion = 1;

// A 3x5 font for timecode, one string of rows per character.
const char* glyph(char c) {
  switch (c) {
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001001001001";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case ':': return "000010000010000";
    case ';': return "000010000010100";
    case '-': return "000000111000000";
    default: return "000000000000000";
  }
}

void draw_text(Image& img, const std::string& text, int x, int y) {
  for (char c : text) {
    const char* rows = glyph(c);
    for (int r = 0; r < 5; ++r) {
      for (int col = 0; col < 3; ++col) {
        if (rows[r * 3 + col] == '1') img.fill({x + col * 2, y + r * 2, 2, 2}, kInk);
      }
    }
    x += 8;
  }
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::uint32_t get32(const std::vector<std::uint8_t>& in, std::size_t at) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in[at + i]) << (8 * i);
  return v;
}

std::vector<std::uint8_t> header(const FilmstripInfo& info) {
  std::vector<std::uint8_t> b(kMagic, kMagic + 4);
  b.push_back(kVersion);
  put32(b, static_cast<std::uint32_t>(info.width));
  put32(b, static_cast<std::uint32_t>(info.height));
  put32(b, static_cast<std::uint32_t>(info.count));
  put32(b, static_cast<std::uint32_t>(info.rate.num));
  put32(b, static_cast<std::uint32_t>(info.rate.den));
  b.push_back(info.rate.drop_frame ? 1 : 0);
  put32(b, static_cast<std::uint32_t>(info.first));
  std::uint8_t sum = 0;
  for (std::uint8_t v : b) sum = static_cast<std::uint8_t>(sum + v);
  b.push_back(sum);
  return b;
}

constexpr std::size_t kHeaderBytes = 4 + 1 + 4 * 5 + 1 + 4 + 1;

}  // namespace

Image make_filmstrip(const std::vector<Image>& frames, Rate rate, std::int64_t first_frame) {
  if (frames.empty()) throw std::invalid_argument("A filmstrip needs at least one frame");
  const int w = frames[0].width(), h = frames[0].height();
  const int pitch = h + kFilmstripBand;
  Image strip(w, pitch * static_cast<int>(frames.size()), kBand);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].width() != w || frames[i].height() != h) {
      throw std::invalid_argument("Filmstrip frames must all be the same size");
    }
    const int top = static_cast<int>(i) * pitch;
    strip.blit(frames[i], {0, top});
    draw_text(strip, format_timecode(first_frame + static_cast<std::int64_t>(i), rate), 4, top + h + 4);
  }
  // The settings, a byte a pixel, along the top of the first band.
  FilmstripInfo info{w, h, static_cast<int>(frames.size()), rate, first_frame};
  const std::vector<std::uint8_t> bytes = header(info);
  for (std::size_t k = 0; k < bytes.size(); ++k) {
    const int x = static_cast<int>(k % static_cast<std::size_t>(w));
    const int y = h + static_cast<int>(k / static_cast<std::size_t>(w));
    strip.set_pixel(x, y, {bytes[k], bytes[k], bytes[k], 255});
  }
  return strip;
}

std::optional<FilmstripInfo> read_filmstrip_info(const Image& strip) {
  const int w = strip.width();
  if (w <= 0) return std::nullopt;
  // The first band's top row is after the first frame, whose height is in
  // the header itself: try each row a band could start on.
  for (int y = 1; y + 1 < strip.height(); ++y) {
    if (strip.pixel(0, y).r != 'M' || strip.pixel(std::min(1, w - 1), y).r != (w > 1 ? 'P' : 'M')) continue;
    std::vector<std::uint8_t> b;
    for (std::size_t k = 0; k < kHeaderBytes; ++k) {
      const int x = static_cast<int>(k % static_cast<std::size_t>(w));
      const int row = y + static_cast<int>(k / static_cast<std::size_t>(w));
      if (row >= strip.height()) return std::nullopt;
      const Color c = strip.pixel(x, row);
      if (c.r != c.g || c.r != c.b) break;
      b.push_back(c.r);
    }
    if (b.size() != kHeaderBytes || b[0] != 'M' || b[1] != 'P' || b[2] != 'F' || b[3] != 'S' ||
        b[4] != kVersion) {
      continue;
    }
    std::uint8_t sum = 0;
    for (std::size_t k = 0; k + 1 < b.size(); ++k) sum = static_cast<std::uint8_t>(sum + b[k]);
    if (sum != b.back()) continue;
    FilmstripInfo info;
    info.width = static_cast<int>(get32(b, 5));
    info.height = static_cast<int>(get32(b, 9));
    info.count = static_cast<int>(get32(b, 13));
    info.rate = {static_cast<int>(get32(b, 17)), static_cast<int>(get32(b, 21)), b[25] != 0};
    info.first = get32(b, 26);
    if (info.width != w || info.height != y || info.count < 1 || info.rate.num <= 0 ||
        info.rate.den <= 0 || strip.height() != (info.height + kFilmstripBand) * info.count) {
      return std::nullopt;
    }
    return info;
  }
  return std::nullopt;
}

Image filmstrip_frame(const Image& strip, const FilmstripInfo& info, int index) {
  if (index < 0 || index >= info.count) throw std::out_of_range("No such filmstrip frame");
  return strip.cropped({0, index * (info.height + kFilmstripBand), info.width, info.height});
}

}  // namespace mpp
