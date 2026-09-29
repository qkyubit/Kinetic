// Trampoline: a static pad with very high restitution.
#include "BoxBody.h"

#include "core/RenderBatch.h"

namespace kn {

class TrampolineBody final : public BoxBody {
public:
    TrampolineBody()
        : BoxBody({QStringLiteral("mechanism.trampoline"), QStringLiteral("Trampoline"), QStringLiteral("Mechanisms"),
                   QStringLiteral("go-up"), QStringLiteral(":/icons/tool.svg"),
                   {QStringLiteral("bounce"), QStringLiteral("bouncy"), QStringLiteral("elastic"), QStringLiteral("spring board")},
                   QStringLiteral("Static pad with restitution close to 1 (bodies keep most of their speed).")},
                  Defaults{3.0, 0.25, 3, 0, Color::fromRgb(0x2980b9)})
    {
        auto& e = schema_.at(idx_.restitution);
        e.def = 0.95;
        schema_.at(idx_.material).def = int64_t(0); // custom: rubber density and friction, higher restitution
    }

    void drawExtras(const ItemDef& d, const DrawContext& ctx) const override
    {
        const double w = d.d(width_), h = d.d(height_);
        const Color c = ctx.theme.dim;
        for (double x : {-0.4 * w, 0.4 * w}) {
            ctx.batch.line(Layer::Back, ctx.pose.apply({x, -h / 2}), ctx.pose.apply({x - 0.1 * w, -h / 2 - 0.6}), 3.0f, c);
            ctx.batch.line(Layer::Back, ctx.pose.apply({x, -h / 2}), ctx.pose.apply({x + 0.1 * w, -h / 2 - 0.6}), 3.0f, c);
        }
    }
};

KINETIC_DEFINE_ITEM(TrampolineBody)

} // namespace kn
