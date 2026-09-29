#include "Expr.h"

#include <cmath>

namespace kn::expr {

QString Error::toString() const
{
    return column >= 0 ? QStringLiteral("column %1: %2").arg(column + 1).arg(message) : message;
}

namespace {

enum Fn : uint8_t {
    Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Exp, Ln, Log10, Log2, Sqrt, Cbrt, Abs, Sign, Floor, Ceil, Round,
    Step, // one argument
    Atan2, Min, Max, PowFn, Hypot, Fmod, // two
    Clamp, Lerp, // three
};

struct FnInfo {
    const char* name;
    Fn fn;
    int arity;
};

constexpr FnInfo kFunctions[] = {
    {"sin", Sin, 1},     {"cos", Cos, 1},     {"tan", Tan, 1},     {"asin", Asin, 1},   {"acos", Acos, 1},
    {"atan", Atan, 1},   {"sinh", Sinh, 1},   {"cosh", Cosh, 1},   {"tanh", Tanh, 1},   {"exp", Exp, 1},
    {"ln", Ln, 1},       {"log", Ln, 1},      {"log10", Log10, 1}, {"log2", Log2, 1},   {"sqrt", Sqrt, 1},
    {"cbrt", Cbrt, 1},   {"abs", Abs, 1},     {"sign", Sign, 1},   {"floor", Floor, 1}, {"ceil", Ceil, 1},
    {"round", Round, 1}, {"step", Step, 1},   {"atan2", Atan2, 2}, {"min", Min, 2},     {"max", Max, 2},
    {"pow", PowFn, 2},   {"hypot", Hypot, 2}, {"mod", Fmod, 2},    {"clamp", Clamp, 3}, {"lerp", Lerp, 3},
};

double call1(uint8_t f, double a)
{
    switch (Fn(f)) {
    case Sin: return std::sin(a);
    case Cos: return std::cos(a);
    case Tan: return std::tan(a);
    case Asin: return std::asin(a);
    case Acos: return std::acos(a);
    case Atan: return std::atan(a);
    case Sinh: return std::sinh(a);
    case Cosh: return std::cosh(a);
    case Tanh: return std::tanh(a);
    case Exp: return std::exp(a);
    case Ln: return std::log(a);
    case Log10: return std::log10(a);
    case Log2: return std::log2(a);
    case Sqrt: return std::sqrt(a);
    case Cbrt: return std::cbrt(a);
    case Abs: return std::abs(a);
    case Sign: return a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0);
    case Floor: return std::floor(a);
    case Ceil: return std::ceil(a);
    case Round: return std::round(a);
    case Step: return a >= 0 ? 1.0 : 0.0;
    default: return std::nan("");
    }
}

double call2(uint8_t f, double a, double b)
{
    switch (Fn(f)) {
    case Atan2: return std::atan2(a, b);
    case Min: return std::min(a, b);
    case Max: return std::max(a, b);
    case PowFn: return std::pow(a, b);
    case Hypot: return std::hypot(a, b);
    case Fmod: return std::fmod(a, b);
    default: return std::nan("");
    }
}

double call3(uint8_t f, double a, double b, double c)
{
    switch (Fn(f)) {
    case Clamp: return std::min(std::max(a, b), c);
    case Lerp: return a + (b - a) * c;
    default: return std::nan("");
    }
}

using Ins = Program::Ins;
using Op = Program::Op;

class Parser {
public:
    Parser(const QString& src, const Symbols& sym, std::vector<Ins>& out) : s_(src), sym_(sym), code_(out) {}

    bool run(Error* err)
    {
        next();
        if (!expr()) return fail(err);
        if (tok_ != Tok::End) {
            error(QStringLiteral("unexpected '%1'").arg(text()), start_);
            return fail(err);
        }
        return true;
    }

private:
    enum class Tok { End, Number, Name, Op, Error };

    bool fail(Error* err)
    {
        if (err) *err = err_;
        return false;
    }
    bool error(const QString& msg, int col)
    {
        if (err_.message.isEmpty()) err_ = {msg, col};
        return false;
    }
    QString text() const { return s_.mid(start_, pos_ - start_); }

    void next()
    {
        while (pos_ < s_.size() && s_[pos_].isSpace()) ++pos_;
        start_ = pos_;
        if (pos_ >= s_.size()) {
            tok_ = Tok::End;
            return;
        }
        const QChar c = s_[pos_];
        if (c.isDigit() || (c == QLatin1Char('.') && pos_ + 1 < s_.size() && s_[pos_ + 1].isDigit())) {
            while (pos_ < s_.size() && (s_[pos_].isDigit() || s_[pos_] == QLatin1Char('.'))) ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == QLatin1Char('e') || s_[pos_] == QLatin1Char('E'))) {
                int p = pos_ + 1;
                if (p < s_.size() && (s_[p] == QLatin1Char('+') || s_[p] == QLatin1Char('-'))) ++p;
                if (p < s_.size() && s_[p].isDigit()) {
                    pos_ = p;
                    while (pos_ < s_.size() && s_[pos_].isDigit()) ++pos_;
                }
            }
            bool ok = false;
            number_ = s_.mid(start_, pos_ - start_).toDouble(&ok);
            tok_ = ok ? Tok::Number : Tok::Error;
            if (!ok) error(QStringLiteral("malformed number '%1'").arg(text()), start_);
            return;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            while (pos_ < s_.size() && (s_[pos_].isLetterOrNumber() || s_[pos_] == QLatin1Char('_'))) ++pos_;
            tok_ = Tok::Name;
            return;
        }
        static const char* two[] = {"<=", ">=", "==", "!=", "&&", "||", "**"};
        for (const char* t : two) {
            if (s_.mid(pos_, 2) == QLatin1String(t)) {
                pos_ += 2;
                tok_ = Tok::Op;
                return;
            }
        }
        if (QStringLiteral("+-*/%^()<>!?:,").contains(c)) {
            ++pos_;
            tok_ = Tok::Op;
            return;
        }
        ++pos_;
        tok_ = Tok::Error;
        error(QStringLiteral("unexpected character '%1'").arg(c), start_);
    }

    bool isOp(const char* o) const { return tok_ == Tok::Op && text() == QLatin1String(o); }

    bool put(Op op, int32_t arg = 0, double value = 0.0, uint8_t fn = 0)
    {
        if (int(code_.size()) >= kMaxCode) return error(QStringLiteral("expression is too long"), start_);
        code_.push_back({op, fn, arg, value});
        return true;
    }

    struct Depth {
        Parser& p;
        explicit Depth(Parser& parser) : p(parser) { ++p.depth_; }
        ~Depth() { --p.depth_; }
    };

    bool expr() { return cond(); }

    bool cond()
    {
        Depth d(*this);
        if (depth_ > kMaxDepth) return error(QStringLiteral("expression is nested too deeply"), start_);
        if (!orExpr()) return false;
        if (!isOp("?")) return true;
        next();
        const size_t jz = code_.size();
        if (!put(Op::JumpIfZero)) return false;
        if (!cond()) return false;
        if (!isOp(":")) return error(QStringLiteral("expected ':' in a conditional"), start_);
        next();
        const size_t j = code_.size();
        if (!put(Op::Jump)) return false;
        code_[jz].arg = int32_t(code_.size());
        if (!cond()) return false;
        code_[j].arg = int32_t(code_.size());
        return true;
    }

    bool orExpr()
    {
        if (!andExpr()) return false;
        while (isOp("||")) {
            next();
            if (!andExpr() || !put(Op::Or)) return false;
        }
        return true;
    }

    bool andExpr()
    {
        if (!cmp()) return false;
        while (isOp("&&")) {
            next();
            if (!cmp() || !put(Op::And)) return false;
        }
        return true;
    }

    bool cmp()
    {
        if (!sum()) return false;
        static const std::pair<const char*, Op> ops[] = {{"<=", Op::Le}, {">=", Op::Ge}, {"==", Op::Eq},
                                                         {"!=", Op::Ne}, {"<", Op::Lt},  {">", Op::Gt}};
        for (const auto& [t, op] : ops) {
            if (!isOp(t)) continue;
            next();
            return sum() && put(op);
        }
        return true;
    }

    bool sum()
    {
        if (!product()) return false;
        while (isOp("+") || isOp("-")) {
            const Op op = isOp("+") ? Op::Add : Op::Sub;
            next();
            if (!product() || !put(op)) return false;
        }
        return true;
    }

    bool product()
    {
        if (!unary()) return false;
        while (isOp("*") || isOp("/") || isOp("%")) {
            const Op op = isOp("*") ? Op::Mul : (isOp("/") ? Op::Div : Op::Mod);
            next();
            if (!unary() || !put(op)) return false;
        }
        return true;
    }

    bool unary()
    {
        Depth d(*this);
        if (depth_ > kMaxDepth) return error(QStringLiteral("expression is nested too deeply"), start_);
        if (isOp("-") || isOp("!")) {
            const Op op = isOp("-") ? Op::Neg : Op::Not;
            next();
            return unary() && put(op);
        }
        if (isOp("+")) {
            next();
            return unary();
        }
        return power();
    }

    bool power()
    {
        if (!primary()) return false;
        if (isOp("^") || isOp("**")) {
            next();
            return unary() && put(Op::Pow);
        }
        return true;
    }

    bool primary()
    {
        Depth d(*this);
        if (depth_ > kMaxDepth) return error(QStringLiteral("expression is nested too deeply"), start_);
        switch (tok_) {
        case Tok::Number: {
            const double v = number_;
            next();
            return put(Op::Num, 0, v);
        }
        case Tok::Name: return name();
        case Tok::Op:
            if (isOp("(")) {
                const int open = start_;
                next();
                if (!expr()) return false;
                if (!isOp(")")) return error(QStringLiteral("missing ')' for '(' at column %1").arg(open + 1), start_);
                next();
                return true;
            }
            return error(QStringLiteral("unexpected '%1'").arg(text()), start_);
        case Tok::End: return error(QStringLiteral("unexpected end of expression"), start_);
        case Tok::Error: return false;
        }
        return false;
    }

    bool name()
    {
        const QString n = text();
        const int col = start_;
        next();
        if (isOp("(")) {
            const FnInfo* f = nullptr;
            for (const FnInfo& info : kFunctions)
                if (n == QLatin1String(info.name)) f = &info;
            if (!f) return error(QStringLiteral("unknown function '%1'").arg(n), col);
            next();
            int args = 0;
            if (!isOp(")")) {
                for (;;) {
                    if (!expr()) return false;
                    ++args;
                    if (isOp(",")) {
                        next();
                        continue;
                    }
                    break;
                }
            }
            if (!isOp(")")) return error(QStringLiteral("expected ')' after the arguments of %1").arg(n), start_);
            next();
            if (args != f->arity)
                return error(QStringLiteral("%1() takes %2 argument(s), not %3").arg(n).arg(f->arity).arg(args), col);
            const Op op = f->arity == 1 ? Op::Call1 : (f->arity == 2 ? Op::Call2 : Op::Call3);
            return put(op, 0, 0.0, uint8_t(f->fn));
        }
        const int v = sym_.variables.indexOf(n);
        if (v >= 0) return put(Op::Var, v);
        if (n == QLatin1String("pi")) return put(Op::Num, 0, 3.14159265358979323846);
        if (n == QLatin1String("e")) return put(Op::Num, 0, 2.71828182845904523536);
        for (const FnInfo& info : kFunctions)
            if (n == QLatin1String(info.name)) return error(QStringLiteral("%1 is a function; call it as %1(...)").arg(n), col);
        const int c = sym_.constant ? sym_.constant(n) : -1;
        if (c >= 0) return put(Op::Const, c);
        return error(QStringLiteral("unknown name '%1'").arg(n), col);
    }

    const QString& s_;
    const Symbols& sym_;
    std::vector<Ins>& code_;
    Error err_;
    Tok tok_ = Tok::End;
    int pos_ = 0, start_ = 0, depth_ = 0;
    double number_ = 0.0;
};

// Maximum stack depth the code reaches (jumps only skip balanced branches).
int stackNeed(const std::vector<Ins>& code)
{
    int d = 0, worst = 0;
    for (const Ins& i : code) {
        switch (i.op) {
        case Op::Num:
        case Op::Var:
        case Op::Const: ++d; break;
        case Op::Neg:
        case Op::Not:
        case Op::Call1:
        case Op::Jump: break;
        case Op::Call3: d -= 2; break;
        case Op::JumpIfZero: --d; break;
        default: --d; break; // binary operators and Call2
        }
        worst = std::max(worst, d);
    }
    return worst;
}

} // namespace

const QStringList& functionNames()
{
    static const QStringList names = [] {
        QStringList l;
        for (const FnInfo& f : kFunctions) l << QString::fromLatin1(f.name);
        return l;
    }();
    return names;
}

bool Program::compile(const QString& source, const Symbols& symbols, Error* error)
{
    code_.clear();
    if (source.size() > kMaxSourceLength) {
        if (error) *error = {QStringLiteral("expression is longer than %1 characters").arg(kMaxSourceLength), kMaxSourceLength};
        return false;
    }
    std::vector<Ins> code;
    Parser p(source, symbols, code);
    if (!p.run(error)) return false;
    if (stackNeed(code) > kMaxStack) {
        if (error) *error = {QStringLiteral("expression needs too much evaluation stack"), -1};
        return false;
    }
    code_ = std::move(code);
    return true;
}

double Program::eval(const double* vars, const double* constants) const
{
    double st[kMaxStack + 1];
    int sp = 0;
    const size_t n = code_.size();
    for (size_t pc = 0; pc < n; ++pc) {
        const Ins& i = code_[pc];
        switch (i.op) {
        case Op::Num: st[sp++] = i.value; break;
        case Op::Var: st[sp++] = vars[i.arg]; break;
        case Op::Const: st[sp++] = constants[i.arg]; break;
        case Op::Neg: st[sp - 1] = -st[sp - 1]; break;
        case Op::Not: st[sp - 1] = st[sp - 1] == 0.0 ? 1.0 : 0.0; break;
        case Op::Call1: st[sp - 1] = call1(i.fn, st[sp - 1]); break;
        case Op::Call2:
            --sp;
            st[sp - 1] = call2(i.fn, st[sp - 1], st[sp]);
            break;
        case Op::Call3:
            sp -= 2;
            st[sp - 1] = call3(i.fn, st[sp - 1], st[sp], st[sp + 1]);
            break;
        case Op::JumpIfZero:
            if (st[--sp] == 0.0) pc = size_t(i.arg) - 1;
            break;
        case Op::Jump: pc = size_t(i.arg) - 1; break;
        default: {
            const double b = st[--sp];
            double& a = st[sp - 1];
            switch (i.op) {
            case Op::Add: a += b; break;
            case Op::Sub: a -= b; break;
            case Op::Mul: a *= b; break;
            case Op::Div: a /= b; break;
            case Op::Mod: a = std::fmod(a, b); break;
            case Op::Pow: a = std::pow(a, b); break;
            case Op::Lt: a = a < b; break;
            case Op::Le: a = a <= b; break;
            case Op::Gt: a = a > b; break;
            case Op::Ge: a = a >= b; break;
            case Op::Eq: a = a == b; break;
            case Op::Ne: a = a != b; break;
            case Op::And: a = (a != 0.0 && b != 0.0); break;
            case Op::Or: a = (a != 0.0 || b != 0.0); break;
            default: break;
            }
        }
        }
    }
    return sp > 0 ? st[sp - 1] : std::nan("");
}

} // namespace kn::expr
