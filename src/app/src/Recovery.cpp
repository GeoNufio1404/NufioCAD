#include "Recovery.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace nuf::app {

namespace {

QString directory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recuperacion");
    QDir().mkpath(dir);
    return dir;
}

QString nufPath(const QString& id) { return directory() + QStringLiteral("/rec-") + id + QStringLiteral(".nuf"); }
QString infoPath(const QString& id) { return directory() + QStringLiteral("/rec-") + id + QStringLiteral(".origen"); }
QString lockPath(const QString& id) { return directory() + QStringLiteral("/rec-") + id + QStringLiteral(".lock"); }

std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

} // namespace

Recovery::Recovery()
    : id_(QUuid::createUuid().toString(QUuid::Id128).left(12))
    , lock_(std::make_unique<QLockFile>(lockPath(id_)))
{
    lock_->tryLock(0);
}

bool Recovery::write(const document::Document& document, const io::ViewState& view, const QString& original)
{
    try {
        io::saveDocument(toPath(nufPath(id_)), document, view);
    } catch (const std::exception&) {
        return false;
    }
    QFile info(infoPath(id_));
    if (!info.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    info.write(original.toUtf8() + '\n' + QString::fromStdString(document.name()).toUtf8());
    return true;
}

void Recovery::discard()
{
    QFile::remove(nufPath(id_));
    QFile::remove(infoPath(id_));
}

std::vector<Recovery::Orphan> Recovery::orphans()
{
    std::vector<Orphan> found;
    const QDir dir(directory());
    // Bloqueos de sesiones muertas que no dejaron copia: se limpian.
    for (const QFileInfo& entry : dir.entryInfoList({QStringLiteral("rec-*.lock")}, QDir::Files)) {
        const QString id = entry.completeBaseName().mid(4);
        if (!QFile::exists(nufPath(id))) {
            QLockFile stale(entry.absoluteFilePath());
            if (stale.tryLock(0)) {
                stale.unlock();
            }
        }
    }
    for (const QFileInfo& entry : dir.entryInfoList({QStringLiteral("rec-*.nuf")}, QDir::Files)) {
        const QString id = entry.completeBaseName().mid(4);
        QLockFile probe(lockPath(id));
        if (!probe.tryLock(0)) {
            continue;  // una ventana viva la está usando
        }
        probe.unlock();
        Orphan orphan;
        orphan.id = id;
        orphan.file = toPath(entry.absoluteFilePath());
        orphan.when = entry.lastModified();
        QFile info(infoPath(id));
        if (info.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> lines = info.readAll().split('\n');
            orphan.original = lines.value(0).isEmpty() ? QString() : QString::fromUtf8(lines.value(0));
            orphan.name = QString::fromUtf8(lines.value(1));
        }
        found.push_back(std::move(orphan));
    }
    std::ranges::sort(found, [](const Orphan& a, const Orphan& b) { return a.when > b.when; });
    return found;
}

void Recovery::remove(const Orphan& orphan)
{
    QFile::remove(nufPath(orphan.id));
    QFile::remove(infoPath(orphan.id));
    QFile::remove(lockPath(orphan.id));
}

} // namespace nuf::app
