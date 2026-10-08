#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

// Preserve live panel changes while applying independent preparation edits.
// Concurrent edits of the same field are reported rather than silently lost.
inline QJsonValue mergeConfiguration(const QJsonValue& base, const QJsonValue& draft,
                                    const QJsonValue& live, QStringList& conflicts,
                                    const QString& path = {})
{
    if (draft == base) return live;
    if (live == base || live == draft) return draft;
    if (base.isObject() && draft.isObject() && live.isObject()) {
        const auto b = base.toObject(), d = draft.toObject(), l = live.toObject();
        QJsonObject result;
        QStringList keys = b.keys(); keys.append(d.keys()); keys.append(l.keys());
        keys.removeDuplicates();
        for (const auto& key : keys) {
            const auto value = mergeConfiguration(b.value(key), d.value(key), l.value(key),
                                                   conflicts, path + "/" + key);
            if (!value.isUndefined()) result.insert(key, value);
        }
        return result;
    }
    if (base.isArray() && draft.isArray() && live.isArray()) {
        const auto b = base.toArray(), d = draft.toArray(), l = live.toArray();
        if (b.size() == d.size() && b.size() == l.size()) {
            QJsonArray result;
            for (int i = 0; i < b.size(); ++i)
                result.append(mergeConfiguration(b[i], d[i], l[i], conflicts,
                                                  path + "/" + QString::number(i)));
            return result;
        }
    }
    conflicts.append(path);
    return draft;
}
