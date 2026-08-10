#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <KLocalizedString>
#include <KMessageWidget>
#include <QProcess>
#include <QDialog>
#include <QTextEdit>
#include <QFile>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refreshStatus();
    void onInstallClicked();
    void onInstallOutput();
    void onInstallFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool ensureSecureBootConfigured();

    KMessageWidget *gpuMessage;
    QLabel *currentVersionLabel;
    QComboBox *driverCombo;
    QPushButton *installButton;
    QCheckBox *secureBootCheckBox;

    QProcess *installProcess;
    QDialog *logDialog;
    QTextEdit *logEdit;
    QPushButton *closeButton;
    QStringList packagesToInstall;
    QFile *logFile;
};

#endif // MAINWINDOW_H