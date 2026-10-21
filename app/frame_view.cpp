#include "frame_view.h"

#include <algorithm>
#include <cmath>

#include <nanogui/opengl.h>

void NvgImage::set(NVGcontext* ctx, const mpp::Image& pixels, int flags) {
  if (pixels.empty()) {
    release(ctx);
    return;
  }
  if (id_ > 0 && width_ == pixels.width() && height_ == pixels.height() && flags_ == flags) {
    nvgUpdateImage(ctx, id_, pixels.data());
    return;
  }
  release(ctx);
  id_ = nvgCreateImageRGBA(ctx, pixels.width(), pixels.height(), flags, pixels.data());
  width_ = pixels.width();
  height_ = pixels.height();
  flags_ = flags;
}

void NvgImage::release(NVGcontext* ctx) {
  if (id_ > 0) nvgDeleteImage(ctx, id_);
  id_ = width_ = height_ = 0;
}

FrameView::FrameView(nanogui::Widget* parent) : nanogui::Widget(parent) {}

void FrameView::set_frame(const mpp::Image& frame) {
  frame_ = frame;
  dirty_ = true;
}

void FrameView::clear() {
  frame_ = mpp::Image();
  dirty_ = true;
}

nanogui::Vector4f FrameView::frame_rect() const {
  if (frame_.empty()) return {0, 0, 0, 0};
  const float scale = std::min(static_cast<float>(m_size.x()) / frame_.width(),
                               static_cast<float>(m_size.y()) / frame_.height());
  const float w = std::round(frame_.width() * scale);
  const float h = std::round(frame_.height() * scale);
  return {std::round(m_pos.x() + (m_size.x() - w) / 2), std::round(m_pos.y() + (m_size.y() - h) / 2),
          w, h};
}

void FrameView::draw(NVGcontext* ctx) {
  nvgBeginPath(ctx);
  nvgRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
  nvgFillColor(ctx, nvgRGB(0, 0, 0));
  nvgFill(ctx);
  if (frame_.empty()) {
    if (dirty_) {
      image_.release(ctx);
      dirty_ = false;
    }
    return;
  }
  const nanogui::Vector4f r = frame_rect();
  const bool enlarged = r[2] >= frame_.width();
  if (dirty_) {
    image_.set(ctx, frame_, enlarged ? NVG_IMAGE_NEAREST : 0);
    dirty_ = false;
  }
  if (!image_.valid()) return;
  NVGpaint paint = nvgImagePattern(ctx, r[0], r[1], r[2], r[3], 0, image_.id(), 1.0f);
  nvgBeginPath(ctx);
  nvgRect(ctx, r[0], r[1], r[2], r[3]);
  nvgFillPaint(ctx, paint);
  nvgFill(ctx);
}
