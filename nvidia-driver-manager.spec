Name:           nvidia-driver-manager
Version:        2.0.0
Release:        7%{?dist}
Summary:        NVIDIA driver manager for Fedora

License:        GPL-3.0-or-later
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kwidgetsaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcrash-devel
BuildRequires:  desktop-file-utils
BuildRequires:  gettext

Requires:       pciutils
Requires:       rpm
Requires:       mokutil
Requires:       polkit
Requires:       dnf

%description
NVIDIA Driver Manager is a Qt/KF6 graphical utility for Fedora.
It detects NVIDIA GPUs, lets users select NVIDIA driver branches,
supports Secure Boot MOK enrollment, and installs the matching CUDA
driver package.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install
%find_lang %{name}

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files -f %{name}.lang
%license LICENSE
%{_bindir}/%{name}
%{_libexecdir}/nvidia-driver-manager-helper
%{_datadir}/applications/%{name}.desktop
%{_datadir}/polkit-1/actions/org.fedoraproject.nvidia-driver-manager.policy

%changelog
* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-7
- Fix shell syntax error after successful MOK import caused by an unterminated
  marker echo line.

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-6
- Fix polkit policy matching by using a fixed helper executable under libexec
  instead of calling pkexec on /usr/bin/bash directly.
- Set the polkit prompt message to "安装nvidia驱动需要认证".
- Prevent duplicate MOK import requests by recording the submitted certificate
  SHA256 under /var/lib/nvidia-driver-manager.

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-5
- Show a MOK password reminder dialog after kmodgenca and mokutil complete,
  before NVIDIA driver installation begins.
- Add polkit policy file (org.fedoraproject.nvidia-driver-manager.policy)
  with localized authentication prompt for installing NVIDIA drivers and/or
  configuring MOK.

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-4
- Fix kmodgenca failure when locale country_ab2 is empty under C.UTF-8 or hybrid
  locales by providing a temporary locale wrapper that returns a valid two-letter
  country code for certificate generation.

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-3
- Fix MOK enrollment detection: parse mokutil --test-key output and only skip
  import when it explicitly reports "is already enrolled".
- Send the user-provided MOK password to mokutil --import via stdin twice and
  keep mokutil password prompts visible in the installation log.
- Recreate /tmp/nvidia-drvinst.log as root and force C.UTF-8 locale for UTF-8
  encoded log output.

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-2
- Fix log file permission error: root script now creates /tmp/nvidia-drvinst.log
  with chmod 666, GUI no longer writes to the log file directly.
- Fix missing translations: all 51 i18n strings now translated in zh_CN, zh_TW,
  ja, de, ko, fr (previously only ~32 strings were covered, causing MOK dialog
  body text to remain in English).

* Tue Aug 11 2026 KairikiFedora <packager@example.invalid> - 2.0.0-1
- Rewritten NVIDIA driver manager with GPU detection, driver branch selection,
  Secure Boot MOK support, --skip detection override, and CUDA package handling.
- Install akmods-evernight automatically at runtime when needed, use one Polkit
  authentication for the whole install flow, and add zh_CN/zh_TW/ja/de/ko/fr
  translations.
