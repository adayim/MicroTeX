#ifndef GRIDMICROTEX_FRONT_PRELUDE_H
#define GRIDMICROTEX_FRONT_PRELUDE_H

#include <string_view>

namespace microtex::front {

/** The commands and environments the engine defines in LaTeX, as LaTeX
 *  source: read once by the expander, shared by every parse. */
std::string_view preludeSource();

/** Commands a document may define itself: definitions of its own win over
 *  these, with no complaint that the name exists. */
std::string_view preludeSoftSource();

}  // namespace microtex::front

#endif
