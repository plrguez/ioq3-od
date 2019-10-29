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
Type=Game
StartupNotify=true
Icon=quake3
Categories=games;
EOF

# create opk
FLIST="default.gcw0.desktop"
FLIST="${FLIST} quake3.png"
FLIST="${FLIST} build/release-gcw0-mips/ioquake3.mips"
FLIST="${FLIST} build/release-gcw0-mips/ioq3ded.mips"
FLIST="${FLIST} build/release-gcw0-mips/renderer_opengl1_mips.so"
FLIST="${FLIST} baseq3"

rm -f ${OPK_NAME}
mksquashfs ${FLIST} ${OPK_NAME} -all-root -no-xattrs -noappend -no-exports

cat default.gcw0.desktop
rm -f default.gcw0.desktop
