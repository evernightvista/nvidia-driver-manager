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

    // 与 mainwindow.cpp 中的脚本保持一致（含强制删除旧证书和 stdbuf）
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
    scriptLines << "rm -f \"$CERT_FILE\" \"$KEY_FILE\"";
    scriptLines << "";
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
    scriptLines << "echo \"Generating new signing key and certificate using /usr/bin/kmodgenca -a -f...\"";
    scriptLines << "/usr/bin/kmodgenca -a -f >> /tmp/nvidia-driver-installer.log 2>&1";
    scriptLines << "if [ $? -ne 0 ]; then";
    scriptLines << "  echo \"ERROR: kmodgenca -a -f failed.\"";
    scriptLines << "  exit 1";
    scriptLines << "fi";
    scriptLines << "";
    scriptLines << "if [ ! -f \"$CERT_FILE\" ] || [ ! -f \"$KEY_FILE\" ]; then";
    scriptLines << "  echo \"ERROR: kmodgenca -a -f did not create certificate files.\"";
    scriptLines << "  exit 1";
    scriptLines << "fi";
    scriptLines << "echo \"Certificate and key files exist.\"";
    scriptLines << "";
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