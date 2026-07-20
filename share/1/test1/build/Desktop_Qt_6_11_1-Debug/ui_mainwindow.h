/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.11.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QHBoxLayout *horizontalLayout;
    QSplitter *splitter;
    QWidget *leftPanel;
    QVBoxLayout *leftLayout;
    QGroupBox *groupConfig;
    QFormLayout *formLayout;
    QLabel *labelScene;
    QComboBox *comboScene;
    QLabel *labelModality;
    QComboBox *comboModality;
    QLabel *labelTargetMode;
    QComboBox *comboTargetMode;
    QLabel *labelSteps;
    QSpinBox *spinSteps;
    QLabel *labelSeed;
    QSpinBox *spinSeed;
    QLabel *labelDt;
    QDoubleSpinBox *spinDt;
    QGroupBox *groupActions;
    QVBoxLayout *actionsLayout;
    QPushButton *btnRun;
    QPushButton *btnReset;
    QGroupBox *groupResults;
    QFormLayout *resultsLayout;
    QLabel *label_2;
    QLabel *labelTargetCount;
    QLabel *label_3;
    QLabel *labelMeasDim;
    QLabel *label_4;
    QLabel *labelPosRmse;
    QLabel *label_5;
    QLabel *labelVelRmse;
    QLabel *label_6;
    QLabel *labelElapsed;
    QLabel *label_7;
    QLabel *labelStepTime;
    QSpacerItem *verticalSpacer;
    QWidget *chartContainer;
    QVBoxLayout *chartsLayout;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(1200, 750);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        horizontalLayout = new QHBoxLayout(centralwidget);
        horizontalLayout->setObjectName("horizontalLayout");
        splitter = new QSplitter(centralwidget);
        splitter->setObjectName("splitter");
        splitter->setOrientation(Qt::Horizontal);
        leftPanel = new QWidget(splitter);
        leftPanel->setObjectName("leftPanel");
        leftPanel->setMinimumSize(QSize(280, 0));
        leftPanel->setMaximumSize(QSize(350, 16777215));
        leftLayout = new QVBoxLayout(leftPanel);
        leftLayout->setObjectName("leftLayout");
        groupConfig = new QGroupBox(leftPanel);
        groupConfig->setObjectName("groupConfig");
        formLayout = new QFormLayout(groupConfig);
        formLayout->setObjectName("formLayout");
        labelScene = new QLabel(groupConfig);
        labelScene->setObjectName("labelScene");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelScene);

        comboScene = new QComboBox(groupConfig);
        comboScene->setObjectName("comboScene");

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, comboScene);

        labelModality = new QLabel(groupConfig);
        labelModality->setObjectName("labelModality");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelModality);

        comboModality = new QComboBox(groupConfig);
        comboModality->setObjectName("comboModality");

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, comboModality);

        labelTargetMode = new QLabel(groupConfig);
        labelTargetMode->setObjectName("labelTargetMode");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, labelTargetMode);

        comboTargetMode = new QComboBox(groupConfig);
        comboTargetMode->setObjectName("comboTargetMode");

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, comboTargetMode);

        labelSteps = new QLabel(groupConfig);
        labelSteps->setObjectName("labelSteps");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, labelSteps);

        spinSteps = new QSpinBox(groupConfig);
        spinSteps->setObjectName("spinSteps");
        spinSteps->setMinimum(10);
        spinSteps->setMaximum(500);
        spinSteps->setValue(80);

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, spinSteps);

        labelSeed = new QLabel(groupConfig);
        labelSeed->setObjectName("labelSeed");

        formLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, labelSeed);

        spinSeed = new QSpinBox(groupConfig);
        spinSeed->setObjectName("spinSeed");
        spinSeed->setMinimum(0);
        spinSeed->setMaximum(999999);
        spinSeed->setValue(1);

        formLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, spinSeed);

        labelDt = new QLabel(groupConfig);
        labelDt->setObjectName("labelDt");

        formLayout->setWidget(5, QFormLayout::ItemRole::LabelRole, labelDt);

        spinDt = new QDoubleSpinBox(groupConfig);
        spinDt->setObjectName("spinDt");
        spinDt->setDecimals(2);
        spinDt->setMinimum(0.010000000000000);
        spinDt->setMaximum(1.000000000000000);
        spinDt->setSingleStep(0.050000000000000);
        spinDt->setValue(0.100000000000000);

        formLayout->setWidget(5, QFormLayout::ItemRole::FieldRole, spinDt);


        leftLayout->addWidget(groupConfig);

        groupActions = new QGroupBox(leftPanel);
        groupActions->setObjectName("groupActions");
        actionsLayout = new QVBoxLayout(groupActions);
        actionsLayout->setObjectName("actionsLayout");
        btnRun = new QPushButton(groupActions);
        btnRun->setObjectName("btnRun");

        actionsLayout->addWidget(btnRun);

        btnReset = new QPushButton(groupActions);
        btnReset->setObjectName("btnReset");

        actionsLayout->addWidget(btnReset);


        leftLayout->addWidget(groupActions);

        groupResults = new QGroupBox(leftPanel);
        groupResults->setObjectName("groupResults");
        resultsLayout = new QFormLayout(groupResults);
        resultsLayout->setObjectName("resultsLayout");
        label_2 = new QLabel(groupResults);
        label_2->setObjectName("label_2");

        resultsLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_2);

        labelTargetCount = new QLabel(groupResults);
        labelTargetCount->setObjectName("labelTargetCount");

        resultsLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, labelTargetCount);

        label_3 = new QLabel(groupResults);
        label_3->setObjectName("label_3");

        resultsLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_3);

        labelMeasDim = new QLabel(groupResults);
        labelMeasDim->setObjectName("labelMeasDim");

        resultsLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, labelMeasDim);

        label_4 = new QLabel(groupResults);
        label_4->setObjectName("label_4");

        resultsLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, label_4);

        labelPosRmse = new QLabel(groupResults);
        labelPosRmse->setObjectName("labelPosRmse");

        resultsLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, labelPosRmse);

        label_5 = new QLabel(groupResults);
        label_5->setObjectName("label_5");

        resultsLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, label_5);

        labelVelRmse = new QLabel(groupResults);
        labelVelRmse->setObjectName("labelVelRmse");

        resultsLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, labelVelRmse);

        label_6 = new QLabel(groupResults);
        label_6->setObjectName("label_6");

        resultsLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, label_6);

        labelElapsed = new QLabel(groupResults);
        labelElapsed->setObjectName("labelElapsed");

        resultsLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, labelElapsed);

        label_7 = new QLabel(groupResults);
        label_7->setObjectName("label_7");

        resultsLayout->setWidget(5, QFormLayout::ItemRole::LabelRole, label_7);

        labelStepTime = new QLabel(groupResults);
        labelStepTime->setObjectName("labelStepTime");

        resultsLayout->setWidget(5, QFormLayout::ItemRole::FieldRole, labelStepTime);


        leftLayout->addWidget(groupResults);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        leftLayout->addItem(verticalSpacer);

        splitter->addWidget(leftPanel);
        chartContainer = new QWidget(splitter);
        chartContainer->setObjectName("chartContainer");
        chartsLayout = new QVBoxLayout(chartContainer);
        chartsLayout->setObjectName("chartsLayout");
        chartsLayout->setContentsMargins(0, 0, 0, 0);
        splitter->addWidget(chartContainer);

        horizontalLayout->addWidget(splitter);

        MainWindow->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        btnRun->setDefault(true);


        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "3D PS Tracker UI", nullptr));
        groupConfig->setTitle(QCoreApplication::translate("MainWindow", "\344\273\277\347\234\237\351\205\215\347\275\256", nullptr));
        labelScene->setText(QCoreApplication::translate("MainWindow", "\345\234\272\346\231\257:", nullptr));
        labelModality->setText(QCoreApplication::translate("MainWindow", "\346\265\213\351\207\217\351\207\217:", nullptr));
        labelTargetMode->setText(QCoreApplication::translate("MainWindow", "\347\233\256\346\240\207\346\250\241\345\274\217:", nullptr));
        labelSteps->setText(QCoreApplication::translate("MainWindow", "\346\255\245\346\225\260:", nullptr));
        labelSeed->setText(QCoreApplication::translate("MainWindow", "\351\232\217\346\234\272\347\247\215\345\255\220:", nullptr));
        labelDt->setText(QCoreApplication::translate("MainWindow", "\346\227\266\351\227\264\346\255\245\351\225\277:", nullptr));
        groupActions->setTitle(QCoreApplication::translate("MainWindow", "\346\223\215\344\275\234", nullptr));
        btnRun->setText(QCoreApplication::translate("MainWindow", "\350\277\220\350\241\214\344\273\277\347\234\237", nullptr));
        btnReset->setText(QCoreApplication::translate("MainWindow", "\351\207\215\347\275\256\351\273\230\350\256\244\345\200\274", nullptr));
        groupResults->setTitle(QCoreApplication::translate("MainWindow", "\347\273\223\346\236\234", nullptr));
        label_2->setText(QCoreApplication::translate("MainWindow", "\347\233\256\346\240\207\346\225\260:", nullptr));
        labelTargetCount->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
        label_3->setText(QCoreApplication::translate("MainWindow", "\346\265\213\351\207\217\347\273\264\346\225\260:", nullptr));
        labelMeasDim->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
        label_4->setText(QCoreApplication::translate("MainWindow", "\344\275\215\347\275\256 RMSE:", nullptr));
        labelPosRmse->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
        label_5->setText(QCoreApplication::translate("MainWindow", "\351\200\237\345\272\246 RMSE:", nullptr));
        labelVelRmse->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
        label_6->setText(QCoreApplication::translate("MainWindow", "\346\200\273\350\200\227\346\227\266:", nullptr));
        labelElapsed->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
        label_7->setText(QCoreApplication::translate("MainWindow", "\345\215\225\346\255\245\350\200\227\346\227\266:", nullptr));
        labelStepTime->setText(QCoreApplication::translate("MainWindow", "\342\200\224", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
