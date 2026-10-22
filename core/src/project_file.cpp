#include "mpp/project_file.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace mpp {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kFormat = "mock-premiere";

json colour_json(Color c) { return json::array({c.r, c.g, c.b, c.a}); }

Color colour_from(const json& j) {
  return {j.at(0).get<std::uint8_t>(), j.at(1).get<std::uint8_t>(), j.at(2).get<std::uint8_t>(),
          j.at(3).get<std::uint8_t>()};
}

json title_json(const TitleSpec& t) {
  json shapes = json::array(), texts = json::array();
  for (const TitleShape& s : t.shapes) {
    shapes.push_back({{"kind", s.kind == ShapeKind::Oval ? "oval" : "rectangle"},
                      {"x", s.x}, {"y", s.y}, {"width", s.width}, {"height", s.height},
                      {"color", colour_json(s.color)}, {"filled", s.filled}, {"line", s.line}});
  }
  for (const TitleText& x : t.texts) {
    texts.push_back({{"text", x.text},
                     {"font", x.font == TitleFont::Bold ? "bold" : "regular"},
                     {"size", x.size}, {"color", colour_json(x.color)}, {"x", x.x}, {"y", x.y},
                     {"align", x.align == TextAlign::Left ? "left" : x.align == TextAlign::Right ? "right" : "center"},
                     {"shadow", x.shadow}});
  }
  return {{"background", colour_json(t.background)}, {"shapes", shapes}, {"texts", texts}};
}

TitleSpec title_from(const json& j) {
  TitleSpec t;
  t.background = colour_from(j.at("background"));
  for (const json& s : j.at("shapes")) {
    TitleShape shape;
    shape.kind = s.at("kind").get<std::string>() == "oval" ? ShapeKind::Oval : ShapeKind::Rectangle;
    shape.x = s.at("x").get<double>();
    shape.y = s.at("y").get<double>();
    shape.width = s.at("width").get<double>();
    shape.height = s.at("height").get<double>();
    shape.color = colour_from(s.at("color"));
    shape.filled = s.at("filled").get<bool>();
    shape.line = s.at("line").get<double>();
    t.shapes.push_back(shape);
  }
  for (const json& x : j.at("texts")) {
    TitleText text;
    text.text = x.at("text").get<std::string>();
    text.font = x.at("font").get<std::string>() == "bold" ? TitleFont::Bold : TitleFont::Regular;
    text.size = x.at("size").get<double>();
    text.color = colour_from(x.at("color"));
    text.x = x.at("x").get<double>();
    text.y = x.at("y").get<double>();
    const std::string align = x.at("align").get<std::string>();
    text.align = align == "left" ? TextAlign::Left : align == "right" ? TextAlign::Right : TextAlign::Center;
    text.shadow = x.at("shadow").get<bool>();
    t.texts.push_back(text);
  }
  return t;
}

json rate_json(const Rate& r) { return json::array({r.num, r.den, r.drop_frame}); }

Rate rate_from(const json& j) {
  return {j.at(0).get<int>(), j.at(1).get<int>(), j.at(2).get<bool>()};
}

MediaKind kind_from(const std::string& name) {
  for (MediaKind k : {MediaKind::Movie, MediaKind::Still, MediaKind::Sequence, MediaKind::Title,
                      MediaKind::Filmstrip}) {
    if (name == media_kind_name(k)) return k;
  }
  throw std::runtime_error("Unknown media kind '" + name + "'");
}

Track track_from(const std::string& name) {
  for (Track t : kAllTracks) {
    if (name == track_name(t)) return t;
  }
  throw std::runtime_error("Unknown track '" + name + "'");
}

KeyType key_from(const std::string& name) {
  for (const auto& [type, key] : key_types()) {
    if (name == key) return type;
  }
  throw std::runtime_error("Unknown transparency '" + name + "'");
}

// Paths are saved absolute: one imported as "clip.mov" from a terminal
// means that file in the folder the app was started in.
std::string absolute(const std::string& path) {
  if (path.empty()) return path;
  std::error_code ec;
  const fs::path full = fs::absolute(fs::path(path), ec);
  return ec ? path : full.lexically_normal().string();
}

std::string relative_to(const std::string& path, const std::string& dir) {
  if (dir.empty()) return "";
  std::error_code ec;
  const fs::path rel = fs::path(path).lexically_relative(fs::path(dir));
  return ec || rel.empty() ? "" : rel.generic_string();
}

std::string resolve(const std::string& path, const std::string& relative, const std::string& dir) {
  std::error_code ec;
  if (fs::exists(path, ec) || relative.empty() || dir.empty()) return path;
  const fs::path candidate = (fs::path(dir) / relative).lexically_normal();
  return fs::exists(candidate, ec) ? candidate.string() : path;
}

}  // namespace

std::string encode_project(const Project& p, const std::string& project_dir) {
  json j;
  j["format"] = kFormat;
  j["version"] = kProjectFormatVersion;
  const ProjectSettings& s = p.settings;
  j["settings"] = {{"width", s.width},           {"height", s.height},
                   {"rate", rate_json(s.rate)},   {"audio_rate", s.audio_rate},
                   {"audio_bits", s.audio_bits}, {"still_frames", s.still_frames}};
  j["next_id"] = p.next_id;
  j["work_area"] = json::array({p.work_in, p.work_out});
  json bin = json::array();
  for (const MediaItem& item : p.bin) {
    const MediaInfo& i = item.info;
    json entry = {{"id", item.id},
                   {"path", absolute(item.path)},
                   {"relative", relative_to(absolute(item.path), project_dir)},
                   {"name", item.name},
                   {"in", item.in},
                   {"out", item.out},
                   {"info",
                    {{"kind", media_kind_name(i.kind)},
                     {"width", i.width},
                     {"height", i.height},
                     {"rate", rate_json(i.rate)},
                     {"frames", i.frames},
                     {"has_video", i.has_video},
                     {"has_audio", i.has_audio},
                     {"sample_rate", i.sample_rate},
                     {"channels", i.channels}}}};
    if (item.title) entry["title"] = title_json(*item.title);
    if (item.sequence) {
      entry["sequence"] = {{"pattern", absolute(item.sequence->pattern)},
                           {"relative", relative_to(absolute(item.sequence->pattern), project_dir)},
                           {"first", item.sequence->first},
                           {"count", item.sequence->count}};
    }
    bin.push_back(entry);
  }
  j["bin"] = bin;
  json clips = json::array();
  for (const Clip& c : p.clips) {
    json fade = json::array();
    for (const FadePoint& f : c.fade) fade.push_back(json::array({f.position, f.level}));
    const TransparencyKey& k = c.key;
    json filters = json::array();
    for (const FilterInstance& f : c.filters) {
      filters.push_back({{"id", f.id}, {"start", f.start}, {"end", f.end}});
    }
    json motion = nullptr;
    if (c.motion) {
      motion = json::array();
      for (const MotionPoint& m : c.motion->points) {
        motion.push_back({{"t", m.t}, {"x", m.x}, {"y", m.y}, {"rotation", m.rotation}, {"zoom", m.zoom}});
      }
    }
    clips.push_back({{"filters", filters},
                     {"motion", motion},
                     {"speed", c.speed},
                     {"id", c.id},
                     {"item", c.item},
                     {"track", track_name(c.track)},
                     {"start", c.start},
                     {"in", c.in},
                     {"out", c.out},
                     {"link", c.link},
                     {"fade", fade},
                     {"key",
                      {{"type", key_name(k.type)},
                       {"color", json::array({k.color.r, k.color.g, k.color.b})},
                       {"similarity", k.similarity},
                       {"blend", k.blend},
                       {"reverse", k.reverse}}}});
  }
  j["clips"] = clips;
  json transitions = json::array();
  for (const Transition& t : p.transitions) {
    const TransitionSettings& s = t.settings;
    transitions.push_back({{"id", t.id},
                           {"kind", t.kind},
                           {"start", t.start},
                           {"length", t.length},
                           {"from", s.start},
                           {"to", s.end},
                           {"b_to_a", s.b_to_a},
                           {"reverse", s.reverse},
                           {"border", s.border},
                           {"border_color", json::array({s.border_color.r, s.border_color.g,
                                                         s.border_color.b})},
                           {"direction", s.direction}});
  }
  j["transitions"] = transitions;
  j["sequence"] = p.sequence;
  return j.dump(2);
}

Project decode_project(const std::string& text, const std::string& project_dir) {
  json j;
  try {
    j = json::parse(text);
  } catch (const json::exception& e) {
    throw std::runtime_error(std::string("Not a project file: ") + e.what());
  }
  if (!j.is_object() || j.value("format", "") != kFormat) {
    throw std::runtime_error("Not a Mock Premiere project file");
  }
  const int version = j.value("version", 0);
  if (version > kProjectFormatVersion) {
    throw std::runtime_error("This project was saved by a newer version (format " +
                             std::to_string(version) + ")");
  }
  try {
    Project p;
    const json& s = j.at("settings");
    p.settings.width = s.at("width").get<int>();
    p.settings.height = s.at("height").get<int>();
    p.settings.rate = rate_from(s.at("rate"));
    p.settings.audio_rate = s.at("audio_rate").get<int>();
    p.settings.audio_bits = s.at("audio_bits").get<int>();
    p.settings.still_frames = s.at("still_frames").get<std::int64_t>();
    p.next_id = j.at("next_id").get<int>();
    p.work_in = j.at("work_area").at(0).get<std::int64_t>();
    p.work_out = j.at("work_area").at(1).get<std::int64_t>();
    for (const json& b : j.at("bin")) {
      MediaItem item;
      item.id = b.at("id").get<int>();
      item.path = resolve(b.at("path").get<std::string>(), b.value("relative", ""), project_dir);
      item.name = b.at("name").get<std::string>();
      item.in = b.at("in").get<std::int64_t>();
      item.out = b.at("out").get<std::int64_t>();
      const json& i = b.at("info");
      item.info.kind = kind_from(i.at("kind").get<std::string>());
      item.info.width = i.at("width").get<int>();
      item.info.height = i.at("height").get<int>();
      item.info.rate = rate_from(i.at("rate"));
      item.info.frames = i.at("frames").get<std::int64_t>();
      item.info.has_video = i.at("has_video").get<bool>();
      item.info.has_audio = i.at("has_audio").get<bool>();
      item.info.sample_rate = i.at("sample_rate").get<int>();
      item.info.channels = i.at("channels").get<int>();
      if (b.contains("title")) item.title = title_from(b.at("title"));
      if (b.contains("sequence")) {
        const json& q = b.at("sequence");
        ImageSequence seq;
        seq.pattern = q.at("pattern").get<std::string>();
        seq.first = q.at("first").get<int>();
        seq.count = q.at("count").get<int>();
        // Found again if moved with the project, as files are.
        std::error_code ec;
        const std::string relative = q.value("relative", "");
        if (!fs::exists(seq.path(0), ec) && !relative.empty() && !project_dir.empty()) {
          ImageSequence moved = seq;
          moved.pattern = (fs::path(project_dir) / relative).lexically_normal().string();
          if (fs::exists(moved.path(0), ec)) seq = moved;
        }
        item.sequence = seq;
      }
      p.bin.push_back(std::move(item));
    }
    for (const json& c : j.at("clips")) {
      Clip clip;
      clip.id = c.at("id").get<int>();
      clip.item = c.at("item").get<int>();
      clip.track = track_from(c.at("track").get<std::string>());
      clip.start = c.at("start").get<std::int64_t>();
      clip.in = c.at("in").get<std::int64_t>();
      clip.out = c.at("out").get<std::int64_t>();
      clip.link = c.at("link").get<int>();
      for (const json& f : c.value("fade", json::array())) {
        clip.fade.push_back({f.at(0).get<double>(), f.at(1).get<double>()});
      }
      clip.speed = c.value("speed", 100);
      if (c.contains("motion") && c.at("motion").is_array()) {
        Motion motion;
        for (const json& m : c.at("motion")) {
          motion.points.push_back({m.at("t").get<double>(), m.at("x").get<double>(), m.at("y").get<double>(),
                                   m.at("rotation").get<double>(), m.at("zoom").get<double>()});
        }
        clip.motion = motion;
      }
      for (const json& f : c.value("filters", json::array())) {
        clip.filters.push_back({f.at("id").get<std::string>(),
                                f.at("start").get<std::vector<double>>(),
                                f.at("end").get<std::vector<double>>()});
      }
      if (c.contains("key")) {
        const json& k = c.at("key");
        clip.key.type = key_from(k.at("type").get<std::string>());
        const json& col = k.at("color");
        clip.key.color = {col.at(0).get<std::uint8_t>(), col.at(1).get<std::uint8_t>(),
                          col.at(2).get<std::uint8_t>(), 255};
        clip.key.similarity = k.at("similarity").get<double>();
        clip.key.blend = k.at("blend").get<double>();
        clip.key.reverse = k.at("reverse").get<bool>();
      }
      p.clips.push_back(clip);
    }
    p.sequence = j.value("sequence", std::vector<int>{});
    for (const json& t : j.value("transitions", json::array())) {
      Transition tr;
      tr.id = t.at("id").get<int>();
      tr.kind = t.at("kind").get<std::string>();
      tr.start = t.at("start").get<std::int64_t>();
      tr.length = t.at("length").get<std::int64_t>();
      TransitionSettings& s = tr.settings;
      s.start = t.at("from").get<double>();
      s.end = t.at("to").get<double>();
      s.b_to_a = t.at("b_to_a").get<bool>();
      s.reverse = t.at("reverse").get<bool>();
      s.border = t.at("border").get<int>();
      const json& c = t.at("border_color");
      s.border_color = {c.at(0).get<std::uint8_t>(), c.at(1).get<std::uint8_t>(),
                        c.at(2).get<std::uint8_t>(), 255};
      s.direction = t.at("direction").get<int>();
      p.transitions.push_back(tr);
    }
    return p;
  } catch (const json::exception& e) {
    throw std::runtime_error(std::string("Damaged project file: ") + e.what());
  }
}

void save_project(const Project& project, const std::string& path) {
  const std::string dir = fs::absolute(fs::path(path)).parent_path().string();
  const std::string text = encode_project(project, dir);
  const std::string temp = path + ".saving";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not write '" + path + "'");
    out << text;
    if (!out) throw std::runtime_error("Could not write '" + path + "'");
  }
  std::error_code ec;
  fs::rename(temp, path, ec);
  if (ec) {
    fs::remove(temp, ec);
    throw std::runtime_error("Could not write '" + path + "'");
  }
}

Project load_project(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Could not open '" + path + "'");
  std::ostringstream text;
  text << in.rdbuf();
  const std::string dir = fs::absolute(fs::path(path)).parent_path().string();
  return decode_project(text.str(), dir);
}

bool is_project_path(const std::string& path) {
  if (path.size() < 4) return false;
  std::string ext = path.substr(path.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == kProjectExtension;
}

}  // namespace mpp
