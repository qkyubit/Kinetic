// Expression language for equation-driven items. A hand-written recursive
// descent parser compiles the source to a small stack bytecode; evaluation
// never allocates and cannot run arbitrary code.
//
//   expr    := cond
//   cond    := or ('?' cond ':' cond)?
//   or      := and ('||' and)*
//   and     := cmp ('&&' cmp)*
//   cmp     := sum (('<' | '<=' | '>' | '>=' | '==' | '!=') sum)?
//   sum     := product (('+' | '-') product)*
//   product := unary (('*' | '/' | '%') unary)*
//   unary   := ('-' | '+' | '!') unary | power
//   power   := primary ('^' unary)?          (right-associative; -x^2 = -(x^2))
//   primary := number | name | name '(' args ')' | '(' expr ')'
//
// Names are variables supplied by the item (t, x, vx...), the constants pi and
// e, or named constants from Constant items. Comparisons and logic yield 1 or 0.
#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>

namespace kn::expr {

struct Error {
    QString message;
    int column = -1; // 0-based character offset, -1 when not applicable
    QString toString() const;
};

struct Symbols {
    QStringList variables; // name -> index into the variable array
    // Named constant -> slot in the constants array, or -1 if unknown.
    std::function<int(const QString&)> constant;
};

// Built-in function names (for help texts).
const QStringList& functionNames();

constexpr int kMaxSourceLength = 4096;
constexpr int kMaxDepth = 256; // recursion levels (about 85 nested parentheses)
constexpr int kMaxCode = 4096;
constexpr int kMaxStack = 64;

class Program {
public:
    bool compile(const QString& source, const Symbols& symbols, Error* error = nullptr);
    // Evaluates with the given variables and constants. Non-finite results
    // (division by zero, sqrt of a negative...) are returned as they are; callers
    // decide how to treat them.
    double eval(const double* vars, const double* constants) const;
    bool valid() const { return !code_.empty(); }
    void clear() { code_.clear(); }
    size_t size() const { return code_.size(); }

    enum class Op : uint8_t {
        Num, Var, Const, Neg, Not, Add, Sub, Mul, Div, Mod, Pow,
        Lt, Le, Gt, Ge, Eq, Ne, And, Or, Call1, Call2, Call3, JumpIfZero, Jump,
    };
    struct Ins {
        Op op;
        uint8_t fn = 0;
        int32_t arg = 0; // variable/constant index, jump target
        double value = 0.0;
    };

private:
    std::vector<Ins> code_;
};

} // namespace kn::expr
