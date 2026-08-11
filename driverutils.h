#ifndef DRIVERUTILS_H
#define DRIVERUTILS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QProcess>
#include <functional>

/**
 * 工具类：封装 NVIDIA 驱动检测、安装、安全启动 MOK 配置等底层操作。
 */
class DriverUtils
{
public:
    /* ---- GPU 检测 ---- */
    static bool hasNvidiaGpu();

    /* ---- 已安装驱动查询 ---- */
    static QString installedDriverPackage();
    static QString installedDriverVersion();
    static QMap<QString, QString> availableDrivers();

    /* ---- 驱动安装 ---- */
    static bool installDriver(const QStringList &packages);
    static QString cudaPackageFor(const QString &akmodPkg);

    /* ---- 安全启动 / MOK ---- */
    static bool hasSecureBoot();
    static bool configureMok(const QString &password);
    static bool checkAuthorization();
    static bool akmodsKeyExists();      // 检查 /etc/pki/akmods 下的证书和私钥是否存在
    static bool isMokEnrolled();        // 检查证书是否已导入 MOK

    /* ---- 会话检测 ---- */
    static QString displayServer();     // 返回 "Wayland" / "X11" / "Unknown"

    /* ---- 命令行参数 ---- */
    static void setSkipCheck(bool skip);
    static bool skipCheck();

    /* ---- 日志路径 ---- */
    static QString logFilePath();
};

#endif // DRIVERUTILS_H
