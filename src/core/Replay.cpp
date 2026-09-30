#include "Replay.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace kn {

namespace {
constexpr int kReplayVersion = 1;

const char* typeName(LoggedCommand::Type t)
{
    switch (t) {
    case LoggedCommand::Type::Add: return "add";
    case LoggedCommand::Type::Remove: return "remove";
    case LoggedCommand::Type::Update: return "update";
    case LoggedCommand::Type::SetWorld: return "world";
    case LoggedCommand::Type::SetState: return "state";
    case LoggedCommand::Type::Grab: return "grab";
    case LoggedCommand::Type::GrabMove: return "grabMove";
    case LoggedCommand::Type::Release: return "release";
    }
    return "";
}
} // namespace

QByteArray replayToBytes(const ReplayData& r, const ViewState& view)
{
    SceneData scene = SceneData::empty();
    if (r.world) scene.world = *r.world;
    for (const ItemDefPtr& d : r.items) scene.items.push_back(*d);
    scene.view = view;

    QJsonArray commands;
    for (const LoggedCommand& c : r.log) {
        QJsonObject o;
        o.insert(QStringLiteral("step"), QString::number(c.step));
        o.insert(QStringLiteral("type"), QLatin1String(typeName(c.type)));
        if (c.type == LoggedCommand::Type::Remove) {
            o.insert(QStringLiteral("id"), double(c.id));
        } else if (c.type == LoggedCommand::Type::Grab || c.type == LoggedCommand::Type::GrabMove ||
                   c.type == LoggedCommand::Type::Release) {
            o.insert(QStringLiteral("id"), double(c.id));
            o.insert(QStringLiteral("anchor"), QJsonArray{c.v.x, c.v.y});
            o.insert(QStringLiteral("target"), QJsonArray{c.pose.p.x, c.pose.p.y});
        } else if (c.type == LoggedCommand::Type::SetState) {
            o.insert(QStringLiteral("id"), double(c.id));
            o.insert(QStringLiteral("pose"), QJsonArray{c.pose.p.x, c.pose.p.y, c.pose.c, c.pose.s});
            o.insert(QStringLiteral("v"), QJsonArray{c.v.x, c.v.y, c.w});
        } else if (c.def)
            o.insert(QStringLiteral("item"), itemToJson(*c.def));
        // 64-bit masks and step counts do not fit a JSON double exactly.
        if (c.type == LoggedCommand::Type::Update) o.insert(QStringLiteral("mask"), QString::number(c.mask, 16));
        commands.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("kinetic-replay"));
    root.insert(QStringLiteral("version"), kReplayVersion);
    root.insert(QStringLiteral("scene"), sceneToJson(scene));
    root.insert(QStringLiteral("headStep"), QString::number(r.headStep));
    root.insert(QStringLiteral("commands"), commands);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool replayFromBytes(const QByteArray& bytes, ReplayFile& out, QString* error)
{
    auto fail = [&](const QString& e) {
        if (error) *error = e;
        return false;
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (!doc.isObject()) return fail(pe.errorString());
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("kinetic-replay"))
        return fail(QStringLiteral("not a Kinetic replay file"));
    if (root.value(QStringLiteral("version")).toInt() > kReplayVersion)
        return fail(QStringLiteral("the replay was written by a newer version of Kinetic"));

    SceneLoadResult s = sceneFromJson(root.value(QStringLiteral("scene")).toObject());
    if (!s.ok) return fail(s.error);
    out.scene = std::move(s.scene);
    out.replay = {};
    out.replay.world = std::make_shared<ItemDef>(out.scene.world);
    for (const ItemDef& d : out.scene.items) out.replay.items.push_back(std::make_shared<ItemDef>(d));

    bool ok = true;
    out.replay.headStep = root.value(QStringLiteral("headStep")).toString().toULongLong(&ok);
    if (!ok) return fail(QStringLiteral("missing head step"));
    uint64_t last = 0;
    for (const QJsonValue& v : root.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject o = v.toObject();
        LoggedCommand c;
        c.step = o.value(QStringLiteral("step")).toString().toULongLong(&ok);
        if (!ok || c.step < last || c.step > out.replay.headStep) return fail(QStringLiteral("bad command step"));
        last = c.step;
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("remove")) {
            c.type = LoggedCommand::Type::Remove;
            c.id = ItemId(o.value(QStringLiteral("id")).toDouble());
        } else if (type == QLatin1String("grab") || type == QLatin1String("grabMove") || type == QLatin1String("release")) {
            c.type = type == QLatin1String("grab")       ? LoggedCommand::Type::Grab
                     : type == QLatin1String("grabMove") ? LoggedCommand::Type::GrabMove
                                                         : LoggedCommand::Type::Release;
            c.id = ItemId(o.value(QStringLiteral("id")).toDouble());
            const QJsonArray an = o.value(QStringLiteral("anchor")).toArray(), ta = o.value(QStringLiteral("target")).toArray();
            if (an.size() != 2 || ta.size() != 2) return fail(QStringLiteral("bad grab command"));
            c.v = {an[0].toDouble(), an[1].toDouble()};
            c.pose.p = {ta[0].toDouble(), ta[1].toDouble()};
        } else if (type == QLatin1String("state")) {
            c.type = LoggedCommand::Type::SetState;
            c.id = ItemId(o.value(QStringLiteral("id")).toDouble());
            const QJsonArray pa = o.value(QStringLiteral("pose")).toArray(), va = o.value(QStringLiteral("v")).toArray();
            if (pa.size() != 4 || va.size() != 3) return fail(QStringLiteral("bad body state"));
            c.pose = Pose{{pa[0].toDouble(), pa[1].toDouble()}, pa[2].toDouble(), pa[3].toDouble()};
            c.v = {va[0].toDouble(), va[1].toDouble()};
            c.w = va[2].toDouble();
        } else {
            if (type == QLatin1String("add"))
                c.type = LoggedCommand::Type::Add;
            else if (type == QLatin1String("update"))
                c.type = LoggedCommand::Type::Update;
            else if (type == QLatin1String("world"))
                c.type = LoggedCommand::Type::SetWorld;
            else
                return fail(QStringLiteral("unknown command '%1'").arg(type));
            ItemDef d;
            QString e;
            if (!itemFromJson(o.value(QStringLiteral("item")).toObject(), d, &e, nullptr)) return fail(e);
            c.def = std::make_shared<ItemDef>(std::move(d));
            if (c.type == LoggedCommand::Type::Update) {
                c.mask = o.value(QStringLiteral("mask")).toString().toULongLong(&ok, 16);
                if (!ok) return fail(QStringLiteral("bad update mask"));
            }
        }
        out.replay.log.push_back(std::move(c));
    }
    return true;
}

} // namespace kn
