#include "driverutils.h"
#include <QRegularExpression>
#include <QDebug>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTextStream>

static bool s_skipCheck = false;

// ---------------------------------------------------------------------------
// 日志路径
// ---------------------------------------------------------------------------
QString DriverUtils::logFilePath()
{
    return QStringLiteral("/tmp/nvidia-drvinst.log");
}

// ---------------------------------------------------------------------------
// --skip 参数
// ---------------------------------------------------------------------------
void DriverUtils::setSkipCheck(bool skip)
{
    s_skipCheck = skip;
}

bool DriverUtils::skipCheck()
{
    return s_skipCheck;
}

// ---------------------------------------------------------------------------
// 检测 NVIDIA 显卡（lspci）
// ---------------------------------------------------------------------------
bool DriverUtils::hasNvidiaGpu()
{
    if (s_skipCheck) {
        qDebug() << "Command-line --skip: forcing NVIDIA GPU presence.";
        return true;
    }

    QProcess proc;
    proc.start("lspci", {"-nn"});
    proc.waitForFinished(5000);
    QString output = proc.readAllStandardOutput();
    return output.contains("NVIDIA", Qt::CaseInsensitive) ||
           output.contains("10de", Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
// 通过 rpm 查询已安装的 akmod-nvidia 包名
// ---------------------------------------------------------------------------
QString DriverUtils::installedDriverPackage()
{
    QProcess proc;
    proc.start("rpm", {"-qa", "--queryformat", "%{NAME}\n"});
    proc.waitForFinished(5000);
    const QStringList lines = QString(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        if (line.startsWith("akmod-nvidia") && !line.contains("runtime"))
            return line;
        if (line.startsWith("kmod-nvidia"))
            return line;
    }
    return {};
}

// ---------------------------------------------------------------------------
// 已安装驱动的版本号
// ---------------------------------------------------------------------------
QString DriverUtils::installedDriverVersion()
{
    QString pkg = installedDriverPackage();
    if (pkg.isEmpty()) {
        return {};
    }

    QProcess proc;
    proc.start("rpm", {"-q", "--queryformat", "%{VERSION}", pkg});
    proc.waitForFinished(5000);
    QString ver = proc.readAllStandardOutput().trimmed();
    if (!ver.isEmpty()) {
        return ver;
    }
    return pkg;
}

// ---------------------------------------------------------------------------
// 可选驱动列表：最新版 / 580 / 470 / 390 / 340
// ---------------------------------------------------------------------------
QMap<QString, QString> DriverUtils::availableDrivers()
{
    QMap<QString, QString> map;
    map[QStringLiteral("NVIDIA Latest (open kernel module)")] = QStringLiteral("akmod-nvidia");
    map[QStringLiteral("NVIDIA 580")] = QStringLiteral("akmod-nvidia-580xx");
    map[QStringLiteral("NVIDIA 470")] = QStringLiteral("akmod-nvidia-470xx");
    map[QStringLiteral("NVIDIA 390")] = QStringLiteral("akmod-nvidia-390xx");
    map[QStringLiteral("NVIDIA 340")] = QStringLiteral("akmod-nvidia-340xx");
    return map;
}

// ---------------------------------------------------------------------------
// 根据 akmod 包名推导对应的 xorg-x11-drv-nvidia-cuda 包名
//   akmod-nvidia        -> xorg-x11-drv-nvidia-cuda
//   akmod-nvidia-580xx  -> xorg-x11-drv-nvidia-580xx-cuda
// ---------------------------------------------------------------------------
QString DriverUtils::cudaPackageFor(const QString &akmodPkg)
{
    if (akmodPkg == QStringLiteral("akmod-nvidia")) {
        return QStringLiteral("xorg-x11-drv-nvidia-cuda");
    }
    QString version = akmodPkg;
    version.remove("akmod-nvidia-");
    if (version.isEmpty()) {
        return QStringLiteral("xorg-x11-drv-nvidia-cuda");
    }
    return QStringLiteral("xorg-x11-drv-nvidia-") + version + QStringLiteral("-cuda");
}

// ---------------------------------------------------------------------------
// 安装驱动（静态工具函数，mainwindow 有自己的异步安装流程）
// ---------------------------------------------------------------------------
bool DriverUtils::installDriver(const QStringList &packages)
{
    if (packages.isEmpty()) return false;

    QString pkgMgr = "dnf";
    QProcess whichProc;
    whichProc.start("command", {"-v", "dnf5"});
    whichProc.waitForFinished();
    if (whichProc.exitCode() == 0) {
        pkgMgr = "dnf5";
    }

    QFile logFile(logFilePath());
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << "\n=== Installation started at " << QDateTime::currentDateTime().toString()
            << " ===\n";
        out << "Package manager: " << pkgMgr << "\n";
        out << "Packages: " << packages.join(", ") << "\n";
        logFile.close();
    }

    QStringList args;
    args << pkgMgr << "install" << "-y" << packages;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("pkexec", args);
    if (!proc.waitForStarted()) {
        return false;
    }
    proc.waitForFinished(-1);

    bool success = (proc.exitCode() == 0);
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << "=== Installation " << (success ? "SUCCEEDED" : "FAILED")
            << " at " << QDateTime::currentDateTime().toString() << " ===\n";
        logFile.close();
    }
    return success;
}

// ---------------------------------------------------------------------------
// 安全启动状态检测
// ---------------------------------------------------------------------------
bool DriverUtils::hasSecureBoot()
{
    QProcess proc;
    proc.start("mokutil", {"--sb-state"});
    proc.waitForFinished(3000);
    QString output = proc.readAllStandardOutput() + proc.readAllStandardError();
    return output.contains("SecureBoot enabled", Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
// 检查 /etc/pki/akmods 下的证书和私钥文件是否存在
// ---------------------------------------------------------------------------
bool DriverUtils::akmodsKeyExists()
{
    QFile certFile("/etc/pki/akmods/certs/public_key.der");
    QFile keyFile("/etc/pki/akmods/private/private_key.priv");
    return certFile.exists() && keyFile.exists();
}

// ---------------------------------------------------------------------------
// 检查证书是否已导入 MOK（解析 mokutil --test-key 输出）
// ---------------------------------------------------------------------------
bool DriverUtils::isMokEnrolled()
{
    QProcess proc;
    proc.start("mokutil", {"--test-key", "/etc/pki/akmods/certs/public_key.der"});
    proc.waitForFinished(5000);
    const QString output = QString::fromUtf8(proc.readAllStandardOutput() + proc.readAllStandardError());
    return output.contains("is already enrolled", Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
// 会话类型检测（Wayland / X11）
// ---------------------------------------------------------------------------
QString DriverUtils::displayServer()
{
    QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    if (sessionType == "wayland") {
        return QStringLiteral("Wayland");
    } else if (sessionType == "x11") {
        return QStringLiteral("X11");
    }
    // 回退检测
    if (!qgetenv("WAYLAND_DISPLAY").isEmpty()) {
        return QStringLiteral("Wayland");
    }
    if (!qgetenv("DISPLAY").isEmpty()) {
        return QStringLiteral("X11");
    }
    return QStringLiteral("Unknown");
}

// ---------------------------------------------------------------------------
// 配置 MOK（生成密钥 + 导入证书）
//   - 仅在 /etc/pki/akmods/certs 和 /etc/pki/akmods/private 下的证书/私钥
//     不存在时才使用 kmodgenca 生成新密钥
//   - 只有 mokutil --test-key 输出 is already enrolled 时才跳过导入
// ---------------------------------------------------------------------------
bool DriverUtils::configureMok(const QString &password)
{
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        qWarning() << "Cannot create temporary directory";
        return false;
    }
    QString scriptPath = tmpDir.path() + "/setup-mok.sh";
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot write temporary script";
        return false;
    }

    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "set -o pipefail";
    scriptLines << "LOG=\"" + logFilePath() + "\"";
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
    // ---- 检查证书是否已导入，已导入则跳过 ----
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

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("pkexec", {"bash", scriptPath});
    proc.waitForFinished(-1);

    QByteArray output = proc.readAllStandardOutput();
    QByteArray error = proc.readAllStandardError();
    QFile logFile(logFilePath());
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        logFile.write("=== MOK configuration output ===\n");
        if (!output.isEmpty()) logFile.write(output);
        if (!error.isEmpty()) logFile.write("ERROR: " + error);
        logFile.write("=== MOK configuration finished with exit code " +
                      QByteArray::number(proc.exitCode()) + " ===\n");
        logFile.close();
    }

    if (proc.exitCode() == 0) {
        qDebug() << "MOK configuration submitted successfully.";
        return true;
    } else {
        qWarning() << "MOK configuration failed:" << error;
        return false;
    }
}

// ---------------------------------------------------------------------------
// 检查 pkexec 授权是否可用
// ---------------------------------------------------------------------------
bool DriverUtils::checkAuthorization()
{
    QProcess proc;
    proc.start("pkexec", {"/bin/true"});
    proc.waitForFinished(-1);
    return proc.exitCode() == 0;
}
