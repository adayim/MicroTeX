#ifndef GRIDMICROTEX_FRONT_PRELUDE_H
#define GRIDMICROTEX_FRONT_PRELUDE_H

#include <string_view>

namespace microtex::front {

/** The commands and environments the engine defines in LaTeX, as LaTeX
 *  source: read once by the expander, shared by every parse. */
std::string_view preludeSource();

}  // namespace microtex::front

#endif
