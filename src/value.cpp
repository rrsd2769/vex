#include "vex/value.hpp"

#include <string>

namespace vex {

const std::string* Arena::allocate(std::string value) {
    storage_.push_back(std::move(value));
    return &storage_.back();
}

void print_value(std::ostream& out, const Value& value) {
    if (const auto* v = std::get_if<std::int64_t>(&value.data)) {
        out << *v;
        return;
    }
    if (const auto* v = std::get_if<double>(&value.data)) {
        out << std::to_string(*v);
        return;
    }
    if (const auto* v = std::get_if<bool>(&value.data)) {
        out << (*v ? "true" : "false");
        return;
    }
    out << *std::get<const std::string*>(value.data);
}

}  // namespace vex
