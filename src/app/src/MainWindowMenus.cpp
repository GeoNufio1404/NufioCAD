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
#include <QVBoxLayout>
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
#include <QToolButton>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>

#include "MainWindowCommon.hpp"
#include "RadialMenu.hpp"

namespace nuf::app {

void MainWindow::createMenus()
{
    // Barras con icono y el nombre debajo; el tooltip da el detalle.
    setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    setIconSize(QSize(24, 24));
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Archivo"));
    const auto addFile = [&](const QString& text, const QKeySequence& shortcut, const QString& icon, auto slot) {
        QAction* action = file->addAction(text);
        action->setShortcut(shortcut);
        decorate(action, icon);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    // Ayuda breve de cada acción (se ve al dejar el ratón encima, en la barra y en los menús).
    const auto help = [](const QString& name) -> QString {
        static const std::vector<std::pair<QString, QString>> table = {
            {QStringLiteral("Nuevo diseño"), QStringLiteral("Empieza un diseño vacío (pregunta si hay cambios sin guardar)")},
            {QStringLiteral("Abrir…"), QStringLiteral("Abre un diseño guardado (.nuf)")},
            {QStringLiteral("Recientes…"), QStringLiteral("Lista los diseños abiertos hace poco")},
            {QStringLiteral("Guardar"), QStringLiteral("Guarda el diseño en su archivo")},
            {QStringLiteral("Guardar como…"), QStringLiteral("Guarda el diseño con otro nombre o en otra carpeta")},
            {QStringLiteral("Importar STEP…"), QStringLiteral("Trae un cuerpo desde un archivo STEP")},
            {QStringLiteral("Exportar…"), QStringLiteral("Saca los cuerpos a STL, OBJ u otros formatos")},
            {QStringLiteral("Imprimir…"), QStringLiteral("Revisa los cuerpos y los prepara para imprimir en 3D")},
            {QStringLiteral("Deshacer"), QStringLiteral("Quita el último cambio")},
            {QStringLiteral("Rehacer"), QStringLiteral("Vuelve a poner el cambio deshecho")},
            {QStringLiteral("Renombrar diseño…"), QStringLiteral("Cambia el nombre del diseño")},
            {QStringLiteral("Borrar"), QStringLiteral("Borra lo seleccionado en el sketch")},
            {QStringLiteral("Importar DXF…"), QStringLiteral("Trae un dibujo DXF al sketch")},
            // Herramientas de dibujo (por icono)
            {QStringLiteral("line"), QStringLiteral("Dibuja líneas encadenadas; arrastra para continuar con un arco tangente")},
            {QStringLiteral("rectangle"), QStringLiteral("Rectángulo con dos esquinas opuestas")},
            {QStringLiteral("rectangle-3point"), QStringLiteral("Rectángulo girado: un lado y la altura")},
            {QStringLiteral("rectangle-center"), QStringLiteral("Rectángulo desde su centro hasta una esquina")},
            {QStringLiteral("circle"), QStringLiteral("Círculo con el centro y un punto del borde")},
            {QStringLiteral("circle-2point"), QStringLiteral("Círculo con los dos extremos de un diámetro")},
            {QStringLiteral("circle-3point"), QStringLiteral("Círculo que pasa por tres puntos")},
            {QStringLiteral("circle-tangent2"), QStringLiteral("Círculo tangente a dos curvas")},
            {QStringLiteral("circle-tangent3"), QStringLiteral("Círculo tangente a tres curvas")},
            {QStringLiteral("arc"), QStringLiteral("Arco con inicio, fin y un punto por el que pasa")},
            {QStringLiteral("arc-center"), QStringLiteral("Arco con el centro, el inicio y el fin")},
            {QStringLiteral("arc-tangent"), QStringLiteral("Arco que continúa tangente desde el extremo de una curva")},
            {QStringLiteral("polygon-circumscribed"), QStringLiteral("Polígono regular que rodea un círculo (centro y punto medio de un lado)")},
            {QStringLiteral("polygon-inscribed"), QStringLiteral("Polígono regular dentro de un círculo (centro y vértice)")},
            {QStringLiteral("polygon-edge"), QStringLiteral("Polígono regular a partir de un lado")},
            {QStringLiteral("slot-center-to-center"), QStringLiteral("Ranura con los centros de sus extremos y el ancho")},
            {QStringLiteral("slot-overall"), QStringLiteral("Ranura con su longitud total y el ancho")},
            {QStringLiteral("slot-center-point"), QStringLiteral("Ranura desde su punto central y el ancho")},
            {QStringLiteral("slot-arc"), QStringLiteral("Ranura curva sobre un arco y el ancho")},
            {QStringLiteral("ellipse"), QStringLiteral("Elipse con centro, eje mayor y eje menor")},
            {QStringLiteral("ellipse-arc"), QStringLiteral("Un trozo de elipse: elipse más inicio y fin")},
            {QStringLiteral("spline-fit"), QStringLiteral("Curva suave que pasa por los puntos que marcas (Enter termina)")},
            {QStringLiteral("spline-control"), QStringLiteral("Curva suave guiada por puntos de control (Enter termina)")},
            {QStringLiteral("conic"), QStringLiteral("Curva cónica (elipse, parábola o hipérbola) con inicio, fin y vértice")},
            {QStringLiteral("trim"), QStringLiteral("Recorta una curva hasta la siguiente que la cruza")},
            {QStringLiteral("extend"), QStringLiteral("Alarga una curva hasta la siguiente")},
            {QStringLiteral("break"), QStringLiteral("Parte una curva en el punto donde cruza otra")},
            {QStringLiteral("fillet"), QStringLiteral("Redondea la esquina entre dos líneas con un radio")},
            {QStringLiteral("chamfer"), QStringLiteral("Corta la esquina entre dos líneas con una distancia")},
            {QStringLiteral("offset"), QStringLiteral("Copia una curva (y las unidas a ella) a una distancia")},
            {QStringLiteral("mirror"), QStringLiteral("Refleja lo seleccionado respecto a una línea")},
            {QStringLiteral("move"), QStringLiteral("Mueve lo seleccionado de un punto base a otro")},
            {QStringLiteral("copy"), QStringLiteral("Copia lo seleccionado de un punto base a otro")},
            {QStringLiteral("rotate"), QStringLiteral("Gira lo seleccionado alrededor de un centro")},
            {QStringLiteral("scale"), QStringLiteral("Cambia el tamaño de lo seleccionado con un factor")},
            {QStringLiteral("pattern-rectangular"), QStringLiteral("Repite lo seleccionado en filas y columnas")},
            {QStringLiteral("pattern-circular"), QStringLiteral("Repite lo seleccionado alrededor de un centro")},
            {QStringLiteral("project"), QStringLiteral("Trae al sketch las aristas de la pieza como referencia")},
            {QStringLiteral("text"), QStringLiteral("Escribe un texto que luego se puede extruir")},
            {QStringLiteral("image"), QStringLiteral("Pone una imagen de referencia para calcar")},
            // Restricciones (por texto)
            {QStringLiteral("Coincidente"), QStringLiteral("Une dos puntos, o un punto con una curva")},
            {QStringLiteral("Horizontal"), QStringLiteral("Deja una línea (o dos puntos) a la misma altura")},
            {QStringLiteral("Vertical"), QStringLiteral("Deja una línea (o dos puntos) alineados en vertical")},
            {QStringLiteral("Paralela"), QStringLiteral("Hace que dos líneas tengan la misma dirección")},
            {QStringLiteral("Perpendicular"), QStringLiteral("Hace que dos líneas formen 90°")},
            {QStringLiteral("Tangente"), QStringLiteral("Hace que dos curvas se toquen sin cortarse")},
            {QStringLiteral("Igual"), QStringLiteral("Iguala el largo de dos líneas o el radio de dos círculos")},
            {QStringLiteral("Colineal"), QStringLiteral("Pone dos líneas sobre la misma recta")},
            {QStringLiteral("Concéntrica"), QStringLiteral("Da el mismo centro a dos círculos o arcos")},
            {QStringLiteral("Punto medio"), QStringLiteral("Deja un punto en el medio de una línea")},
            {QStringLiteral("Simetría"), QStringLiteral("Hace simétricos dos puntos respecto a una línea")},
            {QStringLiteral("Fijar"), QStringLiteral("Fija un punto o una curva para que no se mueva")},
            {QStringLiteral("Curvatura (G2)"), QStringLiteral("Une dos curvas con la misma tangente y curvatura")},
            {QStringLiteral("Cota"), QStringLiteral("Mide y fija una distancia, un radio o un ángulo")},
            {QStringLiteral("Intersección con la pieza"), QStringLiteral("Traza la intersección de la pieza con el plano del sketch")},
            {QStringLiteral("Alternar construcción"), QStringLiteral("Convierte la geometría en auxiliar (no forma perfiles)")},
            {QStringLiteral("Alternar bloqueo"), QStringLiteral("Impide mover la geometría seleccionada")},
        };
        for (const auto& [key, text] : table) {
            if (key == name) {
                return text;
            }
        }
        return {};
    };
    addFile(QStringLiteral("&Nuevo diseño"), QKeySequence(QKeySequence::New), QStringLiteral("file-new"),
            &MainWindow::newDocument);
    addFile(QStringLiteral("&Abrir…"), QKeySequence(QKeySequence::Open), QStringLiteral("file-open"),
            &MainWindow::openDocument);
    addFile(QStringLiteral("&Recientes…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), QStringLiteral("file-open"),
            &MainWindow::showRecents);
    recentMenu_ = file->addMenu(QStringLiteral("Abrir &reciente"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addSeparator();
    saveAction_ = addFile(QStringLiteral("&Guardar"), QKeySequence(QKeySequence::Save), QStringLiteral("file-save"),
            [this] { saveDocument(); });
    addFile(QStringLiteral("Guardar &como…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
            QStringLiteral("file-save"), [this] { saveDocumentAs(); });
    file->addSeparator();
    importAction_ = addFile(QStringLiteral("&Importar STEP…"), QKeySequence(Qt::CTRL | Qt::Key_I),
                            QStringLiteral("file-open"), &MainWindow::importStep);
    exportAction_ = addFile(QStringLiteral("&Exportar…"), QKeySequence(), QStringLiteral("file-export"),
                            &MainWindow::exportBodies);
    exportAction_->setEnabled(false);
    printAction_ = addFile(QStringLiteral("&Imprimir…"), QKeySequence(QStringLiteral("Ctrl+P")), QStringLiteral("file-export"),
                           &MainWindow::printBodies);
    printAction_->setEnabled(false);
    file->addSeparator();
    file->addAction(QStringLiteral("&Configuración…"), this, [this] {
        SettingsDialog dialog(this);
        dialog.exec();
    });
    file->addSeparator();
    file->addAction(QStringLiteral("&Salir"), QKeySequence(QKeySequence::Quit), this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Editar"));
    // Mientras se edita un sketch, deshacer/rehacer actúan dentro del sketch.
    undoAction_ = edit->addAction(QStringLiteral("Deshacer"), QKeySequence(QKeySequence::Undo), this, [this] {
        if (sketch_) {
            sketch_->undo();
        } else {
            commands_.undo();
        }
    });
    decorate(undoAction_, QStringLiteral("undo"));
    redoAction_ = edit->addAction(QStringLiteral("Rehacer"), QKeySequence(QKeySequence::Redo), this, [this] {
        if (sketch_) {
            sketch_->redo();
        } else {
            commands_.redo();
        }
    });
    decorate(redoAction_, QStringLiteral("redo"));
    edit->addSeparator();
    QAction* renameAction = edit->addAction(QStringLiteral("Renombrar diseño…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Renombrar diseño"), QStringLiteral("Nombre:"), QLineEdit::Normal,
            QString::fromStdString(document_.name()), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            commands_.push(std::make_unique<commands::RenameDocument>(name.trimmed().toStdString()));
        }
    });
    decorate(renameAction, QStringLiteral("rename"));

    // Esc cancela lo que esté en curso (por ahora, elegir el plano del sketch).
    QAction* cancelAction = new QAction(QStringLiteral("Cancelar"), this);
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, &MainWindow::cancel);
    addAction(cancelAction);

    QMenu* sketchMenu = menuBar()->addMenu(QStringLiteral("&Sketch"));
    createSketchAction_ = sketchMenu->addAction(QStringLiteral("&Crear sketch"), this, &MainWindow::createSketch);
    decorate(createSketchAction_, QStringLiteral("create-sketch"),
             QStringLiteral("Crear un sketch en un plano de origen o en una cara plana"));
    finishSketchAction_ = sketchMenu->addAction(QStringLiteral("&Terminar sketch"), this, &MainWindow::finishSketch);
    decorate(finishSketchAction_, QStringLiteral("finish-sketch"), QStringLiteral("Salir del sketch y guardarlo"),
             QStringLiteral("Terminar sketch"));
    sketchMenu->addSeparator();

    QMenu* solidMenu = menuBar()->addMenu(QStringLiteral("S&ólido"));
    extrudeAction_ = solidMenu->addAction(QStringLiteral("&Extruir"), QKeySequence(Qt::Key_E), this, [this] {
        startTool(std::make_unique<ExtrudeTool>(document_, *viewport_, std::nullopt));
    });
    decorate(extrudeAction_, QStringLiteral("extrude"),
             QStringLiteral("Extruir perfiles de un sketch o una cara plana"));
    revolveAction_ = solidMenu->addAction(QStringLiteral("&Revolución"), this, [this] {
        startTool(std::make_unique<RevolveTool>(document_, *viewport_, std::nullopt));
    });
    decorate(revolveAction_, QStringLiteral("revolve"),
             QStringLiteral("Girar perfiles de un sketch alrededor de un eje"));
    sweepAction_ = solidMenu->addAction(QStringLiteral("&Barrido"), this, [this] {
        startTool(std::make_unique<SweepTool>(document_, *viewport_, std::nullopt));
    });
    decorate(sweepAction_, QStringLiteral("sweep"),
             QStringLiteral("Llevar un perfil a lo largo de una trayectoria de otro sketch"), QStringLiteral("Barrido"));
    loftAction_ = solidMenu->addAction(QStringLiteral("&Loft"), this, [this] {
        startTool(std::make_unique<LoftTool>(document_, *viewport_, std::nullopt));
    });
    decorate(loftAction_, QStringLiteral("loft"),
             QStringLiteral("Unir varios perfiles (en el orden en que los eliges) con una superficie"), QStringLiteral("Loft"));
    solidMenu->addSeparator();
    filletAction_ = solidMenu->addAction(QStringLiteral("Re&dondeo"), QKeySequence(Qt::Key_F), this, [this] {
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, std::nullopt, EdgeTool::Style::Fillet));
    });
    decorate(filletAction_, QStringLiteral("fillet"), QStringLiteral("Redondear aristas de un cuerpo"));
    chamferAction_ = solidMenu->addAction(QStringLiteral("&Chaflán"), this, [this] {
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, std::nullopt, EdgeTool::Style::Chamfer));
    });
    decorate(chamferAction_, QStringLiteral("chamfer"), QStringLiteral("Achaflanar aristas de un cuerpo"));
    shellAction_ = solidMenu->addAction(QStringLiteral("&Vaciado"), this, [this] {
        startTool(std::make_unique<ShellTool>(document_, *viewport_, std::nullopt));
    });
    holeAction_ = solidMenu->addAction(QStringLiteral("&Agujero"), QKeySequence(Qt::Key_H), this, [this] {
        startTool(std::make_unique<HoleTool>(document_, *viewport_, std::nullopt));
    });
    decorate(holeAction_, QStringLiteral("hole"),
             QStringLiteral("Hacer agujeros en los círculos de un sketch (simple, abocardado o avellanado)"));
    mirrorAction_ = solidMenu->addAction(QStringLiteral("&Espejo"), this, [this] {
        startTool(std::make_unique<MirrorTool>(document_, *viewport_, std::nullopt));
    });
    decorate(mirrorAction_, QStringLiteral("mirror-solid"),
             QStringLiteral("Reflejar un cuerpo sobre un plano de origen o una cara plana"));
    patternAction_ = solidMenu->addAction(QStringLiteral("&Patrón"), this, [this] {
        startTool(std::make_unique<PatternTool>(document_, *viewport_, std::nullopt));
    });
    decorate(patternAction_, QStringLiteral("pattern-solid"),
             QStringLiteral("Repetir un cuerpo en fila o alrededor de un eje"));
    planeAction_ = solidMenu->addAction(QStringLiteral("Plano de &construcción"), this, [this] {
        startTool(std::make_unique<PlaneTool>(document_, *viewport_, std::nullopt));
    });
    decorate(planeAction_, QStringLiteral("plane-solid"),
             QStringLiteral("Crear un plano desfasado o girado respecto a un plano de origen, una cara u otro plano"),
             QStringLiteral("Plano"));
    axisAction_ = solidMenu->addAction(QStringLiteral("Eje de construcción"), this, [this] {
        startTool(std::make_unique<AxisTool>(document_, *viewport_, std::nullopt));
    });
    decorate(axisAction_, QStringLiteral("axis-solid"),
             QStringLiteral("Crear un eje: la intersección de dos planos o la perpendicular a un plano"), QStringLiteral("Eje"));
    parametersAction_ = solidMenu->addAction(QStringLiteral("&Parámetros…"), this, [this] {
        ParametersDialog dialog(document_.parameters(), this);
        if (dialog.exec() == QDialog::Accepted && dialog.parameters() != document_.parameters()) {
            commands_.push(std::make_unique<commands::SetParameters>(dialog.parameters(), dialog.renames()));
        }
    });
    decorate(parametersAction_, QStringLiteral("parameters-solid"),
             QStringLiteral("Nombrar valores (grosor = 3) y usarlos en las operaciones"), QStringLiteral("Parámetros"));
    boxAction_ = solidMenu->addAction(QStringLiteral("&Caja"), QKeySequence(Qt::Key_B), this, [this] {
        startTool(std::make_unique<BoxTool>(document_, *viewport_, std::nullopt));
    });
    decorate(boxAction_, QStringLiteral("box-solid"), QStringLiteral("Una caja con medidas y posición, ligable a parámetros"),
             QStringLiteral("Caja"));
    cylinderAction_ = solidMenu->addAction(QStringLiteral("Cili&ndro"), QKeySequence(Qt::SHIFT | Qt::Key_C), this, [this] {
        startTool(std::make_unique<CylinderTool>(document_, *viewport_, std::nullopt));
    });
    decorate(cylinderAction_, QStringLiteral("cylinder-solid"),
             QStringLiteral("Un cilindro con diámetro y posición, ligable a parámetros"), QStringLiteral("Cilindro"));
    gearAction_ = solidMenu->addAction(QStringLiteral("&Engranaje"), QKeySequence(Qt::Key_G), this, [this] {
        startTool(std::make_unique<GearTool>(document_, *viewport_, std::nullopt));
    });
    decorate(gearAction_, QStringLiteral("gear-solid"),
             QStringLiteral("Generar un engranaje recto o helicoidal (perfil de involuta)"), QStringLiteral("Engranaje"));
    threadAction_ = solidMenu->addAction(QStringLiteral("&Rosca"), QKeySequence(Qt::SHIFT | Qt::Key_T), this, [this] {
        startTool(std::make_unique<ThreadTool>(document_, *viewport_, std::nullopt));
    });
    decorate(threadAction_, QStringLiteral("thread-solid"),
             QStringLiteral("Generar un tornillo o varilla roscada (rosca métrica)"), QStringLiteral("Rosca"));
    moveAction_ = solidMenu->addAction(QStringLiteral("&Mover cuerpo"), this, [this] {
        startTool(std::make_unique<MoveTool>(document_, *viewport_, std::nullopt));
    });
    decorate(moveAction_, QStringLiteral("move"), QStringLiteral("Mover, girar o copiar un cuerpo"), QStringLiteral("Mover"));
    combineAction_ = solidMenu->addAction(QStringLiteral("Co&mbinar cuerpos"), this, [this] {
        startTool(std::make_unique<CombineTool>(document_, *viewport_, std::nullopt));
    });
    decorate(combineAction_, QStringLiteral("combine-solid"),
             QStringLiteral("Unir, cortar o intersecar cuerpos entre sí"), QStringLiteral("Combinar"));
    organizeAction_ = solidMenu->addAction(QStringLiteral("Cuerpos y &componentes…"), this, [this] {
        BodiesDialog dialog(document_, this);
        if (dialog.exec() == QDialog::Accepted && dialog.info() != document_.bodyInfo()) {
            commands_.push(std::make_unique<commands::SetBodyInfo>(dialog.info()));
        }
    });
    decorate(organizeAction_, QStringLiteral("components-solid"),
             QStringLiteral("Ponerle nombre a los cuerpos y agruparlos en componentes"), QStringLiteral("Componentes"));
    jointAction_ = solidMenu->addAction(QStringLiteral("&Unión"), this, [this] {
        startTool(std::make_unique<JointTool>(document_, *viewport_, std::nullopt));
    });
    decorate(jointAction_, QStringLiteral("joint-solid"),
             QStringLiteral("Unir dos cuerpos por sus caras (rígida, giratoria o deslizante)"), QStringLiteral("Unión"));
    measureAction_ = solidMenu->addAction(QStringLiteral("&Medir"), this, [this](bool on) { toggleMeasure(on); });
    measureAction_->setCheckable(true);
    decorate(measureAction_, QStringLiteral("measure-solid"),
             QStringLiteral("Medir caras y aristas: área, longitud, radio, distancia y ángulo (Mayús o clic para añadir otra)"),
             QStringLiteral("Medir"));
    sectionAction_ = solidMenu->addAction(QStringLiteral("&Sección"), this, [this](bool on) { toggleSection(on); });
    sectionAction_->setCheckable(true);
    decorate(sectionAction_, QStringLiteral("section-solid"),
             QStringLiteral("Cortar la vista con un plano para ver el interior (no cambia el modelo)"), QStringLiteral("Sección"));
    decorate(shellAction_, QStringLiteral("shell"), QStringLiteral("Vaciar un cuerpo dejando abiertas las caras elegidas"));

    // Herramientas generadoras: hacen unas preguntas y crean un diseño normal en el historial.
    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Herramientas"));
    QString currentGroup;
    for (const document::Generator* generator : document::generators()) {
        // Las herramientas van agrupadas por familia (Cajas, Letreros, Engranajes), con su título en el menú.
        const QString group = QString::fromStdString(generator->category());
        if (group != currentGroup) {
            toolsMenu->addSection(group);
            currentGroup = group;
        }
        generatorGroups_.push_back(group);
        QAction* action = toolsMenu->addAction(QString::fromStdString(generator->title()), this,
                                               [this, generator] { startGenerator(*generator); });
        decorate(action, QString::fromStdString(generator->icon()), QString::fromStdString(generator->description()),
                 QString::fromStdString(generator->title()));
        // Alt+1, Alt+2… abren las herramientas generadoras en el orden del menú (las seis primeras).
        if (generatorActions_.size() < 9) {
            action->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(generatorActions_.size() + 1)));
        }
        generatorActions_.push_back(action);
    }

    // Herramientas de dibujo por familias (como en Fusion 360); la primera
    // de cada familia lleva su tecla.
    using Tool = SketchSession::Tool;
    struct ToolEntry {
        Tool tool;
        QString text;
        QKeySequence key;
        QString icon;
    };
    const std::vector<std::pair<QString, std::vector<ToolEntry>>> families{
        {QStringLiteral("Línea"), {{Tool::Line, QStringLiteral("Línea"), QKeySequence(Qt::Key_L), QStringLiteral("line")}}},
        {QStringLiteral("Rectángulo"),
         {{Tool::Rectangle, QStringLiteral("Rectángulo por 2 esquinas"), QKeySequence(Qt::Key_R), QStringLiteral("rectangle")},
          {Tool::Rectangle3Point, QStringLiteral("Rectángulo por 3 puntos"), {}, QStringLiteral("rectangle-3point")},
          {Tool::RectangleCenter, QStringLiteral("Rectángulo por centro"), {}, QStringLiteral("rectangle-center")}}},
        {QStringLiteral("Círculo"),
         {{Tool::Circle, QStringLiteral("Círculo por centro y diámetro"), QKeySequence(Qt::Key_C), QStringLiteral("circle")},
          {Tool::Circle2Point, QStringLiteral("Círculo por 2 puntos"), {}, QStringLiteral("circle-2point")},
          {Tool::Circle3Point, QStringLiteral("Círculo por 3 puntos"), {}, QStringLiteral("circle-3point")},
          {Tool::CircleTangent2, QStringLiteral("Círculo tangente a 2 curvas"), {}, QStringLiteral("circle-tangent2")},
          {Tool::CircleTangent3, QStringLiteral("Círculo tangente a 3 curvas"), {}, QStringLiteral("circle-tangent3")}}},
        {QStringLiteral("Arco"),
         {{Tool::Arc, QStringLiteral("Arco por 3 puntos"), QKeySequence(Qt::Key_A), QStringLiteral("arc")},
          {Tool::ArcCenter, QStringLiteral("Arco por centro"), {}, QStringLiteral("arc-center")},
          {Tool::ArcTangent, QStringLiteral("Arco tangente"), {}, QStringLiteral("arc-tangent")}}},
        {QStringLiteral("Polígono"),
         {{Tool::PolygonCircumscribed, QStringLiteral("Polígono circunscrito"), QKeySequence(Qt::Key_P), QStringLiteral("polygon-circumscribed")},
          {Tool::PolygonInscribed, QStringLiteral("Polígono inscrito"), {}, QStringLiteral("polygon-inscribed")},
          {Tool::PolygonEdge, QStringLiteral("Polígono por lado"), {}, QStringLiteral("polygon-edge")}}},
        {QStringLiteral("Ranura"),
         {{Tool::SlotCenterToCenter, QStringLiteral("Ranura centro a centro"), {}, QStringLiteral("slot-center-to-center")},
          {Tool::SlotOverall, QStringLiteral("Ranura total"), {}, QStringLiteral("slot-overall")},
          {Tool::SlotCenterPoint, QStringLiteral("Ranura por punto central"), {}, QStringLiteral("slot-center-point")},
          {Tool::SlotArc, QStringLiteral("Ranura en arco"), {}, QStringLiteral("slot-arc")}}},
        {QStringLiteral("Elipse"),
         {{Tool::Ellipse, QStringLiteral("Elipse"), {}, QStringLiteral("ellipse")},
          {Tool::EllipseArc, QStringLiteral("Arco de elipse"), {}, QStringLiteral("ellipse-arc")}}},
        {QStringLiteral("Spline"),
         {{Tool::SplineFit, QStringLiteral("Spline por puntos de paso"), {}, QStringLiteral("spline-fit")},
          {Tool::SplineControl, QStringLiteral("Spline por polos"), {}, QStringLiteral("spline-control")}}},
        {QStringLiteral("Cónica"), {{Tool::Conic, QStringLiteral("Curva cónica"), {}, QStringLiteral("conic")}}},
        // Modificar
        {QStringLiteral("Recortar"),
         {{Tool::Trim, QStringLiteral("Recortar"), QKeySequence(Qt::Key_T), QStringLiteral("trim")},
          {Tool::Extend, QStringLiteral("Extender"), {}, QStringLiteral("extend")},
          {Tool::Break, QStringLiteral("Partir"), {}, QStringLiteral("break")}}},
        {QStringLiteral("Empalme"),
         {{Tool::Fillet, QStringLiteral("Empalme"), {}, QStringLiteral("fillet")},
          {Tool::Chamfer, QStringLiteral("Chaflán"), {}, QStringLiteral("chamfer")}}},
        {QStringLiteral("Desfase"), {{Tool::Offset, QStringLiteral("Desfase"), QKeySequence(Qt::Key_O), QStringLiteral("offset")}}},
        {QStringLiteral("Simetría"), {{Tool::Mirror, QStringLiteral("Simetría"), {}, QStringLiteral("mirror")}}},
        {QStringLiteral("Mover"),
         {{Tool::Move, QStringLiteral("Mover"), QKeySequence(Qt::Key_M), QStringLiteral("move")},
          {Tool::Copy, QStringLiteral("Copiar"), {}, QStringLiteral("copy")},
          {Tool::Rotate, QStringLiteral("Girar"), {}, QStringLiteral("rotate")},
          {Tool::Scale, QStringLiteral("Escalar"), {}, QStringLiteral("scale")}}},
        {QStringLiteral("Patrón"),
         {{Tool::PatternRectangular, QStringLiteral("Patrón rectangular"), {}, QStringLiteral("pattern-rectangular")},
          {Tool::PatternCircular, QStringLiteral("Patrón circular"), {}, QStringLiteral("pattern-circular")}}},
        {QStringLiteral("Proyectar"), {{Tool::Project, QStringLiteral("Proyectar aristas"), QKeySequence(Qt::Key_P | Qt::ShiftModifier), QStringLiteral("project")}}},
        {QStringLiteral("Texto"),
         {{Tool::Text, QStringLiteral("Texto"), {}, QStringLiteral("text")},
          {Tool::Image, QStringLiteral("Imagen de referencia"), {}, QStringLiteral("image")}}},
    };
    // A partir de esta familia van a la barra «Modificar».
    const std::size_t firstModifyFamily = 9;
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    for (const auto& [family, entries] : families) {
        QMenu* menu = entries.size() > 1 ? sketchMenu->addMenu(family) : sketchMenu;
        std::vector<QAction*> actions;
        for (const ToolEntry& entry : entries) {
            QAction* action = menu->addAction(entry.text);
            action->setShortcut(entry.key);
            action->setCheckable(true);
            decorate(action, entry.icon, help(entry.icon), family);
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, tool = entry.tool](bool checked) {
                if (sketch_) {
                    sketch_->setTool(checked ? tool : Tool::None);
                }
            });
            toolActions_[entry.tool] = action;
            connect(action, &QAction::triggered, this, [this] { clearNavigation(); });
            actions.push_back(action);
        }
        toolFamilies_.push_back({entries.size() > 1 ? menu : nullptr, actions});
        if (toolFamilies_.size() == firstModifyFamily) {
            sketchMenu->addSeparator();
        }
    }

    sketchMenu->addSeparator();
    deleteAction_ = sketchMenu->addAction(QStringLiteral("&Borrar"), QKeySequence(QKeySequence::Delete), this,
                                          [this] { sketch_->deleteSelection(); });
    decorate(deleteAction_, QStringLiteral("delete"));
    constructionAction_ = sketchMenu->addAction(QStringLiteral("Alternar &construcción"), QKeySequence(Qt::Key_X),
                                                this, [this] { sketch_->toggleConstruction(); });
    decorate(constructionAction_, QStringLiteral("construction"),
             QStringLiteral("Convierte la geometría en auxiliar (no forma perfiles)"),
             QStringLiteral("Construcción"));
    lockAction_ = sketchMenu->addAction(QStringLiteral("Alternar b&loqueo"), QKeySequence(Qt::Key_K), this,
                                        [this] { sketch_->toggleLock(); });
    decorate(lockAction_, QStringLiteral("lock"), QStringLiteral("Impide mover la geometría seleccionada"),
             QStringLiteral("Bloqueo"));

    // Restricciones y cotas: se seleccionan las entidades y se aplica.
    QMenu* constraintMenu = sketchMenu->addMenu(QStringLiteral("Res&tricciones"));
    using T = sketch::ConstraintType;
    for (const auto& [type, text, icon] :
         {std::tuple{T::Coincident, QStringLiteral("Coincidente"), QStringLiteral("c-coincident")},
          std::tuple{T::Horizontal, QStringLiteral("Horizontal"), QStringLiteral("c-horizontal")},
          std::tuple{T::Vertical, QStringLiteral("Vertical"), QStringLiteral("c-vertical")},
          std::tuple{T::Parallel, QStringLiteral("Paralela"), QStringLiteral("c-parallel")},
          std::tuple{T::Perpendicular, QStringLiteral("Perpendicular"), QStringLiteral("c-perpendicular")},
          std::tuple{T::Tangent, QStringLiteral("Tangente"), QStringLiteral("c-tangent")},
          std::tuple{T::Equal, QStringLiteral("Igual"), QStringLiteral("c-equal")},
          std::tuple{T::Collinear, QStringLiteral("Colineal"), QStringLiteral("c-collinear")},
          std::tuple{T::Concentric, QStringLiteral("Concéntrica"), QStringLiteral("c-concentric")},
          std::tuple{T::Midpoint, QStringLiteral("Punto medio"), QStringLiteral("c-midpoint")},
          std::tuple{T::Symmetric, QStringLiteral("Simetría"), QStringLiteral("c-symmetric")},
          std::tuple{T::Fix, QStringLiteral("Fijar"), QStringLiteral("c-fix")},
          std::tuple{T::Curvature, QStringLiteral("Curvatura (G2)"), QStringLiteral("c-curvature")}}) {
        QAction* action = constraintMenu->addAction(text, this, [this, type = type] { sketch_->applyConstraint(type); });
        decorate(action, icon, help(text) + QStringLiteral(" · selecciona las entidades y aplica"));
        constraintActions_.push_back(action);
    }
    dimensionAction_ = sketchMenu->addAction(QStringLiteral("C&ota"), QKeySequence(Qt::Key_D), this,
                                             [this] { sketch_->addDimension(); });
    decorate(dimensionAction_, QStringLiteral("dimension"), QStringLiteral("Acotar la selección"));
    intersectAction_ = sketchMenu->addAction(QStringLiteral("&Intersección con la pieza"), this,
                                             [this] { sketch_->intersectModel(); });
    decorate(intersectAction_, QStringLiteral("intersect"), QStringLiteral("Traza la intersección de la pieza con el plano del sketch"),
             QStringLiteral("Intersección"));
    importDxfAction_ = sketchMenu->addAction(QStringLiteral("Importar &DXF…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Importar DXF"), QString(),
                                                          QStringLiteral("Dibujos DXF (*.dxf)"));
        if (!path.isEmpty()) {
            sketch_->importDxf(path);
        }
    });
    decorate(importDxfAction_, QStringLiteral("import-dxf"));

    // Cinta al estilo de Fusion 360: pestaña Sólido y, mientras se edita un
    // sketch, la pestaña Sketch con «Terminar sketch» al final.
    ribbon_ = new Ribbon(this);
    solidTab_ = ribbon_->addTab(QStringLiteral("SÓLIDO"));
    sketchTab_ = ribbon_->addTab(QStringLiteral("SKETCH"));
    ribbon_->addGroup(solidTab_, QStringLiteral("CREAR"));
    ribbon_->addAction(solidTab_, createSketchAction_);
    ribbon_->addAction(solidTab_, extrudeAction_);
    ribbon_->addAction(solidTab_, revolveAction_);
    ribbon_->addAction(solidTab_, sweepAction_);
    ribbon_->addAction(solidTab_, loftAction_);
    ribbon_->addAction(solidTab_, boxAction_);
    ribbon_->addAction(solidTab_, cylinderAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("MODIFICAR"));
    ribbon_->addAction(solidTab_, filletAction_);
    ribbon_->addAction(solidTab_, chamferAction_);
    ribbon_->addAction(solidTab_, shellAction_);
    ribbon_->addAction(solidTab_, holeAction_);
    ribbon_->addAction(solidTab_, mirrorAction_);
    ribbon_->addAction(solidTab_, patternAction_);
    ribbon_->addAction(solidTab_, moveAction_);
    ribbon_->addAction(solidTab_, combineAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("ORGANIZAR"));
    ribbon_->addAction(solidTab_, organizeAction_);
    ribbon_->addAction(solidTab_, jointAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("CONSTRUIR"));
    ribbon_->addAction(solidTab_, planeAction_);
    ribbon_->addAction(solidTab_, axisAction_);
    ribbon_->addAction(solidTab_, parametersAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("GENERAR"));
    ribbon_->addAction(solidTab_, gearAction_);
    ribbon_->addAction(solidTab_, threadAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("INSPECCIONAR"));
    ribbon_->addAction(solidTab_, measureAction_);
    ribbon_->addAction(solidTab_, sectionAction_);
    toolsTab_ = ribbon_->addTab(QStringLiteral("HERRAMIENTAS"));
    // Un grupo por familia: las cajas y lightbox juntas, los letreros aparte y los engranajes en el suyo.
    QString shownGroup;
    for (std::size_t i = 0; i < generatorActions_.size(); ++i) {
        if (generatorGroups_[i] != shownGroup) {
            shownGroup = generatorGroups_[i];
            ribbon_->addGroup(toolsTab_, shownGroup.toUpper());
        }
        ribbon_->addAction(toolsTab_, generatorActions_[i]);
    }

    auto family = [this, &families](const QString& name) {
        const std::size_t i = static_cast<std::size_t>(
            std::find_if(families.begin(), families.end(), [&](const auto& f) { return f.first == name; }) -
            families.begin());
        ribbon_->addFamily(sketchTab_, toolFamilies_[i].menu, toolFamilies_[i].actions);
    };
    ribbon_->addGroup(sketchTab_, QStringLiteral("CREAR"));
    for (const char* name : {"Línea", "Rectángulo", "Círculo", "Arco", "Polígono", "Elipse", "Ranura", "Spline",
                             "Cónica", "Simetría", "Patrón", "Proyectar"}) {
        family(QString::fromUtf8(name));
    }
    ribbon_->addAction(sketchTab_, intersectAction_);
    ribbon_->addAction(sketchTab_, dimensionAction_);
    ribbon_->addGroup(sketchTab_, QStringLiteral("MODIFICAR"));
    for (const char* name : {"Recortar", "Empalme", "Desfase", "Mover"}) {
        family(QString::fromUtf8(name));
    }
    ribbon_->addGroup(sketchTab_, QStringLiteral("RESTRICCIONES"));
    if (QToolButton* constraints = ribbon_->addMenu(sketchTab_, QStringLiteral("Restricciones"),
                                                    uiIcon(QStringLiteral("c-perpendicular")), constraintMenu)) {
        constraints->setToolTip(QStringLiteral("<b>Restricciones</b><br>Relaciones entre entidades: horizontal, "
                                               "paralela, tangente, igual…"));
    }
    ribbon_->addGroup(sketchTab_, QStringLiteral("INSERTAR"));
    family(QStringLiteral("Texto"));
    ribbon_->addAction(sketchTab_, importDxfAction_);
    ribbon_->addGroup(sketchTab_, QStringLiteral("OPCIONES"));
    ribbon_->addAction(sketchTab_, constructionAction_);
    ribbon_->addAction(sketchTab_, lockAction_);
    ribbon_->addTrailing(sketchTab_, finishSketchAction_);
    ribbon_->setTabVisible(sketchTab_, false);

    // Barra de acceso rápido arriba de la cinta (U5): guardar, deshacer, rehacer y el nombre del diseño, como la barra de
    // aplicación de Fusion 360.
    QToolBar* quickBar = addToolBar(QStringLiteral("Acceso rápido"));
    quickBar->setObjectName(QStringLiteral("quickToolbar"));
    quickBar->setMovable(false);
    quickBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    quickBar->setIconSize(QSize(20, 20));
    for (QAction* action : {saveAction_, undoAction_, redoAction_}) {
        if (action != nullptr) {
            quickBar->addAction(action);
        }
    }
    quickBar->addSeparator();
    docTitle_ = new QLabel(this);
    docTitle_->setObjectName(QStringLiteral("docTitle"));
    docTitle_->setContentsMargins(8, 0, 8, 0);
    QFont titleFont = docTitle_->font();
    titleFont.setBold(true);
    docTitle_->setFont(titleFont);
    quickBar->addWidget(docTitle_);
    addToolBarBreak();

    QToolBar* ribbonBar = addToolBar(QStringLiteral("Cinta"));
    ribbonBar->setObjectName(QStringLiteral("ribbonToolbar"));
    ribbonBar->setMovable(false);
    ribbonBar->setContentsMargins(0, 0, 0, 0);
    ribbonBar->addWidget(ribbon_);


    QMenu* view = menuBar()->addMenu(QStringLiteral("&Ver"));
    QAction* homeAction = view->addAction(QStringLiteral("Vista &inicial"), QKeySequence(Qt::Key_Home), this,
                                          [this] { viewport_->resetView(); });
    decorate(homeAction, QStringLiteral("view-home"), QStringLiteral("Vista isométrica de todo el modelo"));
    QAction* fitAction = view->addAction(QStringLiteral("&Ajustar a la ventana"), QKeySequence(Qt::Key_F6), this,
                                         [this] { viewport_->fitAll(); });
    decorate(fitAction, QStringLiteral("view-fit"), QStringLiteral("Encuadrar todo el modelo en la ventana"));
    lookAtSketchAction_ = view->addAction(QStringLiteral("&Mirar al sketch"), this,
                                          [this] { viewport_->lookAtSketch(); });
    decorate(lookAtSketchAction_, QStringLiteral("look-at-sketch"),
             QStringLiteral("Orientar la cámara de frente al plano del sketch"));
    // Barra flotante sobre el visor, como la de navegación de Fusion 360.
    // Orbitar / desplazar / zoom como herramientas: activas, el arrastre
    // izquierdo hace eso. Esc o volver a pulsar las desactiva.
    navGroup_ = new QActionGroup(this);
    navGroup_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    std::vector<QAction*> navActions;
    using Mode = render::Viewport::NavigationMode;
    for (const auto& [text, icon, hint, mode] :
         {std::tuple{QStringLiteral("Orbitar"), QStringLiteral("nav-orbit"),
                     QStringLiteral("Arrastra con el botón izquierdo para girar la vista (Shift + botón central también)"),
                     Mode::Orbit},
          std::tuple{QStringLiteral("Desplazar"), QStringLiteral("nav-pan"),
                     QStringLiteral("Arrastra para mover la vista (botón derecho o central también)"), Mode::Pan},
          std::tuple{QStringLiteral("Zoom"), QStringLiteral("nav-zoom"),
                     QStringLiteral("Arrastra arriba o abajo para acercar o alejar (la rueda también)"), Mode::Zoom}}) {
        QAction* action = new QAction(text, this);
        action->setCheckable(true);
        decorate(action, icon, hint);
        navGroup_->addAction(action);
        connect(action, &QAction::toggled, this, [this, mode = mode](bool on) {
            viewport_->setNavigationMode(on ? mode : Mode::Default);
            if (on) {
                // Una herramienta de navegación reemplaza a la de dibujo.
                if (sketch_) {
                    sketch_->setTool(SketchSession::Tool::None);
                }
                for (const auto& [tool, toolAction] : toolActions_) {
                    toolAction->setChecked(false);
                }
            }
        });
        navActions.push_back(action);
    }
    navActions.push_back(homeAction);
    navActions.push_back(fitAction);
    navActions.push_back(lookAtSketchAction_);
    new NavBar(viewport_, navActions);
    // Encima de todo lo que flota sobre el visor.
    startScreen_ = new StartScreen(viewport_);
    startScreen_->hide();
    // Cualquier cosa que se elija en el panel lo cierra antes de actuar.
    const auto closeRecents = [this] {
        recentsOpen_ = false;
        updateStartScreen();
    };
    connect(startScreen_, &StartScreen::closeRequested, this, closeRecents);
    connect(startScreen_, &StartScreen::newRequested, this, [this, closeRecents] {
        closeRecents();
        newDocument();
    });
    connect(startScreen_, &StartScreen::openRequested, this, [this, closeRecents] {
        closeRecents();
        openDocument();
    });
    connect(startScreen_, &StartScreen::importRequested, this, [this, closeRecents] {
        closeRecents();
        importStep();
    });
    connect(startScreen_, &StartScreen::recentRequested, this, [this, closeRecents](const QString& path) {
        closeRecents();
        openRecent(path);
    });
    updateSketchActions();
    view->addSeparator();
    auto* measures = view->addAction(QStringLiteral("Mostrar &medidas del sketch"));
    measures->setCheckable(true);
    measures->setChecked(QSettings().value(QStringLiteral("view/sketchMeasures"), true).toBool());
    viewport_->setSketchMeasuresVisible(measures->isChecked());
    connect(measures, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("view/sketchMeasures"), on);
        viewport_->setSketchMeasuresVisible(on);
    });
    // Medidas y restricciones: por defecto solo en lo que se mira (cursor o selección).
    auto* allMeasures = view->addAction(QStringLiteral("Medidas de &todo el sketch a la vez"));
    auto* allConstraints = view->addAction(QStringLiteral("Restricciones de t&odo el sketch a la vez"));
    for (QAction* action : {allMeasures, allConstraints}) {
        action->setCheckable(true);
    }
    sketchMeasuresAction_ = measures;
    allMeasuresAction_ = allMeasures;
    allConstraintsAction_ = allConstraints;
    allMeasures->setChecked(QSettings().value(QStringLiteral("view/sketchAllMeasures"), false).toBool());
    allConstraints->setChecked(QSettings().value(QStringLiteral("view/sketchAllConstraints"), false).toBool());
    const auto applyAll = [this, allMeasures, allConstraints] {
        QSettings().setValue(QStringLiteral("view/sketchAllMeasures"), allMeasures->isChecked());
        QSettings().setValue(QStringLiteral("view/sketchAllConstraints"), allConstraints->isChecked());
        viewport_->setSketchShowAll(allMeasures->isChecked(), allConstraints->isChecked());
    };
    connect(allMeasures, &QAction::toggled, this, applyAll);
    connect(allConstraints, &QAction::toggled, this, applyAll);
    applyAll();
    view->addSeparator();
    auto* floating = view->addAction(QStringLiteral("Comandos &flotantes sobre el lienzo"));
    floating->setCheckable(true);
    floating->setChecked(QSettings().value(QStringLiteral("view/floatingCommands"), false).toBool());
    floating->setToolTip(QStringLiteral("El panel de cada herramienta flota sobre el lienzo (arriba a la derecha) en vez de ir acoplado"));
    connect(floating, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("view/floatingCommands"), on); });
    QMenu* panels = view->addMenu(QStringLiteral("&Paneles"));
    panels->addAction(navigatorDock_->toggleViewAction());
    panels->addAction(propertiesDock_->toggleViewAction());
    view->addAction(QStringLiteral("&Restablecer paneles"), this, [this] {
        restoreState(defaultLayout_, kLayoutVersion);
        for (QDockWidget* dock : {navigatorDock_, propertiesDock_, timelineDock_}) {
            dock->setFloating(false);
            dock->show();
        }
    });
    view->addSeparator();
    QMenu* themeMenu = view->addMenu(QStringLiteral("&Tema"));
    auto* themeGroup = new QActionGroup(this);
    for (const auto& [text, theme] : {std::pair{QStringLiteral("&Sistema"), Theme::System},
                                      std::pair{QStringLiteral("&Claro"), Theme::Light},
                                      std::pair{QStringLiteral("&Oscuro"), Theme::Dark}}) {
        QAction* action = themeMenu->addAction(text, this, [theme] { setTheme(theme); });
        action->setCheckable(true);
        action->setChecked(theme == currentTheme());
        themeGroup->addAction(action);
    }

    // Todas las acciones con ayuda breve en su tooltip, y los menús la enseñan
    // también (por defecto un menú no muestra tooltips).
    for (QAction* action : findChildren<QAction*>()) {
        QString name = action->text();
        name.remove(QLatin1Char('&'));
        if (const QString text = help(name); !text.isEmpty() && !action->menu()) {
            addHint(action, text);
        }
    }
    for (QMenu* menu : findChildren<QMenu*>()) {
        menu->setToolTipsVisible(true);
    }
    assignShortcuts();
    createSketchPalette();
}

void MainWindow::createSketchPalette()
{
    sketchPalette_ = new QFrame(viewport_);
    sketchPalette_->setObjectName(QStringLiteral("sketchPalette"));
    sketchPalette_->setStyleSheet(QStringLiteral(
        "#sketchPalette { background: rgba(32, 38, 48, 225); border: 1px solid #3A4352; border-radius: 6px; }"
        "#sketchPalette QCheckBox, #sketchPalette QLabel { color: #E6EAF0; }"));
    auto* column = new QVBoxLayout(sketchPalette_);
    column->setContentsMargins(10, 6, 10, 8);
    column->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("<b>Mostrar</b>"), sketchPalette_);
    column->addWidget(title);
    // Cada casilla es la misma acción del menú Ver: se mantienen al día en los dos sentidos.
    const std::vector<std::pair<QString, QAction*>> rows = {
        {QStringLiteral("Medidas"), sketchMeasuresAction_},
        {QStringLiteral("Medidas de todo"), allMeasuresAction_},
        {QStringLiteral("Restricciones de todo"), allConstraintsAction_}};
    for (const auto& [text, action] : rows) {
        if (action == nullptr) {
            continue;
        }
        auto* box = new QCheckBox(text, sketchPalette_);
        box->setChecked(action->isChecked());
        box->setFocusPolicy(Qt::NoFocus);
        box->setToolTip(action->toolTip());
        column->addWidget(box);
        connect(box, &QCheckBox::toggled, action, &QAction::setChecked);
        connect(action, &QAction::toggled, box, [box](bool on) {
            const QSignalBlocker block(box);
            box->setChecked(on);
        });
    }
    auto* ground = new QCheckBox(QStringLiteral("Rejilla"), sketchPalette_);
    ground->setObjectName(QStringLiteral("paletteGrid"));
    ground->setChecked(groundVisible_);
    ground->setFocusPolicy(Qt::NoFocus);
    ground->setToolTip(QStringLiteral("La rejilla del suelo (la misma casilla del Navegador)"));
    column->addWidget(ground);
    connect(ground, &QCheckBox::toggled, this, [this](bool on) {
        groundVisible_ = on;
        setViewDirty(true);
        viewport_->setGroundVisible(on);
    });
    sketchPalette_->adjustSize();
    sketchPalette_->move(12, 12);
    sketchPalette_->hide();
}

void MainWindow::updateSketchPalette()
{
    if (sketchPalette_ == nullptr) {
        return;
    }
    const bool show = sketch_ != nullptr;
    if (show) {
        if (auto* ground = sketchPalette_->findChild<QCheckBox*>(QStringLiteral("paletteGrid"))) {
            const QSignalBlocker block(ground);
            ground->setChecked(groundVisible_);
        }
        sketchPalette_->adjustSize();
        sketchPalette_->move(12, 12);
        sketchPalette_->raise();
    }
    sketchPalette_->setVisible(show);
}

// Atajos de una tecla (o Mayús + tecla) para las herramientas del sólido, sin pisar los del sketch (L R C A P T O M X K D,
// que solo valen con un sketch abierto) ni los de archivo. Los de las acciones que ya tenían el suyo (E F H B G…) se
// dejan como estaban. La tabla está también en la guía de pruebas (sección ATJ).
void MainWindow::assignShortcuts()
{
    const auto key = [](QAction* action, const QKeySequence& sequence) {
        if (action != nullptr && action->shortcut().isEmpty()) {
            action->setShortcut(sequence);
        }
    };
    key(createSketchAction_, QKeySequence(Qt::Key_S));
    key(revolveAction_, QKeySequence(Qt::SHIFT | Qt::Key_R));
    key(sweepAction_, QKeySequence(Qt::SHIFT | Qt::Key_W));
    key(loftAction_, QKeySequence(Qt::SHIFT | Qt::Key_Y));
    key(chamferAction_, QKeySequence(Qt::SHIFT | Qt::Key_F));
    key(shellAction_, QKeySequence(Qt::Key_V));
    key(mirrorAction_, QKeySequence(Qt::SHIFT | Qt::Key_M));
    key(patternAction_, QKeySequence(Qt::SHIFT | Qt::Key_A));
    key(planeAction_, QKeySequence(Qt::SHIFT | Qt::Key_N));
    key(axisAction_, QKeySequence(Qt::SHIFT | Qt::Key_X));
    key(parametersAction_, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    key(moveAction_, QKeySequence(Qt::SHIFT | Qt::Key_G));
    key(combineAction_, QKeySequence(Qt::SHIFT | Qt::Key_U));
    key(jointAction_, QKeySequence(Qt::SHIFT | Qt::Key_J));
    key(measureAction_, QKeySequence(Qt::Key_I));
    key(sectionAction_, QKeySequence(Qt::SHIFT | Qt::Key_S));
}

void MainWindow::openRadialMenu(const QPoint& at)
{
    std::vector<QAction*> items;
    if (sketch_ != nullptr) {
        for (const auto tool : {SketchSession::Tool::Line, SketchSession::Tool::Rectangle, SketchSession::Tool::Circle,
                                SketchSession::Tool::Arc, SketchSession::Tool::Trim, SketchSession::Tool::Offset}) {
            if (const auto it = toolActions_.find(tool); it != toolActions_.end()) {
                items.push_back(it->second);
            }
        }
        items.push_back(dimensionAction_);
        items.push_back(finishSketchAction_);
    } else {
        items = {createSketchAction_, extrudeAction_, revolveAction_, boxAction_, filletAction_,
                 chamferAction_,      holeAction_,    moveAction_};
    }
    items.erase(std::remove(items.begin(), items.end(), nullptr), items.end());
    if (items.empty()) {
        return;
    }
    auto* menu = new RadialMenu(items, viewport_);
    menu->openAt(at);
}

} // namespace nuf::app
