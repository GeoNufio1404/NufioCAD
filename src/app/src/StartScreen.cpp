#include "StartScreen.hpp"

#include <nuf/io/DocumentFile.hpp>

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <filesystem>

namespace nuf::app {

namespace {

constexpr const char* kRecentKey = "files/recent";
constexpr QSize kThumbnail(200, 125);

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QString normalized(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void store(const QStringList& paths)
{
    QSettings().setValue(QLatin1String(kRecentKey), paths);
}

QPixmap placeholder(const QString& text, const QPalette& palette)
{
    QPixmap pixmap(kThumbnail);
    pixmap.fill(palette.color(QPalette::AlternateBase));
    QPainter painter(&pixmap);
    painter.setPen(palette.color(QPalette::Mid));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    painter.setPen(palette.color(QPalette::PlaceholderText));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return pixmap;
}

} // namespace

// --- RecentFiles -------------------------------------------------------------

QStringList RecentFiles::list()
{
    return QSettings().value(QLatin1String(kRecentKey)).toStringList();
}

void RecentFiles::add(const QString& path)
{
    const QString clean = normalized(path);
    QStringList paths = list();
    paths.removeIf([&](const QString& other) { return other.compare(clean, kPathCase) == 0; });
    paths.prepend(clean);
    while (paths.size() > kMax) {
        paths.removeLast();
    }
    store(paths);
}

void RecentFiles::remove(const QString& path)
{
    QStringList paths = list();
    paths.removeIf([&](const QString& other) { return other.compare(path, kPathCase) == 0; });
    store(paths);
}

void RecentFiles::clear()
{
    store({});
}

// --- StartScreen -------------------------------------------------------------

StartScreen::StartScreen(QWidget* viewport) : QWidget(viewport)
{
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(40, 30, 40, 30);

    auto* column = new QWidget(this);
    column->setMaximumWidth(900);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* top = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Recientes y archivos"), column);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    top->addWidget(title);
    top->addStretch();
    auto* close = new QPushButton(QStringLiteral("Cerrar (Esc)"), column);
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QPushButton::clicked, this, &StartScreen::closeRequested);
    top->addWidget(close);
    layout->addLayout(top);
    layout->addSpacing(4);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    const auto addButton = [&](const QString& text, auto signal, bool primary) {
        auto* button = new QPushButton(text, column);
        button->setMinimumHeight(34);
        button->setDefault(primary);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, signal);
        buttons->addWidget(button);
    };
    addButton(QStringLiteral("Nuevo diseño"), &StartScreen::newRequested, true);
    addButton(QStringLiteral("Abrir…"), &StartScreen::openRequested, false);
    addButton(QStringLiteral("Importar STEP…"), &StartScreen::importRequested, false);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addSpacing(6);

    auto* header = new QLabel(QStringLiteral("Recientes"), column);
    QFont headerFont = header->font();
    headerFont.setBold(true);
    headerFont.setPointSizeF(headerFont.pointSizeF() * 1.2);
    header->setFont(headerFont);
    layout->addWidget(header);

    empty_ = new QLabel(QStringLiteral("Aún no hay diseños recientes. Los que abras o guardes aparecerán aquí."), column);
    empty_->setForegroundRole(QPalette::PlaceholderText);
    empty_->setWordWrap(true);
    layout->addWidget(empty_);

    list_ = new QListWidget(column);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(kThumbnail);
    list_->setGridSize(QSize(kThumbnail.width() + 30, kThumbnail.height() + 72));
    list_->setMovement(QListView::Static);
    list_->setResizeMode(QListView::Adjust);
    list_->setWordWrap(true);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setMinimumHeight(list_->gridSize().height() + 8);
    list_->viewport()->setAutoFillBackground(false);
    layout->addWidget(list_, 1);

    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QListWidgetItem* item = list_->itemAt(position);
        if (item == nullptr) {
            return;
        }
        list_->setCurrentItem(item);
        QMenu menu(this);
        QAction* remove = menu.addAction(QStringLiteral("Quitar de la lista"));
        if (menu.exec(list_->viewport()->mapToGlobal(position)) == remove) {
            removeSelected();
        }
    });

    outer->addStretch(1);
    outer->addWidget(column, 6);
    outer->addStretch(1);

    viewport->installEventFilter(this);
    reposition();
    show();
}

void StartScreen::reload()
{
    list_->clear();
    for (const QString& path : RecentFiles::list()) {
        const QFileInfo info(path);
        const bool exists = info.isFile();
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        QPixmap thumbnail;
        if (exists) {
            const auto png = io::readThumbnail(std::filesystem::path(path.toStdU16String()));
            if (png && thumbnail.loadFromData(reinterpret_cast<const uchar*>(png->data()), uint(png->size()))) {
                thumbnail = thumbnail.scaled(kThumbnail, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        if (thumbnail.isNull()) {
            thumbnail = placeholder(exists ? QStringLiteral("Sin vista previa") : QStringLiteral("No encontrado"),
                                    palette());
        }
        item->setIcon(QIcon(thumbnail));
        const QString when = exists ? QLocale().toString(info.lastModified(), QLocale::ShortFormat)
                                    : QStringLiteral("ya no existe");
        item->setText(info.completeBaseName() + QLatin1Char('\n') + when);
        if (!exists) {
            item->setForeground(palette().color(QPalette::PlaceholderText));
        }
    }
    const bool any = list_->count() > 0;
    list_->setVisible(any);
    empty_->setVisible(!any);
}

void StartScreen::removeSelected()
{
    if (QListWidgetItem* item = list_->currentItem()) {
        RecentFiles::remove(item->data(Qt::UserRole).toString());
        reload();
    }
}

bool StartScreen::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void StartScreen::reposition()
{
    setGeometry(parentWidget()->rect());
    raise();
}

void StartScreen::showEvent(QShowEvent* event)
{
    reload();
    reposition();
    QWidget::showEvent(event);
}

void StartScreen::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
}

} // namespace nuf::app
