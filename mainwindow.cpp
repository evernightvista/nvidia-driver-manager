#include "mainwindow.h"
#include "driverutils.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QIcon>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(i18n("NVIDIA Driver Manager"));
    resize(520, 280);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);

    gpuLabel = new QLabel(i18n("Detecting GPU..."), this);
    gpuLabel->setWordWrap(true);
    mainLayout->addWidget(gpuLabel);

    currentVersionLabel = new QLabel(i18n("Current driver: detecting..."), this);
    currentVersionLabel->setWordWrap(true);
    mainLayout->addWidget(currentVersionLabel);

    auto *selectorGroup = new QGroupBox(i18n("Select Driver Version"), this);
    auto *selectorLayout = new QVBoxLayout(selectorGroup);

    driverCombo = new QComboBox(this);
    const auto drivers = DriverUtils::availableDrivers();
    for (auto it = drivers.begin(); it != drivers.end(); ++it) {
        driverCombo->addItem(it.key(), it.value());
    }
    selectorLayout->addWidget(driverCombo);

    installButton = new QPushButton(i18n("Install / Switch Driver"), this);
    installButton->setIcon(QIcon::fromTheme("system-run"));
    selectorLayout->addWidget(installButton);

    mainLayout->addWidget(selectorGroup);
    mainLayout->addStretch();

    connect(installButton, &QPushButton::clicked, this, &MainWindow::onInstallClicked);

    refreshStatus();
}

void MainWindow::refreshStatus()
{
    bool hasGpu = DriverUtils::hasNvidiaGpu();
    if (hasGpu) {
        gpuLabel->setText(i18n("NVIDIA GPU detected ✓"));
        gpuLabel->setStyleSheet("color: green;");
        driverCombo->setEnabled(true);
        installButton->setEnabled(true);
    } else {
        gpuLabel->setText(i18n("This computer does not need additional drivers."));
        gpuLabel->setStyleSheet("color: black;");
        driverCombo->setEnabled(false);
        installButton->setEnabled(false);
    }

    QString installed = DriverUtils::installedDriverVersion();
    if (installed.isEmpty()) {
        currentVersionLabel->setText(i18n("Current driver: not installed"));
    } else {
        currentVersionLabel->setText(i18n("Current driver: %1", installed));
    }

    QString installedPkg = DriverUtils::installedDriverPackage();
    if (!installedPkg.isEmpty()) {
        for (int i = 0; i < driverCombo->count(); ++i) {
            if (driverCombo->itemData(i).toString() == installedPkg) {
                driverCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::onInstallClicked()
{
    QString selectedPkg = driverCombo->currentData().toString();
    if (selectedPkg.isEmpty()) return;

    // 1. Secure Boot check
    if (!ensureSecureBootConfigured()) {
        return;
    }

    // 2. Polkit authentication (pre‑authorization)
    if (!DriverUtils::checkAuthorization()) {
        QMessageBox::warning(this, i18n("Authentication Required"),
                             i18n("Administrator privileges are required to install drivers.\n"
                                  "Operation cancelled."));
        return;
    }

    // 3. Confirm installation
    auto answer = QMessageBox::question(
        this,
        i18n("Confirm Installation"),
        i18n("The system will install/switch to %1.\n\n"
             "This requires root privileges and may download packages. Continue?",
             driverCombo->currentText()),
        QMessageBox::Yes | QMessageBox::No
    );
    if (answer != QMessageBox::Yes) return;

    // 4. Perform installation
    bool success = DriverUtils::installDriver(selectedPkg);
    if (success) {
        QMessageBox::information(this, i18n("Success"),
                                 i18n("Driver installed successfully.\n"
                                      "Please reboot to apply changes."));
    } else {
        QMessageBox::critical(this, i18n("Error"),
                              i18n("Driver installation failed.\n"
                                   "Check terminal output for details."));
    }
    refreshStatus();
}

bool MainWindow::ensureSecureBootConfigured()
{
    if (!DriverUtils::hasSecureBoot()) {
        return true;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(i18n("Secure Boot Detected"));
    msgBox.setText(i18n("Secure Boot is enabled on this system.\n"
                        "NVIDIA drivers must be signed to load.\n\n"
                        "Do you want to configure Machine Owner Key (MOK) now?"));
    msgBox.setIcon(QMessageBox::Question);
    QPushButton *configureBtn = msgBox.addButton(i18n("Configure MOK"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = msgBox.addButton(i18n("Skip (Install anyway)"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(configureBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == skipBtn) {
        return true;
    }

    bool success = DriverUtils::configureMok();
    if (!success) {
        QMessageBox::critical(this, i18n("MOK Configuration Failed"),
                              i18n("Failed to set up MOK. You can still install the driver, "
                                   "but it may not load with Secure Boot enabled."));
        return true;
    }

    QMessageBox::information(this, i18n("MOK Configured"),
                             i18n("The Machine Owner Key has been imported successfully.\n\n"
                                  "When you reboot, the MOK management interface will appear.\n"
                                  "Select 'Enroll MOK' -> 'Continue' -> 'Yes' and enter the password:\n"
                                  "    nvidia-mok\n\n"
                                  "After enrolling, the driver will load correctly."));
    return true;
}