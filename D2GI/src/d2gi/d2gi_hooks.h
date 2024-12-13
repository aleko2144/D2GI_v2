#pragma once

#include "../common/common.h"
#include "../common/m3x4.h"

//#include "d2gi_common.h"


class D2GIHookInjector
{
	enum D2VERSION
	{
		D2V_UNKNOWN = -1,
		D2V_8_1,
		D2V_8_2,
	};

	static D2VERSION s_eCurrentD2Version;

	static D2GI* ObtainD2GI();
	static INT __stdcall SetupTransforms(VOID* pThis, MAT3X4* pmView, MAT3X4* pmProj);
	static INT CallOriginalSetupTransforms(VOID* pThis, MAT3X4* pmView, MAT3X4* pmProj);

	static D2VERSION DetectD2Version();
	static BOOL PatchCallOperation(DWORD dwOperationAddress, DWORD dwNewCallAddress);
	static void __cdecl D2GIHookInjector::ScreenshotHook(void *a2);
public:
	static VOID InjectHooks();
};
