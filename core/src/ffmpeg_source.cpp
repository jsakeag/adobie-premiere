#include "mpp/ffmpeg_source.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace mpp {

namespace {

std::string av_error(int code) {
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, text, sizeof(text));
  return text;
}

[[noreturn]] void fail(const std::string& path, const std::string& what) {
  throw std::runtime_error("Could not read '" + path + "': " + what);
}

// Snaps rates that files store approximately (2997/100) to the exact NTSC
// fractions.
Rate rate_from(AVRational r) {
  if (r.num <= 0 || r.den <= 0) return {30, 1, false};
  const double fps = av_q2d(r);
  for (int base : {24, 30, 60}) {
    if (std::abs(fps - base * 1000.0 / 1001) < 0.005) return {base * 1000, 1001, false};
  }
  AVRational reduced{};
  av_reduce(&reduced.num, &reduced.den, r.num, r.den, 1 << 20);
  return {reduced.num, reduced.den, false};
}

struct FormatCloser {
  void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;

struct CodecCloser {
  void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;

struct FrameFreer {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameFreer>;

struct PacketFreer {
  void operator()(AVPacket* p) const { av_packet_free(&p); }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketFreer>;

FormatPtr open_format(const std::string& path) {
  AVFormatContext* raw = nullptr;
  int r = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
  if (r < 0) fail(path, av_error(r));
  FormatPtr fmt(raw);
  r = avformat_find_stream_info(fmt.get(), nullptr);
  if (r < 0) fail(path, av_error(r));
  return fmt;
}

CodecPtr open_decoder(const std::string& path, AVStream* stream) {
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) fail(path, "no decoder for its codec");
  CodecPtr ctx(avcodec_alloc_context3(codec));
  if (!ctx) fail(path, "out of memory");
  int r = avcodec_parameters_to_context(ctx.get(), stream->codecpar);
  if (r < 0) fail(path, av_error(r));
  ctx->pkt_timebase = stream->time_base;
  r = avcodec_open2(ctx.get(), codec, nullptr);
  if (r < 0) fail(path, av_error(r));
  return ctx;
}

}  // namespace

struct FfmpegSource::Impl {
  std::string path;
  MediaInfo info;
  FormatPtr fmt;
  int vstream = -1;
  int astream = -1;
  CodecPtr vdec;
  SwsContext* sws = nullptr;
  PacketPtr packet{av_packet_alloc()};
  FramePtr decoded{av_frame_alloc()};
  // The last frame decoded at or before the last request, and the first
  // one past it, kept so reading on in order needs no seek.
  FramePtr held{av_frame_alloc()};
  std::int64_t held_index = -1;
  FramePtr pending{av_frame_alloc()};
  std::int64_t pending_index = -1;
  bool decoder_drained = false;  // flushed to the end; needs a seek to reuse
  bool positioned = false;       // the decoder has been seeked at least once
  std::int64_t start_pts = 0;    // the video stream's first timestamp

  ~Impl() { sws_freeContext(sws); }

  std::int64_t index_of(const AVFrame* f) const {
    std::int64_t ts = f->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = f->pts;
    if (ts == AV_NOPTS_VALUE) return held_index + 1;
    const AVStream* s = fmt->streams[vstream];
    const double t = (ts - start_pts) * av_q2d(s->time_base);
    return std::llround(t * info.rate.fps());
  }

  void seek(std::int64_t index) {
    const AVStream* s = fmt->streams[vstream];
    const double seconds = info.rate.seconds(index);
    const std::int64_t ts =
        start_pts + static_cast<std::int64_t>(std::floor(seconds / av_q2d(s->time_base)));
    const int r = av_seek_frame(fmt.get(), vstream, ts, AVSEEK_FLAG_BACKWARD);
    if (r < 0) {
      // Some formats only seek by bytes; start over from the beginning.
      av_seek_frame(fmt.get(), vstream, start_pts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
    }
    avcodec_flush_buffers(vdec.get());
    av_frame_unref(held.get());
    av_frame_unref(pending.get());
    held_index = pending_index = -1;
    decoder_drained = false;
    positioned = true;
  }

  // Moves `f` into held or pending relative to the target. Returns true
  // once the target is settled.
  bool place(AVFrame* f, std::int64_t index, std::int64_t target) {
    if (index <= target) {
      av_frame_unref(held.get());
      av_frame_move_ref(held.get(), f);
      held_index = index;
      return index == target;
    }
    av_frame_unref(pending.get());
    av_frame_move_ref(pending.get(), f);
    pending_index = index;
    return true;
  }

  // Decodes until `held` is the frame showing at `target`.
  void decode_to(std::int64_t target) {
    if (pending_index >= 0 && pending_index <= target) {
      place(pending.get(), pending_index, target);
      pending_index = -1;
    }
    if (held_index == target || pending_index > target) return;
    while (!decoder_drained) {
      int r = avcodec_receive_frame(vdec.get(), decoded.get());
      if (r == 0) {
        if (place(decoded.get(), index_of(decoded.get()), target)) return;
        continue;
      }
      if (r == AVERROR_EOF) {
        decoder_drained = true;
        return;
      }
      if (r != AVERROR(EAGAIN)) fail(path, av_error(r));
      r = av_read_frame(fmt.get(), packet.get());
      if (r < 0) {
        avcodec_send_packet(vdec.get(), nullptr);  // drain what's buffered
        continue;
      }
      if (packet->stream_index == vstream) {
        r = avcodec_send_packet(vdec.get(), packet.get());
        if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_INVALIDDATA) {
          av_packet_unref(packet.get());
          fail(path, av_error(r));
        }
      }
      av_packet_unref(packet.get());
    }
  }

  Image convert(const AVFrame* f) {
    const auto format = static_cast<AVPixelFormat>(f->format);
    sws = sws_getCachedContext(sws, f->width, f->height, format, f->width,
                               f->height, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                               nullptr, nullptr, nullptr);
    if (!sws) fail(path, "can't convert its pixel format");
    Image image(f->width, f->height);
    std::uint8_t* dst[4] = {image.data(), nullptr, nullptr, nullptr};
    const int stride[4] = {f->width * Image::kChannels, 0, 0, 0};
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride);
    // Formats without alpha come back opaque already; this keeps it so if
    // a converter leaves the channel untouched.
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    if (desc && !(desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
      for (std::size_t i = 3; i < image.size_bytes(); i += 4) image.data()[i] = 255;
    }
    return image;
  }
};

FfmpegSource::FfmpegSource(const std::string& path) : impl_(std::make_unique<Impl>()) {
  Impl& m = *impl_;
  m.path = path;
  m.fmt = open_format(path);
  m.vstream = av_find_best_stream(m.fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  m.astream = av_find_best_stream(m.fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  // Cover art in a sound file shows up as a one-frame video stream.
  if (m.vstream >= 0 &&
      (m.fmt->streams[m.vstream]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
    m.vstream = -1;
  }
  if (m.vstream < 0 && m.astream < 0) fail(path, "no video or audio");

  MediaInfo& info = m.info;
  info.kind = MediaKind::Movie;
  if (m.astream >= 0) {
    const AVCodecParameters* a = m.fmt->streams[m.astream]->codecpar;
    info.has_audio = true;
    info.sample_rate = a->sample_rate;
    info.channels = a->ch_layout.nb_channels;
  }
  if (m.vstream >= 0) {
    AVStream* s = m.fmt->streams[m.vstream];
    info.has_video = true;
    info.width = s->codecpar->width;
    info.height = s->codecpar->height;
    info.rate = rate_from(s->avg_frame_rate.num > 0 ? s->avg_frame_rate : s->r_frame_rate);
    m.start_pts = s->start_time == AV_NOPTS_VALUE ? 0 : s->start_time;
    if (s->nb_frames > 0) {
      info.frames = s->nb_frames;
    } else if (s->duration != AV_NOPTS_VALUE) {
      info.frames = std::llround(s->duration * av_q2d(s->time_base) * info.rate.fps());
    } else if (m.fmt->duration != AV_NOPTS_VALUE) {
      info.frames = std::llround(m.fmt->duration / double(AV_TIME_BASE) * info.rate.fps());
    }
    info.frames = std::max<std::int64_t>(info.frames, 1);
    m.vdec = open_decoder(path, s);
  } else {
    info.rate = {info.sample_rate, 1, false};
    const AVStream* s = m.fmt->streams[m.astream];
    double seconds = 0;
    if (s->duration != AV_NOPTS_VALUE) {
      seconds = s->duration * av_q2d(s->time_base);
    } else if (m.fmt->duration != AV_NOPTS_VALUE) {
      seconds = m.fmt->duration / double(AV_TIME_BASE);
    }
    info.frames = std::max<std::int64_t>(std::llround(seconds * info.sample_rate), 1);
  }
}

FfmpegSource::~FfmpegSource() = default;

const MediaInfo& FfmpegSource::info() const { return impl_->info; }

Image FfmpegSource::frame(std::int64_t index) {
  Impl& m = *impl_;
  if (m.vstream < 0) return Image(1, 1, kBlack);
  index = std::clamp<std::int64_t>(index, 0, m.info.frames - 1);
  const bool ahead = m.positioned && !m.decoder_drained && index >= m.held_index &&
                     index - m.held_index <= 2 * m.info.rate.nominal();
  if (!(m.positioned && index == m.held_index) && !ahead) m.seek(index);
  m.decode_to(index);
  if (m.held_index < 0 && m.pending_index >= 0) {
    // The first frame starts after the target: show it.
    return m.convert(m.pending.get());
  }
  if (m.held_index < 0) {
    // Nothing at or before the target (seek past the end): try the start.
    m.seek(0);
    m.decode_to(0);
    if (m.held_index < 0 && m.pending_index < 0) {
      return Image(m.info.width, m.info.height, kBlack);
    }
    return m.convert(m.held_index >= 0 ? m.held.get() : m.pending.get());
  }
  return m.convert(m.held.get());
}

AudioBuffer FfmpegSource::audio(int sample_rate) {
  const Impl& m = *impl_;
  AudioBuffer out;
  out.sample_rate = sample_rate;
  if (m.astream < 0) return out;

  // A second reader, so decoding sound leaves the picture decoder alone.
  FormatPtr fmt = open_format(m.path);
  AVStream* stream = fmt->streams[m.astream];
  CodecPtr dec = open_decoder(m.path, stream);

  // Mono stays mono through the resampler and is copied to both sides
  // here; letting it upmix would pan it to the centre at -3 dB.
  const int out_channels = dec->ch_layout.nb_channels == 1 ? 1 : 2;
  SwrContext* swr = nullptr;
  AVChannelLayout out_layout{};
  av_channel_layout_default(&out_layout, out_channels);
  int r = swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, sample_rate,
                              &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0,
                              nullptr);
  if (r < 0 || swr_init(swr) < 0) {
    swr_free(&swr);
    fail(m.path, "can't convert its sound");
  }
  struct SwrFreer {
    SwrContext** s;
    ~SwrFreer() { swr_free(s); }
  } swr_guard{&swr};

  PacketPtr packet(av_packet_alloc());
  FramePtr frame(av_frame_alloc());
  std::vector<float> converted;
  const auto append = [&](const AVFrame* f) {
    const int in_count = f ? f->nb_samples : 0;
    const int capacity = swr_get_out_samples(swr, in_count);
    if (capacity <= 0) return;
    converted.resize(static_cast<std::size_t>(capacity) * out_channels);
    std::uint8_t* dst[1] = {reinterpret_cast<std::uint8_t*>(converted.data())};
    const int got = swr_convert(swr, dst, capacity,
                                f ? const_cast<const std::uint8_t**>(f->extended_data) : nullptr,
                                in_count);
    if (got <= 0) return;
    if (out_channels == 2) {
      out.samples.insert(out.samples.end(), converted.begin(), converted.begin() + got * 2);
      return;
    }
    for (int i = 0; i < got; ++i) {
      out.samples.push_back(converted[i]);
      out.samples.push_back(converted[i]);
    }
  };
  const auto drain = [&]() {
    while ((r = avcodec_receive_frame(dec.get(), frame.get())) == 0) {
      append(frame.get());
      av_frame_unref(frame.get());
    }
  };
  while (av_read_frame(fmt.get(), packet.get()) >= 0) {
    if (packet->stream_index == m.astream) {
      avcodec_send_packet(dec.get(), packet.get());
      drain();
    }
    av_packet_unref(packet.get());
  }
  avcodec_send_packet(dec.get(), nullptr);
  drain();
  append(nullptr);  // what the resampler still holds
  return out;
}

}  // namespace mpp
