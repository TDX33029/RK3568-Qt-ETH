#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QToolButton>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PS Tracker UI");
    resize(1000, 700);
    setMinimumSize(800, 600);

    m_ctrl = new SimulationController(this);

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    setupMenuPage();
    setupResultPage();

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    updateMenuDimLabel();

    connect(m_ctrl, &SimulationController::configChanged, this, &MainWindow::updateMenuDimLabel);
    connect(m_ctrl, &SimulationController::resultReady, this, &MainWindow::switchToResult);
}

MainWindow::~MainWindow() {}

// ── Menu Page ──

void MainWindow::setupMenuPage() {
    m_menuPage = new QWidget;
    m_stack->addWidget(m_menuPage);

    auto *outer = new QVBoxLayout(m_menuPage);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    auto *header = new QFrame;
    header->setStyleSheet("background:#1F4E5F;");
    header->setFixedHeight(54);
    auto *hl = new QHBoxLayout(header);
    auto *title = new QLabel("PS TRACKER UI");
    title->setStyleSheet("color:#FFFFFF; font-size:18px; font-weight:bold;");
    hl->addWidget(title);
    hl->addStretch();

    m_dimLabel = new QLabel;
    m_dimLabel->setStyleSheet("color:#C0D0D8; font-size:13px;");
    hl->addWidget(m_dimLabel);
    outer->addWidget(header);

    // Help text
    auto *help = new QLabel("  Select parameters and run simulation");
    help->setStyleSheet("color:#30424E; font-size:12px; padding:10px 24px;");
    outer->addWidget(help);

    // Config panel
    auto *panel = new QWidget;
    auto *playout = new QVBoxLayout(panel);
    playout->setContentsMargins(60, 0, 60, 0);
    playout->setSpacing(8);

    auto makeRow = [&](const QString &label, QWidget *&valWidget, QWidget *&prevBtn, QWidget *&nextBtn) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);

        auto *lbl = new QLabel(label);
        lbl->setFixedWidth(100);
        lbl->setStyleSheet("background:#D9E6EA; border-radius:4px; font-weight:bold; font-size:13px; color:#1B2C34; padding:6px 10px;");
        lbl->setAlignment(Qt::AlignCenter);
        row->addWidget(lbl);

        auto *prev = new QToolButton;
        prev->setText("<");
        prev->setFixedSize(32, 36);
        prev->setStyleSheet("QToolButton{background:#D9E6EA; border-radius:4px; font-weight:bold;} QToolButton:hover{background:#C8D4DA;}");
        row->addWidget(prev);
        prevBtn = prev;

        valWidget->setStyleSheet("background:#FFFFFF; border:1px solid #B0BEC5; border-radius:4px; font-size:13px; font-weight:bold; color:#1B2C34; padding:0 10px;");
        valWidget->setFixedHeight(36);
        valWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(valWidget, 1);

        auto *next = new QToolButton;
        next->setText(">");
        next->setFixedSize(32, 36);
        next->setStyleSheet("QToolButton{background:#D9E6EA; border-radius:4px; font-weight:bold;} QToolButton:hover{background:#C8D4DA;}");
        row->addWidget(next);
        nextBtn = next;

        playout->addLayout(row);
    };

    // SCENE
    m_sceneCombo = new QComboBox;
    m_sceneCombo->addItems({"Straight", "Climb", "Turn"});
    {
        QWidget *vw = m_sceneCombo, *pb = nullptr, *nb = nullptr;
        makeRow("SCENE", vw, pb, nb);
        connect(static_cast<QToolButton*>(pb), &QToolButton::clicked, this, [this]{ m_sceneCombo->setCurrentIndex((m_sceneCombo->currentIndex()+2)%3); });
        connect(static_cast<QToolButton*>(nb), &QToolButton::clicked, this, [this]{ m_sceneCombo->setCurrentIndex((m_sceneCombo->currentIndex()+1)%3); });
        connect(m_sceneCombo, &QComboBox::currentIndexChanged, m_ctrl, [this](int i){ m_ctrl->setSceneIndex(i); });
    }

    // TARGETS
    m_targetCombo = new QComboBox;
    m_targetCombo->addItems({"Single", "Multi-3"});
    {
        QWidget *vw = m_targetCombo, *pb = nullptr, *nb = nullptr;
        makeRow("TARGETS", vw, pb, nb);
        connect(static_cast<QToolButton*>(pb), &QToolButton::clicked, this, [this]{ m_targetCombo->setCurrentIndex((m_targetCombo->currentIndex()+1)%2); });
        connect(static_cast<QToolButton*>(nb), &QToolButton::clicked, this, [this]{ m_targetCombo->setCurrentIndex((m_targetCombo->currentIndex()+1)%2); });
        connect(m_targetCombo, &QComboBox::currentIndexChanged, m_ctrl, [this](int i){ m_ctrl->setTargetMode(i); });
    }

    // MODALITY
    m_modalityCombo = new QComboBox;
    m_modalityCombo->addItems({"TDOA", "TOA", "AOA", "RSS"});
    {
        QWidget *vw = m_modalityCombo, *pb = nullptr, *nb = nullptr;
        makeRow("MODALITY", vw, pb, nb);
        connect(static_cast<QToolButton*>(pb), &QToolButton::clicked, this, [this]{ m_modalityCombo->setCurrentIndex((m_modalityCombo->currentIndex()+3)%4); });
        connect(static_cast<QToolButton*>(nb), &QToolButton::clicked, this, [this]{ m_modalityCombo->setCurrentIndex((m_modalityCombo->currentIndex()+1)%4); });
        connect(m_modalityCombo, &QComboBox::currentIndexChanged, m_ctrl, [this](int i){ m_ctrl->setModalityIndex(i); });
    }

    // STEPS
    m_stepsSpin = new QSpinBox;
    m_stepsSpin->setRange(20, 10000);
    m_stepsSpin->setValue(80);
    m_stepsSpin->setSingleStep(10);
    m_stepsSpin->setStyleSheet("QSpinBox{background:#FFFFFF; border:1px solid #B0BEC5; border-radius:4px; font-size:13px; font-weight:bold; color:#1B2C34; padding:0 10px;}");
    m_stepsSpin->setFixedHeight(36);
    {
        QWidget *vw = m_stepsSpin, *pb = nullptr, *nb = nullptr;
        makeRow("STEPS", vw, pb, nb);
        connect(static_cast<QToolButton*>(pb), &QToolButton::clicked, this, [this]{ m_stepsSpin->setValue(m_stepsSpin->value() - m_stepsSpin->singleStep()); });
        connect(static_cast<QToolButton*>(nb), &QToolButton::clicked, this, [this]{ m_stepsSpin->setValue(m_stepsSpin->value() + m_stepsSpin->singleStep()); });
        connect(m_stepsSpin, &QSpinBox::valueChanged, m_ctrl, [this](int v){ m_ctrl->setSteps(v); });
    }

    // SEED
    m_seedSpin = new QSpinBox;
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue(42);
    m_seedSpin->setStyleSheet("QSpinBox{background:#FFFFFF; border:1px solid #B0BEC5; border-radius:4px; font-size:13px; font-weight:bold; color:#1B2C34; padding:0 10px;}");
    m_seedSpin->setFixedHeight(36);
    {
        QWidget *vw = m_seedSpin, *pb = nullptr, *nb = nullptr;
        makeRow("SEED", vw, pb, nb);
        connect(static_cast<QToolButton*>(pb), &QToolButton::clicked, this, [this]{ m_seedSpin->setValue(m_seedSpin->value() - 1); });
        connect(static_cast<QToolButton*>(nb), &QToolButton::clicked, this, [this]{ m_seedSpin->setValue(m_seedSpin->value() + 1); });
        connect(m_seedSpin, &QSpinBox::valueChanged, m_ctrl, [this](int v){ m_ctrl->setSeed(v); });
    }

    playout->addStretch();
    outer->addWidget(panel, 1);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 30);
    btnRow->addStretch();

    auto *runBtn = new QPushButton("RUN");
    runBtn->setFixedSize(120, 44);
    runBtn->setStyleSheet("QPushButton{background:#2E8B57; color:#FFFFFF; border-radius:6px; font-size:15px; font-weight:bold;} QPushButton:hover{background:#1A7A5A;}");
    btnRow->addWidget(runBtn);
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::onRunClicked);

    btnRow->addSpacing(20);

    auto *quitBtn = new QPushButton("QUIT");
    quitBtn->setFixedSize(120, 44);
    quitBtn->setStyleSheet("QPushButton{background:#C44E35; color:#FFFFFF; border-radius:6px; font-size:15px; font-weight:bold;} QPushButton:hover{background:#B0302A;}");
    btnRow->addWidget(quitBtn);
    connect(quitBtn, &QPushButton::clicked, this, &QWidget::close);

    btnRow->addStretch();
    outer->addLayout(btnRow);

    m_stack->setCurrentWidget(m_menuPage);
}

// ── Result Page ──

void MainWindow::setupResultPage() {
    m_resultPage = new QWidget;
    m_stack->addWidget(m_resultPage);

    auto *outer = new QVBoxLayout(m_resultPage);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    auto *header = new QFrame;
    header->setStyleSheet("background:#23403B;");
    header->setFixedHeight(54);
    auto *hl = new QHBoxLayout(header);

    auto *title = new QLabel("TRACK RESULT");
    title->setStyleSheet("color:#FFFFFF; font-size:18px; font-weight:bold;");
    hl->addWidget(title);
    hl->addStretch();

    auto *menuBtn = new QPushButton("MENU");
    menuBtn->setStyleSheet("QPushButton{background:#4A6A65; color:#FFFFFF; border-radius:4px; padding:6px 14px;} QPushButton:hover{background:#3A5A55;}");
    hl->addWidget(menuBtn);
    connect(menuBtn, &QPushButton::clicked, this, &MainWindow::onBackToMenu);

    auto *rerunBtn = new QPushButton("RERUN");
    rerunBtn->setStyleSheet("QPushButton{background:#2E8B57; color:#FFFFFF; border-radius:4px; padding:6px 14px;} QPushButton:hover{background:#1A7A5A;}");
    hl->addWidget(rerunBtn);
    connect(rerunBtn, &QPushButton::clicked, this, &MainWindow::onRerun);

    outer->addWidget(header);

    // Stats area
    auto *statsRow = new QHBoxLayout;
    statsRow->setContentsMargins(24, 10, 24, 4);
    statsRow->setSpacing(40);

    // Left stats
    auto *leftStats = new QVBoxLayout;
    leftStats->setSpacing(3);
    auto makeStat = [](const QString &txt) {
        auto *l = new QLabel(txt);
        l->setStyleSheet("color:#30424E; font-size:12px;");
        return l;
    };
    m_sceneLabel = makeStat("");
    m_targetLabel = makeStat("");
    m_modalityLabel = makeStat("");
    m_stepsLabel = makeStat("");
    m_dimResultLabel = makeStat("");
    leftStats->addWidget(m_sceneLabel);
    leftStats->addWidget(m_targetLabel);
    leftStats->addWidget(m_modalityLabel);
    leftStats->addWidget(m_stepsLabel);
    leftStats->addWidget(m_dimResultLabel);
    statsRow->addLayout(leftStats);

    // Per-target RMSE
    auto *targetStats = new QVBoxLayout;
    targetStats->setSpacing(2);
    for (int i = 0; i < 3; ++i) {
        m_targetRmseLabels[i] = new QLabel;
        m_targetRmseLabels[i]->setStyleSheet("color:#2A75BB; font-size:11px;");
        targetStats->addWidget(m_targetRmseLabels[i]);
        m_targetRmseLabels[i]->hide();
    }
    statsRow->addLayout(targetStats);

    // Right stats
    auto *rightStats = new QVBoxLayout;
    rightStats->setSpacing(3);
    m_posRmseLabel = makeStat("");
    m_velRmseLabel = makeStat("");
    m_timeLabel = makeStat("");
    m_stepTimeLabel = makeStat("");
    rightStats->addWidget(m_posRmseLabel);
    rightStats->addWidget(m_velRmseLabel);
    rightStats->addWidget(m_timeLabel);
    rightStats->addWidget(m_stepTimeLabel);
    statsRow->addLayout(rightStats);

    statsRow->addStretch();
    outer->addLayout(statsRow);

    // Legend
    m_legendWidget = new QWidget;
    auto *legLay = new QHBoxLayout(m_legendWidget);
    legLay->setContentsMargins(28, 2, 24, 2);
    legLay->setSpacing(20);
    // Filled in updateResultStats
    auto *legNote = new QLabel("LIGHT = Truth    DARK = Estimate");
    legNote->setStyleSheet("color:#30424E; font-size:10px;");
    legLay->addStretch();
    legLay->addWidget(legNote);
    outer->addWidget(m_legendWidget);

    // Charts
    auto *chartsRow = new QHBoxLayout;
    chartsRow->setContentsMargins(10, 4, 10, 10);
    chartsRow->setSpacing(10);

    auto *xyBox = new QGroupBox("XY VIEW");
    auto *xzBox = new QGroupBox("XZ VIEW");
    auto *tdBox = new QGroupBox("3D VIEW");
    for (auto *b : {xyBox, xzBox, tdBox}) {
        b->setStyleSheet("QGroupBox{font-weight:bold; color:#1B2C34; border:1px solid #6A7A8C; border-radius:4px; margin-top:10px; padding-top:14px;} QGroupBox::title{subcontrol-origin:margin; left:8px;}");
    }

    m_xyChart = new TrajectoryChart(TrajectoryChart::XY);
    m_xzChart = new TrajectoryChart(TrajectoryChart::XZ);
    m_3dChart = new TrajectoryChart(TrajectoryChart::Iso3D);
    m_xyChart->setController(m_ctrl);
    m_xzChart->setController(m_ctrl);
    m_3dChart->setController(m_ctrl);

    auto *xyl = new QVBoxLayout(xyBox); xyl->setContentsMargins(4,4,4,4); xyl->addWidget(m_xyChart);
    auto *xzl = new QVBoxLayout(xzBox); xzl->setContentsMargins(4,4,4,4); xzl->addWidget(m_xzChart);
    auto *tdl = new QVBoxLayout(tdBox); tdl->setContentsMargins(4,4,4,4); tdl->addWidget(m_3dChart);

    chartsRow->addWidget(xyBox, 1);
    chartsRow->addWidget(xzBox, 1);
    chartsRow->addWidget(tdBox, 1);
    outer->addLayout(chartsRow, 1);
}

// ── Slots ──

void MainWindow::onRunClicked() {
    m_ctrl->runSimulation();
}

void MainWindow::onBackToMenu() {
    switchToMenu();
}

void MainWindow::onRerun() {
    m_ctrl->runSimulation();
}

void MainWindow::switchToMenu() {
    m_stack->setCurrentWidget(m_menuPage);
}

void MainWindow::switchToResult() {
    updateResultStats();
    m_xyChart->refresh();
    m_xzChart->refresh();
    m_3dChart->refresh();
    m_stack->setCurrentWidget(m_resultPage);
}

void MainWindow::updateResultStats() {
    auto sm = m_ctrl->sceneNames();
    auto tm = m_ctrl->targetModeNames();
    auto mm = m_ctrl->modalityNames();

    m_sceneLabel->setText(QString("SCENE: %1").arg(sm[m_ctrl->sceneIndex()]));
    m_targetLabel->setText(QString("TARGETS: %1").arg(tm[m_ctrl->targetMode()]));
    m_modalityLabel->setText(QString("MODALITY: %1").arg(mm[m_ctrl->modalityIndex()]));
    m_stepsLabel->setText(QString("STEPS: %1").arg(m_ctrl->stepCount()));

    int tc = m_ctrl->targetCount();
    if (tc > 1)
        m_dimResultLabel->setText(QString("DIM %1 x %2").arg(m_ctrl->measurementDim()).arg(tc));
    else
        m_dimResultLabel->setText(QString("DIM %1").arg(m_ctrl->measurementDim()));

    if (tc > 1) {
        m_posRmseLabel->setText(QString("AVG POS RMSE: %1").arg(m_ctrl->posRmse(), 0, 'f', 4));
        m_velRmseLabel->setText(QString("AVG VEL RMSE: %1").arg(m_ctrl->velRmse(), 0, 'f', 4));
    } else {
        m_posRmseLabel->setText(QString("POS RMSE: %1").arg(m_ctrl->posRmse(), 0, 'f', 4));
        m_velRmseLabel->setText(QString("VEL RMSE: %1").arg(m_ctrl->velRmse(), 0, 'f', 4));
    }
    m_timeLabel->setText(QString("TOTAL: %1 ms").arg(m_ctrl->elapsedMs(), 0, 'f', 3));
    m_stepTimeLabel->setText(QString("STEP: %1 ms").arg(m_ctrl->avgStepMs(), 0, 'f', 3));

    // Per-target RMSE
    for (int i = 0; i < 3; ++i) {
        if (i < tc) {
            m_targetRmseLabels[i]->setText(QString("T%1 POS: %2  VEL: %3")
                .arg(i+1)
                .arg(m_ctrl->targetPosRmse(i), 0, 'f', 4)
                .arg(m_ctrl->targetVelRmse(i), 0, 'f', 4));
            m_targetRmseLabels[i]->setStyleSheet(QString("color:#%1; font-size:11px;")
                .arg(SimulationController::estimateColor(i) & 0xFFFFFF, 6, 16, QLatin1Char('0')));
            m_targetRmseLabels[i]->show();
        } else {
            m_targetRmseLabels[i]->hide();
        }
    }

    // Update legend
    auto *legLay = static_cast<QHBoxLayout *>(m_legendWidget->layout());
    // Remove old target legend items (keep stretch and note)
    while (legLay->count() > 2) {
        QLayoutItem *item = legLay->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }
    for (int i = 0; i < tc; ++i) {
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        auto *tcSwatch = new QFrame; tcSwatch->setFixedSize(16, 4);
        tcSwatch->setStyleSheet(QString("background:#%1; border:none;").arg(SimulationController::truthColor(i) & 0xFFFFFF, 6, 16, QLatin1Char('0')));
        row->addWidget(tcSwatch);
        auto *tcl = new QLabel(QString("T%1 truth").arg(i+1)); tcl->setStyleSheet("color:#30424E; font-size:10px;");
        row->addWidget(tcl);
        auto *ecSwatch = new QFrame; ecSwatch->setFixedSize(16, 4);
        ecSwatch->setStyleSheet(QString("background:#%1; border:none;").arg(SimulationController::estimateColor(i) & 0xFFFFFF, 6, 16, QLatin1Char('0')));
        row->addWidget(ecSwatch);
        auto *ecl = new QLabel("est"); ecl->setStyleSheet("color:#30424E; font-size:10px;");
        row->addWidget(ecl);
        legLay->insertLayout(legLay->count() - 2, row);
    }
}

QString MainWindow::dimLabelText() const {
    int tc = m_ctrl->targetMode() == 1 ? 3 : 1;
    int dim = m_ctrl->measurementDimEstimate();
    return tc > 1 ? QString("DIM %1 x %2").arg(dim).arg(tc) : QString("DIM %1").arg(dim);
}

void MainWindow::updateMenuDimLabel() {
    m_dimLabel->setText(dimLabelText());
}
