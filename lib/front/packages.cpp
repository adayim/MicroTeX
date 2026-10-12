#include "front/packages.h"

#include <map>
#include <utility>

namespace microtex::front {

namespace {

struct Store {
  std::string prelude;
  std::map<std::string, std::string> packages;
  std::uint64_t generation = 0;
};

Store& store() {
  static Store s;
  return s;
}

}  // namespace

void setPrelude(std::string source) {
  store().prelude = std::move(source);
  store().generation++;
}

const std::string& preludeText() {
  return store().prelude;
}

void registerPackage(const std::string& name, std::string source) {
  store().packages[name] = std::move(source);
  store().generation++;
}

const std::string* packageSource(const std::string& name) {
  const auto it = store().packages.find(name);
  return it == store().packages.end() ? nullptr : &it->second;
}

std::vector<std::string> packageNames() {
  std::vector<std::string> names;
  for (const auto& kv : store().packages) names.push_back(kv.first);
  return names;
}

std::uint64_t packageGeneration() {
  return store().generation;
}

}  // namespace microtex::front
