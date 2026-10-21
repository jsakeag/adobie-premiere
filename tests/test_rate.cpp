#include <catch2/catch_test_macros.hpp>

#include "mpp/rate.h"

using mpp::format_timecode;
using mpp::parse_timecode;
using mpp::Rate;

namespace {

const Rate k30{30, 1, false};
const Rate k15{15, 1, false};
const Rate kNtsc{30000, 1001, false};
const Rate kNtscDrop{30000, 1001, true};

}  // namespace

TEST_CASE("non-drop timecode counts whole frames", "[timecode]") {
  CHECK(format_timecode(0, k30) == "00:00:00:00");
  CHECK(format_timecode(29, k30) == "00:00:00:29");
  CHECK(format_timecode(30, k30) == "00:00:01:00");
  CHECK(format_timecode(30 * 3600 + 30 * 61 + 5, k30) == "01:01:01:05");
  CHECK(format_timecode(16, k15) == "00:00:01:01");
  CHECK(format_timecode(-31, k30) == "-00:00:01:01");
  // 29.97 without drop-frame counts 30 labels a second, as tape does.
  CHECK(format_timecode(1800, kNtsc) == "00:01:00:00");
}

TEST_CASE("drop-frame skips labels 00 and 01 each minute but every tenth", "[timecode]") {
  CHECK(format_timecode(1799, kNtscDrop) == "00:00:59;29");
  CHECK(format_timecode(1800, kNtscDrop) == "00:01:00;02");
  CHECK(format_timecode(17981, kNtscDrop) == "00:09:59;29");
  CHECK(format_timecode(17982, kNtscDrop) == "00:10:00;00");
  CHECK(format_timecode(17984, kNtscDrop) == "00:10:00;02");
  // One hour of real time at 29.97 is 107892 frames and reads as one hour.
  CHECK(format_timecode(107892, kNtscDrop) == "01:00:00;00");
  // Drop-frame only applies to rates that need it.
  CHECK(format_timecode(1800, Rate{30, 1, true}) == "00:01:00:00");
}

TEST_CASE("parsing timecode is the inverse of formatting", "[timecode]") {
  for (Rate rate : {k30, k15, kNtsc, kNtscDrop, Rate{60000, 1001, true}, Rate{25, 1, false}}) {
    for (std::int64_t f : {0, 1, 29, 1799, 1800, 1801, 17982, 107891, 107892, 250000}) {
      const auto back = parse_timecode(format_timecode(f, rate), rate);
      REQUIRE(back);
      CHECK(*back == f);
    }
  }
}

TEST_CASE("parsing accepts short forms and rejects bad text", "[timecode]") {
  CHECK(parse_timecode("1:15", k30) == 45);
  CHECK(parse_timecode("12", k30) == 12);
  CHECK(parse_timecode("00:00:02;00", k30) == 60);
  CHECK_FALSE(parse_timecode("", k30));
  CHECK_FALSE(parse_timecode("00:00:00:30", k30));  // frame out of range
  CHECK_FALSE(parse_timecode("00:60:00:00", k30));
  CHECK_FALSE(parse_timecode("1:2:3:4:5", k30));
  CHECK_FALSE(parse_timecode("ab", k30));
  CHECK_FALSE(parse_timecode("1:", k30));
  // A label drop-frame skips doesn't exist.
  CHECK_FALSE(parse_timecode("00:01:00;01", kNtscDrop));
  CHECK(parse_timecode("00:10:00;01", kNtscDrop) == 17983);
}

TEST_CASE("rates convert between frames and seconds", "[rate]") {
  CHECK(kNtsc.nominal() == 30);
  CHECK(k15.seconds(15) == 1.0);
  CHECK(kNtsc.frame_at(kNtsc.seconds(1234)) == 1234);
  CHECK(k30.frame_at(0.999) == 29);
  CHECK(mpp::rate_name(kNtscDrop) == "29.97 fps DF");
  CHECK(mpp::rate_name(k15) == "15 fps");
  CHECK(mpp::rate_name(Rate{25, 2, false}) == "12.5 fps");
}
