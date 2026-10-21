#include "mpp/rate.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace mpp {

namespace {

// Frame labels skipped at the start of a drop-frame minute: 2 at 29.97,
// 4 at 59.94.
int dropped_per_minute(const Rate& rate) { return rate.nominal() / 15; }

bool use_drop_frame(const Rate& rate) {
  return rate.drop_frame && rate.can_drop_frame();
}

}  // namespace

int Rate::nominal() const {
  return static_cast<int>(std::lround(fps()));
}

std::int64_t Rate::frame_at(double seconds) const {
  // The epsilon keeps exact frame starts (seconds(n)) from rounding down.
  return static_cast<std::int64_t>(std::floor(seconds * num / den + 1e-9));
}

std::string format_timecode(std::int64_t frames, Rate rate) {
  const bool negative = frames < 0;
  std::int64_t n = negative ? -frames : frames;
  const int base = rate.nominal();
  const bool drop = use_drop_frame(rate);
  if (drop) {
    const int dropped = dropped_per_minute(rate);
    const std::int64_t per_minute = base * 60 - dropped;
    const std::int64_t per_ten = base * 600 - dropped * 9;
    const std::int64_t tens = n / per_ten;
    const std::int64_t rest = n % per_ten;
    n += dropped * 9 * tens;
    if (rest > dropped) n += dropped * ((rest - dropped) / per_minute);
  }
  const std::int64_t ff = n % base;
  const std::int64_t total_seconds = n / base;
  char text[48];
  std::snprintf(text, sizeof(text), "%s%02lld:%02lld:%02lld%c%02lld",
                negative ? "-" : "",
                static_cast<long long>(total_seconds / 3600),
                static_cast<long long>(total_seconds / 60 % 60),
                static_cast<long long>(total_seconds % 60), drop ? ';' : ':',
                static_cast<long long>(ff));
  return text;
}

std::optional<std::int64_t> parse_timecode(const std::string& text, Rate rate) {
  std::vector<long long> fields;
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] < '0' || text[i] > '9') return std::nullopt;
    long long value = 0;
    std::size_t digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      value = value * 10 + (text[i] - '0');
      if (++digits > 6) return std::nullopt;
      ++i;
    }
    fields.push_back(value);
    if (i < text.size()) {
      if (text[i] != ':' && text[i] != ';' && text[i] != '.') return std::nullopt;
      if (++i == text.size()) return std::nullopt;  // trailing separator
    }
  }
  if (fields.empty() || fields.size() > 4) return std::nullopt;
  while (fields.size() < 4) fields.insert(fields.begin(), 0);
  const long long h = fields[0], m = fields[1], s = fields[2], f = fields[3];
  const int base = rate.nominal();
  if (m >= 60 || s >= 60 || f >= base) return std::nullopt;
  std::int64_t total = ((h * 60 + m) * 60 + s) * base + f;
  if (use_drop_frame(rate)) {
    const int dropped = dropped_per_minute(rate);
    if (s == 0 && f < dropped && m % 10 != 0) return std::nullopt;
    const std::int64_t minutes = h * 60 + m;
    total -= dropped * (minutes - minutes / 10);
  }
  return total;
}

std::string rate_name(Rate rate) {
  char text[32];
  if (rate.den == 1) {
    std::snprintf(text, sizeof(text), "%d fps", rate.num);
  } else if (rate.den == 1001) {
    std::snprintf(text, sizeof(text), "%.2f fps", rate.fps());
  } else {
    std::snprintf(text, sizeof(text), "%g fps", rate.fps());
  }
  std::string name = text;
  if (use_drop_frame(rate)) name += " DF";
  return name;
}

}  // namespace mpp
