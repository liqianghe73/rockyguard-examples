#include "document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>  // std::max -- previously arrived only via QVector's
                     // internal include of <algorithm>, which is a Qt
                     // implementation detail and not a guarantee.
#include <cmath>
#include <limits>

namespace core {
namespace {

QJsonArray pointToJson(const QPointF& p) {
    QJsonArray a;
    a.append(p.x());
    a.append(p.y());
    return a;
}

bool pointFromJson(const QJsonValue& v, QPointF* out) {
    if (!v.isArray()) return false;
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a.at(0).isDouble() || !a.at(1).isDouble()) return false;
    *out = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
    return true;
}

double cross(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool segmentsProperlyIntersect(const QPointF& p1, const QPointF& p2, const QPointF& p3,
                               const QPointF& p4) {
    const double d1 = cross(p3, p4, p1);
    const double d2 = cross(p3, p4, p2);
    const double d3 = cross(p1, p2, p3);
    const double d4 = cross(p1, p2, p4);
    // Strict signs only. Touching endpoints are how a closed polyline is built, so
    // treating collinear or touching cases as intersections would reject every
    // legitimate loop.
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

}  // namespace

// --- free helpers ------------------------------------------------------------

double distancePointToSegmentSquared(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0.0) {
        const QPointF d = p - a;
        return d.x() * d.x() + d.y() * d.y();
    }
    // Project p onto ab, clamped to the segment.
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = std::fmax(0.0, std::fmin(1.0, t));
    const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
    const QPointF d = p - proj;
    return d.x() * d.x() + d.y() * d.y();
}

double polygonSignedArea(const QVector<QPointF>& poly) {
    if (poly.size() < 3) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF& a = poly.at(i);
        const QPointF& b = poly.at((i + 1) % poly.size());
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * sum;
}

bool polygonSelfIntersects(const QVector<QPointF>& poly) {
    const int n = poly.size();
    if (n < 4) return false;
    for (int i = 0; i < n; ++i) {
        const QPointF& a1 = poly.at(i);
        const QPointF& a2 = poly.at((i + 1) % n);
        for (int j = i + 1; j < n; ++j) {
            // Skip adjacent edges, and the wrap-around pair that shares a vertex.
            if (j == i || j == (i + 1) % n) continue;
            if (i == 0 && j == n - 1) continue;
            const QPointF& b1 = poly.at(j);
            const QPointF& b2 = poly.at((j + 1) % n);
            if (segmentsProperlyIntersect(a1, a2, b1, b2)) return true;
        }
    }
    return false;
}

// --- Entity ------------------------------------------------------------------

QString Entity::kindName() const {
    switch (kind) {
        case Kind::Line: return QStringLiteral("line");
        case Kind::Polyline: return QStringLiteral("polyline");
        case Kind::Circle: return QStringLiteral("circle");
    }
    return QStringLiteral("line");
}

bool Entity::isClosedLoop() const {
    return kind == Kind::Polyline && closed && pts.size() >= 3;
}

double Entity::distanceSquaredTo(const QPointF& p) const {
    switch (kind) {
        case Kind::Line:
            if (pts.size() < 2) return std::numeric_limits<double>::max();
            return distancePointToSegmentSquared(p, pts.at(0), pts.at(1));

        case Kind::Polyline: {
            if (pts.size() < 2) return std::numeric_limits<double>::max();
            double best = std::numeric_limits<double>::max();
            const int last = static_cast<int>(closed ? pts.size() : pts.size() - 1);
            for (int i = 0; i < last; ++i) {
                const QPointF& a = pts.at(i);
                const QPointF& b = pts.at((i + 1) % pts.size());
                best = std::fmin(best, distancePointToSegmentSquared(p, a, b));
            }
            return best;
        }

        case Kind::Circle: {
            if (pts.isEmpty()) return std::numeric_limits<double>::max();
            // Distance to the RIM, not to the centre -- clicking the middle of a
            // circle should not select it, exactly as in real CAD.
            const QPointF d = p - pts.at(0);
            const double dist = std::hypot(d.x(), d.y()) - radius;
            return dist * dist;
        }
    }
    return std::numeric_limits<double>::max();
}

QVector<QPointF> Entity::snapPoints() const {
    switch (kind) {
        case Kind::Line:
        case Kind::Polyline:
            return pts;
        case Kind::Circle: {
            if (pts.isEmpty()) return {};
            const QPointF c = pts.at(0);
            // Centre plus the four quadrant points. Quadrants are nearly free
            // here and they are what makes a circle usable as a drafting anchor.
            return {c,
                    QPointF(c.x() + radius, c.y()),
                    QPointF(c.x() - radius, c.y()),
                    QPointF(c.x(), c.y() + radius),
                    QPointF(c.x(), c.y() - radius)};
        }
    }
    return {};
}

// --- Document ----------------------------------------------------------------

int Document::add(Entity e) {
    e.id = m_nextId++;
    m_entities.append(e);
    m_modified = true;
    return e.id;
}

void Document::insertAt(int index, const Entity& e) {
    // qBound is a SINGLE template parameter, so passing (int, int, qsizetype)
    // is a deduction failure on both MSVC and GCC. Narrow explicitly.
    const int i = qBound(0, index, static_cast<int>(m_entities.size()));
    m_entities.insert(i, e);
    // Keep the counter ahead of any id we have seen, so a redo cannot mint a
    // duplicate id later.
    m_nextId = std::max(m_nextId, e.id + 1);
    m_modified = true;
}

bool Document::removeById(int id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    m_entities.remove(i);
    m_modified = true;
    return true;
}

int Document::indexOf(int id) const {
    for (int i = 0; i < m_entities.size(); ++i) {
        if (m_entities.at(i).id == id) return i;
    }
    return -1;
}

const Entity* Document::find(int id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_entities.at(i);
}

void Document::clear() {
    m_entities.clear();
    m_nextId = 1;
    m_modified = false;
}

int Document::hitTest(const QPointF& p, double tolerance) const {
    const double tol2 = tolerance * tolerance;
    int best = -1;
    double bestDist = tol2;
    // Keep the tolerance bound separate from best-so-far. Using `d <= bestDist`
    // with a backwards walk let each older entity replace an equal-distance
    // match, so the OLDEST entity won a tie -- the reverse of the intent. Exact
    // ties are common: two segments through the same snapped vertices give
    // bit-identical distances.
    // Iterate backwards so the most recently drawn entity wins a tie, which is
    // what a user expects when shapes overlap.
    for (int i = m_entities.size() - 1; i >= 0; --i) {
        const double d = m_entities.at(i).distanceSquaredTo(p);
        if (d <= tol2 && (best < 0 || d < bestDist)) {
            bestDist = d;
            best = m_entities.at(i).id;
        }
    }
    return best;
}

QVector<QPointF> Document::snapCandidates() const {
    QVector<QPointF> out;
    for (const Entity& e : m_entities) out += e.snapPoints();
    return out;
}

QVector<QPointF> Document::firstClosedLoop() const {
    for (const Entity& e : m_entities) {
        if (e.isClosedLoop()) return e.pts;
    }
    return {};
}

bool Document::hasClosedLoop() const { return !firstClosedLoop().isEmpty(); }

QJsonObject Document::toJson() const {
    QJsonArray arr;
    for (const Entity& e : m_entities) {
        QJsonObject o;
        o["id"] = e.id;
        o["kind"] = e.kindName();
        if (e.proAuthored) o["proAuthored"] = true;

        if (e.kind == Kind::Circle) {
            o["center"] = pointToJson(e.pts.value(0));
            o["radius"] = e.radius;
        } else {
            QJsonArray pts;
            for (const QPointF& p : e.pts) pts.append(pointToJson(p));
            o["pts"] = pts;
            if (e.kind == Kind::Polyline) o["closed"] = e.closed;
        }
        arr.append(o);
    }

    QJsonObject root;
    root["format"] = QStringLiteral("rgcad");
    root["version"] = kFormatVersion;
    root["entities"] = arr;
    return root;
}

LoadResult Document::fromJson(const QJsonObject& root) {
    LoadResult r;

    if (root.value("format").toString() != QLatin1String("rgcad")) {
        r.error = QStringLiteral("Not an rgcad drawing (missing or wrong \"format\" field).");
        return r;
    }

    const int version = root.value("version").toInt(-1);
    if (version < 0) {
        r.error = QStringLiteral("Missing \"version\" field.");
        return r;
    }
    if (version > kFormatVersion) {
        // Refuse rather than partially parse. Dropping fields we do not
        // understand and then saving would silently corrupt a newer drawing.
        r.error = QStringLiteral("This drawing is format version %1, but this build "
                                 "understands only up to %2. Upgrade rgcad.")
                      .arg(version)
                      .arg(kFormatVersion);
        return r;
    }

    QVector<Entity> loaded;
    const QJsonArray arr = root.value("entities").toArray();
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        Entity e;
        e.id = o.value("id").toInt(i + 1);
        e.proAuthored = o.value("proAuthored").toBool(false);

        const QString kind = o.value("kind").toString();
        if (kind == QLatin1String("circle")) {
            e.kind = Kind::Circle;
            QPointF c;
            if (!pointFromJson(o.value("center"), &c)) {
                r.error = QStringLiteral("Entity %1: circle has no valid \"center\".").arg(i);
                return r;
            }
            e.pts = {c};
            e.radius = o.value("radius").toDouble(0.0);
            if (!(e.radius > 0.0)) {
                r.error = QStringLiteral("Entity %1: circle radius must be positive.").arg(i);
                return r;
            }
        } else if (kind == QLatin1String("line") || kind == QLatin1String("polyline")) {
            e.kind = (kind == QLatin1String("line")) ? Kind::Line : Kind::Polyline;
            e.closed = o.value("closed").toBool(false);
            const QJsonArray pts = o.value("pts").toArray();
            for (const QJsonValue& v : pts) {
                QPointF p;
                if (!pointFromJson(v, &p)) {
                    r.error = QStringLiteral("Entity %1: malformed point.").arg(i);
                    return r;
                }
                e.pts.append(p);
            }
            // Both branches used to be 2, so the ternary was dead and a closed
            // 2-point polyline loaded happily -- then isClosedLoop() (which wants
            // >= 3) silently excluded it, so the status bar disagreed with the
            // file about whether a closed loop existed.
            const int need = (e.kind == Kind::Polyline && e.closed) ? 3 : 2;
            if (e.pts.size() < need) {
                r.error = QStringLiteral("Entity %1: %2 needs at least %3 points.")
                              .arg(i)
                              .arg(kind)
                              .arg(need);
                return r;
            }
        } else {
            // An unknown kind is a hard error for the same reason as a future
            // version: we cannot round-trip what we cannot represent.
            r.error = QStringLiteral("Entity %1: unknown kind \"%2\".").arg(i).arg(kind);
            return r;
        }

        loaded.append(e);
    }

    m_entities = loaded;
    m_nextId = 1;
    for (const Entity& e : m_entities) m_nextId = std::max(m_nextId, e.id + 1);
    m_modified = false;

    r.ok = true;
    return r;
}

LoadResult Document::load(const QString& path) {
    LoadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());
        return r;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        r.error = QStringLiteral("%1 is not valid JSON: %2 (offset %3)")
                      .arg(path, err.errorString())
                      .arg(err.offset);
        return r;
    }
    if (!doc.isObject()) {
        r.error = QStringLiteral("%1 does not contain a JSON object.").arg(path);
        return r;
    }
    return fromJson(doc.object());
}

LoadResult Document::save(const QString& path) const {
    LoadResult r;
    // QSaveFile writes to a temporary and renames on commit, so a crash or a full
    // disk mid-write leaves the previous drawing intact rather than truncated.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        r.error = QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
        return r;
    }
    // Indented, because the whole point of a plain-JSON format is that a human
    // can read and diff it.
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        r.error = QStringLiteral("Cannot commit %1: %2").arg(path, f.errorString());
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace core
