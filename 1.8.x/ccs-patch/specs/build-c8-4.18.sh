#! /bin/sh
#
# This is a kernel build script for CentOS 8's 4.18 kernel.
#

die () {
    echo $1
    exit 1
}

cd /tmp/ || die "Can't chdir to /tmp/ ."

if [ ! -r kernel-4.18.0-80.7.1.el8_0.src.rpm ]
then
    wget http://vault.centos.org/centos/8/BaseOS/Source/SPackages/kernel-4.18.0-80.7.1.el8_0.src.rpm || die "Can't download source package."
fi
LANG=C rpm --checksig kernel-4.18.0-80.7.1.el8_0.src.rpm | grep -F ': digests signatures OK' || die "Can't verify signature."
rpm -ivh kernel-4.18.0-80.7.1.el8_0.src.rpm || die "Can't install source package."

cd ~/rpmbuild/SOURCES/ || die "Can't chdir to ~/rpmbuild/SOURCES/ ."
if [ ! -r ccs-patch-1.8.6-20191111.tar.gz ]
then
    wget -O ccs-patch-1.8.6-20191111.tar.gz 'https://osdn.jp/frs/redir.php?f=/tomoyo/49684/ccs-patch-1.8.6-20191111.tar.gz' || die "Can't download patch."
fi

cd ~/rpmbuild/SPECS/ || die "Can't chdir to ~/rpmbuild/SPECS/ ."
cp -p kernel.spec ccs-kernel.spec || die "Can't copy spec file."
patch << "EOF" || die "Can't patch spec file."
--- ccs-kernel.spec
+++ ccs-kernel.spec
@@ -30,7 +30,7 @@
 %global zipsed -e 's/\.ko$/\.ko.xz/'
 %endif
 
-# define buildid .local
+%define buildid _tomoyo_1.8.6
 
 %define rpmversion 4.18.0
 %define pkgrelease 80.7.1.el8_0
@@ -882,6 +882,10 @@
 
 # END OF PATCH APPLICATIONS
 
+# TOMOYO Linux
+tar -zxf %_sourcedir/ccs-patch-1.8.6-20191111.tar.gz
+patch -sp1 < patches/ccs-patch-4.18-centos-8.diff
+
 # Any further pre-build tree manipulations happen here.
 
 chmod +x scripts/checkpatch.pl
@@ -1009,6 +1013,18 @@
     cp %{SOURCE11} certs/.
     %endif
 
+    # TOMOYO Linux 2.5
+    sed -i -e 's/# CONFIG_SECURITY_PATH is not set/CONFIG_SECURITY_PATH=y/' -- .config
+    sed -i -e 's/# CONFIG_SECURITY_TOMOYO is not set/CONFIG_SECURITY_TOMOYO=y/' -- .config
+    echo 'CONFIG_SECURITY_TOMOYO_MAX_ACCEPT_ENTRY=2048' >> .config
+    echo 'CONFIG_SECURITY_TOMOYO_MAX_AUDIT_LOG=1024' >> .config
+    echo '# CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER is not set' >> .config
+    echo 'CONFIG_SECURITY_TOMOYO_POLICY_LOADER="/sbin/tomoyo-init"' >> .config
+    echo 'CONFIG_SECURITY_TOMOYO_ACTIVATION_TRIGGER="/usr/lib/systemd/systemd"' >> .config
+    echo '# CONFIG_DEFAULT_SECURITY_TOMOYO is not set' >> .config
+    # TOMOYO Linux 1.8
+    sed -e 's@/sbin/init@/usr/lib/systemd/systemd@' -- config.ccs >> .config
+
     Arch=`head -1 .config | cut -b 3-`
     echo USING ARCH=$Arch
 
EOF
echo ""
echo ""
echo ""
echo "Edit ~/rpmbuild/SPECS/ccs-kernel.spec if needed, and run"
echo "rpmbuild -bb ~/rpmbuild/SPECS/ccs-kernel.spec"
echo "to build kernel rpm packages."
echo ""
ARCH=`uname -m`
echo "I'll start 'rpmbuild -bb --target $ARCH --with baseonly --without debug --without debuginfo ~/rpmbuild/SPECS/ccs-kernel.spec' in 30 seconds. Press Ctrl-C to stop."
sleep 30
exec rpmbuild -bb --target $ARCH --with baseonly --without debug --without debuginfo ~/rpmbuild/SPECS/ccs-kernel.spec
exit 0
