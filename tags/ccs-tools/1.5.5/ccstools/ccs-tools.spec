Summary: TOMOYO Linux tools

Name: ccs-tools
Version: 1.5.5
Release: 1
License: GPL
Group: System Environment/Kernel
ExclusiveOS: Linux
Autoreqprov: no
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Conflicts: ccs-tools < 1.5.5-1

Source0: http://osdn.dl.sourceforge.jp/tomoyo/27220/ccs-tools-1.5.5-20080903.tar.gz

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
/usr/sbin/
/usr/share/man/
%attr(4755,root,root) /usr/lib/ccs/misc/proxy

%changelog
* Wed Sep 03 2008 1.5.5-1
- Minor update release.

* Sat May 10 2008 1.5.4-1
- Minor update release.

* Thu Jan 31 2008 1.5.3-1
- Minor update release.

* Wed Dec 05 2007 1.5.2-1
- Minor update release.
- Added manpage.

* Thu Oct 19 2007 1.5.1-1
- Minor update release.

* Thu Sep 20 2007 1.5.0-1
- First-release.