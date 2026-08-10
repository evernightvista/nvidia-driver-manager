#ifndef DRIVERUTILS_H
#define DRIVERUTILS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QProcess>
#include <functional>

class DriverUtils
{
public:
    static bool hasNvidiaGpu();
    static QString installedDriverPackage();
    static QString installedDriverVersion();
    static QMap<QString, QString> availableDrivers();

    static bool installDriver(const QStringList &packages);

    static bool hasSecureBoot();
    static bool configureMok(const QString &password);
    static bool checkAuthorization();

    static void setSkipCheck(bool skip);
};

#endif // DRIVERUTILS_H