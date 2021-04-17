// Version number, written as  vV.V.BB  or  vV.V.BBaa
// (0xVVBBaa, in BCD notation)

#define VER_MAJOR  1
#define VER_MINOR  0
#define VER_BETA  0
#ifndef VER_ALPHA
	#define VER_ALPHA 0
#endif

#define BURN_VERSION (VER_MAJOR * 0x100000) + (VER_MINOR * 0x010000) + (((VER_BETA / 10) * 0x001000) + ((VER_BETA % 10) * 0x000100)) + (((VER_ALPHA / 10) * 0x000010) + (VER_ALPHA % 10))




