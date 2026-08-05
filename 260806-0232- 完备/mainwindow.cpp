#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QApplication>
#include <QMessageBox>
#include <QFrame>
#include <QFont>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PS Tracker UI");
    resize(1100, 720);

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    tracker_sim_default_options(&m_options);

    buildMenuPage();
    buildResultPage();
    m_stack->setCurrentWidget(m_menuPage);
}

MainWindow::~MainWindow() {
    if (m_hasResult) tracker_free_result(&m_result);
}

// ─── Menu page ────────────────────────────────────────

void MainWindow::buildMenuPage() {
    m_menuPage = new QWidget;
    auto *root = new QVBoxLayout(m_menuPage);
    root->setContentsMargins(0, 0, 0, 0);

    // Title bar
    auto *titleBar = new QWidget;
    titleBar->setFixedHeight(64);
    titleBar->setStyleSheet("background-color:#1F4E5F;");
    auto *titleLay = new QHBoxLayout(titleBar);
    auto *titleLabel = new QLabel("PS TRACKER UI");
    titleLabel->setStyleSheet("color:white; font-size:24px; font-weight:bold; padding-left:20px;");
    titleLay->addWidget(titleLabel);
    titleLay->addStretch();
    root->addWidget(titleBar);

    // Hint
    auto *hint = new QLabel("Configure simulation parameters and click RUN");
    hint->setStyleSheet("color:#30424E; font-size:13px; padding:8px 20px 0 20px;");
    root->addWidget(hint);

    // Form area
    auto *formWidget = new QWidget;
    formWidget->setStyleSheet("padding:12px 20px;");
    auto *form = new QFormLayout(formWidget);
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setContentsMargins(40, 8, 40, 8);

    auto makeCombo = [&](const QStringList &items) -> QComboBox* {
        auto *cb = new QComboBox;
        cb->addItems(items);
        cb->setFixedWidth(200);
        cb->setStyleSheet("font-size:14px; padding:4px 8px;");
        return cb;
    };

    m_sceneCombo = makeCombo({"straight", "climb", "turn"});
    m_sceneCombo->setStyleSheet(m_sceneCombo->styleSheet() + "QComboBox{color:#1B2C34;}");
    form->addRow("Scene:", m_sceneCombo);

    m_targetsCombo = makeCombo({"single", "multi3"});
    form->addRow("Targets:", m_targetsCombo);

    m_modalityCombo = makeCombo({"TDOA", "TOA", "AOA", "RSS"});
    form->addRow("Modality:", m_modalityCombo);

    m_stepsSpin = new QSpinBox;
    m_stepsSpin->setRange(20, 500);
    m_stepsSpin->setValue((int)m_options.steps);
    m_stepsSpin->setFixedWidth(200);
    m_stepsSpin->setStyleSheet("font-size:14px; padding:4px 8px;");
    form->addRow("Steps:", m_stepsSpin);

    m_seedSpin = new QSpinBox;
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue((int)m_options.seed);
    m_seedSpin->setFixedWidth(200);
    m_seedSpin->setStyleSheet("font-size:14px; padding:4px 8px;");
    form->addRow("Seed:", m_seedSpin);

    root->addWidget(formWidget);

    // Dim label
    m_dimLabel = new QLabel;
    m_dimLabel->setStyleSheet("color:#30424E; font-size:14px; padding:0 60px;");
    root->addWidget(m_dimLabel);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(60, 8, 60, 16);

    auto *runBtn = new QPushButton("Run Simulation");
    runBtn->setStyleSheet(
        "QPushButton{background-color:#2A75BB; color:white; font-size:15px; font-weight:bold;"
        "padding:10px 32px; border-radius:6px; border:none;}"
        "QPushButton:hover{background-color:#3585CC;}"
        "QPushButton:pressed{background-color:#1E65AA;}"
    );
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::runSimulation);

    auto *quitBtn = new QPushButton("Quit");
    quitBtn->setStyleSheet(
        "QPushButton{background-color:#C44E35; color:white; font-size:15px; font-weight:bold;"
        "padding:10px 32px; border-radius:6px; border:none;}"
        "QPushButton:hover{background-color:#D45E45;}"
        "QPushButton:pressed{background-color:#B43E25;}"
    );
    connect(quitBtn, &QPushButton::clicked, qApp, &QApplication::quit);

    btnRow->addStretch();
    btnRow->addWidget(runBtn);
    btnRow->addSpacing(20);
    btnRow->addWidget(quitBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);
    root->addStretch();

    // Connections for dim label update
    connect(m_modalityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModalityChanged);
    connect(m_targetsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTargetModeChanged);

    onModalityChanged(0);

    m_stack->addWidget(m_menuPage);
}

// ─── Result page ──────────────────────────────────────

void MainWindow::buildResultPage() {
    m_resultPage = new QWidget;
    auto *root = new QVBoxLayout(m_resultPage);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Title bar with buttons
    auto *titleBar = new QWidget;
    titleBar->setFixedHeight(52);
    titleBar->setStyleSheet("background-color:#23403B;");
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(12, 0, 12, 0);

    auto *menuBtn = new QPushButton("Menu");
    menuBtn->setStyleSheet(
        "QPushButton{background:#3A6B63; color:white; font-size:13px; font-weight:bold;"
        "padding:6px 18px; border-radius:4px; border:none;}"
        "QPushButton:hover{background:#4A7B73;}"
    );
    connect(menuBtn, &QPushButton::clicked, this, &MainWindow::goToMenu);

    auto *rerunBtn = new QPushButton("Rerun");
    rerunBtn->setStyleSheet(
        "QPushButton{background:#3A6B63; color:white; font-size:13px; font-weight:bold;"
        "padding:6px 18px; border-radius:4px; border:none;}"
        "QPushButton:hover{background:#4A7B73;}"
    );
    connect(rerunBtn, &QPushButton::clicked, this, &MainWindow::runSimulation);

    auto *resTitle = new QLabel("TRACK RESULT");
    resTitle->setStyleSheet("color:white; font-size:18px; font-weight:bold; padding-left:12px;");

    titleLay->addWidget(menuBtn);
    titleLay->addSpacing(8);
    titleLay->addWidget(rerunBtn);
    titleLay->addSpacing(16);
    titleLay->addWidget(resTitle);
    titleLay->addStretch();
    root->addWidget(titleBar);

    // Info area
    auto *infoWidget = new QWidget;
    infoWidget->setStyleSheet("background-color:#F9F7F2;");
    auto *infoGrid = new QGridLayout(infoWidget);
    infoGrid->setContentsMargins(20, 8, 20, 4);
    infoGrid->setSpacing(4);

    auto makeInfo = [&](const QString &text) -> QLabel* {
        auto *l = new QLabel(text);
        l->setStyleSheet("color:#30424E; font-size:13px;");
        return l;
    };
    auto makeVal = [&](const QString &text) -> QLabel* {
        auto *l = new QLabel(text);
        l->setStyleSheet("color:#1B2C34; font-size:13px; font-weight:bold;");
        return l;
    };

    m_rScene = makeVal(""); m_rTargets = makeVal(""); m_rModality = makeVal("");
    m_rSteps = makeVal(""); m_rDim = makeVal("");
    m_rPosRmse = makeVal(""); m_rVelRmse = makeVal("");
    m_rElapsed = makeVal(""); m_rStepMs = makeVal("");
    m_rTargetInfo = new QLabel;
    m_rTargetInfo->setStyleSheet("color:#1B2C34; font-size:12px; padding:4px 0;");

    infoGrid->addWidget(makeInfo("Scene:"),      0, 0);
    infoGrid->addWidget(m_rScene,                0, 1);
    infoGrid->addWidget(makeInfo("Pos RMSE:"),   0, 2);
    infoGrid->addWidget(m_rPosRmse,              0, 3);
    infoGrid->addWidget(makeInfo("Targets:"),     1, 0);
    infoGrid->addWidget(m_rTargets,               1, 1);
    infoGrid->addWidget(makeInfo("Vel RMSE:"),   1, 2);
    infoGrid->addWidget(m_rVelRmse,               1, 3);
    infoGrid->addWidget(makeInfo("Modality:"),    2, 0);
    infoGrid->addWidget(m_rModality,              2, 1);
    infoGrid->addWidget(makeInfo("Total:"),       2, 2);
    infoGrid->addWidget(m_rElapsed,               2, 3);
    infoGrid->addWidget(makeInfo("Steps:"),       3, 0);
    infoGrid->addWidget(m_rSteps,                 3, 1);
    infoGrid->addWidget(makeInfo("Step/ms:"),    3, 2);
    infoGrid->addWidget(m_rStepMs,                3, 3);
    infoGrid->addWidget(m_rTargetInfo,            4, 0, 1, 4);

    infoGrid->setColumnStretch(0, 0);
    infoGrid->setColumnStretch(1, 1);
    infoGrid->setColumnStretch(2, 0);
    infoGrid->setColumnStretch(3, 1);

    root->addWidget(infoWidget);

    // Plot labels + plots
    auto *plotSection = new QWidget;
    auto *plotVBox = new QVBoxLayout(plotSection);
    plotVBox->setContentsMargins(12, 4, 12, 8);
    plotVBox->setSpacing(2);

    auto *plotLabelRow = new QHBoxLayout;
    auto makePlotLabel = [&](const QString &text) -> QLabel* {
        auto *l = new QLabel(text);
        l->setStyleSheet("color:#1B2C34; font-size:14px; font-weight:bold;");
        l->setAlignment(Qt::AlignCenter);
        return l;
    };
    plotLabelRow->addWidget(makePlotLabel("XY VIEW"));
    plotLabelRow->addSpacing(12);
    plotLabelRow->addWidget(makePlotLabel("XZ VIEW"));
    plotLabelRow->addSpacing(12);
    plotLabelRow->addWidget(makePlotLabel("3D VIEW"));
    plotVBox->addLayout(plotLabelRow);

    auto *plotRow = new QHBoxLayout;
    m_xyPlot = new TrackerPlot;
    m_xyPlot->setViewMode(TrackerPlot::ViewXY);
    m_xzPlot = new TrackerPlot;
    m_xzPlot->setViewMode(TrackerPlot::ViewXZ);
    m_plot3d = new TrackerPlot;
    m_plot3d->setViewMode(TrackerPlot::View3D);

    plotRow->addWidget(m_xyPlot, 1);
    plotRow->addSpacing(12);
    plotRow->addWidget(m_xzPlot, 1);
    plotRow->addSpacing(12);
    plotRow->addWidget(m_plot3d, 1);
    plotVBox->addLayout(plotRow, 1);

    root->addWidget(plotSection, 1);
    m_stack->addWidget(m_resultPage);
}

// ─── Slots ────────────────────────────────────────────

void MainWindow::runSimulation() {
    // Gather options
    m_options.scene = (DemoScene)m_sceneCombo->currentIndex();
    m_options.target_mode = (TrackerTargetMode)m_targetsCombo->currentIndex();
    m_options.modality = (TrackerModality)m_modalityCombo->currentIndex();
    m_options.steps = (size_t)m_stepsSpin->value();
    m_options.seed = (unsigned int)m_seedSpin->value();

    if (m_hasResult) {
        tracker_free_result(&m_result);
        m_hasResult = false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    int status = tracker_run_simulation(&m_options, &m_result);
    if (status == 0) {
        m_hasResult = true;
        updateResultDisplay();
        m_stack->setCurrentWidget(m_resultPage);
    } else {
        QMessageBox::warning(this, "Simulation Error",
            QString("Simulation failed with status %1").arg(status));
    }

    QApplication::restoreOverrideCursor();
}

void MainWindow::updateResultDisplay() {
    m_rScene->setText(tracker_scene_name(m_options.scene));
    m_rTargets->setText(tracker_target_mode_name(m_result.target_mode));
    m_rModality->setText(tracker_modality_name(m_result.modality));
    m_rSteps->setText(QString::number(m_result.steps));
    m_rDim->setText(QString("Dim: %1").arg(m_result.measurement_dim));
    m_rPosRmse->setText(QString::number(m_result.pos_rmse, 'f', 4));
    m_rVelRmse->setText(QString::number(m_result.vel_rmse, 'f', 4));
    m_rElapsed->setText(QString::number(m_result.elapsed_ms, 'f', 3) + " ms");
    m_rStepMs->setText(QString::number(m_result.avg_step_ms, 'f', 3) + " ms");

    QString ti;
    for (size_t i = 0; i < m_result.target_count; ++i) {
        if (i > 0) ti += "  |  ";
        ti += QString("T%1 P%2 V%3")
            .arg(i + 1)
            .arg(m_result.target_pos_rmse[i], 0, 'f', 4)
            .arg(m_result.target_vel_rmse[i], 0, 'f', 4);
    }
    m_rTargetInfo->setText(ti);

    // Update plots
    m_xyPlot->setResult(&m_result);
    m_xzPlot->setResult(&m_result);
    m_plot3d->setResult(&m_result);
}

void MainWindow::goToMenu() {
    m_stack->setCurrentWidget(m_menuPage);
}

void MainWindow::onModalityChanged(int) {
    size_t dim = tracker_expected_measurement_dim_for_modality(
        (TrackerModality)m_modalityCombo->currentIndex());
    size_t ntargets = tracker_target_count_for_mode(
        (TrackerTargetMode)m_targetsCombo->currentIndex());

    if (ntargets > 1) {
        m_dimLabel->setText(QString("Measurement: %1 x %2 targets").arg(dim).arg(ntargets));
    } else {
        m_dimLabel->setText(QString("Measurement dimension: %1").arg(dim));
    }
}

void MainWindow::onTargetModeChanged(int) {
    onModalityChanged(0);
}
