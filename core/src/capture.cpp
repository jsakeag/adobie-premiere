#include "mpp/capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "mpp/audio_mixer.h"
#include "mpp/resample.h"

namespace mpp {

namespace {

std::string av_error(int code) {
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, text, sizeof(text));
  return text;
}

void register_devices() {
  static std::once_flag once;
  std::call_once(once, [] { avdevice_register_all(); });
}

// FFmpeg's log goes to one callback for the whole program; while devices
// are listed it's collected here instead.
std::mutex log_mutex;
std::string* log_sink = nullptr;

void collect_log(void*, int level, const char* format, va_list args) {
  if (level > AV_LOG_INFO) return;
  char line[1024];
  std::vsnprintf(line, sizeof(line), format, args);
  std::lock_guard<std::mutex> lock(log_mutex);
  if (log_sink) log_sink->append(line);
}

int interrupted(void* quit) { return static_cast<std::atomic<bool>*>(quit)->load() ? 1 : 0; }

// How long sound is kept while not recording, in seconds.
constexpr double kSoundKept = 1.0;
// How long a finished recording waits for sound to catch up with its
// pictures before padding it with silence.
constexpr auto kSoundGrace = std::chrono::seconds(1);

}  // namespace

CaptureDevices parse_device_list(const std::string& log) {
  CaptureDevices devices;
  std::vector<CaptureDevice>* section = nullptr;
  std::istringstream lines(log);
  std::string line;
  while (std::getline(lines, line)) {
    // "[AVFoundation indev @ 0x7f..] " in front of a line names who logged it.
    if (!line.empty() && line[0] == '[') {
      const std::size_t close = line.find("] ");
      if (close != std::string::npos && line.find(" @ ") < close) line.erase(0, close + 2);
    }
    if (line.find("AVFoundation video devices") != std::string::npos) {
      section = &devices.cameras;
    } else if (line.find("AVFoundation audio devices") != std::string::npos) {
      section = &devices.microphones;
    } else if (section && line.size() > 3 && line[0] == '[') {
      const std::size_t close = line.find("] ");
      if (close == std::string::npos || close < 2) continue;
      const std::string number = line.substr(1, close - 1);
      if (number.find_first_not_of("0123456789") != std::string::npos) continue;
      std::string name = line.substr(close + 2);
      while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
      section->push_back({std::stoi(number), name});
    }
  }
  return devices;
}

CaptureDevices list_capture_devices() {
  register_devices();
  const AVInputFormat* format = av_find_input_format("avfoundation");
  if (!format) return {};
  std::string log;
  {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_sink = &log;
  }
  av_log_set_callback(collect_log);
  AVDictionary* options = nullptr;
  av_dict_set(&options, "list_devices", "true", 0);
  AVFormatContext* context = nullptr;
  // Asking for the list makes opening fail once the list is logged.
  if (avformat_open_input(&context, "", format, &options) >= 0) avformat_close_input(&context);
  av_dict_free(&options);
  av_log_set_callback(av_log_default_callback);
  {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_sink = nullptr;
  }
  return parse_device_list(log);
}

CaptureInput device_input(int camera, int microphone) {
  CaptureInput input;
  input.format = "avfoundation";
  input.url = (camera >= 0 ? std::to_string(camera) : "none") + ":" +
              (microphone >= 0 ? std::to_string(microphone) : "none");
  // Cameras refuse NTSC's 29.97, the input's default; every camera does 30.
  if (camera >= 0) input.options.push_back({"framerate", "30"});
  return input;
}

CaptureInput test_pattern_input(const ProjectSettings& settings, bool sound) {
  CaptureInput input;
  input.format = "lavfi";
  input.url = "testsrc2=size=" + std::to_string(settings.width) + "x" +
              std::to_string(settings.height) + ":rate=" + std::to_string(settings.rate.num) +
              "/" + std::to_string(settings.rate.den) + ",format=rgba,realtime[out0]";
  if (sound) {
    input.url += ";sine=frequency=440:sample_rate=" + std::to_string(settings.audio_rate) +
                 ",arealtime[out1]";
  }
  return input;
}

// The open input and its decoders. Lives on the reading thread.
struct CaptureSession::Reader {
  AVFormatContext* format = nullptr;
  AVCodecContext* video = nullptr;
  AVCodecContext* audio = nullptr;
  int vstream = -1;
  int astream = -1;
  SwsContext* sws = nullptr;
  SwrContext* swr = nullptr;
  int sound_channels = 2;  // what swr gives: mono stays mono, see below
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();

  ~Reader() {
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr);
    sws_freeContext(sws);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    avformat_close_input(&format);
  }

  static AVCodecContext* open_decoder(AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) throw std::runtime_error("This FFmpeg can't decode the input");
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
        avcodec_open2(context, codec, nullptr) < 0) {
      avcodec_free_context(&context);
      throw std::runtime_error("Could not start decoding the input");
    }
    return context;
  }

  void open(const CaptureInput& input, std::atomic<bool>* quit, int sample_rate) {
    register_devices();
    const AVInputFormat* kind = av_find_input_format(input.format.c_str());
    if (!kind) throw std::runtime_error("This FFmpeg can't record from " + input.format);
    format = avformat_alloc_context();
    format->interrupt_callback = {interrupted, quit};
    AVDictionary* options = nullptr;
    for (const auto& [key, value] : input.options) av_dict_set(&options, key.c_str(), value.c_str(), 0);
    int r = avformat_open_input(&format, input.url.c_str(), kind, &options);
    av_dict_free(&options);
    if (r < 0) throw std::runtime_error("Could not open the camera: " + av_error(r));
    if ((r = avformat_find_stream_info(format, nullptr)) < 0) {
      throw std::runtime_error("Could not read the camera: " + av_error(r));
    }
    vstream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vstream < 0) throw std::runtime_error("The input has no picture");
    video = open_decoder(format->streams[vstream]);
    astream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (astream < 0) return;
    audio = open_decoder(format->streams[astream]);
    // Mono stays mono through the resampler and is copied to both sides,
    // as FfmpegSource does; letting it upmix would pan it to -3 dB.
    sound_channels = audio->ch_layout.nb_channels == 1 ? 1 : 2;
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, sound_channels);
    if (swr_alloc_set_opts2(&swr, &layout, AV_SAMPLE_FMT_FLT, sample_rate, &audio->ch_layout,
                            audio->sample_fmt, audio->sample_rate, 0, nullptr) < 0 ||
        swr_init(swr) < 0) {
      throw std::runtime_error("Could not convert the input's sound");
    }
  }

  Image picture(const AVFrame* f) {
    const auto pixels = static_cast<AVPixelFormat>(f->format);
    sws = sws_getCachedContext(sws, f->width, f->height, pixels, f->width, f->height,
                               AV_PIX_FMT_RGBA, SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                               nullptr, nullptr, nullptr);
    if (!sws) throw std::runtime_error("Can't convert the camera's pictures");
    Image image(f->width, f->height);
    std::uint8_t* dst[4] = {image.data(), nullptr, nullptr, nullptr};
    const int stride[4] = {f->width * Image::kChannels, 0, 0, 0};
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pixels);
    if (desc && !(desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
      for (std::size_t i = 3; i < image.size_bytes(); i += 4) image.data()[i] = 255;
    }
    return image;
  }

  // Appends the frame's sound as stereo.
  void sound(const AVFrame* f, std::vector<float>& out) {
    const int capacity = swr_get_out_samples(swr, f->nb_samples);
    if (capacity <= 0) return;
    std::vector<float> converted(static_cast<std::size_t>(capacity) * sound_channels);
    std::uint8_t* dst[1] = {reinterpret_cast<std::uint8_t*>(converted.data())};
    const int got = swr_convert(swr, dst, capacity, const_cast<const std::uint8_t**>(f->extended_data),
                                f->nb_samples);
    if (got <= 0) return;
    if (sound_channels == 2) {
      out.insert(out.end(), converted.begin(), converted.begin() + got * 2);
      return;
    }
    for (int i = 0; i < got; ++i) {
      out.push_back(converted[i]);
      out.push_back(converted[i]);
    }
  }
};

CaptureSession::CaptureSession(CaptureInput input, ProjectSettings settings)
    : input_(std::move(input)), settings_(settings) {
  thread_ = std::thread([this] { run(); });
}

CaptureSession::~CaptureSession() {
  quit_ = true;
  if (thread_.joinable()) thread_.join();
}

std::string CaptureSession::wait_open() {
  std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait(lock, [this] { return settled_; });
  return error_;
}

std::string CaptureSession::error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

Image CaptureSession::picture() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return picture_;
}

std::string CaptureSession::record(const std::string& path, MovieFormat format, std::int64_t frames) {
  if (!open_) return "The camera isn't ready";
  if (recording_) return "Already recording";
  MovieSettings movie;
  movie.format = format;
  movie.width = settings_.width;
  movie.height = settings_.height;
  movie.rate = settings_.rate;
  movie.sound = has_sound_;
  movie.sample_rate = settings_.audio_rate;
  movie.sample_bits = settings_.audio_bits;
  std::unique_ptr<MovieWriter> writer;
  try {
    writer = std::make_unique<MovieWriter>(path, movie);
  } catch (const std::exception& e) {
    return e.what();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  next_writer_ = std::move(writer);
  next_frames_ = frames;
  result_.clear();
  recorded_ = 0;
  stop_requested_ = false;
  recording_ = true;
  return {};
}

std::string CaptureSession::stop() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (next_writer_) {
    // No picture came in since record(): there's nothing to keep.
    next_writer_.reset();
    recording_ = false;
    return "Nothing was recorded";
  }
  stop_requested_ = true;
  changed_.wait(lock, [this] { return !recording_.load(); });
  return result_;
}

void CaptureSession::run() {
  Reader in;
  const auto settle = [this](const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    settled_ = true;
    error_ = error;
    changed_.notify_all();
  };
  try {
    in.open(input_, &quit_, settings_.audio_rate);
  } catch (const std::exception& e) {
    settle(e.what());
    return;
  }
  has_sound_ = in.astream >= 0;
  open_ = true;
  settle({});

  const ProjectSettings& s = settings_;
  const AVRational vbase = in.format->streams[in.vstream]->time_base;
  const AVRational frame_base{s.rate.den, s.rate.num};

  // Sound read but not yet written. Its first sample plays at sound_start
  // seconds; later sound is taken to follow on without gaps.
  std::vector<float> sound;
  double sound_start = 0;

  // The recording in progress, if any.
  std::unique_ptr<MovieWriter> writer;
  std::int64_t limit = 0;
  std::int64_t start_pts = 0;  // of the first picture, in vbase
  double start_seconds = 0;
  bool align = false;  // the sound still has to be lined up with start_seconds
  Image held;          // the newest picture, not yet written
  std::int64_t frames = 0;
  std::int64_t sound_written = 0;  // sound frames handed to the writer
  bool finishing = false;
  auto deadline = std::chrono::steady_clock::now();

  const auto line_up_sound = [&] {
    if (sound.empty()) return;  // the next sound to come in lines up instead
    const auto offset = static_cast<std::int64_t>(
        std::llround((start_seconds - sound_start) * s.audio_rate));
    if (offset >= 0) {
      const std::size_t drop = std::min<std::size_t>(sound.size(), static_cast<std::size_t>(offset) * 2);
      sound.erase(sound.begin(), sound.begin() + static_cast<std::ptrdiff_t>(drop));
      if (sound.empty()) return;  // all of it came before the recording
    } else {
      sound.insert(sound.begin(), static_cast<std::size_t>(-offset) * 2, 0.0f);
    }
    sound_start = start_seconds;
    align = false;
  };
  // Writes sound up to the end of the pictures written so far.
  const auto flush_sound = [&] {
    if (!has_sound_ || align) return;
    const std::int64_t until = frame_to_sample(s, frames);
    const auto count = std::min<std::int64_t>(static_cast<std::int64_t>(sound.size() / 2), until - sound_written);
    if (count <= 0) return;
    writer->write_sound(sound.data(), static_cast<std::size_t>(count));
    sound.erase(sound.begin(), sound.begin() + static_cast<std::ptrdiff_t>(count * 2));
    sound_written += count;
  };
  const auto write_frame = [&] {
    writer->write_picture(held);
    ++frames;
    recorded_ = frames;
    flush_sound();
  };
  const auto end_recording = [&](const std::string& error) {
    writer.reset();  // an unfinished movie deletes itself
    sound.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = error;
    recording_ = false;
    changed_.notify_all();
  };
  // Finishes once the sound has caught up with the pictures, or it's been
  // waited for long enough.
  const auto try_finish = [&](bool now) {
    const std::int64_t until = frame_to_sample(s, frames);
    const bool caught_up = !has_sound_ || (!align && static_cast<std::int64_t>(sound.size() / 2) >= until - sound_written);
    if (!caught_up && !now && std::chrono::steady_clock::now() < deadline) return;
    flush_sound();
    if (has_sound_ && sound_written < until) {
      const std::vector<float> silence(static_cast<std::size_t>(until - sound_written) * 2, 0.0f);
      writer->write_sound(silence.data(), silence.size() / 2);
    }
    writer->finish();
    end_recording({});
  };
  const auto begin_finish = [&] {
    if (limit == 0 || frames < limit) write_frame();  // the newest picture
    finishing = true;
    deadline = std::chrono::steady_clock::now() + kSoundGrace;
  };

  const auto on_picture = [&](const AVFrame* f) {
    Image fitted = fit_into(in.picture(f), s.width, s.height);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      picture_ = fitted;
      if (!writer && next_writer_) {
        writer = std::move(next_writer_);
        limit = next_frames_;
        start_pts = f->best_effort_timestamp;
        start_seconds = start_pts * av_q2d(vbase);
        held = fitted;
        frames = sound_written = 0;
        finishing = false;
        align = true;
        line_up_sound();
        ++pictures_;
        return;
      }
    }
    ++pictures_;
    if (!writer || finishing) return;
    // Every movie frame that starts before this picture shows the one before.
    const std::int64_t since = f->best_effort_timestamp - start_pts;
    while ((limit == 0 || frames < limit) && av_compare_ts(frames, frame_base, since, vbase) < 0) {
      write_frame();
    }
    held = std::move(fitted);
    if (limit > 0 && frames >= limit) begin_finish();
  };
  const auto on_sound = [&](const AVFrame* f) {
    if (sound.empty()) sound_start = f->best_effort_timestamp * av_q2d(in.format->streams[in.astream]->time_base);
    in.sound(f, sound);
    if (writer) {
      if (align) line_up_sound();
      if (!finishing) flush_sound();
      return;
    }
    // Keep only the last moment, for a recording to start with.
    const auto keep = static_cast<std::size_t>(kSoundKept * s.audio_rate) * 2;
    if (sound.size() > keep) {
      const std::size_t drop = sound.size() - keep;
      sound.erase(sound.begin(), sound.begin() + static_cast<std::ptrdiff_t>(drop));
      sound_start += static_cast<double>(drop / 2) / s.audio_rate;
    }
  };
  const auto decode = [&](AVCodecContext* decoder, bool picture) {
    if (avcodec_send_packet(decoder, in.packet) < 0) return;
    while (avcodec_receive_frame(decoder, in.frame) == 0) {
      if (picture) {
        on_picture(in.frame);
      } else {
        on_sound(in.frame);
      }
      av_frame_unref(in.frame);
    }
  };

  while (!quit_) {
    try {
      if (writer && !finishing && stop_requested_) begin_finish();
      if (writer && finishing) try_finish(false);
      const int r = av_read_frame(in.format, in.packet);
      if (r == AVERROR(EAGAIN)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      if (r < 0) {
        if (quit_) break;
        // The input ended or failed: keep what was recorded.
        if (writer) {
          if (!finishing) begin_finish();
          try_finish(true);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (r != AVERROR_EOF) error_ = "The camera stopped: " + av_error(r);
        break;
      }
      if (in.packet->stream_index == in.vstream) {
        decode(in.video, true);
      } else if (in.packet->stream_index == in.astream) {
        decode(in.audio, false);
      }
      av_packet_unref(in.packet);
    } catch (const std::exception& e) {
      av_packet_unref(in.packet);
      if (writer) end_recording(e.what());
    }
  }
  open_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  next_writer_.reset();
  if (recording_) {
    // Quitting: the movie is dropped.
    recording_ = false;
    if (result_.empty()) result_ = "Recording stopped";
    changed_.notify_all();
  }
}

}  // namespace mpp
