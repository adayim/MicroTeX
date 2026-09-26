// The lexer of the front end (lib/front/lexer.h). Each expectation is TeX's
// own rule for the input (The TeXbook, ch. 7-8).

#include <string>
#include <vector>

#include "check.h"
#include "front/lexer.h"

using namespace microtex::front;
using check::Strings;

namespace {

struct Lexed {
  std::vector<Token> toks;  // ending with the `end` token
  Strings messages;
  std::vector<SourceSpan> spans;
};

Lexed lex(const std::string& tex, bool blankLineIsPar = false) {
  Diagnostics diags;
  LexOptions opts;
  opts.blankLineIsPar = blankLineIsPar;
  CatcodeTable catcodes;
  Lexer lexer(tex, opts, diags, catcodes);
  Lexed out;
  do {
    out.toks.push_back(lexer.next());
  } while (out.toks.back().kind != TokKind::end);
  for (const auto& d : diags.items()) {
    out.messages.push_back(d.message);
    out.spans.push_back(d.span);
  }
  return out;
}

const char* kindName(TokKind k) {
  switch (k) {
    case TokKind::controlWord: return "control_word";
    case TokKind::controlSymbol: return "control_symbol";
    case TokKind::character: return "char";
    case TokKind::space: return "space";
    case TokKind::par: return "par";
    case TokKind::end: return "end";
  }
  return "?";
}

// "kind:text" for every token but the final `end`.
Strings toks(const std::string& tex, bool blankLineIsPar = false) {
  Strings out;
  for (const Token& t : lex(tex, blankLineIsPar).toks) {
    if (t.kind != TokKind::end) out.push_back(std::string(kindName(t.kind)) + ":" + t.text);
  }
  return out;
}

std::vector<Token> chars(const Lexed& l) {
  std::vector<Token> out;
  for (const Token& t : l.toks) {
    if (t.kind == TokKind::character) out.push_back(t);
  }
  return out;
}

}  // namespace

TEST(lexer_tells_control_words_symbols_and_characters_apart) {
  CHECK_EQ(toks("\\alpha\\,x"), (Strings{"control_word:alpha", "control_symbol:,", "char:x"}));
  CHECK_EQ(toks("\\\\a"), (Strings{"control_symbol:\\", "char:a"}));
  CHECK_EQ(toks("\\frac12"), (Strings{"control_word:frac", "char:1", "char:2"}));
}

TEST(lexer_control_word_swallows_spaces_after_it_control_symbol_does_not) {
  CHECK_EQ(toks("\\alpha   x"), (Strings{"control_word:alpha", "char:x"}));
  CHECK_EQ(toks("\\, x"), (Strings{"control_symbol:,", "space: ", "char:x"}));
  // a control space is followed by skipped blanks, like a control word
  CHECK_EQ(toks("a\\   b"), (Strings{"char:a", "control_symbol: ", "char:b"}));
}

TEST(lexer_run_of_spaces_is_one_space_token) {
  CHECK_EQ(toks("a  \t b"), (Strings{"char:a", "space: ", "char:b"}));
}

TEST(lexer_line_end_is_a_space_and_next_lines_leading_spaces_are_dropped) {
  CHECK_EQ(toks("a\n    b"), (Strings{"char:a", "space: ", "char:b"}));
  CHECK_EQ(toks("a\r\nb"), (Strings{"char:a", "space: ", "char:b"}));
  CHECK_EQ(toks("a\rb"), (Strings{"char:a", "space: ", "char:b"}));
}

TEST(lexer_blank_line_is_a_paragraph_only_when_asked_for) {
  CHECK_EQ(toks("a\n\nb", true), (Strings{"char:a", "space: ", "par:", "char:b"}));
  CHECK_EQ(toks("a\n  \n b", true), (Strings{"char:a", "space: ", "par:", "char:b"}));
  CHECK_EQ(toks("a\n\nb"), (Strings{"char:a", "space: ", "char:b"}));
}

TEST(lexer_comment_runs_to_the_end_of_the_line_and_takes_the_line_end) {
  CHECK_EQ(toks("a% note\nb"), (Strings{"char:a", "char:b"}));
  CHECK_EQ(toks("a % note\n   b"), (Strings{"char:a", "space: ", "char:b"}));
  CHECK_EQ(toks("50\\% off"), (Strings{"char:5", "char:0", "control_symbol:%", "space: ",
                                      "char:o", "char:f", "char:f"}));
  // a comment before a blank line still leaves the paragraph break
  CHECK_EQ(toks("a%\n\nb", true), (Strings{"char:a", "par:", "char:b"}));
}

TEST(lexer_counts_the_line_ends_tex_drops_on_the_next_token) {
  const Lexed l = lex("\\alpha\nb");
  CHECK(l.toks[0].kind == TokKind::controlWord);
  CHECK(l.toks[1].kind == TokKind::character);
  CHECK_EQ(l.toks[1].lineEnds, 1);
  std::vector<int> ends;
  for (const Token& t : lex("a\nb").toks) ends.push_back(t.lineEnds);
  CHECK_EQ(ends, (std::vector<int>{0, 1, 0, 0}));
}

TEST(lexer_gives_special_characters_texs_category_codes) {
  std::vector<int> cats;
  for (const Token& t : chars(lex("{}$&#^_~a+"))) cats.push_back(static_cast<int>(t.cat));
  CHECK_EQ(cats, (std::vector<int>{1, 2, 3, 4, 6, 7, 8, 13, 11, 12}));
}

TEST(lexer_multibyte_character_is_one_token_and_columns_count_characters) {
  const auto c = chars(lex("\u03b1\u03b2"));
  CHECK_EQ(c.size(), 2u);
  if (c.size() != 2) return;
  CHECK_EQ(c[0].cp, 0x3B1u);
  CHECK_EQ(c[1].cp, 0x3B2u);
  CHECK_EQ(c[0].span.col, 1u);
  CHECK_EQ(c[1].span.col, 2u);
  CHECK_EQ(c[0].span.offset, 0u);
  CHECK_EQ(c[1].span.offset, 2u);
  CHECK_EQ(c[0].text, std::string("\u03b1"));
  CHECK_EQ(c[1].text, std::string("\u03b2"));
}

TEST(lexer_keeps_joined_and_variation_selected_characters_one_token) {
  const std::string womanTechnologist = "\U0001F469\u200D\U0001F4BB";
  const std::string checkMark = "\u2714\uFE0F";
  CHECK_EQ(toks(womanTechnologist), (Strings{"char:" + womanTechnologist}));
  CHECK_EQ(toks(checkMark), (Strings{"char:" + checkMark}));
}

TEST(lexer_positions_follow_lines) {
  for (const Token& t : lex("a\n  \\beta x").toks) {
    if (t.text != "beta") continue;
    CHECK_EQ(t.span.line, 2u);
    CHECK_EQ(t.span.col, 3u);
    CHECK_EQ(t.span.offset, 4u);
  }
}

TEST(lexer_invalid_utf8_becomes_replacement_character_with_a_warning_at_its_position) {
  const Lexed l = lex("a\xFF" "b");
  std::vector<unsigned> cps;
  for (const Token& t : chars(l)) cps.push_back(t.cp);
  CHECK_EQ(cps, (std::vector<unsigned>{0x61, 0xFFFD, 0x62}));
  CHECK_EQ(l.messages.size(), 1u);
  if (l.spans.size() != 1) return;
  CHECK_EQ(l.spans[0].line, 1u);
  CHECK_EQ(l.spans[0].col, 2u);
  CHECK(check::allContain(l.messages, "invalid UTF-8"));
}

TEST(lexer_drops_a_backslash_at_the_end_with_a_warning) {
  const Lexed l = lex("x\\");
  CHECK_EQ(l.toks.size(), 2u);
  CHECK(l.toks[0].kind == TokKind::character);
  CHECK(check::allContain(l.messages, "backslash at the end"));
}

TEST(lexer_byte_order_mark_is_not_text) {
  CHECK_EQ(toks("\uFEFFx"), (Strings{"char:x"}));
}
