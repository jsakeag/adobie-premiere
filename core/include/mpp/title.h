#pragma once

#include <string>
#include <vector>

#include "mpp/image.h"

namespace mpp {

// A title is drawn fresh at whatever size the project asks for, so every
// position and size here is a fraction of the frame: x across its width, y
// and sizes down its height.

enum class TitleFont { Regular, Bold };
enum class TextAlign { Left, Center, Right };

struct TitleText {
  std::string text;          // UTF-8; "\n" starts a new line
  TitleFont font = TitleFont::Bold;
  double size = 0.12;        // line height, as a fraction of the frame height
  Color color = kWhite;
  double x = 0.5;            // left edge, centre or right edge, by `align`
  double y = 0.5;            // the vertical centre of the block of lines
  TextAlign align = TextAlign::Center;
  bool shadow = true;        // a soft black shadow down and to the right

  bool operator==(const TitleText& o) const {
    return text == o.text && font == o.font && size == o.size && color == o.color && x == o.x &&
           y == o.y && align == o.align && shadow == o.shadow;
  }
};

enum class ShapeKind { Rectangle, Oval };

struct TitleShape {
  ShapeKind kind = ShapeKind::Rectangle;
  double x = 0.5, y = 0.5;       // centre
  double width = 0.5, height = 0.2;
  Color color = kWhite;
  bool filled = true;
  double line = 0.01;            // outline thickness when not filled

  bool operator==(const TitleShape& o) const {
    return kind == o.kind && x == o.x && y == o.y && width == o.width && height == o.height &&
           color == o.color && filled == o.filled && line == o.line;
  }
};

struct TitleSpec {
  Color background = kTransparent;  // clear lets it go on Super over a picture
  std::vector<TitleShape> shapes;   // drawn first, in order
  std::vector<TitleText> texts;     // then the text, in order

  bool operator==(const TitleSpec& o) const {
    return background == o.background && shapes == o.shapes && texts == o.texts;
  }
  bool operator!=(const TitleSpec& o) const { return !(*this == o); }
};

// The title at a frame size, anti-aliased. Throws std::runtime_error if
// the fonts can't be loaded. Recent results are kept, so drawing the same
// title at the same size again is cheap; safe from several threads.
Image render_title(const TitleSpec& title, int width, int height);

// A string that differs whenever two titles would draw differently (for
// telling when a thumbnail is out of date).
std::string title_signature(const TitleSpec& title);

// Where a text item lands in a frame, in pixels (for the Title window to
// outline and hit-test it).
Rect text_bounds(const TitleText& text, int width, int height);
Rect shape_bounds(const TitleShape& shape, int width, int height);

}  // namespace mpp
