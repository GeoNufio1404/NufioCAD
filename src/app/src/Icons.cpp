#include "Icons.hpp"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

#include <memory>
#include <utility>

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
    // Se rehace si cambiaron los colores (cambio de tema). Se comparan los
    // colores y no QPalette::cacheKey(): la paleta clara y la oscura pueden
    // tener la misma clave.
    QSvgRenderer& renderer(QIcon::Mode mode)
    {
        const QPalette palette = QApplication::palette();
        QPalette::ColorGroup group = QPalette::Active;
        QPalette::ColorRole textRole = QPalette::WindowText;
        QPalette::ColorRole accentRole = QPalette::Highlight;
        if (mode == QIcon::Disabled) {
            group = QPalette::Disabled;
            accentRole = QPalette::WindowText;
        } else if (mode == QIcon::Selected) {
            textRole = accentRole = QPalette::HighlightedText;
        }
        const QColor text = palette.color(group, textRole);
        const QColor accent = palette.color(group, accentRole);
        const int index = static_cast<int>(mode);
        if (!renderers_[index] || colors_[index] != std::pair{text.rgba(), accent.rgba()}) {
            QByteArray data = svg_;
            data.replace(kAccent, accent.name().toLatin1());
            data.replace("currentColor", text.name().toLatin1());
            renderers_[index] = std::make_unique<QSvgRenderer>(data);
            colors_[index] = {text.rgba(), accent.rgba()};
        }
        return *renderers_[index];
    }

    QByteArray svg_;
    std::pair<QRgb, QRgb> colors_[4];
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
