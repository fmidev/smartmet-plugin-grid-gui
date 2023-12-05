%bcond_without observation
%define DIRNAME grid-gui
%define SPECNAME smartmet-plugin-%{DIRNAME}
Summary: SmartMet grid-gui plugin
Name: %{SPECNAME}
Version: 23.12.5
Release: 1%{?dist}.fmi
License: MIT
Group: SmartMet/Plugins
URL: https://github.com/fmidev/smartmet-plugin-grid-gui
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
%if 0%{?rhel} && 0%{rhel} < 9
%define smartmet_boost boost169
%else
%define smartmet_boost boost
%endif

%define smartmet_fmt_min 8.1.1
%define smartmet_fmt_max 8.2.0

BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: %{smartmet_boost}-devel
BuildRequires: libconfig17-devel
BuildRequires: libwebp13-devel
BuildRequires: smartmet-utils-devel >= 23.9.6
BuildRequires: smartmet-library-spine-devel >= 23.12.5
BuildRequires: smartmet-library-grid-files-devel >= 23.12.4
BuildRequires: smartmet-library-grid-content-devel >= 23.11.27
BuildRequires: smartmet-engine-grid-devel >= 23.11.17
BuildRequires: gdal35-devel
Requires: libconfig17
Requires: smartmet-library-macgyver >= 23.11.21
Requires: smartmet-library-spine >= 23.12.5
Requires: smartmet-server >= 23.12.5
Requires: smartmet-engine-grid >= 23.11.17
Requires: %{smartmet_boost}-date-time
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
* Tue Dec  5 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.12.5-1.fmi
- Indicate being an admin plugin to the server

* Fri Nov 17 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.11.17-1.fmi
- Repackaged due to API changes in grid-files and grid-content

* Fri Nov 10 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.11.10-1.fmi
- Repackaged due to API changes in grid-content

* Mon Oct 30 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.10.30-1.fmi
- Enabling ability to block big projections

* Thu Oct 12 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.10.12-1.fmi
- Repackage due to smartmet-library-grid-files and smartmet-library-grid-files changes

* Tue Oct  3 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.10.3-1.fmi
- Improved error checking on the land-sea mask file

* Mon Oct  2 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.10.2-1.fmi
- Improved browser selection forms for very long lists of timestamps

* Fri Sep 29 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.9.29-1.fmi
- Repackaged due to changes in grid-libraries

* Mon Sep 11 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.9.11-1.fmi
- Added new land-sea mask processing

* Fri Jul 28 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.28-1.fmi
- Repackage due to bulk ABI changes in macgyver/newbase/spine

* Tue Jul 11 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.11-1.fmi
- Use postgresql 15, gdal 3.5, geos 3.11 and proj-9.0

* Tue Jun  6 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.6.6-1.fmi
- Repackaged due to GRID ABI changes

* Mon Apr 17 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.4.17-1.fmi
- Minor fixes

* Wed Feb 22 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.2.22-1.fmi
- Fixed dependencies

* Thu Jan 19 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.1.19-1.fmi
- Added streamlines and streamline animation
- Improved session control

* Mon Dec 12 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.12.12-1.fmi
- Repackaged due to ABI changes

* Mon Dec  5 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.12.5-1.fmi
- Check HTTP request type and handle only POST and OPTIONS requests

* Tue Nov  8 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.11.8-1.fmi
- Repackaged due to base library ABI changes

* Thu Oct 20 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.20-1.fmi
- Repackaged due to base library ABI changes

* Mon Oct 10 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.10-1.fmi
- Added server information

* Wed Oct  5 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.5-1.fmi
- Do not use boost::noncopyable

* Wed Aug 24 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.8.24-1.fmi
- Repackaged since ABI changed in ServiceImplementation

* Tue Aug 23 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.8.23-1.fmi
- Repackaged due to library ABI changes

* Tue Jun 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.6.21-1.fmi
- Add support for RHEL9, upgrade libpqxx to 7.7.0 (rhel8+) and fmt to 8.1.1

* Tue May 24 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.5.24-1.fmi
- Repackaged due to NFmiArea ABI changes

* Thu Apr 28 2022 Andris Pavenis <andris.pavenis@fmi.fi> 22.4.28-1.fmi
- Repackage due to SmartMet::Spine::Reactor ABI changes

* Mon Mar 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.28-1.fmi
- Repackaged due to ABI changes in grid-content library

* Thu Mar 10 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.10-1.fmi
- Repackaged due to base library ABI changes

* Mon Mar  7 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.7-1.fmi
- Repackaged due to base library API changes

* Mon Feb 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.2.28-1.fmi
- Repackaged due to base library/engine ABI changes

* Wed Feb 16 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.2.16-1.fmi
- The plugin is now private and not seen by the frontentds

* Wed Feb  9 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.2.9-1.fmi
- Repackaged due to ABI changes in grid libraries

* Tue Jan 25 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.1.25-1.fmi
- Improved handling of unfinished generations

* Fri Jan 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.1.21-1.fmi
- Repackage due to upgrade of packages from PGDG repo: gdal-3.4, geos-3.10, proj-8.2

* Wed Jan  5 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.1.5-1.fmi
- Repackaged GRID-components

* Tue Dec  7 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.12.7-1.fmi
- Update to postgresql 13 and gdal 3.3

* Mon Nov 15 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.11.15-1.fmi
- Repackaged due to ABI changes in base libraries

* Fri Oct 29 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.29-1.fmi
- Repackaged due to ABI changes in base grid libraries

* Tue Oct 19 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.19-1.fmi
- Added low level filtering capability

* Mon Oct 11 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.11-1.fmi
- Simplified grid storage structures

* Mon Oct  4 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.4-1.fmi
- Repackaged due to grid-files ABI changes

* Mon Sep 27 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.9.27-1.fmi
- Repackage due to dependency change (libgonfig++)

* Wed Sep 15 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.9.15-1.fmi
- Repackaged due to NetCDF related ABI changes in base libraries

* Thu Sep  9 2021 Andris Pavenis <andris.pavenis@fmi.fi> 21.9.9-1.fmi
- Repackage due to dependency change (libconfig->libconfig17)

* Tue Aug 31 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.31-1.fmi
- Repackaged due to Spine ABI changes

* Tue Aug 17 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.17-1.fmi
- Use the new shutdown API

* Tue Jun  8 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.8-1.fmi
- Repackaged due to memory saving ABI changes in base libraries

* Tue Jun  1 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.1-1.fmi
- Repackaged due to ABI changes in grid libraries

* Tue May 25 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.25-1.fmi
- Repackaged due to grid-files API changes

* Thu Apr  1 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.4.1-1.fmi
- Repackaged due to grid-files API changes

* Fri Mar  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.3.5-1.fmi
- Detect whether the grid-engine is disabled

* Wed Mar  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.3.3-1.fmi
- Repackaged due to grid-files API changes

* Wed Feb  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.3-1.fmi
- Repackaged due to base library API changes

* Wed Jan 27 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.27-1.fmi
- Repackaged due to base library ABI changes

* Wed Jan 20 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.20-1.fmi
- Performance improvements

* Tue Jan 19 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.19-1.fmi
- Repackaged due to base library ABI changes

* Thu Jan 14 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.14-1.fmi
- Repackaged smartmet to resolve debuginfo issues

* Mon Jan 11 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.11-1.fmi
- Repackaged due to grid-files API changes

* Mon Jan  4 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.4-1.fmi
- Repackaged due to base library API changes

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
