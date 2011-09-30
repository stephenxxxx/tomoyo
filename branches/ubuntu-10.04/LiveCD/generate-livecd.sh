#!/bin/sh

LIVECD_HOME=~/LiveCD/
CD_LABEL="Ubuntu 10.04.3 i386 TOMOYO 1.8.2"
ISOIMAGE_NAME=../ubuntu-10.04.3-desktop-i386-tomoyo-1.8.2p3.iso
KERNEL_VERSION=2.6.32-33-generic-pae-ccs

# set -v

die () {
    echo '********** ' $1 ' **********'
    exit 1
}

cd ${LIVECD_HOME}
echo "********** Updating root filesystem for LiveCD. **********"

echo '<kernel>' > squash/etc/ccs/domain_policy.conf
echo 'use_profile 1' >> squash/etc/ccs/domain_policy.conf

echo '<kernel>' > squash/etc/tomoyo/domain_policy.conf
echo 'use_profile 1' >> squash/etc/tomoyo/domain_policy.conf

mkdir -p -m 700 squash/var/log/tomoyo
if  ! grep -q ccs-auditd squash/etc/init.d/rc.local
then
    (
	echo 'if [ `stat -f --printf=%t /` -eq 61756673 ]'
	echo 'then'
	echo 'ccs-loadpolicy -e << EOF
initialize_domain /usr/share/ubiquity/install.py
keep_domain <kernel> /usr/share/ubiquity/install.py
EOF'
	echo 'mount -t tmpfs -o size=64m none /var/log/tomoyo/'
	echo 'fi'
	echo '/usr/sbin/ccs-auditd'
	) >> squash/etc/init.d/rc.local
fi

if ! grep -qF tomoyo.sourceforge.jp squash/etc/init.d/rc.local
then
    (
	echo 'if ! grep -qF tomoyo.sourceforge.jp /etc/apt/sources.list'
	echo 'then'
	echo 'echo "" >> /etc/apt/sources.list'
	echo 'echo "# TOMOYO Linux 1.8 kernel and tools" >> /etc/apt/sources.list'
	echo 'echo "deb http://tomoyo.sourceforge.jp/repos-1.8/Ubuntu10.04/ ./" >> /etc/apt/sources.list'
	echo 'fi'
    ) >> squash/etc/init.d/rc.local
fi

cd squash/usr/share/doc/ || die "Can't change directory."
rm -fR tomoyo/ || die "Can't delete directory."
mkdir tomoyo/ || die "Can't create directory."
cd tomoyo/ || die "Can't change directory."
wget -O ubuntu10.04-live.html.en 'http://sourceforge.jp/projects/tomoyo/svn/view/tags/htdocs/1.8/ubuntu10.04-live.html.en?revision=HEAD&root=tomoyo' || die "Can't copy document."
wget -O ubuntu10.04-live.html.ja 'http://sourceforge.jp/projects/tomoyo/svn/view/tags/htdocs/1.8/ubuntu10.04-live.html.ja?revision=HEAD&root=tomoyo' || die "Can't copy document."
wget -O - 'http://sourceforge.jp/projects/tomoyo/svn/view/tags/htdocs/1.8/media.ubuntu10.04.tar.gz?root=tomoyo&view=tar' | tar -zxf - || die "Can't copy document."
ln -s ubuntu10.04-live.html.en index.html.en
ln -s ubuntu10.04-live.html.ja index.html.ja
cd ../../../../../ || die "Can't change directory."
cp -p resources/tomoyo*.desktop squash/etc/skel/ || die "Can't copy shortcut."

rm -f squash/var/cache/apt/*.bin
rm -f squash/boot/*.bak
rm -f squash/*.deb
rm -f squash/root/.bash_history
rm -f squash/etc/resolv.conf
rm -f squash/sources.list
rm -f squash/package-install.sh
rm -f squash/var/lib/apt/lists/*_*
rm -f squash/var/cache/debconf/*-old
rm -f squash/boot/initrd.img-*-ccs

cd ${LIVECD_HOME}
echo "********** Copying kernel. **********"
cp -af squash/boot/vmlinuz-*-ccs cdrom/casper/vmlinuz || die "Can't copy kernel."

cd ${LIVECD_HOME}
echo "********** Updating initramfs. **********"
[ -e cdrom/casper/initrd.lz ] || die "Copy original initramfs image file to cdrom/casper/initrd.lz ."
rm -fR initrd/
mkdir initrd
lzcat -S .lz cdrom/casper/initrd.lz | ( cd initrd/ ; cpio -id ) || die "Can't extract initramfs."

for KERNEL_DIR in modules firmware
do
    if [ ! -d initrd/lib/${KERNEL_DIR}/${KERNEL_VERSION}/ ]
    then
	mkdir initrd/lib/${KERNEL_DIR}/${KERNEL_VERSION} || die "Can't create kernel modules directory."
	( cd initrd/lib/${KERNEL_DIR}/*-generic/ ; find ! -type d -print0 ) | ( cd squash/lib/${KERNEL_DIR}/${KERNEL_VERSION} ; xargs -0 tar -cf - ) | ( cd initrd/lib/${KERNEL_DIR}/${KERNEL_VERSION} ; tar -xf - ) || die "Can't copy kernel modules."
	rm -fR initrd/lib/${KERNEL_DIR}/*-generic || die "Can't delete kernel modules directory."
    fi
done

SETUP_SCRIPT=initrd/scripts/casper-bottom/10adduser

if ! grep -q TOMOYO ${SETUP_SCRIPT}
then
(
    echo '# --- TOMOYO Linux Project (begin) ---'
    echo 'mv /root/home/$USERNAME/tomoyo-*.desktop /root/home/$USERNAME/Desktop/'
    echo 'grep -q security=tomoyo /proc/cmdline && mv /root/home/$USERNAME/tomoyo2-editpolicy.desktop /root/home/$USERNAME/Desktop/' 
    echo '# --- TOMOYO Linux Project (end) ---'
) >> ${SETUP_SCRIPT}
fi

(cd initrd/ ; find -print0 | cpio -o0 -H newc | lzma -9 ) > cdrom/casper/initrd.lz || die "Can't update initramfs."

cd ${LIVECD_HOME}
echo "********** Generating squashfs image file. **********"
mksquashfs squash cdrom/casper/filesystem.squashfs -noappend || die "Can't generate squashfs image file."

cd ${LIVECD_HOME}
echo "********** Updating MD5 information. **********"
cd cdrom/
mv md5sum.txt md5sum.txt.old
cat md5sum.txt.old | awk '{ print $2 }' | xargs md5sum > md5sum.txt
rm -f md5sum.txt.old
[ -r .disk/casper-uuid-ccs ] || mv .disk/casper-uuid-generic .disk/casper-uuid-ccs
sed -i -e 's/casper-uuid-generic/casper-uuid-ccs/' -- md5sum.txt

cd ${LIVECD_HOME}
echo "********** Generating iso image file. **********"
cd cdrom/
mkisofs -r -V "${CD_LABEL}" -cache-inodes -J -l -b isolinux/isolinux.bin -c isolinux/boot.cat -no-emul-boot -boot-load-size 4 -boot-info-table -o ${ISOIMAGE_NAME} . || die "Can't generate iso image file."

echo "********** Done. **********"
exit 0