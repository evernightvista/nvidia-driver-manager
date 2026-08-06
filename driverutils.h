#ifndef DRIVERUTILS_H
#define DRIVERUTILS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QProcess>

class DriverUtils
{
public:
    static bool hasNvidiaGpu();
    static QString installedDriverPackage();
    static QString installedDriverVersion();
    static QMap<QString, QString> availableDrivers();
    static bool installDriver(const QString &packageName);
    static bool hasSecureBoot();
    static bool configureMok();
    static bool checkAuthorization();  // New: Polkit pre‑auth
};

#endif // DRIVERUTILS_H