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

  /** Warnings at byte offsets in [from, to) are not kept: a preamble's, say,
   *  whose package settings a grob cannot honour and does not draw. Errors
   *  still are. */
  void mute(std::uint32_t from, std::uint32_t to) { _muted.emplace_back(from, to); }

  void add(Severity severity, const SourceSpan& span, std::string message) {
    if (severity == Severity::error && !_hasError) {
      _hasError = true;
      _firstError = {severity, span, message};
    }
    if (severity == Severity::warning) {
      for (const auto& [from, to] : _muted) {
        if (span.offset >= from && span.offset < to) return;
      }
    }
    if (_items.size() >= _limit) {
      _dropped++;
      return;
    }
    _items.push_back({severity, span, std::move(message)});
  }

  void warn(const SourceSpan& span, std::string message) {
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
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _muted;
  bool _hasError = false;
  Diagnostic _firstError{Severity::error, {}, {}};
  std::size_t _dropped = 0;
  std::size_t _limit;
};

}  // namespace microtex::front

#endif
