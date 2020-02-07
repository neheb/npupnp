Summary: UPnP base library
Name: libnpupnp
Version: 2.0.10
Release: 1%{?dist}
License: BSD
Group: Application/Multimedia
URL: http://www.lesbonscomptes.com/upmpdcli/
Source: http://www.lesbonscomptes.com/upmpdcli/downloads/%{name}-%{version}.tar.gz
BuildRequires:  expat-devel
BuildRequires:  libcurl-devel
BuildRequires:  libmicrohttpd-devel
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
UPnP library, based on pupnp code, extensively
rewritten. As its predecessor, it provides developers with an
API and open source code for building control points, devices, and
bridges.

%prep
%setup -q

%build
%configure
%{__make} %{?_smp_mflags}

%install
%{__rm} -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot} STRIP=/bin/true INSTALL='install -p'
%{__rm} -f %{buildroot}%{_libdir}/libnpupnp.a
%{__rm} -f %{buildroot}%{_libdir}/libnpupnp.la

%clean
%{__rm} -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_includedir}/npupnp
%{_libdir}/libnpupnp.so*
%{_libdir}/pkgconfig/*.pc

%changelog
* Fri Feb 07 2020 Jean-Francois Dockes <jf@dockes.org> - 2.0.1-1
- Small fix in message format
* Wed Feb 05 2020 Jean-Francois Dockes <jf@dockes.org> - 2.0.0-1
- V 2 changes API and removes dep on libixml
* Tue Jan 28 2020 Jean-Francois Dockes <jf@dockes.org> - 1.0.0-1
- Initial version

