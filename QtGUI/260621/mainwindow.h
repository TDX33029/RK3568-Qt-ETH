#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>

#include "simulation_controller.h"
#include "trajectory_chart.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRunClicked();
    void onBackToMenu();
    void onRerun();
    void updateMenuDimLabel();

private:
    void setupMenuPage();
    void setupResultPage();
    void switchToMenu();
    void switchToResult();
    void updateResultStats();
    QString dimLabelText() const;

    SimulationController *m_ctrl;

    QStackedWidget *m_stack;

    // Menu page widgets
    QWidget *m_menuPage;
    QComboBox *m_sceneCombo;
    QComboBox *m_targetCombo;
    QComboBox *m_modalityCombo;
    QSpinBox *m_stepsSpin;
    QSpinBox *m_seedSpin;
    QLabel *m_dimLabel;

    // Result page widgets
    QWidget *m_resultPage;
    TrajectoryChart *m_xyChart;
    TrajectoryChart *m_xzChart;
    TrajectoryChart *m_3dChart;
    QLabel *m_sceneLabel;
    QLabel *m_targetLabel;
    QLabel *m_modalityLabel;
    QLabel *m_stepsLabel;
    QLabel *m_dimResultLabel;
    QLabel *m_posRmseLabel;
    QLabel *m_velRmseLabel;
    QLabel *m_timeLabel;
    QLabel *m_stepTimeLabel;
    QLabel *m_targetRmseLabels[3];
    QWidget *m_legendWidget;

    // Status bar label
    QLabel *m_statusLabel;
};

#endif // MAINWINDOW_H
