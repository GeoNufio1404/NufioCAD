#include "Icons.hpp"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

#include <memory>

namespace nuf::app {

namespace {

// Color de acento con el que se dibujan los SVG (ver resources/icons/ui/LICENSE.md).
const QByteArray kAccent = "#2F80ED";

class SvgIconEngine final : public QIconEngine {
public:
    explicit SvgIconEngine(QByteArray svg) : svg_(std::move(svg)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        renderer(mode).render(painter, QRectF(rect));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

    QIconEngine* clone() const override { return new SvgIconEngine(svg_); }
    QString key() const override { return QStringLiteral("nufsvg"); }

private:
    // Se rehace si cambió la paleta (cambio de tema).
    QSvgRenderer& renderer(QIcon::Mode mode)
    {
        const QPalette palette = QApplication::palette();
        if (palette.cacheKey() != paletteKey_) {
            paletteKey_ = palette.cacheKey();
            for (auto& r : renderers_) {
                r.reset();
            }
        }
        const int index = static_cast<int>(mode);
        if (!renderers_[index]) {
            QPalette::ColorGroup group = QPalette::Active;
            QPalette::ColorRole text = QPalette::WindowText;
            QPalette::ColorRole accent = QPalette::Highlight;
            if (mode == QIcon::Disabled) {
                group = QPalette::Disabled;
                accent = QPalette::WindowText;
            } else if (mode == QIcon::Selected) {
                text = accent = QPalette::HighlightedText;
            }
            QByteArray data = svg_;
            data.replace(kAccent, palette.color(group, accent).name().toLatin1());
            data.replace("currentColor", palette.color(group, text).name().toLatin1());
            renderers_[index] = std::make_unique<QSvgRenderer>(data);
        }
        return *renderers_[index];
    }

    QByteArray svg_;
    qint64 paletteKey_ = 0;
    std::unique_ptr<QSvgRenderer> renderers_[4];  // Normal, Disabled, Active, Selected
};

} // namespace

QIcon uiIcon(const QString& name)
{
    QFile file(QStringLiteral(":/icons/ui/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QIcon(new SvgIconEngine(file.readAll()));
}

} // namespace nuf::app
