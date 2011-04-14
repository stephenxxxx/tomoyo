Summary: Userspace tools for TOMOYO Linux 2.4.x

##
## Change to /usr/lib64 if needed.
##
%define usrlibdir /usr/lib

Name: tomoyo-tools
Version: 2.4.0
Release: 1
License: GPL
Group: System Environment/Kernel
ExclusiveOS: Linux
Autoreqprov: no
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
##
## This spec file is intended for distribution independent.
## I don't enable "BuildRequires:" line because rpmbuild will fail on
## environments where packages are managed by (e.g.) apt.
##
# BuildRequires: ncurses-devel
Requires: ncurses
Conflicts: tomoyo-tools < 1.8.1-1

Source0: http://osdn.dl.sourceforge.jp/tomoyo/?????/tomoyo-tools-2.4.0-2011????.tar.gz

%description
This package contains userspace tools for administrating TOMOYO Linux 2.4.
Please see http://tomoyo.sourceforge.jp/2.4/ for documentation.

%prep

%setup -q -n tomoyotools

%build

make USRLIBDIR=%{usrlibdir} CFLAGS="-Wall $RPM_OPT_FLAGS"

%install

rm -rf $RPM_BUILD_ROOT
make INSTALLDIR=$RPM_BUILD_ROOT USRLIBDIR=%{usrlibdir} \
    CFLAGS="-Wall $RPM_OPT_FLAGS" install

%clean

rm -rf $RPM_BUILD_ROOT

%post
ldconfig || true

%files
%defattr(-,root,root)
/sbin/
%{usrlibdir}/tomoyo/
%{usrlibdir}/libtomoyo*
/usr/sbin/
/usr/share/man/man8/

%changelog
* ??? ??? ?? 2011 2.4.0-1
