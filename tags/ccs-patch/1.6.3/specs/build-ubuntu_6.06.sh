#! /bin/sh
#
# This is kernel build script for ubuntu 6.06's 2.6.15 kernel.
#

die () {
    echo $1
    exit 1
}

VERSION=`uname -r | cut -d - -f 1,2`

apt-get -y install wget
wget -O - 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x0A0AC927' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x17063E6D' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x174BF01A' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x191FCD8A' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x60E80B5B' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x63549F8E' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x76682A37' 'http://pgp.nic.ad.jp/pks/lookup?op=get&search=0x8BF9EFE6' | gpg --import || die "Can't import PGP key."

# Download TOMOYO Linux patches.
mkdir -p /usr/src/rpm/SOURCES/
cd /usr/src/rpm/SOURCES/ || die "Can't chdir to /usr/src/rpm/SOURCES/ ."
if [ ! -r ccs-patch-1.6.3-20080715.tar.gz ]
then
    wget http://osdn.dl.sourceforge.jp/tomoyo/30297/ccs-patch-1.6.3-20080715.tar.gz || die "Can't download patch."
fi

# Install kernel source packages.
cd /usr/src/ || die "Can't chdir to /usr/src/ ."
apt-get install linux-kernel-devel fakeroot build-essential || die "Can't install packages."
apt-get build-dep linux-image-${VERSION}-686 || die "Can't install packages."
apt-get source linux-image-${VERSION}-686 || die "Can't install kernel source."
apt-get build-dep linux-restricted-modules-${VERSION}-686 || die "Can't install packages."
apt-get source linux-restricted-modules-${VERSION}-686 || die "Can't install kernel source."

# Apply patches and create kernel config.
cd linux-source-2.6.15-2.6.15/ || die "Can't chdir to linux-source-2.6.15-2.6.15/ ."
tar -zxf /usr/src/rpm/SOURCES/ccs-patch-1.6.3-20080715.tar.gz || die "Can't extract patch."
patch -p1 < patches/ccs-patch-2.6.15.7-ubuntu1.diff || die "Can't apply patch."
cat debian/config/i386/config.686 config.ccs > debian/config/i386/config.686-ccs || die "Can't create config."
awk ' BEGIN { flag = 0; print ""; } { if ( $1 == "Package:") { if ( index($2, "-686") > 0) { flag = 1; $2 = $2 "-ccs"; } else flag = 0; }; if (flag) print $0; } ' debian/control.stub > debian/control.stub.tmp || die "Can't create file."
cat debian/control.stub.tmp >> debian/control.stub || die "Can't edit file."
cat debian/control.stub.tmp >> debian/control || die "Can't edit file."
chmod +x debian/post-install || die "Can't chmod post-install ."
chmod -R +x debian/bin/ || die "Can't chmod debian/bin/ ."

# Start compilation.
export CONCURRENCY_LEVEL=`grep -c '^processor' /proc/cpuinfo` || die "Can't export."
debian/rules binary-debs flavours=686-ccs || die "Failed to build kernel package."

# Install header package for compiling additional modules.
dpkg -i debian/build/linux-headers-${VERSION}*.deb || die "Can't install packages."
ln -sf asm-i386 /usr/src/linux-headers-${VERSION}-686-ccs/include/asm || die "Can't create symlink."
cd /usr/src/linux-restricted-modules-2.6.15-2.6.15.12/ || die "Can't chdir to /usr/src/linux-restricted-modules-2.6.15-2.6.15.12/ ."
awk ' BEGIN { flag = 0; print ""; } { if ( $1 == "Package:") { if ( index($2, "-686") > 0) flag = 1; else flag = 0; }; if (flag) print $0; } ' debian/control.stub.in | sed -e 's:-686:-686-ccs:g' > debian/control.stub.in.tmp || die "Can't create file."
cat debian/control.stub.in.tmp >> debian/control.stub.in || die "Can't edit file."
debian/rules debian/control || die "Can't run control."
debian/rules binary || die "Failed to build kernel package."

exit 0