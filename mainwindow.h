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
#include <QTemporaryDir>

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

    // MOK 配置专用槽
    void onMokOutput();
    void onMokFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool ensureSecureBootConfigured();
    void startMokConfiguration(const QString &password);

    KMessageWidget *gpuMessage;         // GPU 检测结果
    KMessageWidget *driverMessage;      // 已安装驱动提示
    QLabel *currentVersionLabel;        // 当前驱动版本
    QLabel *sessionLabel;               // Wayland / X11 会话类型
    QComboBox *driverCombo;             // 驱动版本选择
    QPushButton *installButton;
    QCheckBox *secureBootCheckBox;      // 安全启动 MOK 复选框

    // 安装进程
    QProcess *installProcess;
    QDialog *logDialog;
    QTextEdit *logEdit;
    QPushButton *closeButton;
    QStringList packagesToInstall;
    QFile *logFile;
    QTemporaryDir *installTempDir;

    // MOK 配置专用
    QProcess *mokProcess;
    QDialog *mokLogDialog;
    QTextEdit *mokLogEdit;
    QPushButton *mokCloseButton;
    QString mokPassword;
    bool configureMokDuringInstall;
    bool mokInProgress;
    QTemporaryDir *mokTempDir;
    bool mokSuccess;   // 记录 MOK 配置结果
};

#endif // MAINWINDOW_H
