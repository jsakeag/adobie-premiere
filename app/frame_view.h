#pragma once

#include <nanogui/widget.h>

#include "mpp/image.h"

// A NanoVG image owned by a widget. NanoGUI destroys the NanoVG context
// before child widgets, so this never frees in its destructor: owners call
// release() while the context is alive, and images left at exit go with the
// context.
class NvgImage {
 public:
  // Uploads pixels, reusing the image when the size and flags match.
  void set(NVGcontext* ctx, const mpp::Image& pixels, int flags = 0);
  void release(NVGcontext* ctx);
  int id() const { return id_; }
  int width() const { return width_; }
  int height() const { return height_; }
  bool valid() const { return id_ > 0; }

 private:
  int id_ = 0;
  int width_ = 0;
  int height_ = 0;
  int flags_ = 0;
};

// Draws one video frame scaled to fit, on black, the way the Clip and
// Preview windows show pictures. Enlargements use nearest-neighbour so
// small frames stay crisp.
class FrameView : public nanogui::Widget {
 public:
  explicit FrameView(nanogui::Widget* parent);

  // Copies the pixels; they upload on the next draw.
  void set_frame(const mpp::Image& frame);
  void clear();
  const mpp::Image& frame() const { return frame_; }

  // Where the frame is drawn, in window coordinates (x, y, width, height).
  nanogui::Vector4f frame_rect() const;

  void draw(NVGcontext* ctx) override;

 private:
  mpp::Image frame_;
  bool dirty_ = false;
  NvgImage image_;
};
