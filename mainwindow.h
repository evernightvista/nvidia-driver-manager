#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <KLocalizedString>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refreshStatus();
    void onInstallClicked();

private:
    bool ensureSecureBootConfigured();

    QLabel *gpuLabel;
    QLabel *currentVersionLabel;
    QComboBox *driverCombo;
    QPushButton *installButton;
};

#endif // MAINWINDOW_H