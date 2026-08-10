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

    app.setWindowIcon(QIcon::fromTheme("nvidia-settings"));

    QStringList args = app.arguments();
    if (args.contains("--skip")) {
        DriverUtils::setSkipCheck(true);
    }

    KLocalizedString::setApplicationDomain("nvidia-driver-manager");

    KAboutData aboutData(
        QStringLiteral("nvidia-driver-manager"),
        i18n("NVIDIA Driver Manager"),
        QStringLiteral("1.0"),
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