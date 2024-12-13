#pragma once


#include "d2gi_common.h"


enum WINDOWMODE
{
	WMODE_WINDOWED,
	WMODE_BORDERLESS,
	WMODE_FULLSCREEN,
};

enum IMG_FORMAT
{
	IMG_PNG,
	IMG_JPG,
	IMG_BMP,
};

class D2GIConfig
{
	static WINDOWMODE s_eWindowMode;
	static DWORD      s_dwVideoWidth, s_dwVideoHeight;
	static BOOL       s_bEnableHooks;
	static BOOL       s_bEnableVSync;
	static CHAR       s_cScreenshotsPath[256];
	static IMG_FORMAT s_eImgFormat;
public:
	static VOID ReadFromFile();

	static WINDOWMODE GetWindowMode() { return s_eWindowMode; };
	static DWORD GetVideoWidth();
	static DWORD GetVideoHeight();
	static BOOL  HooksEnabled() { return s_bEnableHooks; };
	static BOOL  VSyncEnabled() { return s_bEnableVSync; };
	static CHAR* GetScreenshotsPath() { return s_cScreenshotsPath; };
	static IMG_FORMAT GetScreenshotsFormat() { return s_eImgFormat;  }
};
