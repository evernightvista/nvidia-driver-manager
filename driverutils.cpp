#include "driverutils.h"
#include <QRegularExpression>
#include <QDebug>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

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
    QProcess proc;
    proc.start("nvidia-smi", {"--query-gpu=driver_version", "--format=csv,noheader"});
    proc.waitForFinished(5000);
    QString ver = proc.readAllStandardOutput().trimmed();
    if (!ver.isEmpty())
        return ver;

    proc.start("modinfo", {"-F", "version", "nvidia"});
    proc.waitForFinished(5000);
    ver = proc.readAllStandardOutput().trimmed();
    return ver;
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

    QStringList args;
    args << "dnf" << "install" << "-y";
    args << packages;
    args << "--allowerasing";

    QProcess proc;
    proc.start("pkexec", args);
    proc.waitForFinished(-1);
    return proc.exitCode() == 0;
}

bool DriverUtils::hasSecureBoot()
{
    QProcess proc;
    proc.start("mokutil", {"--sb-state"});
    proc.waitForFinished(3000);
    QString output = proc.readAllStandardOutput() + proc.readAllStandardError();
    return output.contains("SecureBoot enabled", Qt::CaseInsensitive);
}

bool DriverUtils::configureMok()
{
    const QString certDir = "/etc/pki/akmods/certs";
    const QString keyPath = "/etc/pki/akmods/private/signing_key.pem";
    const QString certPath = certDir + "/public_key.der";

    QStringList scriptLines;
    scriptLines << "#!/bin/bash";
    scriptLines << "set -e";
    scriptLines << "mkdir -p /etc/pki/akmods/private " + certDir;
    scriptLines << "if [ ! -f \"" + keyPath + "\" ]; then";
    scriptLines << "  openssl req -new -x509 -newkey rsa:2048 \\";
    scriptLines << "    -keyout \"" + keyPath + "\" \\";
    scriptLines << "    -out \"" + certPath + "\" \\";
    scriptLines << "    -nodes -days 36500 \\";
    scriptLines << "    -subj \"/CN=NVIDIA Driver Signing Key/\"";
    scriptLines << "fi";
    scriptLines << "echo \"nvidia-mok\" | mokutil --import \"" + certPath + "\"";

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
    scriptFile.write(scriptLines.join("\n").toUtf8());
    scriptFile.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner);
    scriptFile.close();

    QProcess proc;
    proc.start("pkexec", {"bash", scriptPath});
    proc.waitForFinished(-1);

    if (proc.exitCode() == 0) {
        qDebug() << "MOK configured successfully.";
        return true;
    } else {
        qWarning() << "MOK configuration failed:" << proc.readAllStandardError();
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