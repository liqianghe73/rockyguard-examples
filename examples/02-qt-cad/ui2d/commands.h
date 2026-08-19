// Undo commands. Header-only: there are two of them and they are small.
//
// They live in ui2d/ rather than core/ deliberately -- QUndoCommand is QtGui, and
// core/ is documented as QtCore-only so the model stays testable without a
// display. Editing is a UI concern; the model just holds entities.
//
// Scope note: Add and Delete only. Drag-to-move was cut, and with it the hardest
// undo command -- coalescing a press-drag-release into a single QUndoCommand and
// restoring selection afterwards is where the bugs in undo stacks live. Draw and
// delete is enough to prove the tool is real.

#pragma once

#include <QCoreApplication>
#include <QUndoCommand>

#include <functional>
#include <utility>

#include "../core/document.h"

namespace ui2d {

// Anything that mutates the document goes through a command, so the undo stack is
// the single source of truth for "has this drawing changed".
class AddEntity : public QUndoCommand {
public:
    AddEntity(core::Document* doc, const core::Entity& e, std::function<void()> onChanged)
        : m_doc(doc), m_entity(e), m_onChanged(std::move(onChanged)) {
        setText(QCoreApplication::translate("undo", "Add %1").arg(e.kindName()));
    }

    void redo() override {
        if (m_firstRun) {
            // add() assigns the id. Capture it so a later undo/redo pair restores
            // the SAME id -- selection and any external reference stay valid.
            m_entity.id = m_doc->add(m_entity);
            m_index = m_doc->count() - 1;
            m_firstRun = false;
        } else {
            m_doc->insertAt(m_index, m_entity);
        }
        m_onChanged();
    }

    void undo() override {
        m_doc->removeById(m_entity.id);
        m_onChanged();
    }

private:
    core::Document* m_doc;
    core::Entity m_entity;
    std::function<void()> m_onChanged;
    int m_index = -1;
    bool m_firstRun = true;
};

class DeleteEntity : public QUndoCommand {
public:
    DeleteEntity(core::Document* doc, int id, std::function<void()> onChanged)
        : m_doc(doc), m_onChanged(std::move(onChanged)) {
        // Snapshot the entity AND its position now. Re-inserting at the end would
        // reorder the drawing, which changes hit-test priority and which loop
        // firstClosedLoop() returns -- a visible, confusing undo.
        if (const core::Entity* e = doc->find(id)) {
            m_entity = *e;
            m_index = doc->indexOf(id);
            m_valid = true;
        }
        setText(QCoreApplication::translate("undo", "Delete %1").arg(m_entity.kindName()));
    }

    // Both guard on m_valid. A command built against an id that is no longer in
    // the document must do NOTHING. Without this, undo() called insertAt(-1,
    // default-Entity), which qBound-clamped to 0 and injected a phantom
    // (Kind::Line, id 0, no points) at the head of the drawing: the entity count
    // disagreed with the canvas, and the saved file could never be reopened
    // because fromJson rejects a line with fewer than two points.
    void redo() override {
        if (!m_valid) return;
        m_doc->removeById(m_entity.id);
        m_onChanged();
    }

    void undo() override {
        if (!m_valid) return;
        m_doc->insertAt(m_index, m_entity);
        m_onChanged();
    }

private:
    core::Document* m_doc;
    core::Entity m_entity;
    std::function<void()> m_onChanged;
    int m_index = -1;
    bool m_valid = false;
};

}  // namespace ui2d
