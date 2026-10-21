#include "mpp/edl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

#include "mpp/renderer.h"
#include "mpp/transitions.h"

namespace mpp {

namespace {

// What the V channel shows over a stretch of the program.
struct Run {
  std::int64_t start = 0, end = 0;
  const Clip* clip = nullptr;           // a cut to this clip, or black if null
  const Transition* transition = nullptr;
  const Clip* from = nullptr;           // for a transition
  const Clip* to = nullptr;
};

class Writer {
 public:
  Writer(const Project& p) : p_(p), rate_(p.settings.rate) {
    hour_ = parse_timecode(rate_.drop_frame && rate_.can_drop_frame() ? "01:00:00;00" : "01:00:00:00",
                           rate_).value_or(0);
  }

  std::string tc(std::int64_t frames) const { return format_timecode(frames, rate_); }
  std::string rec(std::int64_t frame) const { return tc(hour_ + frame); }

  std::int64_t source_at(const Clip& c, std::int64_t frame) const {
    return std::llround(clip_source_position(c, frame));
  }

  void line(int event, const std::string& reel, const std::string& channel, const std::string& kind,
            std::int64_t duration, std::int64_t src_in, std::int64_t src_out, std::int64_t rec_in,
            std::int64_t rec_out) {
    char text[256];
    const std::string length = duration >= 0 ? (duration < 1000 ? pad3(duration) : std::to_string(duration)) : "   ";
    std::snprintf(text, sizeof(text), "%03d  %-8s %-5s %-4s %3s %s %s %s %s\n", event, reel.c_str(),
                  channel.c_str(), kind.c_str(), length.c_str(), tc(src_in).c_str(), tc(src_out).c_str(),
                  rec(rec_in).c_str(), rec(rec_out).c_str());
    out_ += text;
  }

  void comment(const std::string& text) { out_ += "* " + text + "\n"; }

  std::string name_of(const Clip* c) const {
    const MediaItem* item = c ? p_.item(c->item) : nullptr;
    return item ? item->name : "BLACK";
  }

  void speed(const Clip& c, std::int64_t frame) {
    if (c.speed == 100) return;
    char text[128];
    std::snprintf(text, sizeof(text), "M2   %-8s %05.1f                %s\n", "AX",
                  rate_.fps() * c.speed / 100.0, tc(source_at(c, frame)).c_str());
    out_ += text;
  }

  std::string& text() { return out_; }

 private:
  static std::string pad3(std::int64_t v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%03lld", static_cast<long long>(v));
    return b;
  }

  const Project& p_;
  Rate rate_;
  std::int64_t hour_ = 0;
  std::string out_;
};

std::vector<Run> video_runs(const Project& p) {
  std::set<std::int64_t> cuts = {0, p.duration()};
  for (const Clip& c : p.clips) {
    if (c.track == Track::VideoA || c.track == Track::VideoB) cuts.insert({c.start, c.end()});
  }
  for (const Transition& t : p.transitions) cuts.insert({t.start, t.end()});
  std::vector<Run> runs;
  for (auto it = cuts.begin(); std::next(it) != cuts.end(); ++it) {
    Run r;
    r.start = *it;
    r.end = *std::next(it);
    if (r.start >= p.duration()) break;
    const Clip* a = p.clip_at(Track::VideoA, r.start);
    const Clip* b = p.clip_at(Track::VideoB, r.start);
    const Transition* t = p.transition_at(r.start);
    if (t && find_transition(t->kind)) {
      r.transition = t;
      r.from = t->settings.b_to_a ? b : a;
      r.to = t->settings.b_to_a ? a : b;
    } else {
      r.clip = a ? a : b;
    }
    // Join a stretch that continues the last one.
    if (!runs.empty() && !r.transition && !runs.back().transition && runs.back().clip == r.clip) {
      runs.back().end = r.end;
    } else if (!runs.empty() && r.transition && runs.back().transition == r.transition) {
      runs.back().end = r.end;
    } else {
      runs.push_back(r);
    }
  }
  return runs;
}

}  // namespace

std::string make_edl(const Project& p, const std::string& title) {
  Writer w(p);
  const Rate rate = p.settings.rate;
  w.text() = "TITLE: " + title + "\n" +
             (rate.drop_frame && rate.can_drop_frame() ? "FCM: DROP FRAME\n" : "FCM: NON-DROP FRAME\n") + "\n";
  int event = 0;
  const auto reel = [](const Clip* c) { return std::string(c ? "AX" : "BL"); };
  const auto src = [&](const Clip* c, std::int64_t frame) { return c ? w.source_at(*c, frame) : 0; };

  const std::vector<Run> runs = video_runs(p);
  for (std::size_t i = 0; i < runs.size(); ++i) {
    const Run& r = runs[i];
    ++event;
    if (!r.transition) {
      w.line(event, reel(r.clip), "V", "C", -1, src(r.clip, r.start), src(r.clip, r.end), r.start, r.end);
      if (r.clip) {
        w.speed(*r.clip, r.start);
        w.comment("FROM CLIP NAME: " + w.name_of(r.clip));
      }
      continue;
    }
    // The incoming clip carries on past the transition: one event for both.
    std::int64_t end = r.end;
    if (i + 1 < runs.size() && !runs[i + 1].transition && runs[i + 1].clip == r.to) end = runs[++i].end;
    const TransitionDef* def = find_transition(r.transition->kind);
    const bool dissolve = def->group == "Dissolve";
    w.line(event, reel(r.from), "V", "C", -1, src(r.from, r.start), src(r.from, r.start), r.start, r.start);
    w.line(event, reel(r.to), "V", dissolve ? "D" : "W001", r.transition->length, src(r.to, r.start),
           src(r.to, end), r.start, end);
    if (r.to) w.speed(*r.to, r.start);
    if (!dissolve) w.comment("EFFECT NAME: " + def->name);
    w.comment("FROM CLIP NAME: " + w.name_of(r.from));
    w.comment("TO CLIP NAME: " + w.name_of(r.to));
  }

  const std::pair<Track, const char*> audio[] = {
      {Track::AudioA, "A"}, {Track::AudioB, "A2"}, {Track::AudioC, "A3"}};
  for (const auto& [track, channel] : audio) {
    for (const Clip* c : p.clips_on(track)) {
      ++event;
      w.line(event, "AX", channel, "C", -1, w.source_at(*c, c->start), w.source_at(*c, c->end()), c->start,
             c->end());
      w.speed(*c, c->start);
      w.comment("FROM CLIP NAME: " + w.name_of(c));
    }
  }
  for (const Clip* c : p.clips_on(Track::Superimpose)) {
    w.comment("SUPER: " + w.name_of(c) + " " + w.rec(c->start) + " " + w.rec(c->end()));
  }
  return w.text();
}

void save_edl(const Project& project, const std::string& title, const std::string& path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("Could not write '" + path + "'");
  out << make_edl(project, title);
  if (!out) throw std::runtime_error("Could not write '" + path + "'");
}

}  // namespace mpp
