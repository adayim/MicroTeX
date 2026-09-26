# Tests

The LaTeX front end's lexer, expander and parser (`lib/front/`) are tested
here, together with two libFuzzer targets. CI runs all of it on every push
(`.github/workflows/test.yml`); `test/run.sh` does each step locally:

```sh
test/run.sh test            # the tests, under AddressSanitizer and UBSan
test/run.sh test OFF        # ... built without FriBidi
test/run.sh strict          # our sources under -Wextra -Wpedantic
test/run.sh fuzz 300        # each fuzzer for 300 s (clang)
```

Needs CMake, FreeType and, for `ON`, FriBidi. `CXX` picks the compiler.

- `test_*.cpp` hold the tests. Each is `TEST(name) { CHECK_EQ(...); }` using
  the few lines in `check.h`; `build/.../front_tests lexer` runs only the
  tests whose names contain `lexer`.
- `fuzz/front_fuzz.cpp` runs the front end alone over any bytes, and checks
  its invariants; `fuzz/engine_fuzz.cpp` runs the whole engine, parsing and
  drawing, with a font from `res/`.
- `fuzz/corpus/` is where both start: a few seeds and the inputs of past
  findings (`regress-*`), so every run replays them first. Add a finding's
  input there when it is fixed. A runaway macro takes up to a few seconds
  under the sanitizers until the expander's caps stop it, so these targets
  run at a few inputs a second: CI's two minutes mostly replay the corpus,
  and finding new bugs takes a longer local run.

Layout is not tested here: the engine asks its embedder to measure text,
and a layout's numbers depend on that. gridmicrotex tests layout with its
own measurer, in R.
