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
    static bool installDriver(const QStringList &packages);
    static bool hasSecureBoot();
    static bool configureMok();
    static bool checkAuthorization();

    // 新增：设置跳过检测标志
    static void setSkipCheck(bool skip);
};

#endif // DRIVERUTILS_H