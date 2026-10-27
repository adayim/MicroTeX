#include "front/tblr.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace microtex::front {

namespace {

/** What one input can ask for: more is a runaway, not a table. */
constexpr std::size_t kMaxItems = 4096;
constexpr int kMaxRepeat = 100;

std::string trimmed(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

/** The index of the `}` that closes the `{` at `from`, or the end. */
std::size_t closing(const std::string& s, std::size_t from) {
  int depth = 0;
  for (std::size_t i = from; i < s.size(); i++) {
    if (s[i] == '\\') {
      i++;
    } else if (s[i] == '{') {
      depth++;
    } else if (s[i] == '}' && --depth == 0) {
      return i;
    }
  }
  return s.size();
}

/** `s` split at the commas outside braces and brackets. */
std::vector<std::string> splitTop(const std::string& s) {
  std::vector<std::string> parts;
  std::string cur;
  int brace = 0, bracket = 0;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      cur += c;
      cur += s[++i];
      continue;
    }
    if (c == '{') brace++;
    if (c == '}' && brace > 0) brace--;
    if (c == '[') bracket++;
    if (c == ']' && bracket > 0) bracket--;
    if (c == ',' && brace == 0 && bracket == 0) {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
    if (parts.size() > kMaxItems) break;
  }
  parts.push_back(cur);
  return parts;
}

/** One `key{index}{index}={group}{group}` or `key=value` of a spec. */
struct Item {
  std::string key;
  std::vector<std::string> indexes;
  std::vector<std::string> groups;
  std::string plain;
  bool hasValue = false;
};

Item readItem(const std::string& text) {
  Item item;
  const std::size_t n = text.size();
  std::size_t i = 0;
  const auto skip = [&] {
    while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) i++;
  };
  skip();
  while (i < n && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
    item.key += text[i++];
  }
  skip();
  while (i < n && text[i] == '{') {
    const std::size_t end = closing(text, i);
    item.indexes.push_back(text.substr(i + 1, end - i - 1));
    i = end + 1;
    skip();
  }
  if (i < n && text[i] == '=') {
    item.hasValue = true;
    i++;
    skip();
    if (i < n && text[i] == '{') {
      while (i < n && text[i] == '{') {
        const std::size_t end = closing(text, i);
        item.groups.push_back(text.substr(i + 1, end - i - 1));
        i = end + 1;
        skip();
      }
    } else {
      item.plain = trimmed(text.substr(i));
    }
  }
  return item;
}

TblrIndex parseIndex(const std::string& text) {
  TblrIndex ix;
  if (trimmed(text).empty()) ix.all = true;
  for (const std::string& raw : splitTop(text)) {
    const std::string tok = trimmed(raw);
    if (tok.empty()) continue;
    if (tok == "odd") {
      ix.odd = true;
    } else if (tok == "even") {
      ix.even = true;
    } else if (tok == "-") {
      ix.all = true;
    } else {
      const std::size_t dash = tok.find('-');
      if (dash == std::string::npos) {
        const int v = std::atoi(tok.c_str());
        if (v > 0) ix.ranges.push_back({v, v});
      } else {
        const std::string lo = trimmed(tok.substr(0, dash));
        const std::string hi = trimmed(tok.substr(dash + 1));
        ix.ranges.push_back({lo.empty() ? 1 : std::max(1, std::atoi(lo.c_str())),
                             hi.empty() ? 0 : std::max(1, std::atoi(hi.c_str()))});
      }
    }
  }
  return ix;
}

std::string alignOf(const std::string& value) {
  const std::string v = trimmed(value);
  if (v == "l" || v == "j") return "l";
  if (v == "c") return "c";
  if (v == "r") return "r";
  return "";
}

/** A key's value without the braces that group it: `{\hspace{2em}}`. */
std::string ungrouped(const std::string& value) {
  const std::string v = trimmed(value);
  if (v.size() >= 2 && v.front() == '{' && closing(v, 0) == v.size() - 1) {
    return v.substr(1, v.size() - 2);
  }
  return v;
}

/** The settings of a style or span group: `bg=red, font=\bfseries, c=2`. */
void parseStyle(const std::string& text, TblrSetting& s, std::vector<std::string>& unsupported) {
  for (const std::string& raw : splitTop(text)) {
    const std::string tok = trimmed(raw);
    if (tok.empty()) continue;
    const std::size_t eq = tok.find('=');
    if (eq == std::string::npos) {
      const std::string a = alignOf(tok);
      if (!a.empty()) {
        s.halign = a;
      } else if (tok == "m" || tok == "t" || tok == "b" || tok == "h" || tok == "f" || tok == "p") {
        s.valign = tok == "p" ? 't' : tok[0];
      } else if (tok == "guard") {
        // tabularray's `cmd=` with nothing: the cell's text is not read as a number.
        s.cmd.clear();
        s.hasCmd = true;
      } else {
        unsupported.push_back(tok);
      }
      continue;
    }
    const std::string key = trimmed(tok.substr(0, eq));
    const std::string value = trimmed(tok.substr(eq + 1));
    if (key == "bg") {
      s.background = value;
    } else if (key == "fg") {
      s.foreground = value;
    } else if (key == "font") {
      s.font = value;  // the declarations as written: the lowering sets the cell's text under them
      s.hasFont = true;
    } else if (key == "halign") {
      const std::string a = alignOf(value);
      if (!a.empty()) s.halign = a;
    } else if (key == "rowsep" || key == "abovesep" || key == "belowsep") {
      const Dimen d = Units::getDimen(value);
      if (d.isValid()) {
        if (key != "belowsep") s.aboveSep = d;
        if (key != "abovesep") s.belowSep = d;
      } else {
        unsupported.push_back(key);
      }
    } else if (key == "si") {
      s.siunitx = true;
      s.si = ungrouped(value);  // siunitx's own options: read by the lowering, by siunitx.h
    } else if (key == "cmd") {
      // Kept as written: the lowering applies it to the cell as `\cmd{text}`, as tabularray does.
      s.cmd = value;
      s.hasCmd = true;
    } else if (key == "preto") {
      s.prepend = ungrouped(value);
    } else if (key == "appto") {
      s.append = ungrouped(value);
    } else if (key == "c" || key == "r") {
      const int v = std::atoi(value.c_str());
      if (v > 1) (key == "c" ? s.colspan : s.rowspan) = std::min(v, 1 << 12);
    } else if (key == "valign") {
      // t (and p), m, b: the box of the cell; h, f: its first and last baseline, as tabularray reads them.
      const char v = value.empty() ? 0 : (value[0] == 'p' ? 't' : value[0]);
      if (std::string("tmbhf").find(v) != std::string::npos && v != 0) {
        s.valign = v;
      } else {
        unsupported.push_back(key + "=" + value);
      }
    } else {
      unsupported.push_back(key);
    }
  }
}

/** A rule's thickness and column range from its value groups. */
void parseRule(const Item& item, TblrRule& rule) {
  std::string style;
  const auto isRange = [](const std::string& g) {
    if (trimmed(g).empty()) return false;
    for (const char c : g) {
      if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == ',' || c == ' ')) {
        return false;
      }
    }
    return true;
  };
  rule.cols.all = true;
  // Every range of the list counts: `3-4,7-10,13` is three pieces of one rule.
  const auto setColumns = [&](const std::string& group) {
    const TblrIndex cols = parseIndex(group);
    if (!cols.ranges.empty() || cols.odd || cols.even || cols.all) rule.cols = cols;
  };
  if (item.groups.size() >= 2) {
    setColumns(item.groups[0]);
    style = item.groups[1];
  } else if (item.groups.size() == 1) {
    if (isRange(item.groups[0])) {
      setColumns(item.groups[0]);
    } else {
      style = item.groups[0];
    }
  } else {
    style = item.plain;
  }
  for (const std::string& raw : splitTop(style)) {
    const std::string tok = trimmed(raw);
    if (tok.empty()) continue;
    const std::size_t eq = tok.find('=');
    if (eq != std::string::npos) {
      const std::string key = trimmed(tok.substr(0, eq));
      const std::string value = trimmed(tok.substr(eq + 1));
      const float pos = static_cast<float>(std::atof(value.c_str()));
      if (key == "wd") {
        const Dimen d = Units::getDimen(value);
        if (d.isValid()) rule.thickness = d;
      } else if (key == "l" || key == "leftpos" || key == "lr") {
        rule.leftpos = pos;
      }
      if (key == "r" || key == "rightpos" || key == "lr") rule.rightpos = pos;
    } else if (std::isdigit(static_cast<unsigned char>(tok[0])) || tok[0] == '.') {
      const Dimen d = Units::getDimen(tok);
      if (d.isValid()) rule.thickness = d;
    } else if (tok == "dashed" || tok == "dotted") {
      rule.dashed = true;
    } else if (tok != "solid") {
      rule.colour = tok;  // what is left is a colour
    }
  }
}

/** tabularray's column types as a tabular's: Q[l] and X[2,r] and l, c, r. */
std::string columnsOf(const std::string& spec, std::vector<std::string>& unsupported, int depth) {
  std::string out;
  const std::size_t n = spec.size();
  for (std::size_t i = 0; i < n; i++) {
    const char c = spec[i];
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    if (c == '|') {
      out += '|';
      continue;
    }
    if (c == '*' && depth < 4) {
      // *{n}{columns}
      const std::size_t first = closing(spec, i + 1);
      const std::size_t last = closing(spec, first + 1);
      if (first >= n || last >= n) break;
      const int times = std::min(std::atoi(spec.substr(i + 2, first - i - 2).c_str()), kMaxRepeat);
      const std::string inner = columnsOf(spec.substr(first + 2, last - first - 2), unsupported, depth + 1);
      for (int k = 0; k < times && out.size() < (1u << 14); k++) out += inner;
      i = last;
      continue;
    }
    if (c == '@' || c == '!' || c == '>' || c == '<') {
      // As a tabular's own.
      const std::size_t end = closing(spec, i + 1);
      if (end >= n) break;
      out += spec.substr(i, end - i + 1);
      i = end;
      continue;
    }
    if (c == 'l' || c == 'c' || c == 'r' || c == 'j' || c == 'Q' || c == 'X' || c == 'p' ||
        c == 'm' || c == 'b') {
      std::string options;
      if (i + 1 < n && spec[i + 1] == '[') {
        const std::size_t end = spec.find(']', i + 1);
        if (end == std::string::npos) break;
        options = spec.substr(i + 2, end - i - 2);
        i = end;
      } else if ((c == 'p' || c == 'm' || c == 'b') && i + 1 < n && spec[i + 1] == '{') {
        const std::size_t end = closing(spec, i + 1);
        if (end >= n) break;
        out += spec.substr(i, end - i + 1);
        i = end;
        continue;
      }
      std::string align = c == 'j' ? "l" : std::string(1, c);
      std::string width;
      for (const std::string& raw : splitTop(options)) {
        const std::string tok = trimmed(raw);
        const std::size_t eq = tok.find('=');
        const std::string key = eq == std::string::npos ? tok : trimmed(tok.substr(0, eq));
        const std::string value = eq == std::string::npos ? "" : trimmed(tok.substr(eq + 1));
        if (eq == std::string::npos && !alignOf(tok).empty()) {
          align = alignOf(tok);
        } else if (key == "halign" && !alignOf(value).empty()) {
          align = alignOf(value);
        } else if (key == "wd" && !value.empty()) {
          width = value;
        }
      }
      if (c == 'Q' || c == 'l' || c == 'c' || c == 'r' || c == 'j') {
        if (!width.empty()) {
          out += "p{" + width + "}";
        } else {
          out += align == "r" || align == "c" ? align : "l";
        }
      } else if (c == 'X') {
        out += align == "r"   ? ">{\\raggedleft\\arraybackslash}X"
               : align == "c" ? ">{\\centering\\arraybackslash}X"
                              : "X";
      } else {
        out += width.empty() ? std::string(1, c) : std::string(1, c) + "{" + width + "}";
      }
      continue;
    }
    unsupported.push_back("colspec");
  }
  return out;
}

}  // namespace

bool TblrIndex::matches(int i, int n) const {
  (void)n;
  if (all) return true;
  if (odd && i % 2 == 1) return true;
  if (even && i % 2 == 0) return true;
  for (const auto& [lo, hi] : ranges) {
    if (i >= lo && (hi == 0 || i <= hi)) return true;
  }
  return false;
}

/** `text` without its `%` comments (to the line end): tinytable writes one after each bracket of a table. */
static std::string withoutComments(const std::string& text) {
  if (text.find('%') == std::string::npos) return text;
  std::string out;
  for (std::size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      out += text[i];
      out += text[++i];
    } else if (text[i] == '%') {
      while (i + 1 < text.size() && text[i + 1] != '\n') i++;
    } else {
      out += text[i];
    }
  }
  return out;
}

TblrSpec parseTblr(const std::string& outerText, const std::string& innerText) {
  const std::string outer = withoutComments(outerText);
  const std::string inner = withoutComments(innerText);
  TblrSpec spec;
  for (const std::string& raw : splitTop(outer)) {
    if (trimmed(raw).empty()) continue;
    const Item item = readItem(raw);
    const std::string value = item.groups.empty() ? item.plain : item.groups[0];
    if (item.key == "caption") {
      spec.caption = value;
    } else if (item.key == "note" && item.hasValue) {
      if (spec.notes.size() < kMaxItems) {
        spec.notes.push_back({item.indexes.empty() ? std::string() : trimmed(item.indexes[0]), value});
      }
    } else if (item.key == "headsep" || item.key == "footsep") {
      // The space between the caption and the table, and between the table and its notes (6pt by default).
      const Dimen d = Units::getDimen(trimmed(value));
      if (d.isValid()) {
        (item.key == "headsep" ? spec.headsep : spec.footsep) = d;
      } else {
        spec.unsupported.push_back(item.key);
      }
    } else if (item.key == "entry" || item.key == "label" || item.key == "presep" || item.key == "postsep") {
      // The list of tables, the table's label and its place on a page: a grob has none of them.
    } else if (!item.key.empty()) {
      spec.unsupported.push_back(item.key);
    }
  }
  std::size_t count = 0;
  for (const std::string& raw : splitTop(inner)) {
    if (++count > kMaxItems) break;
    if (trimmed(raw).empty()) continue;
    const Item item = readItem(raw);
    const std::string& key = item.key;
    const std::string value = item.groups.empty() ? item.plain : item.groups[0];
    if (key == "colspec") {
      spec.columns = columnsOf(value, spec.unsupported, 0);
    } else if (key == "width") {
      spec.width = Units::getDimen(trimmed(value));
      if (!spec.width.isValid()) spec.unsupported.push_back(key);
    } else if (key == "hline" || key == "vline") {
      TblrRule rule;
      rule.at = item.indexes.empty() ? TblrIndex() : parseIndex(item.indexes[0]);
      if (item.indexes.empty()) rule.at.all = true;
      parseRule(item, rule);
      (key == "hline" ? spec.horizontal : spec.vertical).push_back(rule);
    } else if (key == "hlines" || key == "vlines") {
      TblrRule rule;
      rule.at.all = true;
      parseRule(item, rule);
      (key == "hlines" ? spec.horizontal : spec.vertical).push_back(rule);
    } else if (key == "cell" || key == "row" || key == "column" || key == "cells" ||
               key == "rows" || key == "columns") {
      TblrSetting s;
      s.rows.all = true;
      s.cols.all = true;
      s.priority = key == "cell" ? 3 : key == "row" ? 2 : key == "column" ? 1 : 0;
      if (key == "cell" || key == "row") {
        if (!item.indexes.empty()) s.rows = parseIndex(item.indexes[0]);
      }
      if (key == "cell") {
        if (item.indexes.size() > 1) s.cols = parseIndex(item.indexes[1]);
      } else if (key == "column") {
        if (!item.indexes.empty()) s.cols = parseIndex(item.indexes[0]);
      }
      // [span][style], or the style alone.
      if (item.groups.size() >= 2) parseStyle(item.groups[0], s, spec.unsupported);
      parseStyle(item.groups.empty() ? item.plain : item.groups.back(), s, spec.unsupported);
      spec.settings.push_back(s);
    } else if (key == "rowsep" || key == "abovesep" || key == "belowsep") {
      // A setting for every row, as `rows={rowsep=3pt}` is.
      TblrSetting s;
      s.rows.all = true;
      s.cols.all = true;
      parseStyle(key + "=" + value, s, spec.unsupported);
      spec.settings.push_back(s);
    } else if (!key.empty()) {
      spec.unsupported.push_back(key);
    }
  }
  return spec;
}

std::string tblrColumns(const TblrSpec& spec, int ncols) {
  // The columns, each with the specifiers that go before it and after it.
  std::vector<std::string> columns;
  std::vector<std::size_t> letterAt;  // where each column's letter is in its string
  std::string pending;
  const std::string& s = spec.columns;
  for (std::size_t i = 0; i < s.size() && columns.size() < (1u << 14); i++) {
    const char c = s[i];
    if (c == '|') continue;
    if (c == '>' || c == '@' || c == '!') {
      const std::size_t end = closing(s, i + 1);
      if (end >= s.size()) break;
      pending += s.substr(i, end - i + 1);
      i = end;
    } else if (c == '<') {
      const std::size_t end = closing(s, i + 1);
      if (end >= s.size()) break;
      if (!columns.empty()) columns.back() += s.substr(i, end - i + 1);
      i = end;
    } else if (c == 'p' || c == 'm' || c == 'b') {
      std::string column = pending + c;
      pending.clear();
      if (i + 1 < s.size() && s[i + 1] == '{') {
        const std::size_t end = closing(s, i + 1);
        if (end >= s.size()) break;
        column += s.substr(i + 1, end - i);
        i = end;
      }
      columns.push_back(column);
      letterAt.push_back(std::string::npos);
    } else {
      letterAt.push_back(pending.size());
      columns.push_back(pending + c);
      pending.clear();
    }
  }
  if (columns.empty()) columns.assign(static_cast<std::size_t>(std::max(ncols, 1)), "l");
  letterAt.resize(columns.size(), 0);
  const int m = static_cast<int>(columns.size());
  // A siunitx column is flush right: its numbers are lined up at their marker.
  for (int j = 0; j < m; j++) {
    const std::size_t at = letterAt[static_cast<std::size_t>(j)];
    if (at != std::string::npos && tblrCell(spec, 1, j + 1, 1, m).siunitx) {
      columns[static_cast<std::size_t>(j)][at] = 'r';
    }
  }
  std::string out;
  for (int k = 1; k <= m + 1; k++) {
    bool rule = false;
    for (const TblrRule& r : spec.vertical) rule = rule || r.at.matches(k, m + 1);
    if (rule) out += '|';
    if (k <= m) out += columns[static_cast<std::size_t>(k - 1)];
  }
  return out;
}

std::vector<TblrRuleSegment> tblrRuleSegments(const TblrSpec& spec, int at, int rows, int columns) {
  std::vector<TblrRuleSegment> out;
  const auto sameThickness = [](const TblrRule& x, const TblrRule& y) {
    const Dimen& a = x.thickness;
    const Dimen& b = y.thickness;
    if (a.isValid() != b.isValid() || x.colour != y.colour || x.dashed != y.dashed) return false;
    return !a.isValid() || (a.val == b.val && a.unit == b.unit);
  };
  const TblrRule* open = nullptr;
  for (int j = 1; j <= columns && j <= (1 << 14); j++) {
    // The last rule written that covers the column wins.
    const TblrRule* rule = nullptr;
    for (const TblrRule& r : spec.horizontal) {
      if (r.at.matches(at, rows + 1) && r.cols.matches(j, columns)) rule = &r;
    }
    if (rule == nullptr) {
      open = nullptr;
      continue;
    }
    // A side drawn short (a negative position) leaves a gap, so the piece ends there.
    const bool joins = open != nullptr && sameThickness(*open, *rule) &&
                       open->rightpos >= 0 && rule->leftpos >= 0;
    if (joins) {
      out.back().to = j;
      out.back().rightpos = rule->rightpos;
    } else {
      TblrRuleSegment s;
      s.from = s.to = j;
      s.leftpos = rule->leftpos;
      s.rightpos = rule->rightpos;
      s.thickness = rule->thickness;
      s.colour = rule->colour;
      s.dashed = rule->dashed;
      out.push_back(s);
    }
    open = rule;
  }
  return out;
}

TblrSetting tblrCell(const TblrSpec& spec, int i, int j, int n, int m) {
  TblrSetting out;
  std::vector<const TblrSetting*> named;
  for (const TblrSetting& s : spec.settings) {
    if (s.rows.matches(i, n) && s.cols.matches(j, m)) named.push_back(&s);
  }
  std::stable_sort(named.begin(), named.end(),
                   [](const TblrSetting* a, const TblrSetting* b) { return a->priority < b->priority; });
  for (const TblrSetting* p : named) {
    const TblrSetting& s = *p;
    if (s.colspan > 1) out.colspan = s.colspan;
    if (s.rowspan > 1) out.rowspan = s.rowspan;
    if (!s.background.empty()) out.background = s.background;
    if (!s.foreground.empty()) out.foreground = s.foreground;
    if (s.hasFont) {
      out.font = s.font;
      out.hasFont = true;
    }
    if (!s.halign.empty()) out.halign = s.halign;
    if (s.valign != 0) out.valign = s.valign;
    if (s.hasCmd) {
      out.cmd = s.cmd;
      out.hasCmd = true;
    }
    if (s.siunitx) {
      out.siunitx = true;
      out.si = s.si;
    }
    if (s.aboveSep.isValid()) out.aboveSep = s.aboveSep;
    if (s.belowSep.isValid()) out.belowSep = s.belowSep;
    // As in tabularray, each setting adds to the cell's text rather than replacing it.
    out.prepend = s.prepend + out.prepend;
    out.append += s.append;
  }
  return out;
}

}  // namespace microtex::front
