#include "audio_player.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct AudioPlayer::Device {
  ma_device device{};
  bool open = false;
  int sample_rate = 0;
  // Read by the audio thread; replaced only while the device is stopped.
  std::shared_ptr<const mpp::AudioBuffer> sound;
  std::atomic<std::size_t> cursor{0};   // next sample frame to play
  std::atomic<std::size_t> played{0};   // sample frames handed out since play()
  std::atomic<bool> running{false};

  static void callback(ma_device* d, void* out, const void*, ma_uint32 frames) {
    auto* self = static_cast<Device*>(d->pUserData);
    float* dst = static_cast<float*>(out);
    const mpp::AudioBuffer* sound = self->sound.get();
    std::size_t at = self->cursor.load();
    const std::size_t total = sound ? sound->frames() : 0;
    const std::size_t n = at < total ? std::min<std::size_t>(frames, total - at) : 0;
    if (n > 0) std::memcpy(dst, sound->samples.data() + at * 2, n * 2 * sizeof(float));
    std::memset(dst + n * 2, 0, (frames - n) * 2 * sizeof(float));
    self->cursor = at + n;
    self->played += frames;
  }

  bool start(int rate) {
    if (open && sample_rate != rate) close();
    if (!open) {
      ma_device_config config = ma_device_config_init(ma_device_type_playback);
      config.playback.format = ma_format_f32;
      config.playback.channels = 2;
      config.sampleRate = static_cast<ma_uint32>(rate);
      config.dataCallback = callback;
      config.pUserData = this;
      if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) return false;
      open = true;
      sample_rate = rate;
    }
    if (ma_device_start(&device) != MA_SUCCESS) return false;
    running = true;
    return true;
  }

  void halt() {
    if (open && running) ma_device_stop(&device);
    running = false;
  }

  void close() {
    halt();
    if (open) ma_device_uninit(&device);
    open = false;
  }

  ~Device() { close(); }
};

AudioPlayer::AudioPlayer() : device_(std::make_unique<Device>()) {}

AudioPlayer::~AudioPlayer() = default;

bool AudioPlayer::play(std::shared_ptr<const mpp::AudioBuffer> sound, std::size_t start) {
  stop();
  if (!enabled_ || !sound || sound->sample_rate <= 0 || start >= sound->frames()) return false;
  device_->sound = std::move(sound);
  device_->cursor = start;
  device_->played = 0;
  if (!device_->start(device_->sound->sample_rate)) {
    device_->sound.reset();
    return false;
  }
  return true;
}

void AudioPlayer::stop() {
  device_->halt();
  device_->sound.reset();
}

bool AudioPlayer::playing() const { return device_->running.load(); }

double AudioPlayer::position() const {
  return device_->sample_rate > 0
             ? static_cast<double>(device_->played.load()) / device_->sample_rate
             : 0.0;
}
