#ifndef GRIDMICROTEX_FRONT_HOOKS_H
#define GRIDMICROTEX_FRONT_HOOKS_H

#include <functional>
#include <string>
#include <vector>

namespace microtex::front {

/**
 * What the host makes of `\includegraphics[options]{path}`: the LaTeX that
 * stands for the image, read as a piece of input of its own. The engine
 * never reads a file. The host finds it -- in `dirs`, those the last
 * \graphicspath named, when it is not where the path says -- sizes it, and
 * returns the command that reserves its box (\gmgraphics, in the R
 * package). An empty result draws the file's name, as does having no
 * resolver at all. Whatever the host throws is not caught on the way out.
 *
 * It is called while the tree is lowered, so it sees an \includegraphics a
 * macro produced, and never one in a comment.
 */
using ImageResolver = std::function<std::string(
  const std::string& path, const std::string& options, const std::vector<std::string>& dirs)>;

/** The resolver for the parses that follow; an empty one draws names. */
void setImageResolver(ImageResolver resolver);

const ImageResolver& imageResolver();

}  // namespace microtex::front

#endif
