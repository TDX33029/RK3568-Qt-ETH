#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include "tracker_bridge.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRun();
    void onReset();

private:
    void initChart(QChartView *&view, QChart *&chart,
                   QLineSeries *truthSeries[3], QLineSeries *estSeries[3],
                   QValueAxis *&axisX, QValueAxis *&axisY,
                   const QString &title);
    void updatePlots();
    void displayResults();
    void clearResults();

    static void projectIso(const double state[6], double &px, double &py);
    static QColor truthColor(int target);
    static QColor estColor(int target);

    Ui::MainWindow *ui;

    // Chart views for the 3 subplots
    QChartView *m_viewXY;
    QChartView *m_viewXZ;
    QChartView *m_view3D;

    QChart *m_chartXY;
    QChart *m_chartXZ;
    QChart *m_chart3D;

    // Series per chart per target: truth[target], est[target]
    QLineSeries *m_truthXY[3];
    QLineSeries *m_estXY[3];
    QLineSeries *m_truthXZ[3];
    QLineSeries *m_estXZ[3];
    QLineSeries *m_truth3D[3];
    QLineSeries *m_est3D[3];

    QValueAxis *m_axisXY_X;
    QValueAxis *m_axisXY_Y;
    QValueAxis *m_axisXZ_X;
    QValueAxis *m_axisXZ_Y;
    QValueAxis *m_axis3D_X;
    QValueAxis *m_axis3D_Y;

    TrackerSimResult m_result;
    bool m_hasResult;
};

#endif // MAINWINDOW_H