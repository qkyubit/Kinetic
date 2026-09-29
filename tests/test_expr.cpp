// Expression parser and equation-driven items.
#include "core/Expr.h"
#include "core/SceneBuilder.h"

#include <QTest>

using namespace kn;

namespace {

double evalExpr(const QString& src, const QStringList& vars = {}, const double* v = nullptr, expr::Error* err = nullptr)
{
    expr::Program p;
    expr::Symbols sym;
    sym.variables = vars;
    sym.constant = [](const QString& n) { return n == QLatin1String("k") ? 0 : (n == QLatin1String("g") ? 1 : -1); };
    if (!p.compile(src, sym, err)) return std::nan("");
    static const double constants[2] = {3.0, 9.81};
    return p.eval(v, constants);
}

expr::Error compileError(const QString& src)
{
    expr::Error e;
    const double r = evalExpr(src, {QStringLiteral("x")}, nullptr, &e);
    (void)r;
    return e;
}

Snapshot snap(const SimCore& core)
{
    Snapshot s;
    core.writeSnapshot(s);
    return s;
}

} // namespace

class TestExpr : public QObject {
    Q_OBJECT
private slots:
    void evaluation_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<double>("expected");
        QTest::newRow("precedence") << "1 + 2 * 3" << 7.0;
        QTest::newRow("parentheses") << "(1 + 2) * 3" << 9.0;
        QTest::newRow("left assoc") << "10 - 4 - 3" << 3.0;
        QTest::newRow("division") << "7 / 2" << 3.5;
        QTest::newRow("power right assoc") << "2 ^ 3 ^ 2" << 512.0;
        QTest::newRow("power alias") << "2 ** 10" << 1024.0;
        QTest::newRow("unary minus binds looser than power") << "-2 ^ 2" << -4.0;
        QTest::newRow("negative exponent") << "2 ^ -1" << 0.5;
        QTest::newRow("modulo") << "7.5 % 2" << 1.5;
        QTest::newRow("scientific") << "1.5e3 + .5 + 2E-1" << 1500.7;
        QTest::newRow("constants") << "pi" << M_PI;
        QTest::newRow("e") << "ln(e)" << 1.0;
        QTest::newRow("functions") << "sqrt(16) + abs(-2) + max(1, 5) + clamp(12, 0, 10)" << 21.0;
        QTest::newRow("trig") << "sin(pi/2) + cos(0) + atan2(1, 1)*4/pi" << 3.0;
        QTest::newRow("comparison") << "(3 > 2) + (2 >= 2) + (1 < 0) + (1 == 1) + (1 != 1)" << 3.0;
        QTest::newRow("logic") << "(1 && 0) + (1 || 0) + !0 + !5" << 2.0;
        QTest::newRow("ternary") << "2 > 1 ? 10 : 20" << 10.0;
        QTest::newRow("ternary false") << "2 < 1 ? 10 : 20" << 20.0;
        QTest::newRow("nested ternary") << "0 ? 1 : 0 ? 2 : 3" << 3.0;
        QTest::newRow("ternary in sum") << "1 + (1 ? 2 : 3) * 2" << 5.0;
        QTest::newRow("variable") << "x * 2 + y" << 7.0;
        QTest::newRow("named constant") << "k * g" << 3.0 * 9.81;
        QTest::newRow("step") << "step(x - 2) + step(-1)" << 1.0;
        QTest::newRow("whitespace") << "  \t1+\n2  " << 3.0;
    }

    void evaluation()
    {
        QFETCH(QString, src);
        QFETCH(double, expected);
        const double v[2] = {2.0, 3.0};
        expr::Error e;
        const double r = evalExpr(src, {QStringLiteral("x"), QStringLiteral("y")}, v, &e);
        QVERIFY2(std::abs(r - expected) < 1e-12, qPrintable(QStringLiteral("%1 = %2 (%3)").arg(src).arg(r).arg(e.toString())));
    }

    void errors_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("empty") << "" << 0 << "unexpected end";
        QTest::newRow("dangling operator") << "1 +" << 3 << "unexpected end";
        QTest::newRow("unknown name") << "x + foo" << 4 << "unknown name 'foo'";
        QTest::newRow("unknown function") << "bar(1)" << 0 << "unknown function 'bar'";
        QTest::newRow("arity") << "atan2(1)" << 0 << "takes 2 argument";
        QTest::newRow("unclosed") << "(1 + 2" << 6 << "missing ')'";
        QTest::newRow("extra close") << "1 + 2)" << 5 << "unexpected ')'";
        QTest::newRow("bad char") << "1 # 2" << 2 << "unexpected character '#'";
        QTest::newRow("missing colon") << "1 ? 2" << 5 << "expected ':'";
        QTest::newRow("function as value") << "sin + 1" << 0 << "is a function";
        QTest::newRow("double operator") << "1 * * 2" << 4 << "unexpected '*'";
        QTest::newRow("malformed number") << "1.2.3" << 0 << "malformed number";
    }

    void errors()
    {
        QFETCH(QString, src);
        QFETCH(int, column);
        QFETCH(QString, fragment);
        const expr::Error e = compileError(src);
        QVERIFY2(e.message.contains(fragment), qPrintable(e.toString()));
        QCOMPARE(e.column, column);
    }

    void limits()
    {
        // Deep nesting is rejected, not a stack overflow.
        const QString deep = QString(200, QLatin1Char('(')) + QStringLiteral("1") + QString(200, QLatin1Char(')'));
        QVERIFY(compileError(deep).message.contains(QLatin1String("nested too deeply")));
        QString unaries = QString(300, QLatin1Char('-')) + QStringLiteral("1");
        QVERIFY(compileError(unaries).message.contains(QLatin1String("nested too deeply")));
        // Moderate nesting is fine.
        const QString ok = QString(60, QLatin1Char('(')) + QStringLiteral("2") + QString(60, QLatin1Char(')'));
        QCOMPARE(evalExpr(ok), 2.0);
        // Length limit.
        QString longSrc = QStringLiteral("1");
        while (longSrc.size() <= expr::kMaxSourceLength) longSrc += QStringLiteral("+1");
        QVERIFY(compileError(longSrc).message.contains(QLatin1String("longer than")));
        // Long flat sums compile (stack stays shallow).
        QString sum = QStringLiteral("1");
        for (int i = 0; i < 1000; ++i) sum += QStringLiteral("+1");
        QCOMPARE(evalExpr(sum), 1001.0);
        // Right-nested powers need a deep stack: rejected cleanly.
        QString tower = QStringLiteral("1");
        for (int i = 0; i < 80; ++i) tower = QStringLiteral("1^(") + tower + QStringLiteral(")");
        const expr::Error e = compileError(tower);
        QVERIFY2(!e.message.isEmpty(), "power tower should be rejected");
    }

    void nonFinite()
    {
        QVERIFY(std::isinf(evalExpr("1/0")));
        QVERIFY(std::isnan(evalExpr("sqrt(-1)")));
        QVERIFY(std::isnan(evalExpr("0/0")));
        QVERIFY(std::isinf(evalExpr("exp(1000)")));
    }

    void odeHarmonicOscillator()
    {
        // x'' = -ω² x with RK4 at 120 Hz: error after 10 s far below 1e-6.
        SceneBuilder sb;
        const ItemId omega = sb.add("equation.constant", {0, 0}, {{"symbol", QStringLiteral("omega")}, {"value", 2.0}});
        const ItemId ode = sb.add("equation.ode", {0, 0},
                                  {{"rate1", QStringLiteral("v")}, {"rate2", QStringLiteral("-omega^2*x")}, {"initial1", 1.0}});
        QCOMPARE(sb.item(ode).d(sb.item(ode).type->schema().indexOf("initial2")), 0.0);
        QVERIFY(sb.get<QString>(ode, "status").isEmpty());
        SimCore core;
        core.start(sb.start(true));
        for (int i = 0; i < 1200; ++i) core.advance();
        const Snapshot s = snap(core);
        const ItemSnap* is = itemSnap(s, ode);
        const double t = s.time;
        qInfo("RK4: x(10) = %.9f (exact %.9f), v = %.9f (exact %.9f)", double(is->v[0]), std::cos(2 * t), double(is->v[1]),
              -2 * std::sin(2 * t));
        QVERIFY(std::abs(is->v[0] - std::cos(2 * t)) < 1e-5);
        QVERIFY(std::abs(is->v[1] + 2 * std::sin(2 * t)) < 1e-5);
        QVERIFY(is->v[6] == 0.0f);

        // Live constant edit: doubling omega changes the rate from then on.
        ItemDef d = sb.item(omega);
        d.set(d.type->schema().indexOf("value"), 4.0);
        LoggedCommand c;
        c.type = LoggedCommand::Type::Update;
        c.def = std::make_shared<ItemDef>(d);
        c.mask = propBit(d.type->schema().indexOf("value"));
        core.edit(c);
        const double x0 = is->v[0], v0 = is->v[1];
        for (int i = 0; i < 120; ++i) core.advance();
        const Snapshot s2 = snap(core);
        // Analytic continuation with ω = 4 over the elapsed time.
        const double dt = s2.time - s.time;
        const double x = x0 * std::cos(4 * dt) + v0 / 4 * std::sin(4 * dt);
        qInfo("after the edit: x = %.7f (exact %.7f)", double(itemSnap(s2, ode)->v[0]), x);
        QVERIFY(std::abs(itemSnap(s2, ode)->v[0] - x) < 1e-5);

        // Scrubbing back across the edit reproduces both halves exactly.
        core.scrubTo(600);
        for (int i = 600; i < 1320; ++i) core.advance();
        QCOMPARE(itemSnap(snap(core), ode)->v[0], itemSnap(s2, ode)->v[0]);
    }

    void undefinedConstantIsFlagged()
    {
        SceneBuilder sb;
        const ItemId ode = sb.add("equation.ode", {0, 0}, {{"rate2", QStringLiteral("-nothing*x")}});
        SimCore core;
        core.start(sb.start());
        for (int i = 0; i < 10; ++i) core.advance();
        const Snapshot s = snap(core);
        QCOMPARE(itemSnap(s, ode)->v[6], 1.0f);
        QCOMPARE(itemSnap(s, ode)->v[0], 2.0f); // state frozen at the last finite value
    }

    void editorValidation()
    {
        SceneBuilder sb;
        const ItemId path = sb.add("equation.path", {0, 0}, {{"x", QStringLiteral("cos(t")}});
        const QString status = sb.get<QString>(path, "status");
        QVERIFY2(status.contains(QLatin1String("x(t): column 6")), qPrintable(status));
    }

    void forceLawSpring()
    {
        // Radial F(r) = -k (r - L): a spring from a formula, period 2π sqrt(m/k).
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.add("equation.constant", {0, 0}, {{"symbol", QStringLiteral("k")}, {"value", 400.0}, {"max", 1000.0}});
        const ItemId b = sb.add("body.circle", {3, 0}, {{"radius", 0.2}});
        sb.connect("equation.force", b, {3, 0}, 0, {0, 0}, {{"fr", QStringLiteral("-k*(r - 2)")}});
        const double m = sb.get<double>(b, "mass");
        SimCore core;
        core.start(sb.start());
        // Time between the first two returns to maximum extension.
        double prev = 3, prevprev = 3, tLast = -1, period = 0;
        for (int i = 0; i < 1200; ++i) {
            core.advance();
            const Snapshot s = snap(core);
            const double x = body(s, b)->x;
            if (prev > prevprev && prev >= x) {
                if (tLast > 0 && period == 0) period = s.time - tLast;
                tLast = s.time;
            }
            prevprev = prev;
            prev = x;
        }
        const double expected = 2 * M_PI * std::sqrt(m / 400.0);
        qInfo("force-law spring period %.4f s (analytic %.4f)", period, expected);
        QVERIFY(std::abs(period - expected) < 2.0 / 120.0);
    }

    void parametricPathFollowsCurve()
    {
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        const ItemId p = sb.add("equation.path", {1, 1}, {{"x", QStringLiteral("2*cos(t)")}, {"y", QStringLiteral("sin(2*t)")}});
        SimCore core;
        core.start(sb.start());
        double worst = 0;
        for (int i = 1; i <= 600; ++i) {
            core.advance();
            const Snapshot s = snap(core);
            const double t = s.time;
            const ItemSnap* is = itemSnap(s, p);
            worst = std::max(worst, std::hypot(is->v[0] - (1 + 2 * std::cos(t)), is->v[1] - (1 + std::sin(2 * t))));
        }
        qInfo("parametric path: max position error %.2e m", worst);
        QVERIFY(worst < 1e-4);
    }
};

QTEST_GUILESS_MAIN(TestExpr)
#include "test_expr.moc"
