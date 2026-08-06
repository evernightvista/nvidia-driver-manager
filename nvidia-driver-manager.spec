Name:           nvidia-driver-manager
Version:        1.0.0
Release:        1%{?dist}
Summary:        NVIDIA driver manager for Fedora

License:        GPLv3
URL:            https://github.com/example/nvidia-driver-manager
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kwidgetsaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcrash-devel
BuildRequires:  gettext
BuildRequires:  desktop-file-utils
Requires:       qt6-qtbase
Requires:       kf6-kcoreaddons
Requires:       kf6-kwidgetsaddons
Requires:       kf6-ki18n
Requires:       kf6-kcrash
Requires:       polkit
Requires:       mokutil
Requires:       openssl
Requires:       pciutils

%description
A Qt6/KF6-based utility to manage NVIDIA drivers on Fedora.
It can detect NVIDIA GPUs, list available driver branches (latest,
580, 470, 390, 340), install or switch drivers, and optionally
set up Machine Owner Key (MOK) for Secure Boot.
Before installation, it verifies administrator privileges
using Polkit.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install

%find_lang %{name} --with-qt

# Validate desktop file if present
desktop-file-validate %{buildroot}%{_datadir}/applications/nvidia-driver-manager.desktop 2>/dev/null || true

%files -f %{name}.lang
%{_bindir}/nvidia-driver-manager
%{_datadir}/applications/nvidia-driver-manager.desktop

%changelog
* Thu Aug 06 2026 Your Name <your@email.com> - 1.0-1
- Initial release with Secure Boot MOK support and Polkit pre-authorization