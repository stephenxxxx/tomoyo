Summary: TOMOYO Linux tools

Name: ccs-tools
Version: 1.5.0
Release: 1
License: GPL
Group: System Environment/Kernel
ExclusiveArch: i386
ExclusiveOS: Linux
Autoreqprov: no
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Conflicts: ccs-tools < 1.5.0-0

Source0: http://osdn.dl.sourceforge.jp/tomoyo/27220/ccs-tools-1.5.0-20070920.tar.gz

%description
This is TOMOYO Linux tools.

%prep

%setup -q -n ccstools

%build

make -s all

%install

make -s install INSTALLDIR=%{buildroot}

%clean

rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
/sbin/ccs-init
/sbin/tomoyo-init
/usr/lib/ccs/
%attr(4755,root,root) /usr/lib/ccs/misc/proxy

%changelog
* Thu Sep 20 2007 1.5.0-1
- First-release.