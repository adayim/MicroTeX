# Tests

The LaTeX front end (`lib/front/`: lexer, expander, parser) and what the
engine draws from it are tested here, together with two libFuzzer targets.
CI runs all of it on every push (`.github/workflows/test.yml`); `test/run.sh`
does each step locally:

```sh
test/run.sh test            # the tests, under AddressSanitizer and UBSan
test/run.sh test OFF        # ... built without FriBidi (not in CI: the R package always has it)
test/run.sh strict          # our sources under -Wextra -Wpedantic
test/run.sh fuzz 300        # each fuzzer for 300 s (clang)
```

Needs CMake, FreeType and, for `ON`, FriBidi. `CXX` picks the compiler.

- `test_*.cpp` hold the tests. Each is `TEST(name) { CHECK_EQ(...); }` using
  the few lines in `check.h`; `build/.../front_tests lexer` runs only the
  tests whose names contain `lexer`.
- `test_lexer`, `test_expander`, `test_parser`, `test_tblr`, `test_siunitx`
  and `test_mhchem` test the front end alone, through its tokens, its text
  and its syntax tree.
- The other `test_*.cpp` test the whole engine, from LaTeX to draw records,
  through `engine.h`: `check::render(tex, options)` starts the engine on first
  use (FiraMath from `res/`, the packages in `res/tex`) and returns what is
  drawn, the diagnostics as `"line:col: message"`, and the size.
  `CHECK_PAIRS` says two spellings draw the same ink. Text is measured by a
  stub, 0.55 em a character, so these tests compare layouts with each other
  and look at rows, columns and order; they say nothing of a real font's
  widths, which gridmicrotex tests in R with its own measurer. `fixtures/`
  holds the LaTeX R's table packages write.
- `fuzz/front_fuzz.cpp` runs the front end alone over any bytes, and checks
  its invariants; `fuzz/engine_fuzz.cpp` runs the whole engine, parsing and
  drawing, with a font from `res/`.
- `fuzz/corpus/` is where both start: a few seeds and the inputs of past
  findings (`regress-*`), so every run replays them first. Add a finding's
  input there when it is fixed, and a test for it in `test_engine.cpp`. A
  runaway macro takes up to a few seconds under the sanitizers until the
  expander's caps stop it, so these targets run at a few inputs a second:
  CI's two minutes mostly replay the corpus, and finding new bugs takes a
  longer local run.
