#pragma once

// Ayudas que comparten los archivos de la ventana principal (MainWindow*.cpp).

#include "MainWindow.hpp"

#include "ExportDialog.hpp"
#include "BodiesDialog.hpp"
#include "ExpressionSpinBox.hpp"
#include "GeneratorTool.hpp"
#include "SettingsDialog.hpp"
#include "Units.hpp"
#include "ParametersDialog.hpp"
#include "FeatureTools.hpp"
#include "Icons.hpp"
#include "ModelPanels.hpp"
#include "NavBar.hpp"
#include "Recovery.hpp"
#include "StartScreen.hpp"
#include "Ribbon.hpp"
#include "Theme.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/commands/RenameDocument.hpp>
#include <nuf/commands/SetBodyInfo.hpp>
#include <nuf/commands/SetParameters.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/commands/AddRecipe.hpp>
#include <nuf/commands/ReplaceRecipe.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>

namespace nuf::app {


// Versión de la distribución de barras y paneles (ver restoreState).
// 6: la barra de acceso rápido (Fase 15) es una barra nueva sobre la cinta; la disposición guardada antes la dejaba al lado.
inline constexpr int kLayoutVersion = 6;

// Ícono y descripción de una acción. El tooltip lleva el nombre completo, la
// tecla y, si se da, una línea de ayuda; el botón de la barra muestra el nombre
// corto (`shortName`, si no el mismo texto de la acción).
inline void decorate(QAction* action, const QString& icon, const QString& hint = {}, const QString& shortName = {})
{
    action->setIcon(uiIcon(icon));
    QString name = action->text();
    name.remove(QLatin1Char('&'));
    action->setIconText(shortName.isEmpty() ? name : shortName);
    QString tip = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
    if (!action->shortcut().isEmpty()) {
        tip += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
    }
    if (!hint.isEmpty()) {
        tip += QStringLiteral("<br>") + hint.toHtmlEscaped();
    }
    action->setToolTip(tip);
}

// Añade (o cambia) la línea de ayuda del tooltip de una acción que ya está decorada.
inline void addHint(QAction* action, const QString& hint)
{
    QString name = action->text();
    name.remove(QLatin1Char('&'));
    QString tip = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
    if (!action->shortcut().isEmpty()) {
        tip += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
    }
    tip += QStringLiteral("<br>") + hint.toHtmlEscaped();
    action->setToolTip(tip);
}

inline QWidget* placeholder(const QString& text)
{
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(12);
    return label;
}


} // namespace nuf::app
