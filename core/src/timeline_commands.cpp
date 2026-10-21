#include "mpp/timeline_commands.h"

#include <algorithm>
#include <stdexcept>

namespace mpp {

namespace {

[[noreturn]] void refuse(const std::string& why) { throw std::invalid_argument(why); }

Clip& find_clip(Project& p, int id) {
  Clip* c = p.clip(id);
  if (!c) refuse("No such clip");
  return *c;
}

bool overlaps(const Clip& a, std::int64_t start, std::int64_t end) {
  return a.start < end && start < a.end();
}

}  // namespace

std::optional<Track> free_audio_track(const Project& p, std::int64_t start, std::int64_t end,
                                      int ignore) {
  for (Track t : kAudioTracks) {
    bool taken = false;
    for (const Clip& c : p.clips) {
      if (c.id != ignore && c.track == t && overlaps(c, start, end)) taken = true;
    }
    if (!taken) return t;
  }
  return std::nullopt;
}

void check_clip(const Project& p, const Clip& clip) {
  const MediaItem* item = p.item(clip.item);
  if (!item) refuse("The clip's file is no longer in the Project window");
  if (is_video_track(clip.track) && !item->info.has_video) {
    refuse(item->name + " has no picture to put on " + track_name(clip.track));
  }
  if (is_audio_track(clip.track) && !item->info.has_audio) {
    refuse(item->name + " has no sound to put on " + track_name(clip.track));
  }
  if (clip.in < 0 || clip.out <= clip.in) refuse("A clip must be at least one frame long");
  const auto length = source_length(item->info, p.settings.rate);
  if (length && clip.out > *length) refuse("That runs past the end of " + item->name);
  if (clip.start < 0) refuse("A clip can't start before the beginning of the program");
  for (const Clip& other : p.clips) {
    if (other.id != clip.id && other.track == clip.track &&
        overlaps(other, clip.start, clip.end())) {
      refuse(std::string("That space on ") + track_name(clip.track) + " is taken");
    }
  }
}

int place_clip(Project& p, int item, Track track, std::int64_t start, std::int64_t in,
               std::int64_t out, bool with_sound) {
  const MediaItem* source = p.item(item);
  if (!source) refuse("No such item in the Project window");
  Clip clip;
  clip.id = p.next_id++;
  clip.item = item;
  clip.track = track;
  clip.start = start;
  clip.in = in;
  clip.out = out;
  p.clips.push_back(clip);
  check_clip(p, clip);
  if (is_video_track(track) && with_sound && source->info.has_audio) {
    const auto audio = free_audio_track(p, clip.start, clip.end());
    if (!audio) refuse("Audio A, B and C are all taken there");
    Clip sound = clip;
    sound.id = p.next_id++;
    sound.track = *audio;
    sound.link = clip.id;
    p.clips.back().link = sound.id;
    p.clips.push_back(sound);
    check_clip(p, sound);
  }
  return clip.id;
}

void PlaceClipCommand::apply(Project& p) { place_clip(p, item, track, start, in, out, with_sound); }

void MoveClipCommand::apply(Project& p) {
  Clip& c = find_clip(p, clip);
  if (is_video_track(track) != is_video_track(c.track)) {
    refuse(is_video_track(c.track) ? "Pictures go on Video A, Video B or Super"
                                   : "Sound goes on Audio A, B or C");
  }
  const std::int64_t delta = start - c.start;
  c.track = track;
  c.start = start;
  const Clip moved = c;
  Clip* partner = c.link ? p.clip(c.link) : nullptr;
  if (partner) partner->start += delta;
  check_clip(p, moved);
  if (partner) check_clip(p, *partner);
}

void TrimClipCommand::apply(Project& p) {
  Clip& c = find_clip(p, clip);
  const std::int64_t d_start = start - c.start, d_in = in - c.in, d_out = out - c.out;
  c.start = start;
  c.in = in;
  c.out = out;
  const Clip trimmed = c;
  Clip* partner = c.link ? p.clip(c.link) : nullptr;
  if (partner) {
    partner->start += d_start;
    partner->in += d_in;
    partner->out += d_out;
  }
  check_clip(p, trimmed);
  if (partner) check_clip(p, *partner);
}

void RazorCommand::apply(Project& p) {
  const Clip original = find_clip(p, clip);
  if (frame <= original.start || frame >= original.end()) {
    refuse("The razor has to cut inside the clip");
  }
  const auto split = [&](int id) {
    Clip& left = find_clip(p, id);
    Clip right = left;
    right.id = p.next_id++;
    const std::int64_t at = left.in + (frame - left.start);
    left.out = at;
    right.in = at;
    right.start = frame;
    right.link = 0;
    p.clips.push_back(right);
    return right.id;
  };
  const int right = split(original.id);
  if (original.link && p.clip(original.link)) {
    const int partner_right = split(original.link);
    p.clip(right)->link = partner_right;
    p.clip(partner_right)->link = right;
  }
}

void ClearCommand::apply(Project& p) {
  std::vector<int> doomed;
  for (int id : ids) {
    if (const Clip* c = p.clip(id)) {
      doomed.push_back(id);
      if (c->link) doomed.push_back(c->link);
    } else if (p.transition(id)) {
      doomed.push_back(id);
    } else {
      refuse("Nothing to clear with that id");
    }
  }
  const auto gone = [&](int id) { return std::find(doomed.begin(), doomed.end(), id) != doomed.end(); };
  p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(),
                               [&](const Clip& c) { return gone(c.id); }),
                p.clips.end());
  p.transitions.erase(std::remove_if(p.transitions.begin(), p.transitions.end(),
                                     [&](const Transition& t) { return gone(t.id); }),
                      p.transitions.end());
}

void check_transition(const Project& p, const Transition& t) {
  if (!find_transition(t.kind)) refuse("Unknown transition '" + t.kind + "'");
  if (t.length < 1) refuse("A transition must be at least one frame long");
  if (t.start < 0) refuse("A transition can't start before the beginning of the program");
  if (t.settings.start < 0 || t.settings.start > 1 || t.settings.end < 0 || t.settings.end > 1) {
    refuse("Start and end must be between 0 and 100%");
  }
  if (t.settings.border < 0 || t.settings.border > 100) refuse("Border must be 0 to 100 pixels");
  for (const Transition& other : p.transitions) {
    if (other.id != t.id && other.start < t.end() && t.start < other.end()) {
      refuse("That space on the transition track is taken");
    }
  }
}

TransitionSpan suggest_transition_span(const Project& p, std::int64_t frame,
                                       std::int64_t default_length) {
  TransitionSpan span;
  span.start = std::max<std::int64_t>(0, frame);
  span.length = std::max<std::int64_t>(1, default_length);
  const Clip* a = p.clip_at(Track::VideoA, frame);
  const Clip* b = p.clip_at(Track::VideoB, frame);
  if (a && b) {
    span.start = std::max(a->start, b->start);
    span.length = std::min(a->end(), b->end()) - span.start;
    // The clip that ends first is the one going out.
    span.b_to_a = b->end() < a->end() || (b->end() == a->end() && b->start < a->start);
  }
  return span;
}

int add_transition(Project& p, const std::string& kind, std::int64_t start, std::int64_t length,
                   const TransitionSettings& settings) {
  Transition t;
  t.id = p.next_id++;
  t.kind = kind;
  t.start = start;
  t.length = length;
  t.settings = settings;
  check_transition(p, t);
  p.transitions.push_back(t);
  return t.id;
}

void AddTransitionCommand::apply(Project& p) { add_transition(p, kind, start, length, settings); }

void SetSequenceCommand::apply(Project& p) {
  for (int id : items) {
    if (!p.item(id)) refuse("No such item in the Project window");
  }
  p.sequence = items;
}

std::int64_t sequence_length(const Project& p, const std::vector<int>& items, bool transitions,
                             std::int64_t overlap) {
  std::int64_t total = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const MediaItem* item = p.item(items[i]);
    if (!item) continue;
    total += item->out - item->in;
    if (transitions && i > 0) total -= overlap;
  }
  return std::max<std::int64_t>(0, total);
}

void BuildSequenceCommand::apply(Project& p) {
  if (items.empty()) refuse("The Sequence window is empty");
  const bool overlapping = !transition.empty();
  if (overlapping && overlap < 1) refuse("Transitions need an overlap of at least one frame");
  std::int64_t at = start;
  bool on_a = true;
  std::int64_t previous = 0;  // the last clip's length
  for (std::size_t i = 0; i < items.size(); ++i) {
    const MediaItem* item = p.item(items[i]);
    if (!item) refuse("No such item in the Project window");
    if (!item->info.has_video) refuse(item->name + " has no picture for the Sequence");
    const std::int64_t length = item->out - item->in;
    if (overlapping && i > 0) {
      if (length <= overlap || previous <= overlap) {
        refuse("Every clip must be longer than the overlap");
      }
      at -= overlap;
      // From the clip going out to this one: A to B when this one is on B.
      TransitionSettings settings;
      settings.b_to_a = on_a;
      add_transition(p, transition, at, overlap, settings);
    }
    place_clip(p, items[i], on_a ? Track::VideoA : Track::VideoB, at, item->in, item->out, true);
    at += length;
    previous = length;
    if (overlapping) on_a = !on_a;
  }
}

void SetTransitionSpanCommand::apply(Project& p) {
  Transition* t = p.transition(transition);
  if (!t) refuse("No such transition");
  t->start = start;
  t->length = length;
  check_transition(p, *t);
}

void SetTransitionSettingsCommand::apply(Project& p) {
  Transition* t = p.transition(transition);
  if (!t) refuse("No such transition");
  t->settings = settings;
  check_transition(p, *t);
}

void SetFadeCommand::apply(Project& p) {
  Clip& c = find_clip(p, clip);
  std::vector<FadePoint> sorted = fade;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const FadePoint& a, const FadePoint& b) { return a.position < b.position; });
  for (const FadePoint& f : sorted) {
    if (f.level < 0.0 || f.level > 1.0) refuse("Fade levels run from 0 to 100%");
  }
  c.fade = std::move(sorted);
}

void SetFiltersCommand::apply(Project& p) {
  Clip& c = find_clip(p, clip);
  for (const FilterInstance& f : filters) {
    const std::vector<FilterParam>* params = nullptr;
    if (is_audio_track(c.track)) {
      if (const AudioFilterDef* def = find_audio_filter(f.id)) params = &def->params;
    } else if (const FilterDef* def = find_video_filter(f.id)) {
      params = &def->params;
    }
    if (!params) {
      refuse("'" + f.id + "' isn't a " + (is_audio_track(c.track) ? "sound" : "video") + " filter");
    }
    if (f.start.size() != params->size() || f.end.size() != params->size()) {
      refuse("Wrong number of settings for '" + f.id + "'");
    }
    for (std::size_t i = 0; i < params->size(); ++i) {
      const FilterParam& param = (*params)[i];
      for (double v : {f.start[i], f.end[i]}) {
        if (v < param.min || v > param.max) refuse(param.name + " is out of range");
      }
    }
  }
  c.filters = filters;
}

void SetKeyCommand::apply(Project& p) {
  Clip& c = find_clip(p, clip);
  if (key.similarity < 0 || key.similarity > 1 || key.blend < 0 || key.blend > 1) {
    refuse("Similarity and blend run from 0 to 100%");
  }
  c.key = key;
}

void SetWorkAreaCommand::apply(Project& p) {
  if (in < 0 || out <= in) refuse("The work area must cover at least one frame");
  p.work_in = in;
  p.work_out = out;
}

}  // namespace mpp
