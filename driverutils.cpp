#include "driverutils.h"
#include <QRegularExpression>
#include <QDebug>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDateTime>

static bool s_skipCheck = false;

void DriverUtils::setSkipCheck(bool skip)
{
    s_skipCheck = skip;
}

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

    QFile logFile("/tmp/nvidia-driver-installer.log");
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

bool DriverUtils::hasSecureBoot()
{
    QProcess proc;
    proc.start("mokutil", {"--sb-state"});
    proc.waitForFinished(3000);
    QString output = proc.readAllStandardOutput() + proc.readAllStandardError();
    return output.contains("SecureBoot enabled", Qt::CaseInsensitive);
}

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

    // 脚本逻辑：
    // 1. 确保两个目录存在（mkdir -p）
    // 2. 若证书或密钥缺失，则运行 kmodgenca -a 生成
    // 3. 导入 MOK（如果已注册，自动跳过）
    // 不再删除任何文件或目录
    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "CERT_DIR=\"/etc/pki/akmods/certs\"";
    scriptLines << "PRIV_DIR=\"/etc/pki/akmods/private\"";
    scriptLines << "CERT_FILE=\"$CERT_DIR/public_key.der\"";
    scriptLines << "KEY_FILE=\"$PRIV_DIR/signing_key.pem\"";
    scriptLines << "PASSWORD=\"" + password + "\"";
    scriptLines << "";
    scriptLines << "# 确保目录存在（不删除任何现有文件）";
    scriptLines << "mkdir -p \"$CERT_DIR\" \"$PRIV_DIR\"";
    scriptLines << "";
    scriptLines << "# 如果证书或密钥缺失，则生成新密钥（不会覆盖现有文件）";
    scriptLines << "if [ ! -f \"$CERT_FILE\" ] || [ ! -f \"$KEY_FILE\" ]; then";
    scriptLines << "  echo \"Generating new signing key and certificate using kmodgenca -a...\"";
    scriptLines << "  kmodgenca -a";
    scriptLines << "else";
    scriptLines << "  echo \"Existing signing key and certificate found, skipping generation.\"";
    scriptLines << "fi";
    scriptLines << "";
    scriptLines << "# 导入 MOK（若已注册则自动跳过）";
    scriptLines << "echo \"Importing certificate to MOK...\"";
    scriptLines << "if printf \"%s\\n%s\\n\" \"$PASSWORD\" \"$PASSWORD\" | mokutil --import \"$CERT_FILE\" 2>&1 | tee /tmp/mok_import.log; then";
    scriptLines << "  echo \"Import succeeded.\"";
    scriptLines << "else";
    scriptLines << "  if grep -qi \"already\" /tmp/mok_import.log; then";
    scriptLines << "    echo \"Certificate already enrolled, skipping.\"";
    scriptLines << "  else";
    scriptLines << "    echo \"Import failed.\"";
    scriptLines << "    exit 1";
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
    QFile logFile("/tmp/nvidia-driver-installer.log");
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

bool DriverUtils::checkAuthorization()
{
    QProcess proc;
    proc.start("pkexec", {"/bin/true"});
    proc.waitForFinished(-1);
    return proc.exitCode() == 0;
}