#ifndef GRIDMICROTEX_FRONT_PACKAGES_H
#define GRIDMICROTEX_FRONT_PACKAGES_H

#include <cstdint>
#include <string>
#include <vector>

namespace microtex::front {

/**
 * The engine's commands that are not written in C++ are LaTeX files: the
 * format (`prelude.tex`: LaTeX's own definitions, then a \RequirePackage for
 * each package every input starts with) and the packages, which a document
 * asks for with \usepackage and which are read when they are first used.
 *
 * The engine reads no file. The host hands over their text, as it answers
 * for images and fonts (hooks.h), and what is built from it is rebuilt when
 * any of it changes (packageGeneration()).
 */

/** The format. Replaces the one before it. */
void setPrelude(std::string source);
const std::string& preludeText();

/** A package by its name, `amsmath` for amsmath.sty. Registering a name
 *  again replaces the package. */
void registerPackage(const std::string& name, std::string source);

/** The text of a package, or nullptr when there is none by that name. */
const std::string* packageSource(const std::string& name);

std::vector<std::string> packageNames();

/** Changes whenever the format or a package does. */
std::uint64_t packageGeneration();

}  // namespace microtex::front

#endif
