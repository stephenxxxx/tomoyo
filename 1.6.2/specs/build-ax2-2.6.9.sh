#! /bin/sh
#
# This is a kernel build script for Asianux 2's 2.6.9 kernel.
#

die () {
    echo $1
    exit 1
}

cd /tmp/ || die "Can't chdir to /tmp/ ."

if [ ! -r kernel-2.6.9-42.22AX.src.rpm ]
then
    wget http://ftp.miraclelinux.com/pub/Miracle/ia32/standard/4.0/updates/SRPMS/kernel-2.6.9-42.22AX.src.rpm || die "Can't download source package."
fi
rpm -ivh kernel-2.6.9-42.22AX.src.rpm || die "Can't install source package."

cd /usr/src/asianux/SOURCES/ || die "Can't chdir to /usr/src/asianux/SOURCES/ ."
if [ ! -r ccs-patch-1.6.2-20080625.tar.gz ]
then
    wget http://osdn.dl.sourceforge.jp/tomoyo/30297/ccs-patch-1.6.2-20080625.tar.gz || die "Can't download patch."
fi

cd /tmp/ || die "Can't chdir to /tmp/ ."
cp -p /usr/src/asianux/SPECS/kernel-2.6.spec . || die "Can't copy spec file."
patch << "EOF" || die "Can't patch spec file."
--- kernel-2.6.spec	2008-05-26 15:23:44.000000000 +0900
+++ kernel-2.6.spec	2008-05-31 05:23:29.000000000 +0900
@@ -29,7 +29,7 @@
 # adding some text to the end of the version number.
 #
 %define axbsys %([ "%{?WITH_LKST}" -eq 0 ] && echo || echo .lkst)
-%define release 42.22AX%{axbsys}
+%define release 42.22AX%{axbsys}_tomoyo_1.6.2
 %define sublevel 9
 %define kversion 2.6.%{sublevel}
 %define rpmversion 2.6.%{sublevel}
@@ -131,6 +131,9 @@
 # to versions below the minimum
 #
 
+# TOMOYO Linux
+%define signmodules 0
+
 #
 # First the general kernel 2.6 required versions as per
 # Documentation/Changes
@@ -167,7 +170,7 @@
 %define __find_provides /usr/lib/rpm/asianux/find-kmod-provides.sh
 %define __find_requires %{nil}
 
-Name: kernel
+Name: ccs-kernel
 Group: System Environment/Kernel
 License: GPLv2
 Version: %{rpmversion}
@@ -4091,6 +4094,11 @@
 
 # END OF PATCH APPLICATIONS
 
+# TOMOYO Linux
+tar -zxf %_sourcedir/ccs-patch-1.6.2-20080625.tar.gz
+sed -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = -42.22AX/" -- Makefile
+patch -sp1 < patches/ccs-patch-2.6.9-42.22AX.diff
+
 cp %{SOURCE10} Documentation/
 
 mkdir configs
@@ -4102,6 +4110,9 @@
 for i in *.config 
 do 
 	mv $i .config 
+	# TOMOYO Linux
+	cat config.ccs >> .config
+	sed -i -e "s/^CONFIG_DEBUG_INFO=.*/# CONFIG_DEBUG_INFO is not set/" -- .config
 	make ARCH=`echo $i | cut -d"-" -f3 | cut -d"." -f1 | sed -e s/i.86/i386/ -e s/s390x/s390/ -e s/ppc64.series/ppc64/  ` nonint_oldconfig > /dev/null 
 	cp .config configs/$i 
 done
EOF
mv kernel-2.6.spec kernel-2.6.9-42.22AX_tomoyo_1.6.2.spec || die "Can't rename spec file."
echo ""
echo ""
echo ""
echo "Edit /tmp/kernel-2.6.9-42.22AX_tomoyo_1.6.2.spec if needed, and run"
echo "rpmbuild -bb /tmp/kernel-2.6.9-42.22AX_tomoyo_1.6.2.spec"
echo "to build kernel rpm packages."
exit 0