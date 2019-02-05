SUMMARY = "Userspace tools for TOMOYO Linux 2.5.x"
DESCRIPTION = "This package contains userspace tools for administrating TOMOYO Linux 2.5.x. \
Please see https://tomoyo.osdn.jp/2.5/ for documentation."
HOMEPAGE = "https://tomoyo.osdn.jp/"
SECTION = "System Environment/Kernel"
PV = "2.5.0"

SRC_URI = "https://jaist.dl.osdn.jp/tomoyo/53357/${PN}-${PV}-20140601.tar.gz"
SRC_URI[md5sum] = "888869b793127f00d6439a3246598b83"
SRC_URI[sha256sum] = "118ef6ba1fbf7c0b83018c3a0d4d5485dfb9b5b7f647f37ce9f63841a3133c2a"

S = "${WORKDIR}/${PN}"

LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING.tomoyo;md5=751419260aa954499f7abaabaa882bbe"

FILES_${PN}     += "${libdir}/tomoyo"
FILES_${PN}-dbg += "${libdir}/tomoyo/.debug"

DEPENDS = "linux-libc-headers ncurses"

do_compile () {
    oe_runmake 'CC=${CC}'
}

do_install() {
    oe_runmake install 'INSTALLDIR=${D}'
}
