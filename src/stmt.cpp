#include "vex/stmt.hpp"

#include <sstream>

#include "vex/type.hpp"

namespace vex {

std::optional<Type> try_resolve_type_ref(const TypeRef& ref,
                                          const std::unordered_map<std::string, const StructDecl*>& structs) {
    Type base = Type::unknown();

    if (ref.name == "int") {
        base = Type::primitive(TypeKind::Int);
    } else if (ref.name == "float") {
        base = Type::primitive(TypeKind::Float);
    } else if (ref.name == "bool") {
        base = Type::primitive(TypeKind::Bool);
    } else if (ref.name == "string") {
        base = Type::primitive(TypeKind::String);
    } else if (structs.contains(ref.name)) {
        base = Type::make_struct(ref.name);
    } else {
        return std::nullopt;
    }

    return ref.array_size ? Type::make_array(std::move(base), *ref.array_size) : base;
}

namespace {

void dump_block(const Block& block, std::ostringstream& out) {
    out << "(block";
    for (const StmtPtr& stmt : block.statements) {
        out << ' ' << dump_stmt(*stmt);
    }
    out << ')';
}

std::string type_ref_name(const TypeRef& type) {
    return type.array_size ? type.name + "[" + std::to_string(*type.array_size) + "]" : type.name;
}

std::string type_name_or_placeholder(const std::optional<TypeRef>& type) {
    return type ? type_ref_name(*type) : "_";
}

struct StmtDumpVisitor {
    std::ostringstream& out;

    void operator()(const VarDecl& n) const {
        out << '(' << (n.is_mutable ? "var" : "let") << ' ' << n.name << ' ' << type_name_or_placeholder(n.type)
            << ' ' << dump_expr(*n.init) << ')';
    }

    void operator()(const AssignStmt& n) const {
        out << "(= " << dump_expr(*n.target) << ' ' << dump_expr(*n.value) << ')';
    }

    void operator()(const ExprStmt& n) const { out << dump_expr(*n.expr); }

    void operator()(const IfStmt& n) const {
        out << "(if " << dump_expr(*n.condition) << ' ';
        dump_block(n.then_block, out);
        out << ' ';
        if (n.else_block) {
            dump_block(*n.else_block, out);
        } else {
            out << '-';
        }
        out << ')';
    }

    void operator()(const WhileStmt& n) const {
        out << "(while " << dump_expr(*n.condition) << ' ';
        dump_block(n.body, out);
        out << ')';
    }

    void operator()(const ForStmt& n) const {
        out << "(for " << n.var_name << ' ' << dump_expr(*n.range_start) << ' ' << dump_expr(*n.range_end) << ' ';
        dump_block(n.body, out);
        out << ')';
    }

    void operator()(const ReturnStmt& n) const {
        out << "(return " << (n.value ? dump_expr(*n.value) : std::string("-")) << ')';
    }
};

}  // namespace

std::string dump_stmt(const Stmt& stmt) {
    std::ostringstream out;
    std::visit(StmtDumpVisitor{out}, stmt.node);
    return out.str();
}

std::string dump_item(const Item& item) {
    std::ostringstream out;
    if (const auto* fn = std::get_if<FunctionDecl>(&item.node)) {
        out << "(fn " << fn->name << " (";
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            if (i > 0) out << ' ';
            out << '(' << fn->params[i].name << ' ' << type_ref_name(fn->params[i].type) << ')';
        }
        out << ") " << type_name_or_placeholder(fn->return_type) << ' ';
        dump_block(fn->body, out);
        out << ')';
    } else {
        const auto& st = std::get<StructDecl>(item.node);
        out << "(struct " << st.name;
        for (const Field& field : st.fields) {
            out << " (" << field.name << ' ' << type_ref_name(field.type) << ')';
        }
        out << ')';
    }
    return out.str();
}

std::string dump_program(const Program& program) {
    std::ostringstream out;
    for (std::size_t i = 0; i < program.items.size(); ++i) {
        if (i > 0) out << '\n';
        out << dump_item(program.items[i]);
    }
    return out.str();
}

}  // namespace vex
