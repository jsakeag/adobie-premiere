#include "mpp/movie_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace mpp {

namespace {

std::string av_error(int code) {
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, text, sizeof(text));
  return text;
}

void check(int code, const std::string& what) {
  if (code < 0) throw std::runtime_error(what + ": " + av_error(code));
}

}  // namespace

const char* movie_format_name(MovieFormat format) {
  return format == MovieFormat::H264 ? "H.264 movie (.mp4)" : "QuickTime Animation, lossless (.mov)";
}

const char* movie_extension(MovieFormat format) {
  return format == MovieFormat::H264 ? ".mp4" : ".mov";
}

struct MovieWriter::Impl {
  std::string path;
  MovieSettings s;
  AVFormatContext* oc = nullptr;
  AVCodecContext* venc = nullptr;
  AVCodecContext* aenc = nullptr;
  AVStream* vst = nullptr;
  AVStream* ast = nullptr;
  SwsContext* sws = nullptr;
  AVFrame* vframe = nullptr;
  AVFrame* aframe = nullptr;
  AVPacket* packet = nullptr;
  std::int64_t vpts = 0;
  std::int64_t apts = 0;
  int audio_frame_size = 1024;
  std::vector<float> pending;  // interleaved stereo waiting for a full frame
  bool opened_file = false;
  bool finished = false;

  ~Impl() {
    if (oc && opened_file && !finished) {
      avio_closep(&oc->pb);
      std::remove(path.c_str());
    }
    sws_freeContext(sws);
    av_frame_free(&vframe);
    av_frame_free(&aframe);
    av_packet_free(&packet);
    avcodec_free_context(&venc);
    avcodec_free_context(&aenc);
    avformat_free_context(oc);
  }

  void drain(AVCodecContext* enc, AVStream* st) {
    while (true) {
      const int r = avcodec_receive_packet(enc, packet);
      if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return;
      check(r, "Encoding failed");
      // Every picture lasts one frame; without this the last one has no
      // length and the movie reads as a frame short.
      if (enc == venc && packet->duration == 0) packet->duration = 1;
      av_packet_rescale_ts(packet, enc->time_base, st->time_base);
      packet->stream_index = st->index;
      check(av_interleaved_write_frame(oc, packet), "Writing failed");
    }
  }

  void encode(AVCodecContext* enc, AVStream* st, const AVFrame* frame) {
    check(avcodec_send_frame(enc, frame), "Encoding failed");
    drain(enc, st);
  }

  void open_video() {
    const bool h264 = s.format == MovieFormat::H264;
    const AVCodec* codec = h264 ? avcodec_find_encoder_by_name("libx264") : nullptr;
    if (!codec) codec = avcodec_find_encoder(h264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_QTRLE);
    if (!codec) throw std::runtime_error("This FFmpeg has no encoder for the picture");
    venc = avcodec_alloc_context3(codec);
    venc->width = s.width;
    venc->height = s.height;
    venc->time_base = {s.rate.den, s.rate.num};
    venc->framerate = {s.rate.num, s.rate.den};
    venc->pix_fmt = h264 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_RGB24;
    if (h264) {
      venc->colorspace = AVCOL_SPC_SMPTE170M;
      venc->color_primaries = AVCOL_PRI_SMPTE170M;
      venc->color_trc = AVCOL_TRC_SMPTE170M;
      venc->color_range = AVCOL_RANGE_MPEG;
      av_opt_set(venc->priv_data, "crf", "18", 0);
      av_opt_set(venc->priv_data, "preset", "medium", 0);
    }
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(venc, codec, nullptr), "Could not start the picture encoder");
    vst = avformat_new_stream(oc, nullptr);
    vst->time_base = venc->time_base;
    check(avcodec_parameters_from_context(vst->codecpar, venc), "Could not set up the picture");

    vframe = av_frame_alloc();
    vframe->format = venc->pix_fmt;
    vframe->width = s.width;
    vframe->height = s.height;
    check(av_frame_get_buffer(vframe, 0), "Out of memory");
    sws = sws_getContext(s.width, s.height, AV_PIX_FMT_RGBA, s.width, s.height, venc->pix_fmt,
                         SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr,
                         nullptr);
    if (!sws) throw std::runtime_error("Could not convert pictures for the encoder");
  }

  void open_audio() {
    AVCodecID id = AV_CODEC_ID_AAC;
    AVSampleFormat format = AV_SAMPLE_FMT_FLTP;
    if (s.format == MovieFormat::Animation) {
      id = s.sample_bits <= 8 ? AV_CODEC_ID_PCM_U8 : AV_CODEC_ID_PCM_S16LE;
      format = s.sample_bits <= 8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;
    }
    const AVCodec* codec = avcodec_find_encoder(id);
    if (!codec) throw std::runtime_error("This FFmpeg has no encoder for the sound");
    aenc = avcodec_alloc_context3(codec);
    aenc->sample_rate = s.sample_rate;
    aenc->sample_fmt = format;
    av_channel_layout_default(&aenc->ch_layout, 2);
    aenc->time_base = {1, s.sample_rate};
    if (id == AV_CODEC_ID_AAC) aenc->bit_rate = s.sample_rate >= 32000 ? 128000 : 64000;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(aenc, codec, nullptr), "Could not start the sound encoder");
    ast = avformat_new_stream(oc, nullptr);
    ast->time_base = aenc->time_base;
    check(avcodec_parameters_from_context(ast->codecpar, aenc), "Could not set up the sound");
    audio_frame_size = aenc->frame_size > 0 ? aenc->frame_size : 1024;

    aframe = av_frame_alloc();
    aframe->format = format;
    aframe->sample_rate = s.sample_rate;
    av_channel_layout_default(&aframe->ch_layout, 2);
    aframe->nb_samples = audio_frame_size;
    check(av_frame_get_buffer(aframe, 0), "Out of memory");
  }

  void encode_sound(std::size_t frames) {
    check(av_frame_make_writable(aframe), "Out of memory");
    aframe->nb_samples = static_cast<int>(frames);
    const float* in = pending.data();
    switch (aenc->sample_fmt) {
      case AV_SAMPLE_FMT_FLTP: {
        float* left = reinterpret_cast<float*>(aframe->data[0]);
        float* right = reinterpret_cast<float*>(aframe->data[1]);
        for (std::size_t i = 0; i < frames; ++i) left[i] = in[2 * i], right[i] = in[2 * i + 1];
        break;
      }
      case AV_SAMPLE_FMT_S16: {
        auto* out = reinterpret_cast<std::int16_t*>(aframe->data[0]);
        for (std::size_t i = 0; i < 2 * frames; ++i) {
          out[i] = static_cast<std::int16_t>(std::clamp(std::lround(in[i] * 32768.0f), -32768L, 32767L));
        }
        break;
      }
      default: {
        std::uint8_t* out = aframe->data[0];
        for (std::size_t i = 0; i < 2 * frames; ++i) {
          out[i] = static_cast<std::uint8_t>(std::clamp(std::lround(in[i] * 128.0f) + 128, 0L, 255L));
        }
        break;
      }
    }
    aframe->pts = apts;
    apts += static_cast<std::int64_t>(frames);
    encode(aenc, ast, aframe);
    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(frames * 2));
  }
};

MovieWriter::MovieWriter(const std::string& path, const MovieSettings& settings)
    : impl_(std::make_unique<Impl>()) {
  Impl& m = *impl_;
  m.path = path;
  m.s = settings;
  if (settings.width < 2 || settings.height < 2) throw std::runtime_error("The frame is too small");
  if (settings.format == MovieFormat::H264 && (settings.width % 2 || settings.height % 2)) {
    throw std::runtime_error("H.264 needs an even frame width and height");
  }
  check(avformat_alloc_output_context2(&m.oc, nullptr,
                                       settings.format == MovieFormat::H264 ? "mp4" : "mov",
                                       path.c_str()),
        "Could not create '" + path + "'");
  m.packet = av_packet_alloc();
  m.open_video();
  if (settings.sound) m.open_audio();
  if (!(m.oc->oformat->flags & AVFMT_NOFILE)) {
    check(avio_open(&m.oc->pb, path.c_str(), AVIO_FLAG_WRITE), "Could not create '" + path + "'");
    m.opened_file = true;
  }
  check(avformat_write_header(m.oc, nullptr), "Could not write '" + path + "'");
}

MovieWriter::~MovieWriter() = default;

void MovieWriter::write_picture(const Image& frame) {
  Impl& m = *impl_;
  if (frame.width() != m.s.width || frame.height() != m.s.height) {
    throw std::invalid_argument("A picture for the movie is the wrong size");
  }
  check(av_frame_make_writable(m.vframe), "Out of memory");
  const std::uint8_t* src[4] = {frame.data(), nullptr, nullptr, nullptr};
  const int stride[4] = {frame.width() * Image::kChannels, 0, 0, 0};
  sws_scale(m.sws, src, stride, 0, frame.height(), m.vframe->data, m.vframe->linesize);
  m.vframe->pts = m.vpts++;
  m.encode(m.venc, m.vst, m.vframe);
}

void MovieWriter::write_sound(const float* stereo, std::size_t frames) {
  Impl& m = *impl_;
  if (!m.aenc) return;
  m.pending.insert(m.pending.end(), stereo, stereo + frames * 2);
  const std::size_t size = static_cast<std::size_t>(m.audio_frame_size);
  while (m.pending.size() / 2 >= size) m.encode_sound(size);
}

void MovieWriter::finish() {
  Impl& m = *impl_;
  if (m.finished) return;
  if (m.aenc && !m.pending.empty()) m.encode_sound(m.pending.size() / 2);
  m.encode(m.venc, m.vst, nullptr);
  if (m.aenc) m.encode(m.aenc, m.ast, nullptr);
  check(av_write_trailer(m.oc), "Could not finish '" + m.path + "'");
  if (m.opened_file) avio_closep(&m.oc->pb);
  m.finished = true;
}

}  // namespace mpp
