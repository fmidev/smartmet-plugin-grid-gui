%bcond_without observation
%define DIRNAME grid-gui
%define SPECNAME smartmet-plugin-%{DIRNAME}
Summary: SmartMet grid-gui plugin
Name: %{SPECNAME}
Version: 18.8.31
Release: 1%{?dist}.fmi
License: MIT
Group: SmartMet/Plugins
URL: https://github.com/fmidev/smartmet-plugin-grid-gui
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost-devel
BuildRequires: libconfig-devel
BuildRequires: smartmet-library-spine-devel
BuildRequires: smartmet-library-grid-files-devel
BuildRequires: smartmet-library-grid-content-devel
BuildRequires: smartmet-engine-grid-devel
BuildRequires: gdal-devel
Requires: libconfig
Requires: smartmet-library-macgyver >= 18.8.20
Requires: smartmet-library-spine >= 18.8.30
Requires: smartmet-server >= 18.8.22
Requires: smartmet-engine-grid >= 18.8.30
Requires: boost-date-time
Requires: smartmet-engine-grid
Provides: %{SPECNAME}

%description
SmartMet grid-gui plugin

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}

%build -q -n %{SPECNAME}
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0775,root,root,0775)
%{_datadir}/smartmet/plugins/%{DIRNAME}.so

%changelog
* Fri Aug 31 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.31-1.fmi
- Silenced CodeChecker warnings
* Tue Aug 28 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.28-1.fmi
- Packaged latest version
* Mon Aug 27 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.27-1.fmi
- Packaged latest version
* Thu Jun 14 2018 Roope Tervo <roope.tervo@fmi.fi> - 18.6.14-1.fmi
- Build for grid support testing
* Thu Feb 8 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.2.8-1.fmi
- Initial build
