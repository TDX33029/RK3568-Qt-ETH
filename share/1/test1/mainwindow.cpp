#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QStatusBar>
#include <QPushButton>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QtMath>

// ---- Color helpers matching original fb_linux_main.c ----

QColor MainWindow::truthColor(int target) {
    static const QColor colors[3] = {
        QColor(0x8D, 0xBB, 0xEA),
        QColor(0x9B, 0xD6, 0xA8),
        QColor(0xF0, 0xC2, 0x7A)
    };
    return colors[target % 3];
}

QColor MainWindow::estColor(int target) {
    static const QColor colors[3] = {
        QColor(0x2A, 0x75, 0xBB),
        QColor(0x2E, 0x8B, 0x57),
        QColor(0xC4, 0x4E, 0x35)
    };
    return colors[target % 3];
}

void MainWindow::projectIso(const double state[6], double &px, double &py) {
    const double iso_cos = 0.8660254037844386; // cos(30°)
    const double iso_sin = 0.5;                // sin(30°)
    px = (state[0] - state[1]) * iso_cos;
    py = state[2] - (state[0] + state[1]) * iso_sin;
}

// ---- Constructor / Destructor ----

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_hasResult(false)
{
    ui->setupUi(this);

    // Create menu bar in code to avoid Qt 6 QAction header issues
    QMenuBar *mb = menuBar();

    QMenu *fileMenu = mb->addMenu(tr("文件(&F)"));
    QAction *actExit = fileMenu->addAction(tr("退出"));
    actExit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    connect(actExit, &QAction::triggered, this, &QMainWindow::close);

    QMenu *helpMenu = mb->addMenu(tr("帮助(&H)"));
    QAction *actAbout = helpMenu->addAction(tr("关于"));
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("关于"),
            tr("3D PS Tracker UI\n\n"
               "基于 Zynq PS 端 EKF 三维定位跟踪算法\n"
               "原 framebuffer 界面移植到 Qt 版本\n"
               "基于 Qt %1 + Qt Charts").arg(qVersion()));
    });

    // Initialize chart views and chart objects
    m_viewXY  = nullptr; m_chartXY  = nullptr;
    m_viewXZ  = nullptr; m_chartXZ  = nullptr;
    m_view3D  = nullptr; m_chart3D  = nullptr;

    for (int i = 0; i < 3; ++i) {
        m_truthXY[i] = m_estXY[i] = nullptr;
        m_truthXZ[i] = m_estXZ[i] = nullptr;
        m_truth3D[i] = m_est3D[i] = nullptr;
    }

    m_axisXY_X = m_axisXY_Y = nullptr;
    m_axisXZ_X = m_axisXZ_Y = nullptr;
    m_axis3D_X = m_axis3D_Y = nullptr;

    initChart(m_viewXY,  m_chartXY,  m_truthXY,  m_estXY,  m_axisXY_X,  m_axisXY_Y,  tr("XY View"));
    initChart(m_viewXZ,  m_chartXZ,  m_truthXZ,  m_estXZ,  m_axisXZ_X,  m_axisXZ_Y,  tr("XZ View"));
    initChart(m_view3D,  m_chart3D,  m_truth3D,  m_est3D,  m_axis3D_X,  m_axis3D_Y,  tr("3D View"));

    // Add chart views to the right panel
    ui->chartsLayout->addWidget(m_viewXY);
    ui->chartsLayout->addWidget(m_viewXZ);
    ui->chartsLayout->addWidget(m_view3D);

    // Connect buttons
    connect(ui->btnRun,   &QPushButton::clicked, this, &MainWindow::onRun);
    connect(ui->btnReset, &QPushButton::clicked, this, &MainWindow::onReset);

    // Populate combos
    ui->comboScene->addItems({tr("Straight"), tr("Climb"), tr("Turn")});
    ui->comboModality->addItems({tr("TDOA"), tr("TOA"), tr("AOA"), tr("RSS")});
    ui->comboTargetMode->addItems({tr("Single"), tr("Multi3")});

    statusBar()->showMessage(tr("就绪 — 点击「运行仿真」开始"));
}

MainWindow::~MainWindow()
{
    if (m_hasResult) {
        tracker_free_result(&m_result);
    }
    delete ui;
}

// ---- Chart initialization ----

void MainWindow::initChart(QChartView *&view, QChart *&chart,
                           QLineSeries *truthSeries[3], QLineSeries *estSeries[3],
                           QValueAxis *&axisX, QValueAxis *&axisY,
                           const QString &title)
{
    chart = new QChart();
    chart->setTitle(title);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->setMargins(QMargins(2, 2, 2, 2));

    for (int t = 0; t < 3; ++t) {
        truthSeries[t] = new QLineSeries();
        truthSeries[t]->setName(QString("T%1 Truth").arg(t + 1));
        truthSeries[t]->setPen(QPen(truthColor(t), 1.5, Qt::DashLine));
        chart->addSeries(truthSeries[t]);

        estSeries[t] = new QLineSeries();
        estSeries[t]->setName(QString("T%1 Est").arg(t + 1));
        estSeries[t]->setPen(QPen(estColor(t), 1.5));
        chart->addSeries(estSeries[t]);
    }

    axisX = new QValueAxis();
    axisX->setTitleText("X");
    axisX->setLabelFormat("%.1f");
    axisX->setGridLineVisible(true);
    chart->addAxis(axisX, Qt::AlignBottom);

    axisY = new QValueAxis();
    axisY->setTitleText("Y");
    axisY->setLabelFormat("%.1f");
    axisY->setGridLineVisible(true);
    chart->addAxis(axisY, Qt::AlignLeft);

    for (int t = 0; t < 3; ++t) {
        truthSeries[t]->attachAxis(axisX);
        truthSeries[t]->attachAxis(axisY);
        estSeries[t]->attachAxis(axisX);
        estSeries[t]->attachAxis(axisY);
    }

    view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
}

// ---- Slots ----

void MainWindow::onRun()
{
    clearResults();

    TrackerSimOptions opts;
    tracker_sim_default_options(&opts);

    opts.scene = static_cast<DemoScene>(ui->comboScene->currentIndex());
    opts.modality = static_cast<TrackerModality>(ui->comboModality->currentIndex());
    opts.target_mode = static_cast<TrackerTargetMode>(ui->comboTargetMode->currentIndex());
    opts.steps = static_cast<size_t>(ui->spinSteps->value());
    opts.seed = static_cast<unsigned int>(ui->spinSeed->value());
    opts.dt = ui->spinDt->value();

    statusBar()->showMessage(tr("仿真运行中..."));

    int status = tracker_run_simulation(&opts, &m_result);
    if (status != 0) {
        statusBar()->showMessage(tr("仿真失败 (错误码 %1)").arg(status));
        return;
    }
    m_hasResult = true;

    updatePlots();
    displayResults();
    statusBar()->showMessage(tr("仿真完成 — %1 步, %2 目标, %3 测量量")
                                 .arg(m_result.steps)
                                 .arg(m_result.target_count)
                                 .arg(tracker_modality_name(m_result.modality)));
}

void MainWindow::onReset()
{
    clearResults();
    ui->comboScene->setCurrentIndex(0);
    ui->comboModality->setCurrentIndex(0);
    ui->comboTargetMode->setCurrentIndex(0);
    ui->spinSteps->setValue(80);
    ui->spinSeed->setValue(1);
    ui->spinDt->setValue(0.1);

    ui->labelPosRmse->setText("—");
    ui->labelVelRmse->setText("—");
    ui->labelElapsed->setText("—");
    ui->labelStepTime->setText("—");
    ui->labelTargetCount->setText("—");
    ui->labelMeasDim->setText("—");

    statusBar()->showMessage(tr("参数已重置"));
}

void MainWindow::clearResults()
{
    if (m_hasResult) {
        tracker_free_result(&m_result);
        m_hasResult = false;
    }

    for (int t = 0; t < 3; ++t) {
        if (m_truthXY[t]) m_truthXY[t]->clear();
        if (m_estXY[t]) m_estXY[t]->clear();
        if (m_truthXZ[t]) m_truthXZ[t]->clear();
        if (m_estXZ[t]) m_estXZ[t]->clear();
        if (m_truth3D[t]) m_truth3D[t]->clear();
        if (m_est3D[t]) m_est3D[t]->clear();
    }
}

void MainWindow::updatePlots()
{
    size_t nTargets = m_result.target_count;

    double xyMinX = 1e30, xyMaxX = -1e30, xyMinY = 1e30, xyMaxY = -1e30;
    double xzMinX = 1e30, xzMaxX = -1e30, xzMinY = 1e30, xzMaxY = -1e30;
    double d3MinX = 1e30, d3MaxX = -1e30, d3MinY = 1e30, d3MaxY = -1e30;

    for (size_t t = 0; t < nTargets; ++t) {
        for (size_t k = 0; k < m_result.steps; ++k) {
            const double *truth = tracker_result_truth_at(&m_result, t, k);
            const double *est   = tracker_result_estimate_at(&m_result, t, k);
            if (!truth || !est) continue;

            // XY: x vs y
            m_truthXY[t]->append(truth[0], truth[1]);
            m_estXY[t]->append(est[0], est[1]);
            if (truth[0] < xyMinX) xyMinX = truth[0];
            if (truth[0] > xyMaxX) xyMaxX = truth[0];
            if (truth[1] < xyMinY) xyMinY = truth[1];
            if (truth[1] > xyMaxY) xyMaxY = truth[1];
            if (est[0] < xyMinX) xyMinX = est[0];
            if (est[0] > xyMaxX) xyMaxX = est[0];
            if (est[1] < xyMinY) xyMinY = est[1];
            if (est[1] > xyMaxY) xyMaxY = est[1];

            // XZ: x vs z
            m_truthXZ[t]->append(truth[0], truth[2]);
            m_estXZ[t]->append(est[0], est[2]);
            if (truth[2] < xzMinY) xzMinY = truth[2];
            if (truth[2] > xzMaxY) xzMaxY = truth[2];
            if (est[2] < xzMinY) xzMinY = est[2];
            if (est[2] > xzMaxY) xzMaxY = est[2];

            // 3D isometric
            double tpx, tpy, epx, epy;
            projectIso(truth, tpx, tpy);
            projectIso(est,   epx, epy);
            m_truth3D[t]->append(tpx, tpy);
            m_est3D[t]->append(epx, epy);
            if (tpx < d3MinX) d3MinX = tpx;
            if (tpx > d3MaxX) d3MaxX = tpx;
            if (tpy < d3MinY) d3MinY = tpy;
            if (tpy > d3MaxY) d3MaxY = tpy;
            if (epx < d3MinX) d3MinX = epx;
            if (epx > d3MaxX) d3MaxX = epx;
            if (epy < d3MinY) d3MinY = epy;
            if (epy > d3MaxY) d3MaxY = epy;
        }
    }

    xzMinX = xyMinX; xzMaxX = xyMaxX;

    auto applyRange = [](QValueAxis *ax, QValueAxis *ay,
                         double minX, double maxX, double minY, double maxY) {
        double padX = (maxX - minX) * 0.1; if (padX < 0.5) padX = 0.5;
        double padY = (maxY - minY) * 0.1; if (padY < 0.5) padY = 0.5;
        ax->setRange(minX - padX, maxX + padX);
        ay->setRange(minY - padY, maxY + padY);
    };

    applyRange(m_axisXY_X, m_axisXY_Y, xyMinX, xyMaxX, xyMinY, xyMaxY);
    applyRange(m_axisXZ_X, m_axisXZ_Y, xzMinX, xzMaxX, xzMinY, xzMaxY);
    applyRange(m_axis3D_X, m_axis3D_Y, d3MinX, d3MaxX, d3MinY, d3MaxY);

    for (size_t t = nTargets; t < 3; ++t) {
        m_truthXY[t]->setVisible(false);
        m_estXY[t]->setVisible(false);
        m_truthXZ[t]->setVisible(false);
        m_estXZ[t]->setVisible(false);
        m_truth3D[t]->setVisible(false);
        m_est3D[t]->setVisible(false);
    }
}

void MainWindow::displayResults()
{
    ui->labelPosRmse->setText(QString::number(m_result.pos_rmse, 'f', 4) + " m");
    ui->labelVelRmse->setText(QString::number(m_result.vel_rmse, 'f', 4) + " m/s");
    ui->labelElapsed->setText(QString::number(m_result.elapsed_ms, 'f', 3) + " ms");
    ui->labelStepTime->setText(QString::number(m_result.avg_step_ms, 'f', 3) + " ms");
    ui->labelTargetCount->setText(QString::number(m_result.target_count));
    ui->labelMeasDim->setText(QString::number(m_result.measurement_dim));
}
