#include <QApplication>
#include <QIcon>
#include <KLocalizedString>
#include <KAboutData>
#include <KCrash>
#include "mainwindow.h"
#include "driverutils.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("nvidia-driver-manager"));
    app.setOrganizationDomain(QStringLiteral("fedoraproject.org"));

    // 窗口图标直接使用 nvidia 图标（Wayland 和 X11 下统一使用）
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("nvidia"));
    if (appIcon.isNull()) {
        appIcon = QIcon::fromTheme(QStringLiteral("nvidia-settings"));
    }
    app.setWindowIcon(appIcon);

    // 解析命令行参数：--skip 跳过 NVIDIA 显卡检测
    QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--skip"))) {
        DriverUtils::setSkipCheck(true);
    }

    KLocalizedString::setApplicationDomain("nvidia-driver-manager");

    KAboutData aboutData(
        QStringLiteral("nvidia-driver-manager"),
        i18n("NVIDIA Driver Manager"),
        QStringLiteral("2.0"),
        i18n("Manage NVIDIA drivers on Fedora Linux"),
        KAboutLicense::GPL_V3,
        i18n("(C) 2027 KairikiFedora")
    );
    KAboutData::setApplicationData(aboutData);

    KCrash::initialize();

    MainWindow w;
    w.show();
    return app.exec();
}
