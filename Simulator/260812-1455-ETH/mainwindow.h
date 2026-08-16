#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>

#include "alg/tracker_app.h"
#include "tracker_plot.h"
#include "eth_reader.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void startLive();

private slots:
    void runSimulation();
    void goToMenu();
    void onModalityChanged();
    void onTargetModeChanged(int index);
    void onEthFrame(const Tracker3DMeasurement &meas, double dt_sec, quint16 seq);
    void onEthError(const QString &msg);
    void onAnchorsUpdated(const QByteArray &payload);
    void onLiveTick();

private:
    void buildMenuPage();
    void buildResultPage();
    void updateResultDisplay();
    void stopLive();

    QStackedWidget *m_stack;

    QWidget *m_menuPage;
    QComboBox *m_sceneCombo;
    QComboBox *m_targetsCombo;
    QCheckBox *m_chkTdoa, *m_chkToa, *m_chkAoa, *m_chkRss;
    QSpinBox *m_stepsSpin;
    QSpinBox *m_seedSpin;
    QLabel *m_dimLabel;
    QSpinBox *m_portSpin;
    QLabel *m_ipLabel;
    QPushButton *m_liveBtn;

    QWidget *m_resultPage;
    QLabel *m_rScene, *m_rTargets, *m_rModality, *m_rSteps, *m_rDim;
    QLabel *m_rPosRmse, *m_rVelRmse, *m_rElapsed, *m_rStepMs;
    QLabel *m_rTargetInfo;
    QLabel *m_rLink;
    TrackerPlot *m_xyPlot;

    TrackerSimOptions m_options;
    TrackerSimResult m_result;
    bool m_hasResult = false;

    EthReader *m_eth = nullptr;
    QTimer    *m_liveTimer = nullptr;
    bool       m_live = false;
    bool       m_liveDirty = false;
    quint16    m_liveSeq = 0;
    double     m_liveDt = 0.0;
    quint64    m_liveFrames = 0;
    QElapsedTimer m_linkTimer;
    qint64     m_lastFrameMs = -1;
};

#endif
