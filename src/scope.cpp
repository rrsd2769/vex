#include "vex/scope.hpp"

namespace vex {

bool Scope::declare(Symbol symbol) {
    std::string name = symbol.name;
    return symbols_.emplace(std::move(name), std::move(symbol)).second;
}

const Symbol* Scope::resolve(const std::string& name) const {
    for (const Scope* scope = this; scope != nullptr; scope = scope->parent_) {
        auto it = scope->symbols_.find(name);
        if (it != scope->symbols_.end()) return &it->second;
    }
    return nullptr;
}

std::vector<std::string> Scope::visible_names() const {
    std::vector<std::string> names;
    for (const Scope* scope = this; scope != nullptr; scope = scope->parent_) {
        for (const auto& [name, symbol] : scope->symbols_) names.push_back(name);
    }
    return names;
}

}  // namespace vex
