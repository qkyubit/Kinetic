// Programmatic scene construction (tests, benchmarks, the example generator).
#pragma once

#include "core/Scene.h"
#include "core/Sim.h"

#include <QtGlobal>

#include <initializer_list>
#include <utility>

namespace kn {

class SceneBuilder : public SceneView {
public:
    SceneData scene = SceneData::empty();

    const ItemDef* def(ItemId id) const override { return scene.find(id); }
    bool pose(ItemId id, Pose& out) const override
    {
        const ItemDef* d = scene.find(id);
        if (!d || !d->type->hasPose()) return false;
        out = d->type->pose(*d);
        return true;
    }

    ItemDef& item(ItemId id) { return *scene.find(id); }

    void set(ItemId id, const char* key, PropValue v)
    {
        ItemDef& d = item(id);
        const int i = d.type->schema().indexOf(QLatin1String(key));
        if (i < 0) qFatal("no property %s on %s", key, qPrintable(d.type->typeId()));
        d.set(i, clampValue(d.type->schema()[i], v));
        d.type->propertyChanged(d, i);
    }

    template <class T>
    T get(ItemId id, const char* key)
    {
        ItemDef& d = item(id);
        return std::get<T>(d.props[size_t(d.type->schema().indexOf(QLatin1String(key)))]);
    }

    ItemId add(const char* type, Vec2 at, std::initializer_list<std::pair<const char*, PropValue>> props = {})
    {
        const ItemType* t = ItemRegistry::instance().find(QLatin1String(type));
        if (!t) qFatal("unknown type %s", type);
        scene.items.push_back(t->makeDefault(++lastId_, at));
        for (const auto& [k, v] : props) set(lastId_, k, v);
        return lastId_;
    }

    // Connects like the viewport's two-click tool: world points on bodies (or the world).
    ItemId connect(const char* type, ItemId a, Vec2 worldA, ItemId b, Vec2 worldB,
                   std::initializer_list<std::pair<const char*, PropValue>> props = {})
    {
        const ItemType* t = ItemRegistry::instance().find(QLatin1String(type));
        ItemDef d = t->makeDefault(++lastId_, worldA);
        const auto& s = t->schema();
        auto anchor = [&](ItemId body, Vec2 w) {
            Pose p;
            return (body && pose(body, p)) ? p.inverse(w) : w;
        };
        d.set(s.indexOf("bodyA"), int64_t(a));
        d.set(s.indexOf("anchorA"), anchor(a, worldA));
        d.set(s.indexOf("bodyB"), int64_t(b));
        d.set(s.indexOf("anchorB"), anchor(b, worldB));
        t->initConnection(d, *this);
        t->propertyChanged(d, -1);
        scene.items.push_back(d);
        for (const auto& [k, v] : props) set(lastId_, k, v);
        return lastId_;
    }

    void setWorld(const char* key, PropValue v)
    {
        const int i = scene.world.type->schema().indexOf(QLatin1String(key));
        scene.world.set(i, clampValue(scene.world.type->schema()[i], v));
    }

    SimStart start(bool recording = false) const
    {
        SimStart st;
        for (const ItemDef& d : scene.items) st.items.push_back(std::make_shared<ItemDef>(d));
        st.world = std::make_shared<ItemDef>(scene.world);
        st.recording = recording;
        return st;
    }

private:
    ItemId lastId_ = 0;
};

inline const BodySnap* body(const Snapshot& s, ItemId id)
{
    for (const BodySnap& b : s.bodies)
        if (b.id == id) return &b;
    return nullptr;
}

inline const ItemSnap* itemSnap(const Snapshot& s, ItemId id)
{
    for (const ItemSnap& i : s.items)
        if (i.id == id) return &i;
    return nullptr;
}

inline double forceOf(const BodySnap& b, ForceSource src, double* fy = nullptr)
{
    for (int i = 0; i < b.forceCount; ++i)
        if (b.forces[i].source == uint8_t(src)) {
            if (fy) *fy = b.forces[i].fy;
            return std::hypot(b.forces[i].fx, b.forces[i].fy);
        }
    if (fy) *fy = 0;
    return 0;
}

} // namespace kn
