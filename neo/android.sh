#!/bin/bash
set -e

export PATH=/pub/android-toolchain-r8b/bin:$PATH
export ARCH=arm-linux-androideabi
export CXX=$ARCH-g++
export CC=$ARCH-gcc

scons \
	ARCH='arm' \
	BUILD='release' \
	\
	CC=$CC \
	CXX=$CXX \
	\
	NOCURL=1 \
	TARGET_ANDROID=1 \
	TARGET_D3XP=0 \
	\
	BASEFLAGS='-I/pub/android-toolchain-r8b/arm-linux-androideabi/include/c++/4.6/arm-linux-androideabi -I/pub/android-toolchain-r8b/arm-linux-androideabi/include/c++/4.6' \
	NDK='/pub/android-ndk-r8b' \
	$*
