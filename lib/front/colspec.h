#ifndef GRIDMICROTEX_FRONT_COLSPEC_H
#define GRIDMICROTEX_FRONT_COLSPEC_H

#include <map>
#include <set>
#include <string>
#include <utility>

namespace microtex::front {

/**
 * dcolumn's `D{in}{out}{places}` columns of a column specification: for each, the index of
 * the column and the separator it is aligned on (`in`, as typed in a cell) and drawn as
 * (`out`, TeX). A specification with a repeated group (`*{3}{D{.}{.}{2}}`) is not read.
 * `numberColumns`, when given, takes the indices of siunitx's S columns.
 */
inline std::map<int, std::pair<std::string, std::string>> decimalColumns(const std::string& spec,
                                                                         std::set<int>* numberColumns = nullptr) {
  std::map<int, std::pair<std::string, std::string>> out;
  if ((spec.find('D') == std::string::npos && spec.find('S') == std::string::npos) ||
      spec.find('*') != std::string::npos) {
    return out;
  }
  // The index of the `}` closing the `{` at `i`, and the text between.
  const auto group = [&](std::size_t& i) {
    std::string text;
    int depth = 0;
    for (; i < spec.size(); i++) {
      if (spec[i] == '{' && depth++ == 0) continue;
      if (spec[i] == '}' && --depth == 0) {
        i++;
        return text;
      }
      text += spec[i];
    }
    return text;
  };
  int column = 0;
  for (std::size_t i = 0; i < spec.size();) {
    const char c = spec[i++];
    if (c == '@' || c == '!' || c == '>' || c == '<') {
      if (i < spec.size() && spec[i] == '{') group(i);
    } else if (c == 'p' || c == 'm' || c == 'b') {
      column++;
      if (i < spec.size() && spec[i] == '{') group(i);
    } else if (c == 'D') {
      std::string in, outSep;
      if (i < spec.size() && spec[i] == '{') in = group(i);
      if (i < spec.size() && spec[i] == '{') outSep = group(i);
      if (i < spec.size() && spec[i] == '{') group(i);
      out[column++] = {in, outSep};
    } else if (c == 'l' || c == 'c' || c == 'r' || c == 'X' || c == 'Q' || c == 'S') {
      if (c == 'S' && numberColumns != nullptr) numberColumns->insert(column);
      column++;
      if (i < spec.size() && spec[i] == '[') {
        const std::size_t end = spec.find(']', i);
        i = end == std::string::npos ? spec.size() : end + 1;
      }
    }
  }
  return out;
}

}  // namespace microtex::front

#endif  // GRIDMICROTEX_FRONT_COLSPEC_H
