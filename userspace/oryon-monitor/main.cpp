#include "telemetry.h"

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <KWindowSystem>
#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QWindow>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
const QString serviceName = QStringLiteral("io.github.OryonMonitor");
const QString objectPath = QStringLiteral("/Monitor");
const QColor red("#ff4161");
const QColor ember("#ff805f");
const QColor violet("#b5a2ff");
const QLocale spanishLocale(QLocale::Spanish, QLocale::Spain);
constexpr int refreshMs = 1000;
constexpr int historyLength = 60;

const char *monitorStyle = R"QSS(
QWidget#monitor, QWidget#content { background: #101117; color: #f4f3fa; }
QScrollArea, QScrollArea > QWidget > QWidget { background: #101117; border: none; }
QFrame#topRule { background: #f43a58; border: none; }
QFrame#card { background: #191923; border: 1px solid #2d2a38; border-radius: 13px; }
QFrame#core { background: #12131b; border: 1px solid #2a2936; border-radius: 8px; }
QLabel { color: #f3f1fa; background: transparent; border: none; }
QLabel#title { font-size: 27px; font-weight: 800; }
QLabel#hardware { color: #acabba; font-size: 12px; }
QLabel#heading { font-size: 15px; font-weight: 700; }
QLabel#metric { font-size: 31px; font-weight: 800; }
QLabel#eyebrow { color: #a8a7b8; font-size: 10px; font-weight: 700; }
QLabel#muted { color: #aaa9bc; font-size: 11px; }
QLabel#detail { color: #dfdce9; font-size: 11px; }
QLabel#detailValue { color: #f1eff9; font-size: 11px; font-weight: 600; }
QLabel#coreFreq { font-size: 18px; font-weight: 750; }
QLabel#keyhint { background: #1e1b28; border: 1px solid #342c40; border-radius: 6px;
                 padding: 5px 8px; color: #ddd8e7; font-size: 11px; }
QLineEdit:focus { border-color: #df405c; }
QLineEdit { background: #12131b; color: #f3f0fa; border: 1px solid #393244; border-radius: 7px;
            padding: 6px 10px; selection-background-color: #8a2942; }
QTableView { background: #16161f; alternate-background-color: #1a1924; color: #e9e6f4;
             selection-background-color: #482238; selection-color: #fff8fc;
             border: 1px solid #2d2a38; border-radius: 6px; outline: none; }
QHeaderView { background: #201e2b; border: none; }
QHeaderView::section { background: #201e2b; color: #b9b6ca; border: none;
                       border-bottom: 1px solid #332e40; padding: 6px 9px; font-weight: 650; }
QHeaderView::section:hover { color: #ff8096; }
QTableView::item { padding-left: 7px; padding-right: 7px; }
QScrollBar:vertical { background: #171620; width: 8px; }
QScrollBar::handle:vertical { background: #5b3b56; min-height: 22px; border-radius: 3px; }
QScrollBar::handle:vertical:hover { background: #a04b65; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)QSS";

QString formatMetric(double value, int decimals = 0, const QString &unit = {})
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    return spanishLocale.toString(value, 'f', decimals) + unit;
}

QString speed(double bytesPerSecond)
{
    if (bytesPerSecond >= 1048576)
        return formatMetric(bytesPerSecond / 1048576, 1, " MiB/s");
    return formatMetric(bytesPerSecond / 1024, bytesPerSecond > 0 && bytesPerSecond < 10240 ? 1 : 0, " KiB/s");
}

QString availableText(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("—") : value;
}

QLabel *label(const QString &value, const char *name, QWidget *parent = nullptr)
{
    auto *result = new QLabel(value, parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    return result;
}

QFrame *card(const char *name = "card")
{
    auto *frame = new QFrame;
    frame->setObjectName(name);
    return frame;
}

QPixmap logo(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 48.0, size / 48.0);
    QLinearGradient background(0, 0, 48, 48);
    background.setColorAt(0, QColor("#452135"));
    background.setColorAt(1, QColor("#261823"));
    p.setPen(QPen(QColor("#6c3047"), 1));
    p.setBrush(background);
    p.drawRoundedRect(QRectF(0.8, 0.8, 46.4, 46.4), 11, 11);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(red, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawRoundedRect(QRectF(12.5, 12.5, 23, 23), 4, 4);
    for (int at : {18, 24, 30}) {
        p.drawLine(QPointF(at, 9), QPointF(at, 12));
        p.drawLine(QPointF(at, 36), QPointF(at, 39));
        p.drawLine(QPointF(9, at), QPointF(12, at));
        p.drawLine(QPointF(36, at), QPointF(39, at));
    }
    p.setPen(QPen(QColor("#ffd1da"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{{16, 25}, {20, 25}, {23, 19}, {26, 29}, {29, 23}, {32, 23}});
    return pixmap;
}

class Meter : public QWidget {
public:
    explicit Meter(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(4); }
    void updateLoad(double percent) { value = percent; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2c2433"));
        p.drawRoundedRect(rect(), 2, 2);
        if (value > 0) {
            p.setBrush(red);
            p.drawRoundedRect(QRectF(0, 0, std::clamp(width() * value / 100, 2.0, double(width())), height()), 2, 2);
        }
    }
private:
    double value = unavailable;
};

class Trace : public QWidget {
public:
    explicit Trace(const QColor &color) : tint(color) { setMinimumHeight(43); }
    void append(double value)
    {
        values.append(value);
        if (values.size() > historyLength)
            values.removeFirst();
        update();
    }
    void clear() { values.clear(); update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const QRectF area = QRectF(rect()).adjusted(2, 3, -2, -2);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#302c3b"), 1, Qt::DashLine));
        for (int line = 0; line < 3; ++line) {
            const double y = area.top() + area.height() * line / 2.0;
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        QPainterPath stroke;
        QPainterPath fill;
        QLinearGradient gradient(0, area.top(), 0, area.bottom());
        QColor shade = tint;
        shade.setAlpha(72);
        gradient.setColorAt(0, shade);
        shade.setAlpha(3);
        gradient.setColorAt(1, shade);
        bool drawing = false;
        QPointF last;
        for (qsizetype i = 0; i < values.size(); ++i) {
            if (!std::isfinite(values[i])) {
                if (drawing) {
                    fill.lineTo(last.x(), area.bottom());
                    fill.closeSubpath();
                }
                drawing = false;
                continue;
            }
            last = {area.right() - area.width() * (values.size() - 1 - i) / (historyLength - 1),
                    area.bottom() - area.height() * std::clamp(values[i], 0.0, 100.0) / 100};
            if (!drawing) {
                fill.moveTo(last.x(), area.bottom());
                fill.lineTo(last);
                stroke.moveTo(last);
                drawing = true;
            } else {
                fill.lineTo(last);
                stroke.lineTo(last);
            }
        }
        if (drawing) {
            fill.lineTo(last.x(), area.bottom());
            fill.closeSubpath();
        }
        p.fillPath(fill, gradient);
        p.setPen(QPen(tint, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(stroke);
        if (drawing) {
            p.setPen(Qt::NoPen);
            p.setBrush(tint);
            p.drawEllipse(last, 2.3, 2.3);
        }
    }
private:
    QColor tint;
    QList<double> values;
};

struct MetricCard {
    QFrame *frame;
    QLabel *value;
    QLabel *summary;
    QLabel *footer;
    Trace *trace;
};

MetricCard makeMetric(const QString &tag, const QString &hardware, const QColor &color, const QString &tooltip)
{
    auto *frame = card();
    auto *column = new QVBoxLayout(frame);
    column->setContentsMargins(15, 12, 15, 11);
    column->setSpacing(4);
    auto *top = new QHBoxLayout;
    auto *heading = label(tag, "eyebrow");
    heading->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    top->addWidget(heading);
    top->addStretch();
    top->addWidget(label(hardware, "muted"));
    column->addLayout(top);
    auto *value = label("—", "metric");
    column->addWidget(value);
    auto *summary = label("—", "detail");
    column->addWidget(summary);
    auto *trace = new Trace(color);
    trace->setToolTip(tooltip);
    column->addWidget(trace);
    auto *bottom = new QHBoxLayout;
    auto *footer = label("—", "muted");
    bottom->addWidget(footer);
    bottom->addStretch();
    bottom->addWidget(label("60 s", "muted"));
    column->addLayout(bottom);
    return {frame, value, summary, footer, trace};
}

struct CoreTile {
    QLabel *frequency;
    QLabel *load;
    Meter *bar;
};

class Processes : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 4; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        static const QStringList names{"Proceso", "PID", "CPU total", "RAM residente"};
        if (orientation != Qt::Horizontal)
            return {};
        if (role == Qt::DisplayRole)
            return names.value(section);
        if (role == Qt::TextAlignmentRole)
            return int((section == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        if (role == Qt::ToolTipRole && section == 2)
            return QStringLiteral("Porcentaje sobre la capacidad total de los ocho núcleos.");
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rows.size())
            return {};
        const auto &process = rows[index.row()];
        const int column = index.column();
        if (role == Qt::TextAlignmentRole)
            return int((column == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        if (role == Qt::ToolTipRole)
            return process.name + " · PID " + QString::number(process.pid);
        if (role == Qt::ForegroundRole && column == 2 && process.cpu >= 2)
            return QColor("#ff8096");
        if (role == Qt::UserRole && column == 2)
            return std::isfinite(process.cpu) ? process.cpu : -1.0;
        if (role == Qt::UserRole && column == 3)
            return std::isfinite(process.ramMiB) ? process.ramMiB : -1.0;
        if (role != Qt::UserRole && role != Qt::DisplayRole)
            return {};
        switch (column) {
        case 0: return process.name;
        case 1: return process.pid;
        case 2: return formatMetric(process.cpu, 1, " %");
        case 3: return formatMetric(process.ramMiB, 1, " MiB");
        default: return {};
        }
    }
    void setRows(QList<Process> next)
    {
        std::sort(next.begin(), next.end(), [](const Process &a, const Process &b) { return a.pid < b.pid; });
        if (rows.size() == next.size() && std::equal(rows.begin(), rows.end(), next.begin(),
                                                   [](const Process &a, const Process &b) { return a.pid == b.pid; })) {
            rows = std::move(next);
            if (!rows.isEmpty())
                emit dataChanged(index(0, 0), index(rows.size() - 1, 3));
        } else {
            beginResetModel();
            rows = std::move(next);
            endResetModel();
        }
    }
private:
    QList<Process> rows;
};

class ProcessFilter : public QSortFilterProxyModel {
protected:
    bool filterAcceptsRow(int row, const QModelIndex &sourceParent) const override
    {
        const auto expression = filterRegularExpression();
        return expression.pattern().isEmpty() ||
            sourceModel()->index(row, 0, sourceParent).data().toString().contains(expression) ||
            sourceModel()->index(row, 1, sourceParent).data().toString().contains(expression);
    }
};

}

class Monitor : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.OryonMonitor")
public:
    Monitor()
        : cpu(makeMetric("CPU", "Qualcomm Oryon", red, "Carga de CPU calculada sobre los ocho núcleos.")),
          gpu(makeMetric("GPU", "Adreno X1-45", ember, "Estimación del tiempo de GPU ocupado a partir de los clientes DRM accesibles.")),
          ram(makeMetric("MEMORIA", "RAM del sistema", violet, "Memoria en uso calculada con MemAvailable."))
    {
        setObjectName("monitor");
        setWindowTitle("Oryon Monitor");
        setMinimumSize(840, 570);
        resize(1120, 805);
        setStyleSheet(QString::fromLatin1(monitorStyle));
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *line = new QFrame;
        line->setObjectName("topRule");
        line->setFixedHeight(3);
        outer->addWidget(line);

        auto *header = new QWidget;
        auto *top = new QHBoxLayout(header);
        top->setContentsMargins(20, 14, 20, 13);
        top->setSpacing(12);
        auto *icon = label({}, "hardware");
        icon->setPixmap(logo(45));
        top->addWidget(icon);
        auto *identity = new QVBoxLayout;
        identity->setSpacing(0);
        identity->addWidget(label("Oryon Monitor", "title"));
        identity->addWidget(label("Snapdragon X Plus  ·  X1P-42-100", "hardware"));
        top->addLayout(identity);
        top->addStretch();
        live = label("●  EN DIRECTO", "eyebrow");
        live->setStyleSheet("color: #ff8096;");
        top->addWidget(live);
        shortcut = label("Meta + Esc", "keyhint");
        shortcut->setToolTip("Atajo global para mostrar u ocultar el panel");
        top->addWidget(shortcut, 0, Qt::AlignVCenter);
        outer->addWidget(header);

        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget;
        content->setObjectName("content");
        auto *column = new QVBoxLayout(content);
        column->setContentsMargins(20, 0, 20, 16);
        column->setSpacing(13);
        scroll->setWidget(content);
        outer->addWidget(scroll, 1);

        auto *metrics = new QHBoxLayout;
        metrics->setSpacing(12);
        for (const auto &tile : {cpu, gpu, ram})
            metrics->addWidget(tile.frame, 1);
        column->addLayout(metrics);

        auto *middle = new QHBoxLayout;
        middle->setSpacing(12);
        column->addLayout(middle);
        auto *coreCard = card();
        middle->addWidget(coreCard, 19);
        auto *coreColumn = new QVBoxLayout(coreCard);
        coreColumn->setContentsMargins(14, 10, 14, 12);
        coreColumn->setSpacing(7);
        auto *coreHeading = new QHBoxLayout;
        coreHeading->addWidget(label("Núcleos Oryon", "heading"));
        coreHeading->addStretch();
        auto *coreNote = label("Frecuencia por grupo", "muted");
        coreNote->setToolTip("El controlador SCMI informa de una frecuencia compartida por los núcleos de cada política cpufreq.");
        coreHeading->addWidget(coreNote);
        coreColumn->addLayout(coreHeading);
        auto *grid = new QGridLayout;
        grid->setSpacing(6);
        coreColumn->addLayout(grid);
        for (int i = 0; i < 8; ++i) {
            auto *frame = card("core");
            auto *layout = new QVBoxLayout(frame);
            layout->setContentsMargins(9, 6, 9, 7);
            layout->setSpacing(3);
            auto *caption = new QHBoxLayout;
            auto *name = label(QStringLiteral("NÚCLEO %1").arg(i), "eyebrow");
            name->setStyleSheet(i < 4 ? "color: #ff8196;" : "color: #ffab91;");
            caption->addWidget(name);
            caption->addStretch();
            auto *load = label("—", "muted");
            caption->addWidget(load);
            layout->addLayout(caption);
            auto *freq = label("—", "coreFreq");
            layout->addWidget(freq);
            auto *bar = new Meter;
            layout->addWidget(bar);
            grid->addWidget(frame, i / 4, i % 4);
            cores.append({freq, load, bar});
        }

        auto *systemCard = card();
        middle->addWidget(systemCard, 10);
        auto *system = new QVBoxLayout(systemCard);
        system->setContentsMargins(14, 10, 14, 12);
        system->setSpacing(7);
        system->addWidget(label("Sistema", "heading"));
        auto *vitalsGrid = new QGridLayout;
        vitalsGrid->setHorizontalSpacing(10);
        vitalsGrid->setVerticalSpacing(5);
        system->addLayout(vitalsGrid);
        const QStringList names{"Temperatura", "Batería", "Disco raíz", "Lectura / escritura", "Red"};
        for (int i = 0; i < names.size(); ++i) {
            vitalsGrid->addWidget(label(names[i], "muted"), i, 0);
            auto *value = label("—", "detailValue");
            value->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
            vitalsGrid->addWidget(value, i, 1);
            vitals.append(value);
        }
        system->addStretch();

        auto *processCard = card();
        column->addWidget(processCard, 1);
        auto *processColumn = new QVBoxLayout(processCard);
        processColumn->setContentsMargins(14, 10, 14, 11);
        processColumn->setSpacing(7);
        auto *processHeader = new QHBoxLayout;
        processHeader->addWidget(label("Procesos", "heading"));
        count = label("—", "muted");
        processHeader->addWidget(count);
        processHeader->addStretch();
        auto *search = new QLineEdit;
        search->setPlaceholderText("Buscar por nombre o PID");
        search->setAccessibleName("Buscar procesos");
        search->setClearButtonEnabled(true);
        search->setFixedWidth(210);
        processHeader->addWidget(search);
        processColumn->addLayout(processHeader);
        auto *table = new QTableView;
        table->setAccessibleName("Procesos del sistema");
        table->setMinimumHeight(141);
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(25);
        processes = new Processes(this);
        filter = new ProcessFilter;
        filter->setParent(this);
        filter->setSourceModel(processes);
        filter->setSortRole(Qt::UserRole);
        filter->setSortCaseSensitivity(Qt::CaseInsensitive);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        table->setModel(filter);
        table->setSortingEnabled(true);
        table->sortByColumn(2, Qt::DescendingOrder);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setColumnWidth(1, 86);
        table->setColumnWidth(2, 115);
        table->setColumnWidth(3, 137);
        processColumn->addWidget(table, 1);
        connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
            filter->setFilterFixedString(query.trimmed());
            updateProcessCount();
        });

        timer.setInterval(refreshMs);
        connect(&timer, &QTimer::timeout, this, &Monitor::requestSample);
        connect(&watcher, &QFutureWatcher<Sample>::finished, this, [this] {
            if (timer.isActive() && requestedSession == session)
                render(watcher.result());
        });
    }

    void registerShortcut()
    {
        const QKeySequence key(Qt::META | Qt::Key_Escape);
        const QString actionId = QStringLiteral("toggle");
        const auto bindings = KGlobalAccel::globalShortcutsByKey(key);
        if (std::any_of(bindings.cbegin(), bindings.cend(), [&](const KGlobalShortcutInfo &binding) {
                return binding.componentUniqueName() != QCoreApplication::applicationName()
                    || binding.uniqueName() != actionId;
            })) {
            showShortcutError();
            return;
        }
        auto *action = new QAction("Mostrar u ocultar Oryon Monitor", this);
        action->setObjectName(actionId);
        action->setAutoRepeat(false);
        connect(action, &QAction::triggered, this, &Monitor::Toggle);
        auto *accel = KGlobalAccel::self();
        accel->setDefaultShortcut(action, {key});
        if (!accel->setShortcut(action, {key}, KGlobalAccel::NoAutoloading))
            showShortcutError();
    }

public slots:
    Q_SCRIPTABLE void Show()
    {
        if (isMinimized())
            setWindowState(windowState() & ~Qt::WindowMinimized);
        show();
        if (windowHandle())
            KWindowSystem::activateWindow(windowHandle());
    }

    Q_SCRIPTABLE void Hide() { hide(); }

    Q_SCRIPTABLE void Toggle()
    {
        if (Visible())
            Hide();
        else
            Show();
    }

    Q_SCRIPTABLE bool Visible() const { return isVisible() && !isMinimized(); }

    Q_SCRIPTABLE QVariantMap Status() const
    {
        auto status = readings;
        status.insert("visible", Visible());
        status.insert("active", isActiveWindow());
        status.insert("sampling", timer.isActive());
        return status;
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (!isMinimized())
            resume();
    }

    void hideEvent(QHideEvent *event) override
    {
        pause();
        QWidget::hideEvent(event);
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::WindowStateChange && isVisible()) {
            if (isMinimized())
                pause();
            else
                resume();
        }
        QWidget::changeEvent(event);
    }

private:
    void showShortcutError()
    {
        shortcut->setText("Meta + Esc ocupado");
        qWarning("No se pudo registrar Meta+Esc en KDE.");
    }

    void resume()
    {
        if (timer.isActive())
            return;
        ++session;
        telemetry = std::make_shared<Telemetry>();
        for (auto *trace : {cpu.trace, gpu.trace, ram.trace})
            trace->clear();
        live->setText("●  MIDIENDO");
        timer.start();
        requestSample();
    }

    void pause()
    {
        timer.stop();
        telemetry.reset();
    }

    void requestSample()
    {
        if (!telemetry || watcher.isRunning())
            return;
        requestedSession = session;
        watcher.setFuture(QtConcurrent::run([source = telemetry] { return source->sample(); }));
    }

    void updateProcessCount()
    {
        const int shown = filter->rowCount();
        const int total = processes->rowCount();
        count->setText(shown == total ? spanishLocale.toString(total)
                                     : QStringLiteral("%1 / %2").arg(spanishLocale.toString(shown), spanishLocale.toString(total)));
    }

    void render(Sample state)
    {
        live->setText(std::isfinite(state.cpu) ? "●  EN DIRECTO" : "●  MIDIENDO");
        cpu.value->setText(formatMetric(state.cpu, 0, " %"));
        cpu.summary->setText("Pico actual " + formatMetric(state.peakMhz, 0, " MHz") + "  ·  " + QString::number(state.cores.size()) + " núcleos");
        const QString turbo = !state.turboKnown ? "Turbo —" : state.turbo ? "Turbo activo" : "Turbo desactivado";
        cpu.footer->setText(turbo + "  ·  " + availableText(state.governor));
        cpu.trace->append(state.cpu);

        gpu.value->setText(formatMetric(state.gpuMhz, 0, " MHz"));
        gpu.summary->setText("Uso estimado " + formatMetric(state.gpu, 0, " %"));
        gpu.footer->setText(state.gpuMinMhz == state.gpuMaxMhz
                                ? "Límite fijado a " + formatMetric(state.gpuMaxMhz, 0, " MHz")
                                : formatMetric(state.gpuMinMhz) + " – " + formatMetric(state.gpuMaxMhz, 0, " MHz"));
        gpu.footer->setToolTip(availableText(state.gpuGovernor));
        gpu.trace->append(state.gpu);

        ram.value->setText(formatMetric(state.ramGiB, 1, " GiB"));
        const double ramLoad = state.ramTotalGiB > 0 ? state.ramGiB / state.ramTotalGiB * 100 : unavailable;
        ram.summary->setText("De " + formatMetric(state.ramTotalGiB, 1, " GiB") + "  ·  " + formatMetric(ramLoad, 0, " %"));
        ram.footer->setText("Swap " + formatMetric(state.swapGiB, 1) + " / " + formatMetric(state.swapTotalGiB, 1, " GiB"));
        ram.trace->append(ramLoad);

        for (qsizetype i = 0; i < cores.size(); ++i) {
            const auto &tile = cores[i];
            const auto core = std::find_if(state.cores.cbegin(), state.cores.cend(), [i](const Core &item) { return item.id == i; });
            const Core value = core != state.cores.cend() ? *core : Core{int(i), -1, unavailable, unavailable};
            tile.frequency->setText(formatMetric(value.mhz, 0, " MHz"));
            tile.load->setText(formatMetric(value.load, 0, " %"));
            tile.bar->updateLoad(value.load);
            tile.frequency->setToolTip(value.policy >= 0 ? QStringLiteral("Política cpufreq %1").arg(value.policy) : QString{});
        }
        vitals[0]->setText("CPU " + formatMetric(state.cpuTemp, 0, " °C") + "   GPU " + formatMetric(state.gpuTemp, 0, " °C"));
        const QString source = state.acKnown ? (state.onAc ? "CA" : "Batería") : "—";
        vitals[1]->setText(formatMetric(state.battery, 0, " %") + "  ·  " + source + "  ·  " + formatMetric(state.watts, 1, " W"));
        vitals[1]->setToolTip("Potencia medida en la batería. En CA, indica la potencia de carga.");
        vitals[2]->setText(formatMetric(state.diskGiB, 1) + " / " + formatMetric(state.diskTotalGiB, 1, " GiB"));
        vitals[3]->setText(speed(state.readSpeed) + " / " + speed(state.writeSpeed));
        vitals[4]->setText("↓ " + speed(state.rxSpeed) + "   ↑ " + speed(state.txSpeed));
        vitals[4]->setToolTip(availableText(state.network));

        readings = {
            {"cpu_percent", state.cpu}, {"cpu_max_current_mhz", state.peakMhz}, {"cpu_celsius", state.cpuTemp},
            {"cores", state.cores.size()}, {"boost", state.turbo}, {"gpu_mhz", state.gpuMhz}, {"gpu_estimated_percent", state.gpu},
            {"gpu_celsius", state.gpuTemp}, {"ram_gib", state.ramGiB}, {"battery_percent", state.battery}, {"battery_watts", state.watts},
            {"disk_total_gib", state.diskTotalGiB}, {"network", state.network}, {"processes", state.processes.size()}
        };
        processes->setRows(std::move(state.processes));
        updateProcessCount();
    }

    MetricCard cpu, gpu, ram;
    QList<CoreTile> cores;
    QList<QLabel *> vitals;
    QLabel *live, *shortcut, *count;
    Processes *processes;
    ProcessFilter *filter;
    QTimer timer;
    QFutureWatcher<Sample> watcher;
    std::shared_ptr<Telemetry> telemetry;
    quint64 session = 0, requestedSession = 0;
    QVariantMap readings;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("oryon-monitor");
    QApplication::setApplicationDisplayName("Oryon Monitor");
    QApplication::setDesktopFileName("bookedge-oryon-monitor");
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(logo(96)));
    QApplication::setFont(QFont("Adwaita Sans", 10));
    QApplication::setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#101117"));
    palette.setColor(QPalette::Text, QColor("#f4f3fa"));
    palette.setColor(QPalette::Base, QColor("#191923"));
    palette.setColor(QPalette::WindowText, QColor("#f4f3fa"));
    palette.setColor(QPalette::ButtonText, QColor("#f4f3fa"));
    palette.setColor(QPalette::Button, QColor("#24202e"));
    palette.setColor(QPalette::ToolTipBase, QColor("#2b2233"));
    palette.setColor(QPalette::ToolTipText, QColor("#f9f1fa"));
    QApplication::setPalette(palette);

    QCommandLineParser parser;
    parser.setApplicationDescription("Monitor para Snapdragon en Linux");
    parser.addHelpOption();
    const QCommandLineOption background("background", "Iniciar con la ventana oculta.");
    const QCommandLineOption preview("snapshot", "Guardar una captura de la ventana y salir.", "png");
    const QCommandLineOption exportIcon("export-icon", "Guardar el icono de la aplicación y salir.", "png");
    parser.addOptions({background, preview, exportIcon});
    parser.process(app);
    if (parser.isSet(exportIcon))
        return logo(192).save(parser.value(exportIcon)) ? 0 : 1;

    auto bus = QDBusConnection::sessionBus();
    if (!parser.isSet(preview) && !bus.registerService(serviceName)) {
        if (bus.interface() && QDBusInterface(serviceName, objectPath, serviceName, bus).isValid()) {
            if (!parser.isSet(background))
                QDBusInterface(serviceName, objectPath, serviceName, bus).call("Show");
            return 0;
        }
        qCritical("No se pudo registrar Oryon Monitor en el bus de sesión.");
        return 1;
    }

    Monitor monitor;
    if (parser.isSet(preview)) {
        monitor.show();
        QTimer::singleShot(18000, &monitor, [&] {
            app.exit(monitor.grab().save(parser.value(preview)) ? 0 : 1);
        });
    } else {
        if (!bus.registerObject(objectPath, &monitor, QDBusConnection::ExportScriptableSlots)) {
            qCritical("No se pudo publicar el panel en el bus de sesión.");
            return 1;
        }
        monitor.registerShortcut();
        if (!parser.isSet(background))
            monitor.Show();
    }
    return app.exec();
}

#include "main.moc"
