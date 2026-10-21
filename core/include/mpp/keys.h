#pragma once

#include <string>
#include <utility>
#include <vector>

#include "mpp/image.h"

namespace mpp {

// How a Super track clip lets the picture beneath show through.
enum class KeyType {
  None,         // opaque, ignoring any alpha channel
  Alpha,        // the clip's own alpha channel
  Chroma,       // a chosen colour drops out
  Luminance,    // dark areas drop out
  BlueScreen,   // pure blue drops out
  GreenScreen,  // pure green drops out
  Multiply,     // darkens what's beneath, like stacked slides
  Screen,       // lightens what's beneath, like two projectors
};

struct TransparencyKey {
  KeyType type = KeyType::None;
  Color color{0, 0, 255, 255};  // the colour Chroma drops out
  double similarity = 0.3;      // how near the colour, or how dark, still drops out (0..1)
  double blend = 0.1;           // how soft the edge between kept and dropped is (0..1)
  bool reverse = false;         // keep what would drop out, and drop the rest

  bool operator==(const TransparencyKey& o) const {
    return type == o.type && color == o.color && similarity == o.similarity &&
           blend == o.blend && reverse == o.reverse;
  }
  bool operator!=(const TransparencyKey& o) const { return !(*this == o); }
};

// Every key type with its name, in menu order.
const std::vector<std::pair<KeyType, std::string>>& key_types();
const char* key_name(KeyType type);
// Whether the type uses the colour, similarity and blend settings.
bool key_uses_color(KeyType type);
bool key_uses_similarity(KeyType type);

// The picture with its alpha set by the key: the colour and luminance keys
// compute it (keeping any transparency it already had, such as a
// letterbox), Alpha keeps the picture's own, and None, Multiply and Screen
// leave it alone; the renderer hands None an opaque picture, and Multiply
// and Screen act when composited.
Image keyed(const Image& picture, const TransparencyKey& key);

// Composites a Super clip's picture (already fitted to the frame, with any
// letterbox transparent) over `base`, keyed and faded to `opacity`.
void composite_superimpose(Image& base, const Image& picture, const TransparencyKey& key,
                           double opacity);

}  // namespace mpp
