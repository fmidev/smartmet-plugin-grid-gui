%bcond_without observation
%define DIRNAME grid-gui
%define SPECNAME smartmet-plugin-%{DIRNAME}
Summary: SmartMet grid-gui plugin
Name: %{SPECNAME}
Version: 20.12.29
Release: 1%{?dist}.fmi
License: MIT
Group: SmartMet/Plugins
URL: https://github.com/fmidev/smartmet-plugin-grid-gui
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost169-devel
BuildRequires: libconfig-devel
BuildRequires: smartmet-library-spine-devel
BuildRequires: smartmet-library-grid-files-devel >= 20.12.28
BuildRequires: smartmet-library-grid-content-devel >= 20.12.28
BuildRequires: smartmet-engine-grid-devel >= 20.12.28
BuildRequires: gdal32-devel
Requires: libconfig
Requires: smartmet-library-macgyver >= 20.12.15
Requires: smartmet-library-spine >= 20.12.15
Requires: smartmet-server >= 20.10.28
Requires: smartmet-engine-grid >= 20.12.28
Requires: boost169-date-time
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
* Tue Dec 29 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.29-1.fmi
- GDAL upgrade

* Thu Dec  3 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.3-1.fmi
- Repackaged due to library ABI changes

* Mon Nov 30 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.11.30-1.fmi
- Repackaged due to grid-content library API changes

* Tue Nov 24 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.11.24-1.fmi
- Print data deletion time

* Thu Oct 22 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.22-1.fmi
- Repackaged due to library ABI changes

* Thu Oct 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.15-1.fmi
- Minor fixes

* Wed Oct  7 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.7-1.fmi
- Repackaged due to library ABI changes

* Thu Oct  1 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.1-1.fmi
- Repackaged due to library ABI changes

* Wed Sep 23 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.23-1.fmi
- Use Fmi::Exception instead of Spine::Exception

* Fri Sep 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.18-1.fmi
- Small updates to parameter handling

* Tue Sep 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.15-1.fmi
- Minor updates

* Mon Sep 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.14-1.fmi
- Repackaged due to library ABI changes

* Tue Sep  8 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.8-1.fmi
- Updated grid coordinate handling

* Mon Aug 31 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.31-1.fmi
- Repackaged due to library ABI changes

* Fri Aug 21 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.21-1.fmi
- Upgrade to fmt 6.2

* Fri Aug 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.14-1.fmi
- Repackaged due to grid library ABI changes

* Mon Jun  8 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.6.8-1.fmi
- Repackaged due to base library changes

* Fri May 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.15-1.fmi
- Added forecast type descriptions

* Tue May  5 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.5-1.fmi
- Added content type headers

* Thu Apr 30 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.30-1.fmi
- Added grib-id into FMI key when FMI parameter name is missing

* Sat Apr 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.18-1.fmi
- Upgraded to Boost 1.69

* Fri Apr  3 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.3-1.fmi
- Repackaged due to library API changes

* Wed Mar 11 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.3.10-1.fmi
- Added thread protection for image caching

* Thu Mar  5 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.3.5-1.fmi
- Minor improvements in HTML table output

* Tue Feb 25 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.25-1.fmi
- Hiding generations whose status is not ready

* Wed Feb 19 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.19-1.fmi
- Added native message view and download
- Minor fixes

* Wed Jan 29 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.29-1.fmi
- Repackaged due to library API changes

* Tue Jan 21 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.21-1.fmi
- Repackaged due to grid-content and grid-engine API changes

* Thu Jan 16 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.16-1.fmi
- Repackaged due to base library API changes

* Wed Dec 11 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.11-1.fmi
- Repackaged due to small API changes in base libraries

* Wed Dec  4 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.4-1.fmi
- Added error checking for grid values fetching

* Fri Nov 22 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.11.22-1.fmi
- Repackaged due to API changes in grid-content library

* Wed Nov 20 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.11.20-1.fmi
- Repackaged all due to newbase/spine ABI changes

* Thu Nov  7 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.11.7-1.fmi
- Minor fixes

* Fri Oct 25 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.10.25-1.fmi
- Added file size and position printout

* Tue Oct  1 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.10.1-1.fmi
- Repackaged due to SmartMet library ABI changes

* Thu Sep 19 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.19-1.fmi
- New release version

* Fri Aug  9 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.8.9-1.fmi
- Increasing the height of the animation bar

* Mon May  6 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.5.6-1.fmi
- Support for downloading GRIB files

* Tue Apr  2 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.4.2-1.fmi
- Added out of bounds checking for empty data

* Tue Mar 19 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.3.19-1.fmi
- Repackaged due to API changes

* Fri Feb 15 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.2.15-1.fmi
- Version update

* Thu Jan 17 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.1.17-1.fmi
- Version update

* Tue Oct 23 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.10.23-1.fmi
- Added projection conversions

* Mon Oct 15 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.10.15-1.fmi
- Added documentation links
- Added module introduction

* Wed Sep 26 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.26-2.fmi
- Cache fix release - include modification times in cache keys

* Wed Sep 26 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.26-1.fmi
- Version update

* Mon Sep 10 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.10-1.fmi
- Version update

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
