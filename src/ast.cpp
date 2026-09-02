#include "vex/ast.hpp"

#include <sstream>

namespace vex {

namespace {

const char* binary_op_symbol(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::EqEq: return "==";
        case TokenKind::BangEq: return "!=";
        case TokenKind::Less: return "<";
        case TokenKind::LessEq: return "<=";
        case TokenKind::Greater: return ">";
        case TokenKind::GreaterEq: return ">=";
        case TokenKind::AmpAmp: return "&&";
        case TokenKind::PipePipe: return "||";
        default: return "?";
    }
}

const char* unary_op_symbol(TokenKind kind) {
    switch (kind) {
        case TokenKind::Minus: return "-";
        case TokenKind::Bang: return "!";
        default: return "?";
    }
}

void dump(const Expr& expr, std::ostringstream& out);

struct DumpVisitor {
    std::ostringstream& out;

    void operator()(const IntLiteral& n) const { out << n.value; }
    void operator()(const FloatLiteral& n) const { out << n.value; }
    void operator()(const BoolLiteral& n) const { out << (n.value ? "true" : "false"); }
    void operator()(const StringLiteral& n) const { out << '"' << n.value << '"'; }
    void operator()(const Identifier& n) const { out << n.name; }

    void operator()(const UnaryExpr& n) const {
        out << '(' << unary_op_symbol(n.op) << ' ';
        dump(*n.operand, out);
        out << ')';
    }

    void operator()(const BinaryExpr& n) const {
        out << '(' << binary_op_symbol(n.op) << ' ';
        dump(*n.lhs, out);
        out << ' ';
        dump(*n.rhs, out);
        out << ')';
    }

    void operator()(const CallExpr& n) const {
        out << "(call ";
        dump(*n.callee, out);
        for (const ExprPtr& arg : n.args) {
            out << ' ';
            dump(*arg, out);
        }
        out << ')';
    }

    void operator()(const IndexExpr& n) const {
        out << "(index ";
        dump(*n.object, out);
        out << ' ';
        dump(*n.index, out);
        out << ')';
    }

    void operator()(const FieldAccessExpr& n) const {
        out << "(field ";
        dump(*n.object, out);
        out << ' ' << n.field << ')';
    }

    void operator()(const ArrayLiteral& n) const {
        out << "(array";
        for (const ExprPtr& elem : n.elements) {
            out << ' ';
            dump(*elem, out);
        }
        out << ')';
    }
};

void dump(const Expr& expr, std::ostringstream& out) { std::visit(DumpVisitor{out}, expr.node); }

}  // namespace

std::string dump_expr(const Expr& expr) {
    std::ostringstream out;
    dump(expr, out);
    return out.str();
}

}  // namespace vex
