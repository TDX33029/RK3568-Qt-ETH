#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

#include "tracker_app.h"
#include "tracker_plot.h"
#include "spi_reader.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void runSimulation();
    void startLive();
    void goToMenu();
    void onModalityChanged(int index);
    void onTargetModeChanged(int index);
    void onSpiFrame(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void onLiveTick();

private:
    void buildMenuPage();
    void buildResultPage();
    void updateResultDisplay();
    void stopLive();

    QStackedWidget *m_stack;

    // Menu page
    QWidget *m_menuPage;
    QComboBox *m_sceneCombo;
    QComboBox *m_targetsCombo;
    QComboBox *m_modalityCombo;
    QSpinBox *m_stepsSpin;
    QSpinBox *m_seedSpin;
    QLabel *m_dimLabel;
    QPushButton *m_liveBtn;

    // Result page
    QWidget *m_resultPage;
    QLabel *m_rScene, *m_rTargets, *m_rModality, *m_rSteps, *m_rDim;
    QLabel *m_rPosRmse, *m_rVelRmse, *m_rElapsed, *m_rStepMs;
    QLabel *m_rTargetInfo;
    TrackerPlot *m_xyPlot, *m_xzPlot, *m_plot3d;

    // Core data
    TrackerSimOptions m_options;
    TrackerSimResult m_result;
    bool m_hasResult = false;

    // 实时(SPI)模式
    SpiReader *m_spi = nullptr;
    QTimer    *m_liveTimer = nullptr;
    bool       m_live = false;
    bool       m_liveDirty = false;
    quint16    m_liveSeq = 0;
    double     m_liveDt = 0.0;
};

#endif
