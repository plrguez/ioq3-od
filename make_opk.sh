#!/bin/sh

OPK_NAME=quake3.opk

echo ${OPK_NAME}

# create default.gcw0.desktop
cat > default.gcw0.desktop <<EOF
[Desktop Entry]
Name=Quake3
Comment=Quake3
Exec=ioquake3.mips
Terminal=false
Type=Games
StartupNotify=true
Icon=generic
Categories=games;
EOF

# create opk
FLIST="default.gcw0.desktop"
FLIST="${FLIST} generic.png"
FLIST="${FLIST} build/release-gcw0-mips/ioquake3.mips"
FLIST="${FLIST} build/release-gcw0-mips/ioq3ded.mips"
FLIST="${FLIST} build/release-gcw0-mips/renderer_opengl1_mips.so"

rm -f ${OPK_NAME}
mksquashfs ${FLIST} ${OPK_NAME} -all-root -no-xattrs -noappend -no-exports

cat default.gcw0.desktop
rm -f default.gcw0.desktop
