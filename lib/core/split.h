#ifndef MICROTEX_SPLIT_H
#define MICROTEX_SPLIT_H

#include <stack>

#include "box/box.h"
#include "box/box_group.h"
#include "core/glue.h"

namespace microtex {

#ifdef HAVE_LOG

void printBox(const sptr<Box>& box);

#endif  // HAVE_LOG

class BoxSplitter {
public:
  struct Position {
    int _index;
    sptr<HBox> _box;

    Position(int index, const sptr<HBox>& box) : _index(index), _box(box) {}
  };

private:
  /**
   * Guard against unbounded recursion when descending a nested box tree.
   * Real formulas nest a handful of levels deep; anything beyond this is
   * pathological input and is left unsplit rather than risking the stack.
   */
  static constexpr int MAX_SPLIT_DEPTH = 32;

  static float canBreak(std::stack<Position>& stack, const sptr<HBox>& hbox, float width);

  /**
   * The break to take when the content overruns at child `index`.
   *
   * `cumWidth` and `width` are needed only for breaks that draw a box --
   * a hyphen widens the line beyond the content before it, so the choice
   * has to account for it here rather than discovering the overrun after
   * the line is built.
   */
  static int getBreakPosition(
    const sptr<HBox>& hb,
    int index,
    const float* cumWidth,
    float width
  );

  static std::pair<bool, sptr<Box>> split(const sptr<HBox>& hb, float width, float lineSpace);

  /**
   * Stretch a line's interword glue so it fills `width` exactly.
   *
   * Only glue reached through a chain of HBoxes is touched, so spaces
   * buried inside a fraction or a matrix cell keep the width their own
   * layout gave them. Returns false when the line has no glue to give,
   * or is already at or past the target, in which case it is left alone.
   */
  static bool justifyLine(const sptr<Box>& line, float width);

  /**
   * Every legal break of `hb`, as cumulative widths from the start of
   * the paragraph, in increasing order.
   *
   * canBreak() only reports a break when the content overruns the
   * ceiling it is handed, so this walks the ceiling down from the
   * natural width to enumerate them one at a time.
   */
  static std::vector<float> enumerateBreakWidths(const sptr<HBox>& hb);

  /**
   * Choose where to break so the paragraph as a whole reads best,
   * rather than filling each line in turn and never reconsidering.
   *
   * Returns one target width per line, always <= `width`. Feeding those
   * back to canBreak() is what makes this safe: it breaks at the last
   * position within the target, so a line can never exceed the measure
   * however the choice was made. An empty result means "no better plan
   * than greedy", and the caller falls back.
   */
  static std::vector<float> optimalLineTargets(const sptr<HBox>& hb, float width);

  /**
   * Split every over-wide row of a vertical box. Added so that content
   * carrying an explicit line break -- `\\`, array/gather/align, and the
   * itemize/enumerate environments, which all produce a VBox at the top
   * level -- still honours the requested width. Without it the splitter
   * would decline such a box wholesale and the text would simply overflow.
   */
  static std::pair<bool, sptr<Box>> split(
    const sptr<VBox>& vb,
    float width,
    float lineSpace,
    int depth
  );

  static std::pair<bool, sptr<Box>> splitDispatch(
    const sptr<Box>& box,
    float width,
    float lineSpace,
    int depth
  );

public:
  /**
   * Stretch interword glue so every line but the last of a wrapped
   * paragraph fills the measure exactly. Off by default: ragged right is
   * what R's own text drawing does, and justification without
   * hyphenation opens ugly gaps in a narrow column.
   *
   * A static toggle, matching how the engine already handles layout
   * modes (RowAtom::_breakEverywhere, MicroTeX::setRenderGlyphUsePath).
   * Set it under a scope guard so it cannot leak into the next parse.
   */
  static bool _justify;

  /**
   * Break paragraphs by total fit rather than first fit.
   *
   * The default splitter is greedy: it fills each line as far as it can
   * and never reconsiders, so pulling one word down early -- which would
   * improve every later line -- is not something it can see. With this
   * on, the breaks are chosen together to minimise the summed badness of
   * the paragraph, in the spirit of Knuth-Plass. The last line is free,
   * as it is in TeX.
   *
   * Off by default, and a static toggle guarded per parse, exactly like
   * _justify.
   */
  static bool _optimalBreak;

  static std::pair<bool, sptr<Box>> split(const sptr<Box>& box, float width, float lineSpace);

  /**
   * Put one finished line into visual order (Unicode rule L2).
   *
   * Public because a formula with no width limit never reaches the
   * splitter at all, and it still has to be ordered: `\text{<hebrew>}
   * \textbf{...}` is a single line whose two children belong the other
   * way round. The builder calls this directly in that case.
   *
   * A no-op unless the row carried levels, i.e. unless something in it
   * was right-to-left, so left-to-right layout is untouched.
   */
  static void reorderLine(const sptr<Box>& line);
};

}  // namespace microtex

#endif  // MICROTEX_SPLIT_H
