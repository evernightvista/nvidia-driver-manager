#include "mainwindow.h"
#include "driverutils.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QIcon>
#include <QApplication>
#include <QInputDialog>
#include <QDateTime>
#include <QTextCursor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , installProcess(nullptr)
    , logDialog(nullptr)
    , logEdit(nullptr)
    , closeButton(nullptr)
    , logFile(nullptr)
{
    setWindowTitle(i18n("NVIDIA Driver Manager"));
    resize(520, 320);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);

    // 高亮信息条（绿色，信息图标）
    gpuMessage = new KMessageWidget(this);
    gpuMessage->setWordWrap(true);
    gpuMessage->setCloseButtonVisible(false);
    gpuMessage->setMessageType(KMessageWidget::Positive);
    gpuMessage->setIcon(QIcon::fromTheme("dialog-information"));
    gpuMessage->hide();
    mainLayout->addWidget(gpuMessage);

    currentVersionLabel = new QLabel(i18n("Current driver: detecting..."), this);
    currentVersionLabel->setWordWrap(true);
    mainLayout->addWidget(currentVersionLabel);

    auto *selectorGroup = new QGroupBox(i18n("Select Driver Version"), this);
    auto *selectorLayout = new QVBoxLayout(selectorGroup);

    driverCombo = new QComboBox(this);
    const auto drivers = DriverUtils::availableDrivers();
    for (auto it = drivers.begin(); it != drivers.end(); ++it) {
        driverCombo->addItem(it.key(), it.value());
    }
    selectorLayout->addWidget(driverCombo);

    secureBootCheckBox = new QCheckBox(i18n("Configure MOK for Secure Boot (if enabled)"), this);
    secureBootCheckBox->setChecked(true);
    selectorLayout->addWidget(secureBootCheckBox);

    installButton = new QPushButton(i18n("Install / Switch Driver"), this);
    installButton->setIcon(QIcon::fromTheme("system-run"));
    selectorLayout->addWidget(installButton);

    mainLayout->addWidget(selectorGroup);
    mainLayout->addStretch();

    connect(installButton, &QPushButton::clicked, this, &MainWindow::onInstallClicked);

    refreshStatus();
}

void MainWindow::refreshStatus()
{
    bool hasGpu = DriverUtils::hasNvidiaGpu();

    gpuMessage->setMessageType(KMessageWidget::Positive);
    gpuMessage->setIcon(QIcon::fromTheme("dialog-information"));

    if (hasGpu) {
        gpuMessage->setText(i18n("NVIDIA GPU detected ✓"));
        driverCombo->setEnabled(true);
        installButton->setEnabled(true);
    } else {
        gpuMessage->setText(i18n("This computer does not need additional drivers."));
        driverCombo->setEnabled(false);
        installButton->setEnabled(false);
    }
    gpuMessage->show();

    QString installed = DriverUtils::installedDriverVersion();
    if (installed.isEmpty()) {
        currentVersionLabel->setText(i18n("Current driver: not installed"));
    } else {
        currentVersionLabel->setText(i18n("Current driver: %1", installed));
    }

    QString installedPkg = DriverUtils::installedDriverPackage();
    if (!installedPkg.isEmpty()) {
        for (int i = 0; i < driverCombo->count(); ++i) {
            if (driverCombo->itemData(i).toString() == installedPkg) {
                driverCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::onInstallClicked()
{
    QString selectedPkg = driverCombo->currentData().toString();
    if (selectedPkg.isEmpty()) return;

    bool needAllowerasing = false;
    QString installedPkg = DriverUtils::installedDriverPackage();
    if (!installedPkg.isEmpty()) {
        if (installedPkg == selectedPkg) {
            QMessageBox::information(this, i18n("Already Installed"),
                                     i18n("The driver version '%1' is already installed.\n"
                                          "No further action is required.", selectedPkg));
            return;
        } else {
            QMessageBox::StandardButton reply = QMessageBox::warning(
                this,
                i18n("Different Version Installed"),
                i18n("You currently have '%1' installed.\n"
                     "Installing '%2' will replace it (this will remove the old driver package).\n\n"
                     "Are you sure you want to continue?",
                     installedPkg, selectedPkg),
                QMessageBox::Yes | QMessageBox::No
            );
            if (reply != QMessageBox::Yes) return;
            needAllowerasing = true;
        }
    }

    if (secureBootCheckBox->isChecked()) {
        if (!ensureSecureBootConfigured()) return;
    } else {
        if (DriverUtils::hasSecureBoot()) {
            QMessageBox::information(this, i18n("Secure Boot Ignored"),
                                     i18n("Secure Boot is enabled, but you chose not to configure MOK.\n"
                                          "The driver may fail to load after installation."));
        }
    }

    QString cudaPkg;
    if (selectedPkg == "akmod-nvidia") {
        cudaPkg = "xorg-x11-drv-nvidia-cuda";
    } else {
        QString version = selectedPkg;
        version.remove("akmod-nvidia-");
        if (version.isEmpty()) {
            cudaPkg = "xorg-x11-drv-nvidia-cuda";
        } else {
            cudaPkg = "xorg-x11-drv-nvidia-" + version + "-cuda";
        }
    }
    packagesToInstall = {selectedPkg, cudaPkg};

    auto answer = QMessageBox::question(
        this,
        i18n("Confirm Installation"),
        i18n("The system will install/switch to the following packages:\n%1\n\n"
             "This requires root privileges and may download packages. Continue?",
             packagesToInstall.join("\n")),
        QMessageBox::Yes | QMessageBox::No
    );
    if (answer != QMessageBox::Yes) return;

    // 日志对话框
    logDialog = new QDialog(this);
    logDialog->setWindowTitle(i18n("Driver Installation Log"));
    logDialog->resize(700, 400);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);

    logEdit = new QTextEdit(logDialog);
    logEdit->setReadOnly(true);
    logEdit->setFontFamily("monospace");
    logEdit->setLineWrapMode(QTextEdit::NoWrap);

    closeButton = new QPushButton(i18n("Close"), logDialog);
    closeButton->setEnabled(false);

    QVBoxLayout *layout = new QVBoxLayout(logDialog);
    layout->addWidget(logEdit);
    layout->addWidget(closeButton, 0, Qt::AlignRight);

    connect(closeButton, &QPushButton::clicked, logDialog, &QDialog::accept);
    logDialog->show();

    installButton->setEnabled(false);

    // 异步安装
    installProcess = new QProcess(this);
    installProcess->setProcessChannelMode(QProcess::MergedChannels);

    QString pkgMgr = "dnf";
    QProcess whichProc;
    whichProc.start("command", {"-v", "dnf5"});
    whichProc.waitForFinished();
    if (whichProc.exitCode() == 0) {
        pkgMgr = "dnf5";
    }

    QStringList args;
    args << pkgMgr << "install" << "-y" << packagesToInstall;
    if (needAllowerasing) {
        args << "--allowerasing";
    }

    logFile = new QFile("/tmp/nvidia-driver-installer.log", this);
    if (logFile->open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(logFile);
        out << "\n=== Installation started at " << QDateTime::currentDateTime().toString()
            << " ===\n";
        out << "Package manager: " << pkgMgr << "\n";
        out << "Packages: " << packagesToInstall.join(", ") << "\n";
        if (needAllowerasing) {
            out << "Option: --allowerasing (to replace existing driver)\n";
        }
        out.flush();
    } else {
        delete logFile;
        logFile = nullptr;
    }

    connect(installProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onInstallOutput);
    connect(installProcess, &QProcess::readyReadStandardError, this, &MainWindow::onInstallOutput);
    connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onInstallFinished);

    installProcess->start("pkexec", args);
    if (!installProcess->waitForStarted()) {
        logEdit->append(i18n("Failed to start pkexec."));
        closeButton->setEnabled(true);
        installButton->setEnabled(true);
        if (logFile) {
            logFile->close();
            delete logFile;
            logFile = nullptr;
        }
        return;
    }
}

void MainWindow::onInstallOutput()
{
    if (!installProcess) return;
    QByteArray data = installProcess->readAllStandardOutput();
    if (data.isEmpty()) return;

    QString text = QString::fromUtf8(data);
    if (logEdit) {
        logEdit->append(text);
        QTextCursor cursor = logEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        logEdit->setTextCursor(cursor);
    }

    if (logFile && logFile->isOpen()) {
        logFile->write(data);
        logFile->flush();
    }
}

void MainWindow::onInstallFinished(int exitCode, QProcess::ExitStatus status)
{
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    if (logFile && logFile->isOpen()) {
        QTextStream out(logFile);
        out << "=== Installation " << (success ? "SUCCEEDED" : "FAILED")
            << " at " << QDateTime::currentDateTime().toString() << " ===\n";
        out.flush();
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }

    if (logEdit) {
        logEdit->append(i18n("\n--- Process finished with exit code %1 ---", exitCode));
    }

    if (closeButton) closeButton->setEnabled(true);
    installButton->setEnabled(true);

    if (success) {
        QString successMsg;
        if (DriverUtils::hasSecureBoot()) {
            successMsg = i18n(
                "The installation is complete. Please wait about 10 minutes, then restart your computer and enter the MOK password. Here's how: after restarting, when the blue screen appears, press any key within 10 seconds, then go through “Enroll MOK”, “Continue”, “Yes”, and then enter your MOK password. After that, select “Reboot” to restart your computer. Then, type `lsmod | grep nvidia` to check if NVIDIA has loaded successfully, and go to KDE System Settings > About This System to see if your NVIDIA GPU model is displayed. If the NVIDIA driver didn't load successfully, open the terminal, type sudo akmods --force, then sudo dracut -v --force, and restart your computer to complete the installation!"
            );
        } else {
            successMsg = i18n(
                "The installation is complete. Please wait about 10 minutes, then restart your computer to start using it！If the NVIDIA driver didn't load successfully, open the terminal, type sudo akmods --force, then sudo dracut -v --force, and restart your computer to complete the installation!"
            );
        }
        successMsg += i18n("\n\nInstallation log saved to:\n/tmp/nvidia-driver-installer.log");
        QMessageBox::information(this, i18n("Success"), successMsg);
    } else {
        QMessageBox::critical(this, i18n("Error"),
                              i18n("Driver installation failed.\n"
                                   "Check the log for details:\n"
                                   "/tmp/nvidia-driver-installer.log"));
    }

    refreshStatus();

    installProcess->deleteLater();
    installProcess = nullptr;
}

bool MainWindow::ensureSecureBootConfigured()
{
    if (!DriverUtils::hasSecureBoot()) {
        return true;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(i18n("Secure Boot Detected"));
    msgBox.setText(i18n("Secure Boot is enabled on this system.\n"
                        "NVIDIA drivers must be signed to load.\n\n"
                        "Do you want to configure Machine Owner Key (MOK) now?"));
    msgBox.setIcon(QMessageBox::Question);
    QPushButton *configureBtn = msgBox.addButton(i18n("Configure MOK"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = msgBox.addButton(i18n("Skip (Install anyway)"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(configureBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == skipBtn) {
        return true;
    }

    bool ok;
    QString password = QInputDialog::getText(this, i18n("Set MOK Password"),
                                             i18n("Please enter a password for the Machine Owner Key (MOK).\n"
                                                  "You will need to enter this password after reboot in the MOK management interface.\n"
                                                  "Password:"),
                                             QLineEdit::Password, "", &ok);
    if (!ok || password.isEmpty()) {
        QMessageBox::warning(this, i18n("Password Required"),
                             i18n("MOK configuration cancelled. You can still install the driver, "
                                  "but it may not load with Secure Boot enabled."));
        return true;
    }

    QString confirm = QInputDialog::getText(this, i18n("Confirm MOK Password"),
                                            i18n("Please re-enter the password to confirm:"),
                                            QLineEdit::Password, "", &ok);
    if (!ok || password != confirm) {
        QMessageBox::warning(this, i18n("Password Mismatch"),
                             i18n("Passwords do not match. MOK configuration cancelled."));
        return true;
    }

    bool success = DriverUtils::configureMok(password);
    if (!success) {
        QMessageBox::critical(this, i18n("MOK Configuration Failed"),
                              i18n("Failed to set up MOK. You can still install the driver, "
                                   "but it may not load with Secure Boot enabled."));
        return true;
    }

    QMessageBox::information(this, i18n("MOK Configured"),
                             i18n("The Machine Owner Key has been imported successfully.\n\n"
                                  "When you reboot, the MOK management interface will appear.\n"
                                  "Select 'Enroll MOK' -> 'Continue' -> 'Yes' and enter the password you just set:\n"
                                  "    %1\n\n"
                                  "After enrolling, the driver will load correctly.", password));
    return true;
}