#ifndef GRIDMICROTEX_FRONT_DIAGNOSTICS_H
#define GRIDMICROTEX_FRONT_DIAGNOSTICS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace microtex::front {

/**
 * Where a token or node came from in the input.
 *
 * Lines and columns are 1-based and columns count code points, not bytes,
 * because they are shown to a person who counts characters. The byte
 * offset and length are for slicing the source.
 */
struct SourceSpan {
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
  std::uint32_t line = 1;
  std::uint32_t col = 1;
};

enum class Severity : std::uint8_t { warning, error };

/** Nesting deeper than this is a capacity error ("Input nested too deeply")
 *  rather than recursion until the stack runs out: groups and arguments in
 *  the parser, a command's own arguments read for a macro in the expander,
 *  and a fragment parsed inside a fragment in the lowering. */
constexpr int kMaxDepth = 400;

struct Diagnostic {
  Severity severity;
  SourceSpan span;
  std::string message;
};

/**
 * The problems found in one parse.
 *
 * The front end recovers from malformed input rather than stopping at the
 * first mistake, so one document can produce many of these. Past `limit`
 * they are counted but not kept: a pasted binary file must not turn into
 * megabytes of messages.
 */
class Diagnostics {
public:
  explicit Diagnostics(std::size_t limit = 100) : _limit(limit) {}

  /** While one lives, warnings are not kept: a preamble's, say, whose
   *  package settings a grob cannot honour and does not draw. Errors still
   *  are. */
  class Quiet {
  public:
    explicit Quiet(Diagnostics& d) : _d(d), _was(d._quiet) { d._quiet = true; }
    ~Quiet() { _d._quiet = _was; }
    Quiet(const Quiet&) = delete;
    Quiet& operator=(const Quiet&) = delete;

  private:
    Diagnostics& _d;
    bool _was;
  };

  /** While one lives, what is reported is reported at `anchor`, with the
   *  package's name and the line and column it really was at in front of the
   *  message: a package's file has positions of its own, which mean nothing
   *  in the input that loaded it. Inside a package, the anchor stays the
   *  outermost one. */
  class Package {
  public:
    Package(Diagnostics& d, std::string name, const SourceSpan& anchor)
        : _d(d), _was(d._package), _wasAnchor(d._anchor) {
      d._package = std::move(name);
      if (_was.empty()) d._anchor = anchor;
    }
    ~Package() {
      _d._package = _was;
      _d._anchor = _wasAnchor;
    }
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

  private:
    Diagnostics& _d;
    std::string _was;
    SourceSpan _wasAnchor;
  };

  void add(Severity severity, SourceSpan span, std::string message) {
    if (!_package.empty()) {
      message = "package " + _package + " line " + std::to_string(span.line) + ":" +
                std::to_string(span.col) + ": " + message;
      span = _anchor;
    }
    if (severity == Severity::error && !_hasError) {
      _hasError = true;
      _firstError = {severity, span, message};
    }
    if (severity == Severity::warning && _quiet) return;
    if (_items.size() >= _limit) {
      _dropped++;
      return;
    }
    _items.push_back({severity, span, std::move(message)});
  }

  void warn(const SourceSpan& span, std::string message) {
    add(Severity::warning, span, std::move(message));
  }

  /** As warn(), unless the same message is already there for the same place: a box made twice says it once. */
  void warnOnce(const SourceSpan& span, std::string message) {
    for (const Diagnostic& d : _items) {
      if (d.span.offset == span.offset && d.message == message) return;
    }
    add(Severity::warning, span, std::move(message));
  }

  void error(const SourceSpan& span, std::string message) {
    add(Severity::error, span, std::move(message));
  }

  const std::vector<Diagnostic>& items() const { return _items; }

  /** How many were not kept because the limit was reached. */
  std::size_t dropped() const { return _dropped; }

  /** The first error, kept even past the limit. An error is a capacity
   *  running out (runaway expansion, nesting), after which nothing drawn
   *  could be trusted; everything else is a warning. */
  const Diagnostic* firstError() const { return _hasError ? &_firstError : nullptr; }

private:
  std::vector<Diagnostic> _items;
  bool _quiet = false;
  std::string _package;
  SourceSpan _anchor;
  bool _hasError = false;
  Diagnostic _firstError{Severity::error, {}, {}};
  std::size_t _dropped = 0;
  std::size_t _limit;
};

}  // namespace microtex::front

#endif
