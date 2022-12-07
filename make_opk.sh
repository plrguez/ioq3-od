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
rm -f baseq3/*.so
mksquashfs ${FLIST} ${OPK_NAME} -all-root -no-xattrs -noappend -no-exports

cat default.gcw0.desktop
rm -f default.gcw0.desktop

echo "================================================================================"
# Using shared libraries instead of qvm
# Set CVARS vm_cgame, vm_game and set vm_ui to 0 in baseq3/q3config.cfg
#   0 - VMI_NATIVE is shared libs / dlls.
#   1 - VMI_BYTECODE is qvm bytecode using the interpreter (slow).
#   2 - VMI_COMPILED is qvm bytecode using various CPU instruction sets (default, fast).
OPK_NAME=quake3-native.opk

echo ${OPK_NAME}

cat > default.gcw0.desktop <<EOF
[Desktop Entry]
Name=Quake3
Comment=Quake3 (Native Libs)
Exec=ioquake3.mips +set sv_pure 0 +set vm_cgame 0 +set vm_game 0 +set vm_ui 0
Terminal=false
Type=Game
StartupNotify=true
Icon=quake3
Categories=games;
EOF

# Copy shared libraries
rm -f baseq3/*.so
cp build/release-gcw0-mips/baseq3/*.so baseq3/

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
rm -f baseq3/*.so
