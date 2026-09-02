#include "vex/type.hpp"

namespace vex {

Type Type::primitive(TypeKind kind) { return Type{kind, "", nullptr, 0}; }

Type Type::make_struct(std::string name) { return Type{TypeKind::Struct, std::move(name), nullptr, 0}; }

Type Type::make_array(Type element, std::uint32_t size) {
    return Type{TypeKind::Array, "", std::make_shared<Type>(std::move(element)), size};
}

Type Type::unknown() { return Type{TypeKind::Unknown, "", nullptr, 0}; }

bool Type::is_numeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }

bool Type::operator==(const Type& other) const {
    if (kind != other.kind) return false;
    if (kind == TypeKind::Struct) return struct_name == other.struct_name;
    if (kind == TypeKind::Array) return array_size == other.array_size && *element_type == *other.element_type;
    return true;
}

std::string to_string(const Type& type) {
    switch (type.kind) {
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Bool: return "bool";
        case TypeKind::String: return "string";
        case TypeKind::Struct: return type.struct_name;
        case TypeKind::Array: return to_string(*type.element_type) + "[" + std::to_string(type.array_size) + "]";
        case TypeKind::Void: return "void";
        case TypeKind::Unknown: return "<unknown>";
    }
    return "<unknown>";
}

}  // namespace vex
