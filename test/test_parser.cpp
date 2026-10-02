// The parser of the front end (lib/front/parser.h), checked through its
// syntax tree, and the command table it reads (lib/front/spec.cpp) against
// the handlers the lowering dispatches to.
//
// `tree()` writes a tree compactly:
//   [a b]      a list          {a b}   a group        \cmd(x y)  a command
//   <a b>      a parsed arg    'text'  a kept-as-text argument   -  an absent one
//   S<order>(base;sub;sup)     scripts, with the order its parts came in
//   \over:(num;den)            an infix command
//   \bf{args|body}             a declaration
//   LR(left;body;...;right)    \left ... \right
//   env:name(args rows)        row:end(cells)   $(...) / $$(...)  math in text
//   ~ a tie, _ a space

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "atom/font_family_atom.h"
#include "atom/image_atom.h"
#include "atom/mark_atom.h"
#include "check.h"
#include "front/ast.h"
#include "front/expander.h"
#include "front/front.h"
#include "front/spec.h"
#include "macro/macro.h"

using namespace microtex::front;
using check::Strings;

namespace {

struct Parsed {
  Ast ast;
  Strings messages;
  std::vector<SourceSpan> spans;
  // Every node, depth first, in document order.
  std::vector<NodeId> order;
};

Parsed parse(const std::string& tex, Mode mode = Mode::math) {
  Diagnostics diags;
  Parsed out;
  out.ast = parseLatex(tex, mode, diags);
  for (const auto& d : diags.items()) {
    out.messages.push_back(d.message);
    out.spans.push_back(d.span);
  }
  std::vector<NodeId> stack;
  if (out.ast.root != kNoNode) stack.push_back(out.ast.root);
  while (!stack.empty()) {
    const NodeId id = stack.back();
    stack.pop_back();
    out.order.push_back(id);
    for (std::uint32_t i = out.ast.childCount(id); i > 0; i--) {
      stack.push_back(out.ast.child(id, i - 1));
    }
  }
  return out;
}

std::string kindOf(const Ast& ast, NodeId id) { return nodeKindName(ast.node(id).kind); }

std::string join(const Strings& parts, const std::string& sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); i++) out += (i ? sep : "") + parts[i];
  return out;
}

std::string render(const Ast& ast, NodeId id);

Strings renderChildren(const Ast& ast, NodeId id) {
  Strings out;
  for (std::uint32_t i = 0; i < ast.childCount(id); i++) out.push_back(render(ast, ast.child(id, i)));
  return out;
}

// A list's items, without its brackets.
std::string inner(const Ast& ast, NodeId id) {
  return join(renderChildren(ast, ast.child(id, 0)), " ");
}

std::string render(const Ast& ast, NodeId id) {
  const Node& x = ast.node(id);
  const std::string kind = kindOf(ast, id);
  const Strings sub = renderChildren(ast, id);
  if (kind == "list") return "[" + join(sub, " ") + "]";
  if (kind == "group") return "{" + inner(ast, id) + "}";
  if (kind == "char") return x.text;
  if (kind == "space") return x.aux == 0 ? "_" : x.aux == 1 ? "~" : "?space";
  if (kind == "command") {
    return "\\" + x.text + (x.flag ? "?" : "") + (sub.empty() ? "" : "(" + join(sub, " ") + ")");
  }
  if (kind == "argument") {
    if (!x.flag) return "-";
    if (ast.childCount(id) > 0) return "<" + inner(ast, id) + ">";
    return "'" + x.raw + "'";
  }
  if (kind == "scripts") return "S" + x.text + "(" + join(sub, ";") + ")";
  if (kind == "infix") return "\\" + x.text + ":(" + join(sub, ";") + ")";
  if (kind == "declaration") {
    const Strings args(sub.begin(), sub.end() - 1);
    return "\\" + x.text + "{" + join(args, " ") + "|" + sub.back() + "}";
  }
  if (kind == "leftright") return "LR(" + join(sub, ";") + ")";
  if (kind == "environment") return "env:" + x.text + "(" + join(sub, " ") + ")";
  if (kind == "row") return "row:" + x.text + "(" + join(sub, " ") + ")";
  if (kind == "cell") return join(sub, " ");
  if (kind == "math") return std::string(x.flag ? "$$(" : "$(") + inner(ast, id) + ")";
  return "?" + kind;
}

std::string tree(const std::string& tex, Mode mode = Mode::math) {
  const Parsed p = parse(tex, mode);
  return render(p.ast, p.ast.root);
}

// The `raw` text of every node of a kind, in document order.
Strings raws(const std::string& tex, const std::string& kind) {
  const Parsed p = parse(tex);
  Strings out;
  for (NodeId id : p.order) {
    if (kindOf(p.ast, id) == kind) out.push_back(p.ast.node(id).raw);
  }
  return out;
}

Strings messages(const std::string& tex) { return parse(tex).messages; }

Strings sorted(Strings v) {
  std::sort(v.begin(), v.end());
  return v;
}

// What is in `a` and not in `b`, sorted.
Strings minus(const Strings& a, const Strings& b) {
  const std::set<std::string> drop(b.begin(), b.end());
  Strings out;
  for (const auto& s : a) {
    if (drop.count(s) == 0) out.push_back(s);
  }
  return sorted(out);
}

Strings concat(Strings a, const Strings& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace

TEST(parser_every_command_is_read_by_the_front_end_or_has_a_handler_and_none_is_orphaned) {
  // As an embedder registers them (gridmicrotex does in its init).
  microtex::register_mark_macro();
  microtex::register_font_family_macro();
  microtex::register_image_macros();
  const Strings engineAll = microtex::MacroInfo::names();
  const Strings specCommands = commandNames();
  const Strings specEnvs = environmentNames();
  const Strings preludeCommands = preludeCommandNames();
  const Strings preludeEnvs = preludeEnvironmentNames();

  // The engine's registry holds the handlers the lowering dispatches to.
  // Each is named in the spec (or prelude): a handler nothing can reach is
  // dead code. `name@@env` are the environment builders.
  const std::string envSuffix = "@@env";
  Strings engine, envs;
  for (const auto& n : engineAll) {
    const bool isEnv = n.size() > envSuffix.size() &&
                       n.compare(n.size() - envSuffix.size(), envSuffix.size(), envSuffix) == 0;
    if (isEnv) {
      envs.push_back(n.substr(0, n.size() - envSuffix.size()));
    } else {
      engine.push_back(n);
    }
  }
  CHECK_EQ(minus(engine, concat(specCommands, preludeCommands)), Strings{});
  CHECK_EQ(minus(envs, concat(specEnvs, preludeEnvs)), Strings{});
  // And a spec command with no handler has to be one the parser or the
  // lowering reads itself; any other would be drawn in red as unknown.
  // Checked against the legacy build when the old parser went: each of
  // these draws as it did there.
  CHECK_EQ(minus(specCommands, engineAll),
           sorted({"(", "[", "\\", "above", "abovewithdelims", "atop", "atopwithdelims",
                   "bangle", "begin", "bf", "boldmath", "brace", "brack", "cal", "caption",
                   "centering", "char", "choose", "cite", "citealp", "citep", "citet",
                   "cmidrule", "color", "cr", "displaystyle", "end", "endfirsthead", "endfoot", "endhead", "endlastfoot", "ensuremath", "eqref",
                   "fontsize", "footnote", "footnotesize", "frak", "gmtagstar", "graphicspath", "href", "huge", "Huge",
                   "hskip", "it", "kern", "label", "large", "Large", "LARGE", "left", "limits", "makeatletter",
                   "makeatother", "mkern", "mskip", "noindent", "nolimits", "nonumber", "normal", "normalsize", "notag", "over",
                   "overwithdelims", "pageref", "par", "paragraph", "raggedleft",
                   "raggedright", "ref", "relscale", "right", "rm", "scriptscriptstyle",
                   "scriptsize", "scriptstyle", "section", "setcounter", "sf", "small", "subsection",
                   "subsubsection", "tag", "textstyle", "tiny", "tt", "url"}));
}

TEST(parser_argument_without_braces_is_one_character_or_one_command_with_its_arguments) {
  CHECK_EQ(tree("\\frac12"), std::string("[\\frac(<1> <2>)]"));
  CHECK_EQ(tree("\\frac \u03b1\u03b2"), std::string("[\\frac(<\u03b1> <\u03b2>)]"));
  CHECK_EQ(tree("\\sqrt\\frac12"), std::string("[\\sqrt(- <\\frac(<1> <2>)>)]"));
  // the recorded source is what the old command handlers received
  CHECK_EQ(raws("\\frac 1 {x^2}", "argument"), (Strings{"1", "x^2"}));
}

TEST(parser_optional_arguments_nest_brackets_and_keep_their_source) {
  CHECK_EQ(tree("\\sqrt[3]{x}"), std::string("[\\sqrt(<3> <x>)]"));
  const Strings args = raws("\\sqrt[\\left[x\\right]]{y}", "argument");
  CHECK(!args.empty() && args[0] == "\\left[x\\right]");
}

TEST(parser_scripts_attach_to_the_item_before_them_and_remember_their_order) {
  CHECK_EQ(tree("x^2"), std::string("[S^(x;[];[2])]"));
  CHECK_EQ(tree("^2"), std::string("[S^([];[];[2])]"));
  CHECK_EQ(tree("x^\\frac12"), std::string("[S^(x;[];[\\frac(<1> <2>)])]"));
  CHECK_EQ(tree("f_1'"), std::string("[S_'(f;[1];[])]"));
  CHECK_EQ(tree("f'_1"), std::string("[S'_(f;[1];[])]"));
  const Parsed primes = parse("f''");
  CHECK(primes.order.size() > 1 && primes.ast.node(primes.order[1]).aux == 2);
  // a double superscript reads as {a^b}^c, with a warning
  CHECK_EQ(tree("a^b^c"), std::string("[S^(S^(a;[];[b]);[];[c])]"));
  CHECK(check::allContain(messages("a^b^c"), "double superscript"));
}

TEST(parser_infix_command_divides_its_list) {
  CHECK_EQ(tree("a \\over b + c"), std::string("[\\over:([a];[b + c])]"));
  CHECK_EQ(tree("{a \\over b} + c"), std::string("[{\\over:([a];[b])} + c]"));
  CHECK_EQ(tree("\\frac{a}{b} \\over c"), std::string("[\\over:([\\frac(<a> <b>)];[c])]"));
}

TEST(parser_bf_stops_at_line_break_and_color_runs_to_the_end_of_the_group) {
  CHECK_EQ(tree("\\bf a \\\\ b"), std::string("[\\bf{|[a]} \\\\(-) b]"));
  CHECK_EQ(tree("\\color{red} a \\\\ b"), std::string("[\\color{'red'|[a \\\\(-) b]}]"));
  CHECK_EQ(tree("{\\bf a} b"), std::string("[{\\bf{|[a]}} b]"));
}

TEST(parser_left_middle_and_right_delimit_a_group) {
  CHECK_EQ(tree("\\left( x \\middle| y \\right)"), std::string("[LR('(';[x];'|';[y];')')]"));
  CHECK(check::allContain(messages("\\left( x"), "missing \\right"));
  CHECK(check::allContain(messages("x \\right)"), "\\right without \\left"));
}

TEST(parser_alignment_is_rows_of_cells_and_a_rule_ends_its_row) {
  CHECK_EQ(tree("\\begin{matrix} a & b \\\\ c & d \\end{matrix}"),
           std::string("[env:matrix(row:\\([a] [b]) row:([c] [d]))]"));
  CHECK_EQ(tree("\\begin{array}{cc} \\hline a & b \\end{array}"),
           std::string("[env:array(- 'cc' row:hline([\\hline]) row:([a] [b]))]"));
  // LaTeX's vertical position comes first, and is not the column spec.
  CHECK_EQ(tree("\\begin{array}[t]{cc} a & b \\end{array}"),
           std::string("[env:array('t' 'cc' row:([a] [b]))]"));
  CHECK_EQ(raws("\\begin{aligned} a \\\\[4pt] b \\end{aligned}", "row"), (Strings{"4pt", ""}));
  CHECK_EQ(raws("\\begin{matrix} a & b \\end{matrix}", "environment"), (Strings{" a & b "}));
}

TEST(parser_prelude_defines_the_engines_latex_written_environments_and_commands) {
  CHECK_EQ(tree("\\begin{pmatrix}a\\end{pmatrix}"),
           std::string("[{LR('(';[env:matrix(row:([a]))];')')}]"));
  CHECK_EQ(tree("\\dfrac12"), std::string("[\\genfrac('' '' '1' '' <1> <2>)]"));
}

TEST(parser_itemize_keeps_its_body_as_text_for_its_builder) {
  CHECK_EQ(raws("\\begin{itemize} \\item a \\begin{itemize}\\item b\\end{itemize} \\end{itemize}",
                "environment"),
           (Strings{" \\item a \\begin{itemize}\\item b\\end{itemize} "}));
}

TEST(parser_an_end_of_another_name_ends_a_list) {
  // LaTeX's "\begin{itemize} ended by \end{enumerate}": the list ends
  // there. Looking only for \end{itemize} read the rest into its last item.
  const std::string tex = "\\begin{itemize}\\item a\\end{enumerate} b";
  CHECK_EQ(raws(tex, "environment"), (Strings{"\\item a"}));
  CHECK_EQ(messages(tex), (Strings{"\\end{enumerate} ends \\begin{itemize}"}));
  // A list in it still ends its own.
  const std::string nested = "\\begin{itemize}\\item a\\begin{enumerate}\\item b\\end{enumerate}\\end{itemize}";
  CHECK_EQ(raws(nested, "environment"), (Strings{"\\item a\\begin{enumerate}\\item b\\end{enumerate}"}));
  CHECK_EQ(messages(nested), Strings{});
}

TEST(parser_modes_switch_at_text_and_at_dollar) {
  CHECK_EQ(tree("\\text{a $b$ c}"), std::string("[\\text(<a _ $(b) _ c>)]"));
  const Parsed p = parse("\\text{a $b$}");
  Strings modes;
  for (NodeId id : p.order) {
    if (kindOf(p.ast, id) != "char") continue;
    const std::string m = p.ast.node(id).mode == Mode::math ? "math" : "text";
    if (std::find(modes.begin(), modes.end(), m) == modes.end()) modes.push_back(m);
  }
  CHECK_EQ(modes, (Strings{"text", "math"}));
}

TEST(parser_value_tex_reads_without_braces_is_kept_as_text) {
  CHECK_EQ(tree("\\kern-.4ex x"), std::string("[\\kern('-.4ex') x]"));
  CHECK_EQ(tree("\\char\"41"), std::string("[\\char('\"41')]"));
}

TEST(parser_file_name_keeps_percent_hash_and_underscore_as_characters) {
  // The second option group is the older [llx,lly][urx,ury] spelling.
  CHECK_EQ(raws("\\includegraphics[width=1in]{a%b_c#1.png}", "argument"),
           (Strings{"width=1in", "", "a%b_c#1.png"}));
}

TEST(parser_text_drops_the_space_after_a_control_word_as_tex_does) {
  CHECK_EQ(tree("\\text{\\alpha b}"), std::string("[\\text(<\\alpha b>)]"));
  // A control symbol keeps it, and so does a word ended by a group.
  CHECK_EQ(tree("\\text{\\% b}"), std::string("[\\text(<\\% _ b>)]"));
  CHECK_EQ(tree("\\text{\\LaTeX{} b}"), std::string("[\\text(<\\LaTeX {} _ b>)]"));
}

TEST(parser_reports_problems_where_they_are_and_goes_on) {
  CHECK_EQ(messages("\\foo x"), (Strings{"unknown command \\foo: drawn as its name"}));
  CHECK_EQ(tree("\\foo x"), std::string("[\\foo? x]"));
  const Parsed p = parse("x\n\\frac{1}{2");
  CHECK_EQ(p.messages, (Strings{"missing } inserted"}));
  if (p.spans.size() == 1) {
    CHECK_EQ(p.spans[0].line, 2u);
    CHECK_EQ(p.spans[0].col, 9u);
  }
  CHECK_EQ(tree("\\frac{1}{2"), std::string("[\\frac(<1> <2>)]"));
  CHECK(check::allContain(messages("a } b"), "extra } ignored"));
  CHECK(check::allContain(messages("a & b"), "& outside an alignment"));
}

TEST(parser_scripts_node_keeps_the_position_of_its_own_operator) {
  // The span was taken by reference from a peeked token, which the first
  // read inside the scripts pops; a later put-back refilled that slot, so
  // the node ended up with some other token's position.
  const auto at = [](const std::string& tex) {
    const Parsed p = parse(tex);
    std::vector<unsigned> out;
    for (NodeId id : p.order) {
      if (kindOf(p.ast, id) != "scripts") continue;
      out.push_back(p.ast.node(id).span.line);
      out.push_back(p.ast.node(id).span.col);
    }
    return out;
  };
  CHECK_EQ(at("x^2"), (std::vector<unsigned>{1, 2}));
  CHECK_EQ(at("f'"), (std::vector<unsigned>{1, 2}));
  CHECK_EQ(at("ab\ncd_{x}"), (std::vector<unsigned>{2, 3}));
}

TEST(parser_tree_is_a_tree_every_node_has_one_parent) {
  for (const std::string tex :
       {"^2", "_1^2", "x'", "\\limits", "a \\over b", "\\sum\\limits_i^n x_i", "\\left( x",
        "\\begin{matrix} a & b \\\\ \\hline c \\end{matrix}", "\\sqrt[3]{x}^2",
        "\\text{a $b^c$}", "\\bf a \\\\ b \\color{red} c", "\\frac{1}{2", "a^b^c_d_e",
        "\\begin{pmatrix}1\\end{pmatrix}'"}) {
    const Parsed p = parse(tex);
    const std::set<NodeId> unique(p.order.begin(), p.order.end());
    CHECK_EQ(unique.size(), p.order.size());
  }
}

TEST(parser_deep_nesting_is_an_error_not_a_crash) {
  const std::string deep = std::string(2000, '{') + "x" + std::string(2000, '}');
  CHECK(check::anyContains(messages(deep), "nested too deeply"));
  CHECK_EQ(messages(std::string(50, '{') + "x" + std::string(50, '}')), Strings{});
}
