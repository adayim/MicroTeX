#include "render.h"

#include "atom/atom.h"
#include "box/box_single.h"
#include "core/split.h"
#include "env/env.h"

using namespace std;
using namespace microtex;

namespace microtex {

struct RenderData {
  sptr<Box> root;
  float textSize;
  float fixedScale;
  color fg;
  bool isSplit;
};

}  // namespace microtex

Render::Render(const sptr<Box>& box, float textSize, bool isSplit) {
  _data = new RenderData{box, textSize, textSize / Env::fixedTextSize(), black, isSplit};
}

Render::~Render() {
  delete _data;
}

float Render::getTextSize() const {
  return _data->textSize;
}

int Render::getHeight() const {
  auto box = _data->root;
  return (int)(box->vlen() * _data->fixedScale);
}

int Render::getDepth() const {
  return (int)(_data->root->_depth * _data->fixedScale);
}

int Render::getWidth() const {
  return (int)(_data->root->_width * _data->fixedScale);
}

float Render::getBaseline() const {
  auto box = _data->root;
  const auto len = box->vlen();
  // Nothing drawn has no height to take a share of.
  return len > 0 ? box->_height / len : 0.f;
}

bool Render::isSplit() const {
  return _data->isSplit;
}

void Render::setTextSize(float textSize) {
  _data->textSize = textSize;
  _data->fixedScale = textSize / Env::fixedTextSize();
}

void Render::setForeground(color fg) {
  _data->fg = fg;
}

void Render::draw(Graphics2D& g2, int x, int y) {
  color old = g2.getColor();
  auto fixedScale = _data->fixedScale;
  auto box = _data->root;

  g2.setColor(isTransparent(_data->fg) ? black : _data->fg);
  g2.translate(x, y);
  g2.scale(fixedScale, fixedScale);

  // draw formula box
  box->draw(g2, 0, box->_height);

  // restore
  g2.scale(1.f / fixedScale, 1.f / fixedScale);
  g2.translate(-x, -y);
  g2.setColor(old);
}
