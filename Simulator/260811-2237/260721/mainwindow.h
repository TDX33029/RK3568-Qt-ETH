#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>

#include "tracker_app.h"
#include "tracker_plot.h"
#include "eth_reader.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void startLive();          /* public: 供 --auto-live 经 QTimer 调用 */

private slots:
    void runSimulation();
    void goToMenu();
    void onModalityChanged(int index);
    void onTargetModeChanged(int index);
    void onEthFrame(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void onEthError(const QString &msg);
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
    QLabel *m_rLink;                 /* 实时链路状态灯 (UI 线程) */
    TrackerPlot *m_xyPlot, *m_xzPlot, *m_plot3d;

    // Core data
    TrackerSimOptions m_options;
    TrackerSimResult m_result;
    bool m_hasResult = false;

    // 实时(ETH1)模式
    EthReader *m_eth = nullptr;
    QTimer    *m_liveTimer = nullptr;
    bool       m_live = false;
    bool       m_liveDirty = false;
    quint16    m_liveSeq = 0;
    double     m_liveDt = 0.0;
    quint64    m_liveFrames = 0;      /* 已收有效帧数 (滚动窗口不影响此计数) */
    QElapsedTimer m_linkTimer;        /* 链路状态计时 (startLive 时启动) */
    qint64     m_lastFrameMs = -1;    /* 最近收到帧的 elapsed() 时刻, -1=未收过 */
};

#endif
