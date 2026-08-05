#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>

#include "tracker_app.h"
#include "tracker_plot.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void runSimulation();
    void goToMenu();
    void onModalityChanged(int index);
    void onTargetModeChanged(int index);

private:
    void buildMenuPage();
    void buildResultPage();
    void updateResultDisplay();

    QStackedWidget *m_stack;

    // Menu page
    QWidget *m_menuPage;
    QComboBox *m_sceneCombo;
    QComboBox *m_targetsCombo;
    QComboBox *m_modalityCombo;
    QSpinBox *m_stepsSpin;
    QSpinBox *m_seedSpin;
    QLabel *m_dimLabel;

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
};

#endif
