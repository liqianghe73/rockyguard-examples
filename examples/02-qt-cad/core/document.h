// The drawing model. QtCore only -- no GUI, no licensing, no knowledge that
// RockyGuard exists. That separation is what lets the whole model be unit-tested
// with no SDK and no display.
//
// Three entity kinds, deliberately. Arcs were cut: they infect hit-testing, snap
// candidate generation, tessellation, the JSON schema, painting, and the extrude
// loop-chainer -- six files to add one primitive. A closed polyline plus a circle
// is a credible drafting demo, and a reader can add arcs as an exercise.

#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace core {

enum class Kind { Line, Polyline, Circle };

struct Entity {
    int id = 0;
    Kind kind = Kind::Line;

    // Line:     pts[0] -> pts[1]
    // Polyline: pts[0..n-1], closed when `closed` is set
    // Circle:   pts[0] is the centre; see `radius`
    QVector<QPointF> pts;
    bool closed = false;
    double radius = 0.0;

    // Set when the entity was authored by a Pro-tier feature (an extrusion
    // profile, for instance). Draft-tier sessions must LOAD and RE-SAVE these
    // unchanged rather than dropping them -- see Document::save.
    bool proAuthored = false;

    QString kindName() const;
    // Squared distance from p, for hit-testing. Squared to avoid the sqrt in the
    // inner loop; callers compare against a squared tolerance.
    double distanceSquaredTo(const QPointF& p) const;
    // Points a snap can latch onto: endpoints, and a circle's centre.
    QVector<QPointF> snapPoints() const;
    bool isClosedLoop() const;
};

// Result of a load attempt. A bool return would force the caller to invent an
// error message, and "could not open the file" and "this file is from a newer
// version" need different ones.
struct LoadResult {
    bool ok = false;
    QString error;
    explicit operator bool() const { return ok; }
};

class Document {
public:
    // Bumped only on a breaking schema change. A file whose version exceeds this
    // is REFUSED with a clear message rather than partially parsed -- silently
    // dropping fields we do not understand would corrupt a drawing made by a
    // newer build.
    static constexpr int kFormatVersion = 1;

    const QVector<Entity>& entities() const { return m_entities; }
    // static_cast because QVector::size() is qsizetype (64-bit). int ids and
    // indices are right for a model this size; only the conversion needs to be
    // visible, so it does not become an error the day /WX is turned on.
    int count() const { return static_cast<int>(m_entities.size()); }
    bool isEmpty() const { return m_entities.isEmpty(); }

    // Returns the assigned id. Ids are never reused, so an undo that re-adds an
    // entity can restore its original id and keep selections coherent.
    int add(Entity e);
    // Insert with a specific id, for undo. Preserves document order.
    void insertAt(int index, const Entity& e);
    bool removeById(int id);
    int indexOf(int id) const;
    const Entity* find(int id) const;

    void clear();

    // Hit-test in world units. Tolerance is in the same units, so callers scale
    // it by the view zoom or picking gets harder as you zoom in.
    int hitTest(const QPointF& p, double tolerance) const;

    QVector<QPointF> snapCandidates() const;

    // The first closed loop, as a point list -- the input to extrusion. Empty
    // when the drawing has none.
    QVector<QPointF> firstClosedLoop() const;
    bool hasClosedLoop() const;

    QJsonObject toJson() const;
    LoadResult fromJson(const QJsonObject& root);

    LoadResult load(const QString& path);
    LoadResult save(const QString& path) const;

    bool modified() const { return m_modified; }
    void setModified(bool m) { m_modified = m; }

private:
    QVector<Entity> m_entities;
    int m_nextId = 1;
    bool m_modified = false;
};

// --- free geometry helpers, used by the UI and by extrusion ------------------

double distancePointToSegmentSquared(const QPointF& p, const QPointF& a, const QPointF& b);
double polygonSignedArea(const QVector<QPointF>& poly);
// True when any two non-adjacent edges cross. Ear clipping silently produces
// garbage on a self-intersecting loop, and one wrong-looking 3D screenshot is
// worse for this repo than no 3D at all.
bool polygonSelfIntersects(const QVector<QPointF>& poly);

}  // namespace core
