#include "vex/type.hpp"

namespace vex {

Type Type::primitive(TypeKind kind) { return Type{kind, ""}; }

Type Type::make_struct(std::string name) { return Type{TypeKind::Struct, std::move(name)}; }

Type Type::unknown() { return Type{TypeKind::Unknown, ""}; }

bool Type::is_numeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }

bool Type::operator==(const Type& other) const {
    if (kind != other.kind) return false;
    if (kind == TypeKind::Struct) return struct_name == other.struct_name;
    return true;
}

std::string to_string(const Type& type) {
    switch (type.kind) {
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Bool: return "bool";
        case TypeKind::String: return "string";
        case TypeKind::Struct: return type.struct_name;
        case TypeKind::Void: return "void";
        case TypeKind::Unknown: return "<unknown>";
    }
    return "<unknown>";
}

}  // namespace vex
