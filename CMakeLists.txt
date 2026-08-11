cmake_minimum_required(VERSION 3.16)
project(NvidiaDriverManager VERSION 2.0.0 LANGUAGES CXX)

set(TRANSLATION_DOMAIN "nvidia-driver-manager")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH "${ECM_MODULE_PATH}" ${CMAKE_MODULE_PATH})
include(GNUInstallDirs)
include(KDEInstallDirs)
include(KDECMakeSettings)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6 REQUIRED COMPONENTS CoreAddons WidgetsAddons I18n Crash)

add_executable(nvidia-driver-manager
    main.cpp
    mainwindow.cpp
    driverutils.cpp
)

target_link_libraries(nvidia-driver-manager
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::I18n
    KF6::Crash
)

install(TARGETS nvidia-driver-manager RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES nvidia-driver-manager.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(PROGRAMS nvidia-driver-manager-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES org.fedoraproject.nvidia-driver-manager.policy DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)

ki18n_install(po)
