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
#include <QLabel>

static QString shellQuote(QString value)
{
    value.replace('\'', "'\"'\"'");
    return QStringLiteral("'") + value + QStringLiteral("'");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , installProcess(nullptr)
    , logDialog(nullptr)
    , logEdit(nullptr)
    , closeButton(nullptr)
    , logFile(nullptr)
    , installTempDir(nullptr)
    , mokProcess(nullptr)
    , mokLogDialog(nullptr)
    , mokLogEdit(nullptr)
    , mokCloseButton(nullptr)
    , configureMokDuringInstall(false)
    , mokInProgress(false)
    , mokTempDir(nullptr)
    , mokSuccess(false)
{
    // 窗口标题直接使用 nvidia 图标
    setWindowTitle(i18n("NVIDIA Driver Manager"));
    resize(540, 360);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);

    // ---- GPU 检测消息 ----
    gpuMessage = new KMessageWidget(this);
    gpuMessage->setWordWrap(true);
    gpuMessage->setCloseButtonVisible(false);
    gpuMessage->setMessageType(KMessageWidget::Positive);
    gpuMessage->setIcon(QIcon::fromTheme("dialog-information"));
    gpuMessage->hide();
    mainLayout->addWidget(gpuMessage);

    // ---- 已安装驱动提示消息 ----
    driverMessage = new KMessageWidget(this);
    driverMessage->setWordWrap(true);
    driverMessage->setCloseButtonVisible(false);
    driverMessage->setMessageType(KMessageWidget::Information);
    driverMessage->setIcon(QIcon::fromTheme("dialog-information"));
    driverMessage->hide();
    mainLayout->addWidget(driverMessage);

    // ---- 当前驱动版本 + 会话类型 ----
    currentVersionLabel = new QLabel(i18n("Current driver: detecting..."), this);
    currentVersionLabel->setWordWrap(true);
    mainLayout->addWidget(currentVersionLabel);

    sessionLabel = new QLabel(this);
    sessionLabel->setWordWrap(true);
    mainLayout->addWidget(sessionLabel);

    // ---- 驱动版本选择区 ----
    auto *selectorGroup = new QGroupBox(i18n("Select Driver Version"), this);
    auto *selectorLayout = new QVBoxLayout(selectorGroup);

    driverCombo = new QComboBox(this);
    const auto drivers = DriverUtils::availableDrivers();
    for (auto it = drivers.begin(); it != drivers.end(); ++it) {
        driverCombo->addItem(it.key(), it.value());
    }
    selectorLayout->addWidget(driverCombo);

    // 安全启动支持复选框
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

// ---------------------------------------------------------------------------
// 刷新状态：检测 GPU、已安装驱动、会话类型
// ---------------------------------------------------------------------------
void MainWindow::refreshStatus()
{
    bool hasGpu = DriverUtils::hasNvidiaGpu();

    // ---- GPU 检测 ----
    gpuMessage->setMessageType(KMessageWidget::Positive);
    gpuMessage->setIcon(QIcon::fromTheme("dialog-information"));

    if (hasGpu) {
        gpuMessage->setText(i18n("NVIDIA GPU detected"));
        driverCombo->setEnabled(true);
        installButton->setEnabled(true);
    } else {
        // 没有 NVIDIA 显卡，提示无需安装
        gpuMessage->setMessageType(KMessageWidget::Warning);
        gpuMessage->setIcon(QIcon::fromTheme("dialog-warning"));
        gpuMessage->setText(i18n("This computer does not need additional NVIDIA drivers."));
        driverCombo->setEnabled(false);
        installButton->setEnabled(false);
    }
    gpuMessage->show();

    // ---- 已安装驱动版本 ----
    QString installed = DriverUtils::installedDriverVersion();
    if (installed.isEmpty()) {
        currentVersionLabel->setText(i18n("Current driver: not installed"));
    } else {
        currentVersionLabel->setText(i18n("Current driver: %1", installed));
    }

    // ---- 通过 rpm 检测已安装的 akmod-nvidia 包 ----
    QString installedPkg = DriverUtils::installedDriverPackage();
    if (!installedPkg.isEmpty()) {
        // 检测到已安装 akmod-nvidia（或 akmod-nvidia-580xx 等旧版本），提示无需操作
        driverMessage->setMessageType(KMessageWidget::Positive);
        driverMessage->setIcon(QIcon::fromTheme("dialog-information"));
        driverMessage->setText(i18n("NVIDIA driver package '%1' is already installed. "
                                    "No action is needed unless you want to switch versions.",
                                    installedPkg));
        driverMessage->show();

        // 在下拉框中选中当前已安装的版本
        for (int i = 0; i < driverCombo->count(); ++i) {
            if (driverCombo->itemData(i).toString() == installedPkg) {
                driverCombo->setCurrentIndex(i);
                break;
            }
        }
    } else {
        driverMessage->hide();
    }

    // ---- 会话类型（Wayland / X11）----
    QString session = DriverUtils::displayServer();
    sessionLabel->setText(i18n("Display server: %1", session));
}

// ---------------------------------------------------------------------------
// MOK 配置：生成密钥 + 导入证书
// ---------------------------------------------------------------------------
void MainWindow::startMokConfiguration(const QString &password)
{
    mokSuccess = false;

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

    QString logPath = DriverUtils::logFilePath();

    // ========== MOK 配置脚本 ==========
    // 1. 仅在 /etc/pki/akmods/certs 和 /etc/pki/akmods/private 下的证书/私钥
    //    不存在时，才使用 kmodgenca 生成新密钥
    // 2. 使用 mokutil --test-key 检查证书是否已导入，已导入则跳过
    // 3. 未导入则使用 mokutil --import 导入 kmodgenca 生成的证书
    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "set -o pipefail";
    scriptLines << "LOG=\"" + logPath + "\"";
    scriptLines << "export LANG=C.UTF-8";
    scriptLines << "export LC_ALL=C.UTF-8";
    scriptLines << "CERT_DIR=\"/etc/pki/akmods/certs\"";
    scriptLines << "PRIV_DIR=\"/etc/pki/akmods/private\"";
    scriptLines << "CERT_FILE=\"$CERT_DIR/public_key.der\"";
    scriptLines << "KEY_FILE=\"$PRIV_DIR/private_key.priv\"";
    scriptLines << "STATE_DIR=\"/var/lib/nvidia-driver-manager\"";
    scriptLines << "MOK_IMPORT_STATE=\"$STATE_DIR/mok-import.sha256\"";
    scriptLines << "PASSWORD=\"" + password + "\"";
    scriptLines << "";
    scriptLines << "if ! rpm -q akmods-evernight >/dev/null 2>&1; then";
    scriptLines << "  echo \"akmods-evernight is not installed, installing it now...\"";
    scriptLines << "  stdbuf -oL dnf install -y --nogpgcheck akmods-evernight 2>&1 | tee -a \"$LOG\"";
    scriptLines << "fi";
    scriptLines << "";
    // ---- 仅在证书/私钥不存在时生成新密钥 ----
    scriptLines << "if [ -f \"$CERT_FILE\" ] && [ -f \"$KEY_FILE\" ]; then";
    scriptLines << "  echo \"Certificate and key files already exist, skipping generation.\"";
    scriptLines << "else";
    scriptLines << "  mkdir -p \"$CERT_DIR\" \"$PRIV_DIR\"";
    scriptLines << "";
    scriptLines << "  # 检查 kmodgenca 是否可用";
    scriptLines << "  if ! command -v kmodgenca &>/dev/null && ! [ -x /usr/bin/kmodgenca ]; then";
    scriptLines << "    echo \"kmodgenca not found, reinstalling akmods-evernight...\"";
    scriptLines << "    stdbuf -oL dnf install -y --nogpgcheck akmods-evernight 2>&1 | tee -a \"$LOG\"";
    scriptLines << "    if [ $? -ne 0 ]; then";
    scriptLines << "      echo \"ERROR: dnf install akmods-evernight failed.\"";
    scriptLines << "      exit 1";
    scriptLines << "    fi";
    scriptLines << "    if ! command -v kmodgenca &>/dev/null && ! [ -x /usr/bin/kmodgenca ]; then";
    scriptLines << "      echo \"ERROR: kmodgenca still not found after installation.\"";
    scriptLines << "      exit 1";
    scriptLines << "    fi";
    scriptLines << "  fi";
    scriptLines << "";
    scriptLines << "  LOCALE_WRAPPER_DIR=$(mktemp -d /tmp/nvidia-driver-manager-locale.XXXXXX)";
    scriptLines << "  cat > \"$LOCALE_WRAPPER_DIR/locale\" <<'EOF'";
    scriptLines << "#!/bin/bash";
    scriptLines << "if [ \"$1\" = \"country_ab2\" ]; then";
    scriptLines << "  echo \"US\"";
    scriptLines << "  exit 0";
    scriptLines << "fi";
    scriptLines << "exec /usr/bin/locale \"$@\"";
    scriptLines << "EOF";
    scriptLines << "  chmod +x \"$LOCALE_WRAPPER_DIR/locale\"";
    scriptLines << "  echo \"Generating new signing key and certificate using kmodgenca...\"";
    scriptLines << "  set +e";
    scriptLines << "  PATH=\"$LOCALE_WRAPPER_DIR:$PATH\" /usr/bin/kmodgenca -a -f >> \"$LOG\" 2>&1";
    scriptLines << "  KMODGENCA_EXIT=$?";
    scriptLines << "  set -e";
    scriptLines << "  rm -rf \"$LOCALE_WRAPPER_DIR\"";
    scriptLines << "  if [ $KMODGENCA_EXIT -ne 0 ]; then";
    scriptLines << "    echo \"ERROR: kmodgenca failed.\"";
    scriptLines << "    exit 1";
    scriptLines << "  fi";
    scriptLines << "";
    scriptLines << "  if [ ! -f \"$CERT_FILE\" ] || [ ! -f \"$KEY_FILE\" ]; then";
    scriptLines << "    echo \"ERROR: kmodgenca did not create certificate files.\"";
    scriptLines << "    exit 1";
    scriptLines << "  fi";
    scriptLines << "  echo \"Certificate and key files generated successfully.\"";
    scriptLines << "fi";
    scriptLines << "";
    // ---- 检查证书是否已导入 MOK，已导入则跳过 ----
    scriptLines << "echo \"Checking if certificate is already enrolled...\"";
    scriptLines << "set +e";
    scriptLines << "MOK_TEST_OUTPUT=$(mokutil --test-key \"$CERT_FILE\" 2>&1)";
    scriptLines << "MOK_TEST_EXIT=$?";
    scriptLines << "set -e";
    scriptLines << "echo \"$MOK_TEST_OUTPUT\"";
    scriptLines << "";
    scriptLines << "if echo \"$MOK_TEST_OUTPUT\" | grep -qi \"is already enrolled\"; then";
    scriptLines << "  echo \"Certificate is already enrolled in MOK, skipping import.\"";
    scriptLines << "  rm -f \"$MOK_IMPORT_STATE\" 2>/dev/null || true";
    scriptLines << "else";
    scriptLines << "  CERT_SHA256=$(sha256sum \"$CERT_FILE\" | awk '{print $1}')";
    scriptLines << "  if [ -f \"$MOK_IMPORT_STATE\" ] && grep -qx \"$CERT_SHA256\" \"$MOK_IMPORT_STATE\"; then";
    scriptLines << "    echo \"MOK import request for this certificate was already submitted, skipping duplicate import.\"";
    scriptLines << "  else";
    scriptLines << "    echo \"Certificate is not enrolled in MOK, importing it now...\"";
    scriptLines << "    set +e";
    scriptLines << "    printf \"%s\\n%s\\n\" \"$PASSWORD\" \"$PASSWORD\" | mokutil --import \"$CERT_FILE\" 2>&1 | tee /tmp/mok_import.log";
    scriptLines << "    MOK_EXIT=${PIPESTATUS[1]}";
    scriptLines << "    set -e";
    scriptLines << "    if [ $MOK_EXIT -eq 0 ]; then";
    scriptLines << "      mkdir -p \"$STATE_DIR\"";
    scriptLines << "      echo \"$CERT_SHA256\" > \"$MOK_IMPORT_STATE\"";
    scriptLines << "      echo \"MOK import request submitted successfully.\"";
    scriptLines << "    elif grep -qi -e \"already\" -e \"exists\" /tmp/mok_import.log; then";
    scriptLines << "      mkdir -p \"$STATE_DIR\"";
    scriptLines << "      echo \"$CERT_SHA256\" > \"$MOK_IMPORT_STATE\"";
    scriptLines << "      echo \"MOK import request already exists, skipping duplicate import.\"";
    scriptLines << "    else";
    scriptLines << "      echo \"Import failed with exit code $MOK_EXIT. Error output:\"";
    scriptLines << "      cat /tmp/mok_import.log";
    scriptLines << "      exit 1";
    scriptLines << "    fi";
    scriptLines << "  fi";
    scriptLines << "fi";

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

    // 写入日志文件
    if (logFile && logFile->isOpen()) {
        logFile->write(text.toUtf8());
        logFile->flush();
    }
}

void MainWindow::onMokFinished(int exitCode, QProcess::ExitStatus status)
{
    bool success = (status == QProcess::NormalExit && exitCode == 0);
    mokSuccess = success;

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

    // 追加写入日志文件
    QFile logFile(DriverUtils::logFilePath());
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
    mokInProgress = false;
}

// ---------------------------------------------------------------------------
// 安装前检查安全启动状态，如需要则配置 MOK
// ---------------------------------------------------------------------------
bool MainWindow::ensureSecureBootConfigured()
{
    configureMokDuringInstall = false;
    mokPassword.clear();

    if (!DriverUtils::hasSecureBoot()) {
        return true;
    }

    // 如果证书已导入 MOK，跳过配置
    if (DriverUtils::isMokEnrolled()) {
        QMessageBox::information(this, i18n("MOK Already Configured"),
                                 i18n("The MOK certificate is already enrolled.\n"
                                      "No further MOK configuration is needed."));
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

    // 弹出提示框要求用户选择一个 MOK 密码
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

    // 确认密码
    QString confirm = QInputDialog::getText(this, i18n("Confirm MOK Password"),
                                            i18n("Please re-enter the password to confirm:"),
                                            QLineEdit::Password, "", &ok);
    if (!ok || password != confirm) {
        QMessageBox::warning(this, i18n("Password Mismatch"),
                             i18n("Passwords do not match. MOK configuration cancelled."));
        return true;
    }

    mokPassword = password;
    configureMokDuringInstall = true;
    return true;
}

// ---------------------------------------------------------------------------
// 安装按钮点击
// ---------------------------------------------------------------------------
void MainWindow::onInstallClicked()
{
    QString selectedPkg = driverCombo->currentData().toString();
    if (selectedPkg.isEmpty()) return;

    bool needAllowerasing = false;
    QString installedPkg = DriverUtils::installedDriverPackage();
    if (!installedPkg.isEmpty()) {
        if (installedPkg == selectedPkg) {
            // 已安装相同版本，提示无需操作
            QMessageBox::information(this, i18n("Already Installed"),
                                     i18n("The driver version '%1' is already installed.\n"
                                          "No further action is required.", selectedPkg));
            return;
        } else {
            // 已安装不同版本，询问是否替换
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

    // 安全启动 MOK 配置
    if (secureBootCheckBox->isChecked()) {
        if (!ensureSecureBootConfigured()) return;
    } else {
        if (DriverUtils::hasSecureBoot()) {
            QMessageBox::information(this, i18n("Secure Boot Ignored"),
                                     i18n("Secure Boot is enabled, but you chose not to configure MOK.\n"
                                          "The driver may fail to load after installation."));
        }
    }

    // 构建 CUDA 包名（有对应版本号时加在 nvidia 后面）
    QString cudaPkg = DriverUtils::cudaPackageFor(selectedPkg);
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

    // ---- 安装日志窗口 ----
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

    // ---- 准备一次性 root 安装脚本：akmods-evernight -> MOK -> NVIDIA 驱动 ----
    QString pkgMgr = "dnf";
    QProcess whichProc;
    whichProc.start("command", {"-v", "dnf5"});
    whichProc.waitForFinished();
    if (whichProc.exitCode() == 0) {
        pkgMgr = "dnf5";
    }

    installTempDir = new QTemporaryDir();
    if (!installTempDir->isValid()) {
        logEdit->append(i18n("Error: Cannot create temporary directory."));
        closeButton->setEnabled(true);
        installButton->setEnabled(true);
        delete installTempDir;
        installTempDir = nullptr;
        return;
    }

    QString scriptPath = installTempDir->path() + "/install-nvidia-driver.sh";
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logEdit->append(i18n("Error: Cannot write temporary script."));
        closeButton->setEnabled(true);
        installButton->setEnabled(true);
        delete installTempDir;
        installTempDir = nullptr;
        return;
    }

    QStringList quotedPackages;
    for (const QString &pkg : packagesToInstall) {
        quotedPackages << shellQuote(pkg);
    }

    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "set -o pipefail";
    scriptLines << "LOG=" + shellQuote(DriverUtils::logFilePath());
    scriptLines << "PKG_MGR=" + shellQuote(pkgMgr);
    scriptLines << "export LANG=C.UTF-8";
    scriptLines << "export LC_ALL=C.UTF-8";
    scriptLines << "# 以 root 身份重新创建日志文件并设置权限，避免旧文件属主或 SELinux 上下文导致写入失败";
    scriptLines << "rm -f \"$LOG\" 2>/dev/null || true";
    scriptLines << ": > \"$LOG\"";
    scriptLines << "chmod 666 \"$LOG\" 2>/dev/null || true";
    scriptLines << "# 将所有输出同时发送到 stdout（GUI 捕获）和日志文件";
    scriptLines << "exec > >(tee -a \"$LOG\") 2>&1";
    scriptLines << "echo \"=== Privileged installation script started at $(date) ===\"";
    scriptLines << "";
    scriptLines << "echo \"Checking akmods-evernight...\"";
    scriptLines << "if ! rpm -q akmods-evernight >/dev/null 2>&1; then";
    scriptLines << "  echo \"akmods-evernight is not installed, installing it now...\"";
    scriptLines << "  \"$PKG_MGR\" install -y --nogpgcheck akmods-evernight";
    scriptLines << "else";
    scriptLines << "  echo \"akmods-evernight is already installed.\"";
    scriptLines << "fi";

    if (configureMokDuringInstall) {
        scriptLines << "";
        scriptLines << "echo \"Starting MOK configuration...\"";
        scriptLines << "CERT_DIR=\"/etc/pki/akmods/certs\"";
        scriptLines << "PRIV_DIR=\"/etc/pki/akmods/private\"";
        scriptLines << "CERT_FILE=\"$CERT_DIR/public_key.der\"";
        scriptLines << "KEY_FILE=\"$PRIV_DIR/private_key.priv\"";
        scriptLines << "STATE_DIR=\"/var/lib/nvidia-driver-manager\"";
        scriptLines << "MOK_IMPORT_STATE=\"$STATE_DIR/mok-import.sha256\"";
        scriptLines << "PASSWORD=" + shellQuote(mokPassword);
        scriptLines << "";
        scriptLines << "if [ -f \"$CERT_FILE\" ] && [ -f \"$KEY_FILE\" ]; then";
        scriptLines << "  echo \"Certificate and key files already exist, skipping generation.\"";
        scriptLines << "else";
        scriptLines << "  mkdir -p \"$CERT_DIR\" \"$PRIV_DIR\"";
        scriptLines << "  KMODGENCA=$(command -v kmodgenca || true)";
        scriptLines << "  if [ -z \"$KMODGENCA\" ] && [ -x /usr/bin/kmodgenca ]; then";
        scriptLines << "    KMODGENCA=/usr/bin/kmodgenca";
        scriptLines << "  fi";
        scriptLines << "  if [ -z \"$KMODGENCA\" ]; then";
        scriptLines << "    echo \"ERROR: kmodgenca was not found after installing akmods-evernight.\"";
        scriptLines << "    exit 1";
        scriptLines << "  fi";
        scriptLines << "  LOCALE_WRAPPER_DIR=$(mktemp -d /tmp/nvidia-driver-manager-locale.XXXXXX)";
        scriptLines << "  cat > \"$LOCALE_WRAPPER_DIR/locale\" <<'EOF'";
        scriptLines << "#!/bin/bash";
        scriptLines << "if [ \"$1\" = \"country_ab2\" ]; then";
        scriptLines << "  echo \"US\"";
        scriptLines << "  exit 0";
        scriptLines << "fi";
        scriptLines << "exec /usr/bin/locale \"$@\"";
        scriptLines << "EOF";
        scriptLines << "  chmod +x \"$LOCALE_WRAPPER_DIR/locale\"";
        scriptLines << "  echo \"Generating new signing key and certificate using kmodgenca...\"";
        scriptLines << "  set +e";
        scriptLines << "  PATH=\"$LOCALE_WRAPPER_DIR:$PATH\" \"$KMODGENCA\" -a -f";
        scriptLines << "  KMODGENCA_EXIT=$?";
        scriptLines << "  set -e";
        scriptLines << "  rm -rf \"$LOCALE_WRAPPER_DIR\"";
        scriptLines << "  if [ $KMODGENCA_EXIT -ne 0 ]; then";
        scriptLines << "    echo \"ERROR: kmodgenca failed.\"";
        scriptLines << "    exit 1";
        scriptLines << "  fi";
        scriptLines << "  if [ ! -f \"$CERT_FILE\" ] || [ ! -f \"$KEY_FILE\" ]; then";
        scriptLines << "    echo \"ERROR: kmodgenca did not create certificate files.\"";
        scriptLines << "    exit 1";
        scriptLines << "  fi";
        scriptLines << "  echo \"Certificate and key files generated successfully.\"";
        scriptLines << "fi";
        scriptLines << "";
        scriptLines << "echo \"Checking if certificate is already enrolled...\"";
        scriptLines << "set +e";
        scriptLines << "MOK_TEST_OUTPUT=$(mokutil --test-key \"$CERT_FILE\" 2>&1)";
        scriptLines << "MOK_TEST_EXIT=$?";
        scriptLines << "set -e";
        scriptLines << "echo \"$MOK_TEST_OUTPUT\"";
        scriptLines << "if echo \"$MOK_TEST_OUTPUT\" | grep -qi \"is already enrolled\"; then";
        scriptLines << "  echo \"Certificate is already enrolled in MOK, skipping import.\"";
        scriptLines << "  rm -f \"$MOK_IMPORT_STATE\" 2>/dev/null || true";
        scriptLines << "else";
        scriptLines << "  CERT_SHA256=$(sha256sum \"$CERT_FILE\" | awk '{print $1}')";
        scriptLines << "  if [ -f \"$MOK_IMPORT_STATE\" ] && grep -qx \"$CERT_SHA256\" \"$MOK_IMPORT_STATE\"; then";
        scriptLines << "    echo \"MOK import request for this certificate was already submitted, skipping duplicate import.\"";
        scriptLines << "  else";
        scriptLines << "    echo \"Certificate is not enrolled in MOK, importing it now...\"";
        scriptLines << "    set +e";
        scriptLines << "    printf \"%s\\n%s\\n\" \"$PASSWORD\" \"$PASSWORD\" | mokutil --import \"$CERT_FILE\" 2>&1 | tee /tmp/mok_import.log";
        scriptLines << "    MOK_EXIT=${PIPESTATUS[1]}";
        scriptLines << "    set -e";
        scriptLines << "    if [ $MOK_EXIT -eq 0 ]; then";
        scriptLines << "      mkdir -p \"$STATE_DIR\"";
        scriptLines << "      echo \"$CERT_SHA256\" > \"$MOK_IMPORT_STATE\"";
        scriptLines << "      echo \"MOK import request submitted successfully.\"";
        scriptLines << "    elif grep -qi -e \"already\" -e \"exists\" /tmp/mok_import.log; then";
        scriptLines << "      mkdir -p \"$STATE_DIR\"";
        scriptLines << "      echo \"$CERT_SHA256\" > \"$MOK_IMPORT_STATE\"";
        scriptLines << "      echo \"MOK import request already exists, skipping duplicate import.\"";
        scriptLines << "    else";
        scriptLines << "      echo \"Import failed with exit code $MOK_EXIT. Error output:\"";
        scriptLines << "      cat /tmp/mok_import.log";
        scriptLines << "      exit 1";
        scriptLines << "    fi";
        scriptLines << "  fi";
        scriptLines << "fi";
        // MOK 配置完成标记，GUI 检测到后弹出密码提醒对话框
        scriptLines << "echo \"===MOK_CONFIG_COMPLETE===\"";
    }

    scriptLines << "";
    scriptLines << "echo \"Installing NVIDIA driver packages...\"";
    if (needAllowerasing) {
        scriptLines << "\"$PKG_MGR\" install -y --allowerasing " + quotedPackages.join(" ");
    } else {
        scriptLines << "\"$PKG_MGR\" install -y " + quotedPackages.join(" ");
    }
    scriptLines << "echo \"=== Privileged installation script finished at $(date) ===\"";

    scriptFile.write(scriptLines.join("\n").toUtf8());
    scriptFile.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner);
    scriptFile.close();

    // 日志文件由 root 脚本自己创建（touch + chmod 666），GUI 不再直接写入，避免权限冲突

    // ---- 启动安装进程：开始安装时只触发一次 Polkit 认证 ----
    installProcess = new QProcess(this);
    installProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(installProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onInstallOutput);
    connect(installProcess, &QProcess::readyReadStandardError, this, &MainWindow::onInstallOutput);
    connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onInstallFinished);

    installProcess->start("pkexec", {"/usr/libexec/nvidia-driver-manager-helper", scriptPath});
    if (!installProcess->waitForStarted()) {
        logEdit->append(i18n("Failed to start pkexec."));
        closeButton->setEnabled(true);
        installButton->setEnabled(true);
        if (installTempDir) {
            delete installTempDir;
            installTempDir = nullptr;
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

    // 检测 MOK 配置完成标记，弹出密码提醒对话框
    if (text.contains("===MOK_CONFIG_COMPLETE===") && configureMokDuringInstall && !mokPassword.isEmpty()) {
        QMessageBox::information(this, i18n("MOK Configuration Complete"),
                                 i18n("MOK key generation and certificate import have been completed.\n\n"
                                      "Please remember the MOK password you set earlier. After reboot, "
                                      "the MOK management interface (blue screen) will appear. You must "
                                      "enter this password to enroll the key:\n\n"
                                      "    %1\n\n"
                                      "Steps: Enroll MOK -> Continue -> Yes -> enter password -> Reboot.",
                                      mokPassword));
        // 从显示文本中移除标记行
        text.remove("===MOK_CONFIG_COMPLETE===");
    }

    if (logEdit) {
        logEdit->append(text);
        QTextCursor cursor = logEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        logEdit->setTextCursor(cursor);
    }
    // 日志写入由 root 脚本通过 tee 完成，GUI 不再直接写文件
}

void MainWindow::onInstallFinished(int exitCode, QProcess::ExitStatus status)
{
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    // 日志的结尾标记由 root 脚本输出，GUI 不再写文件

    if (logEdit) {
        logEdit->append(i18n("\n--- Process finished with exit code %1 ---", exitCode));
    }

    if (closeButton) closeButton->setEnabled(true);
    installButton->setEnabled(true);

    if (success) {
        QString successMsg;
        if (DriverUtils::hasSecureBoot()) {
            successMsg = i18n(
                "The installation is complete. Please wait about 10 minutes, then restart your computer and enter the MOK password. Here's how: after restarting, when the blue screen appears, press any key within 10 seconds, then go through Enroll MOK, Continue, Yes, and then enter your MOK password. After that, select Reboot to restart your computer. Then, type `lsmod | grep nvidia` to check if NVIDIA has loaded successfully, and go to KDE System Settings > About This System to see if your NVIDIA GPU model is displayed. If the NVIDIA driver didn't load successfully, open the terminal, type sudo akmods --force, then sudo dracut -v --force, and restart your computer to complete the installation!"
            );
        } else {
            successMsg = i18n(
                "The installation is complete. Please wait about 10 minutes, then restart your computer to start using it! If the NVIDIA driver didn't load successfully, open the terminal, type sudo akmods --force, then sudo dracut -v --force, and restart your computer to complete the installation!"
            );
        }
        successMsg += i18n("\n\nInstallation log saved to:\n%1", DriverUtils::logFilePath());
        QMessageBox::information(this, i18n("Success"), successMsg);
    } else {
        QMessageBox::critical(this, i18n("Error"),
                              i18n("Driver installation failed.\n"
                                   "Check the log for details:\n"
                                   "%1", DriverUtils::logFilePath()));
    }

    refreshStatus();

    installProcess->deleteLater();
    installProcess = nullptr;
    if (installTempDir) {
        delete installTempDir;
        installTempDir = nullptr;
    }
}
