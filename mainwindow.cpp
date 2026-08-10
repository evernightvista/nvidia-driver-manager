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
#include <QEventLoop>
#include <QTemporaryDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , installProcess(nullptr)
    , logDialog(nullptr)
    , logEdit(nullptr)
    , closeButton(nullptr)
    , logFile(nullptr)
    , mokProcess(nullptr)
    , mokLogDialog(nullptr)
    , mokLogEdit(nullptr)
    , mokCloseButton(nullptr)
    , mokInProgress(false)
    , mokTempDir(nullptr)
    , mokSuccess(false)          // <--- 初始化 mokSuccess
{
    setWindowTitle(i18n("NVIDIA Driver Manager"));
    resize(520, 320);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);

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

void MainWindow::startMokConfiguration(const QString &password)
{
    mokSuccess = false;   // <--- 重置成功标志

    mokLogDialog = new QDialog(this);
    mokLogDialog->setWindowTitle(i18n("MOK Configuration Log"));
    mokLogDialog->resize(700, 400);
    mokLogDialog->setAttribute(Qt::WA_DeleteOnClose);

    mokLogEdit = new QTextEdit(mokLogDialog);
    mokLogEdit->setReadOnly(true);
    mokLogEdit->setFontFamily("monospace");
    mokLogEdit->setLineWrapMode(QTextEdit::NoWrap);

    mokCloseButton = new QPushButton(i18n("Close"), mokLogDialog);
    mokCloseButton->setEnabled(false);

    QVBoxLayout *layout = new QVBoxLayout(mokLogDialog);
    layout->addWidget(mokLogEdit);
    layout->addWidget(mokCloseButton, 0, Qt::AlignRight);

    connect(mokCloseButton, &QPushButton::clicked, mokLogDialog, &QDialog::accept);
    mokLogDialog->show();

    mokTempDir = new QTemporaryDir();
    if (!mokTempDir->isValid()) {
        mokLogEdit->append(i18n("Error: Cannot create temporary directory."));
        mokCloseButton->setEnabled(true);
        delete mokTempDir;
        mokTempDir = nullptr;
        return;
    }

    QString scriptPath = mokTempDir->path() + "/setup-mok.sh";
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mokLogEdit->append(i18n("Error: Cannot write temporary script."));
        mokCloseButton->setEnabled(true);
        delete mokTempDir;
        mokTempDir = nullptr;
        return;
    }

    // ========== 最终修正脚本（强制删除旧证书 + 改进导入） ==========
    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "set -o pipefail";
    scriptLines << "CERT_DIR=\"/etc/pki/akmods/certs\"";
    scriptLines << "PRIV_DIR=\"/etc/pki/akmods/private\"";
    scriptLines << "CERT_FILE=\"$CERT_DIR/public_key.der\"";
    scriptLines << "KEY_FILE=\"$PRIV_DIR/private_key.priv\"";
    scriptLines << "PASSWORD=\"" + password + "\"";
    scriptLines << "";
    scriptLines << "mkdir -p \"$CERT_DIR\" \"$PRIV_DIR\"";
    scriptLines << "";
    scriptLines << "# 强制删除旧证书和私钥，确保生成全新密钥对";
    scriptLines << "rm -f \"$CERT_FILE\" \"$KEY_FILE\"";
    scriptLines << "";
    scriptLines << "# 检查 kmodgenca 是否可用，若不可用则安装 akmods-evernight";
    scriptLines << "if ! command -v kmodgenca &>/dev/null && ! [ -x /usr/bin/kmodgenca ]; then";
    scriptLines << "  echo \"kmodgenca not found, installing akmods-evernight...\"";
    scriptLines << "  stdbuf -oL dnf install -y --nogpgcheck akmods-evernight 2>&1 | tee -a /tmp/nvidia-driver-installer.log";
    scriptLines << "  if [ $? -ne 0 ]; then";
    scriptLines << "    echo \"ERROR: dnf install akmods-evernight failed.\"";
    scriptLines << "    exit 1";
    scriptLines << "  fi";
    scriptLines << "  if ! command -v kmodgenca &>/dev/null && ! [ -x /usr/bin/kmodgenca ]; then";
    scriptLines << "    echo \"ERROR: kmodgenca still not found after installation.\"";
    scriptLines << "    exit 1";
    scriptLines << "  fi";
    scriptLines << "fi";
    scriptLines << "";
    scriptLines << "# 强制生成新证书（直接重定向，避免管道导致失败）";
    scriptLines << "echo \"Generating new signing key and certificate using /usr/bin/kmodgenca -a -f...\"";
    scriptLines << "/usr/bin/kmodgenca -a -f >> /tmp/nvidia-driver-installer.log 2>&1";
    scriptLines << "if [ $? -ne 0 ]; then";
    scriptLines << "  echo \"ERROR: kmodgenca -a -f failed.\"";
    scriptLines << "  exit 1";
    scriptLines << "fi";
    scriptLines << "";
    scriptLines << "# 确保证书和密钥文件已生成";
    scriptLines << "if [ ! -f \"$CERT_FILE\" ] || [ ! -f \"$KEY_FILE\" ]; then";
    scriptLines << "  echo \"ERROR: kmodgenca -a -f did not create certificate files.\"";
    scriptLines << "  exit 1";
    scriptLines << "fi";
    scriptLines << "echo \"Certificate and key files exist.\"";
    scriptLines << "";
    scriptLines << "# 导入 MOK（若已注册则自动跳过）";
    scriptLines << "echo \"Importing certificate to MOK...\"";
    scriptLines << "set +e";
    scriptLines << "printf \"%s\\n%s\\n\" \"$PASSWORD\" \"$PASSWORD\" | stdbuf -oL mokutil --import \"$CERT_FILE\" > /tmp/mok_import.log 2>&1";
    scriptLines << "MOK_EXIT=$?";
    scriptLines << "if [ $MOK_EXIT -eq 0 ]; then";
    scriptLines << "  echo \"Import succeeded.\"";
    scriptLines << "else";
    scriptLines << "  if grep -qi -e \"already\" -e \"exists\" /tmp/mok_import.log; then";
    scriptLines << "    echo \"Certificate already enrolled, skipping.\"";
    scriptLines << "  else";
    scriptLines << "    echo \"Import failed with exit code $MOK_EXIT. Error output:\"";
    scriptLines << "    cat /tmp/mok_import.log";
    scriptLines << "    exit 1";
    scriptLines << "  fi";
    scriptLines << "fi";
    scriptLines << "set -e";

    scriptFile.write(scriptLines.join("\n").toUtf8());
    scriptFile.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner);
    scriptFile.close();

    mokProcess = new QProcess(this);
    mokProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(mokProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onMokOutput);
    connect(mokProcess, &QProcess::readyReadStandardError, this, &MainWindow::onMokOutput);
    connect(mokProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onMokFinished);

    mokProcess->start("pkexec", {"bash", scriptPath});
    if (!mokProcess->waitForStarted()) {
        mokLogEdit->append(i18n("Failed to start pkexec."));
        mokCloseButton->setEnabled(true);
        mokProcess->deleteLater();
        mokProcess = nullptr;
        delete mokTempDir;
        mokTempDir = nullptr;
        return;
    }

    mokInProgress = true;
    installButton->setEnabled(false);
}

void MainWindow::onMokOutput()
{
    if (!mokProcess) return;
    QByteArray data = mokProcess->readAllStandardOutput();
    if (data.isEmpty()) return;

    QString text = QString::fromUtf8(data);
    if (mokLogEdit) {
        mokLogEdit->append(text);
        QTextCursor cursor = mokLogEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        mokLogEdit->setTextCursor(cursor);
    }

    // 写入日志文件时强制转换为 UTF-8，避免乱码
    if (logFile && logFile->isOpen()) {
        QString utf8Text = QString::fromUtf8(data);
        logFile->write(utf8Text.toUtf8());
        logFile->flush();
    }
}

void MainWindow::onMokFinished(int exitCode, QProcess::ExitStatus status)
{
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    mokSuccess = success;   // <--- 记录结果

    if (mokLogEdit) {
        mokLogEdit->append(i18n("\n--- MOK configuration finished with exit code %1 ---", exitCode));
        if (success) {
            mokLogEdit->append(i18n("MOK configuration completed successfully."));
        } else {
            mokLogEdit->append(i18n("MOK configuration failed. Please check the log for details."));
        }
    }

    if (mokCloseButton) mokCloseButton->setEnabled(true);
    installButton->setEnabled(true);

    QFile logFile("/tmp/nvidia-driver-installer.log");
    if (logFile.open(QIODevice::Append | QIODevice::WriteOnly)) {
        logFile.write("=== MOK configuration finished with exit code ");
        logFile.write(QByteArray::number(exitCode));
        logFile.write(" ===\n");
        logFile.close();
    }

    if (mokProcess) {
        mokProcess->deleteLater();
        mokProcess = nullptr;
    }
    if (mokTempDir) {
        delete mokTempDir;
        mokTempDir = nullptr;
    }
    mokInProgress = false;   // <--- 最后再设置，确保 success 已保存
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

    startMokConfiguration(password);

    QEventLoop loop;
    connect(mokProcess, &QProcess::finished, &loop, &QEventLoop::quit);
    while (mokInProgress) {
        loop.exec();
    }

    // 使用 mokSuccess 判断，而不是 mokProcess 指针
    bool success = mokSuccess;
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

// 以下函数（onInstallClicked, onInstallOutput, onInstallFinished）与之前相同，但为了保证完整性，一并给出
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
    if (logFile->open(QIODevice::Append | QIODevice::WriteOnly)) {
        QByteArray header;
        header += "\n=== Installation started at " + QDateTime::currentDateTime().toString().toUtf8() + " ===\n";
        header += "Package manager: " + pkgMgr.toUtf8() + "\n";
        header += "Packages: " + packagesToInstall.join(", ").toUtf8() + "\n";
        if (needAllowerasing) {
            header += "Option: --allowerasing (to replace existing driver)\n";
        }
        logFile->write(header);
        logFile->flush();
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
        QString utf8Text = QString::fromUtf8(data);
        logFile->write(utf8Text.toUtf8());
        logFile->flush();
    }
}

void MainWindow::onInstallFinished(int exitCode, QProcess::ExitStatus status)
{
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    if (logFile && logFile->isOpen()) {
        QByteArray footer;
        footer += "=== Installation " + QByteArray(success ? "SUCCEEDED" : "FAILED") +
                  " at " + QDateTime::currentDateTime().toString().toUtf8() + " ===\n";
        logFile->write(footer);
        logFile->flush();
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