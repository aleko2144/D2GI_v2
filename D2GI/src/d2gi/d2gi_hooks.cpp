
#include "../common/common.h"
#include "../common/logger.h"
#include "../common/dir.h"

#include "../detours/CPatch.h"

#include "d2gi.h"
#include "d2gi_device.h"
#include "d2gi_hooks.h"
#include "d2gi_config.h"

#include "d2gi_common.h"

// Normally a bad practice, but wincodec.h expects some D3D9 types in the global scope, so provide them.
using namespace D3D9;

#include "ScreenGrab/ScreenGrab9.h"
#include <wincodec.h>

#include <wrl/client.h>

#define CALL_INSTRUCTION_SIZE 5
#define OPCODE_SIZE           1


D2GIHookInjector::D2VERSION D2GIHookInjector::s_eCurrentD2Version;


D2GI* D2GIHookInjector::ObtainD2GI()
{
	//IDirect3DDevice7 *g_pDirect3DDevice
	static DWORD c_adwDeviceAddresses[] =
	{
		0x71F868,
		0x720908,
		0x720928
	};

	DWORD                   dwDevAddr = c_adwDeviceAddresses[s_eCurrentD2Version];
	D3D7::IDirect3DDevice7* pDev      = *(D3D7::IDirect3DDevice7**)dwDevAddr;

	if (pDev == NULL)
		return NULL;

	return ((D2GIDevice*)pDev)->GetD2GI();
}


INT D2GIHookInjector::SetupTransforms(VOID* pThis, MAT3X4* pmView, MAT3X4* pmProj)
{
	D2GI* pD2GI = ObtainD2GI();
	MAT3X4 mPatchedView = *pmView, mPatchedProj = *pmProj;

	if(pD2GI != NULL)
		pD2GI->OnTransformsSetup(pThis, &mPatchedView, &mPatchedProj);

	return CallOriginalSetupTransforms(pThis, &mPatchedView, &mPatchedProj);
}


__declspec(naked) VOID SetupTransformsHook()
{
	__asm
	{
		mov eax, [esp + 8];
		push eax;
		mov eax, [esp + 8];
		push eax;
		push ecx;
		call D2GIHookInjector::SetupTransforms;
		ret 8;
	};
}


INT D2GIHookInjector::CallOriginalSetupTransforms(VOID* pThis, MAT3X4* pmView, MAT3X4* pmProj)
{
	static DWORD c_adwSetupTransformsAddresses[] =
	{
		0x5AD7F0, 0x5AE0E0, 0x5AE070
	};

	INT nResult, nAddr = c_adwSetupTransformsAddresses[s_eCurrentD2Version];

	__asm
	{
		push ecx;
		push eax;

		mov ecx, pThis;
		push pmProj;
		push pmView;
		call nAddr;

		mov nResult, eax;
		pop eax;
		pop ecx;
	};

	return nResult;
}


D2GIHookInjector::D2VERSION D2GIHookInjector::DetectD2Version()
{
	static DWORD c_adwTimestamps[] =
	{
		0x3C970FF7, //v 1.3 - 19.03.2002 [EN/GOG]
      //0x3E3E392B, //v 8.0 - 03.02.2003 [RU]
		0x400502EA, //v 8.1 - 14.01.2004 [RU/GOG]
		0x4760F7AC  //v 8.2 - 13.12.2007 [RU]
	};

	FILE* pFile;
	IMAGE_DOS_HEADER sDOSHeader;
	IMAGE_FILE_HEADER sImageHeader;
	INT i;

	pFile = _tfopen(Directory::GetEXEPath(), TEXT("rb"));
	if (pFile == NULL)
	{
		Logger::Warning(
			TEXT("Failed to open D2 EXE file to detect version (%s)"), Directory::GetEXEPath());
		return D2V_UNKNOWN;
	}

	fread(&sDOSHeader, sizeof(sDOSHeader), 1, pFile);
	fseek(pFile, sDOSHeader.e_lfanew + 4, SEEK_SET);
	fread(&sImageHeader, sizeof(sImageHeader), 1, pFile);
	fclose(pFile);

	for (i = 0; i < ARRAYSIZE(c_adwTimestamps); i++)
		if (c_adwTimestamps[i] == sImageHeader.TimeDateStamp)
			return (D2VERSION)i;

	return D2V_UNKNOWN;
}


BOOL D2GIHookInjector::PatchCallOperation(DWORD dwOperationAddress, DWORD dwNewCallAddress)
{
	INT nCallOffset;

	nCallOffset = (INT32)dwNewCallAddress;
	nCallOffset -= (INT32)dwOperationAddress + CALL_INSTRUCTION_SIZE;

	return WriteProcessMemory(GetCurrentProcess(), 
		(BYTE*)dwOperationAddress + OPCODE_SIZE, &nCallOffset, sizeof(nCallOffset), NULL);
}

class CCoInitialize
{
public:
	CCoInitialize(DWORD dwCoInit) : m_hr(CoInitializeEx(NULL, dwCoInit)) {}
	~CCoInitialize() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
	operator HRESULT() const { return m_hr; }

private:
	const HRESULT m_hr;
};

//struct tagBITMAPINFO *__cdecl WritePhotoToFile(FILE *a2) (0x5E9EA0)
void __cdecl D2GIHookInjector::WriteScreenshotFunc(void *a2)
{
	D2GI* pD2GI = ObtainD2GI();

	if (pD2GI == NULL) {
		Logger::Warning(TEXT("ScreenshotHook->!pD2GI"));
		return;
	}

	CCoInitialize coInit(COINIT_MULTITHREADED);
	if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
	{
		return;
	}

	using namespace Microsoft::WRL;

	ComPtr<D3D9::IDirect3DDevice9> device(pD2GI->GetD3D9Device());
	
	ComPtr<D3D9::IDirect3DSurface9> backbuffer;
	if (FAILED(device->GetRenderTarget(0, backbuffer.GetAddressOf())))
	{
		return;
	}

	D3D9::D3DSURFACE_DESC desc;
	if (FAILED(backbuffer->GetDesc(&desc)))
	{
		return;
	}

	ComPtr<D3D9::IDirect3DSurface9> buffer;
	if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3D9::D3DPOOL_SYSTEMMEM, buffer.GetAddressOf(), nullptr)))
	{
		return;
	}
	
	if (FAILED(device->GetRenderTargetData(backbuffer.Get(), buffer.Get())))
	{
		return;
	}

	CreateDirectoryW(D2GIConfig::GetScreenshotsPath(), nullptr);

	const wchar_t* extension;
	const GUID* imageContainerFormat;
	switch (D2GIConfig::GetScreenshotsFormat())
	{
	case IMG_PNG:
		extension = L"png";
		imageContainerFormat = &GUID_ContainerFormatPng;
		break;
	case IMG_JPG:
		extension = L"jpg";
		imageContainerFormat = &GUID_ContainerFormatJpeg;
		break;
	case IMG_BMP:
	default:
		extension = L"bmp";
		imageContainerFormat = &GUID_ContainerFormatBmp;
		break;
	}

	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);

	wchar_t screenshot_path[MAX_PATH];
	swprintf_s(screenshot_path, L"%ls\\ddphoto_%u-%02u-%02u_%02u-%02u-%02u.%ls", D2GIConfig::GetScreenshotsPath(),
		systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, extension);

	if (SUCCEEDED(DirectX::SaveWICTextureToFile(buffer.Get(), *imageContainerFormat, screenshot_path)))
	{
		Logger::Log(TEXT("Screenshot saved."));
	}
}

void D2GIHookInjector::InjectScreenshotsPatch() {
	DWORD scr_WriteAddr;
	DWORD scr_mkdirAddr;
	DWORD scr_fopenAddr;
	DWORD scr_fcloseAddr;

	switch (s_eCurrentD2Version) {
	case D2V_1_3:
		scr_WriteAddr  = 0x575B92;
		scr_mkdirAddr  = 0x575B5E;
		scr_fopenAddr  = 0x575B8A;
		scr_fcloseAddr = 0x575B98;
		break;

	case D2V_8_1:
		scr_WriteAddr  = 0x5764C2;
		scr_mkdirAddr  = 0x57648E;
		scr_fopenAddr  = 0x5764BA;
		scr_fcloseAddr = 0x5764C8;
		break;

	case D2V_8_2:
		scr_WriteAddr  = 0x576452;
		scr_mkdirAddr  = 0x57641E;
		scr_fopenAddr  = 0x57644A;
		scr_fcloseAddr = 0x576458;
		break;
	}

	bool result = PatchCallOperation(scr_WriteAddr, (DWORD)WriteScreenshotFunc);

	//_mkdir(".\\screenshots");
	CPatch::Nop(scr_mkdirAddr, 5);

	//v3 = fopen(&Dest, "wb");
	CPatch::Nop(scr_fopenAddr, 5);

	//fclose(v3);
	CPatch::Nop(scr_fcloseAddr, 5);

	if (result)
		Logger::Log(TEXT("Successfully injected screenshots hook"));
	else
		Logger::Log(TEXT("Unable to hook screenshots function"));
}

//selected item ID in mainMenu->graphics->screenResolution
int menuSettingsValue;
//interface aspect (for interiors FOV fix)
float ui_aspect;

//resolution forced by D2GI
int xres, yres;

void D2GIHookInjector::OnPrepareStartGame() {
	//overwrite selected resolution in the game settings to 1024x768
	// hex   int  &1F0
	//0x110 (274) (272) 640x480
	//0x120 (290) (288) 800x600
	//0x130 (306) (304) 1024x768
	//0x140 (322) (320) 1600x900

	int dw_PrepareStartGame, *dw_TheGamePtr;

	switch (s_eCurrentD2Version) {
	case D2V_1_3:
		dw_TheGamePtr = (int*)0x695C00;
		dw_PrepareStartGame = 0x5126D0;

		*(int *)(*(int*)0x6CDBD0 + 408) = menuSettingsValue;
		break;

	case D2V_8_1:
		dw_TheGamePtr = (int*)0x696CA0;
		dw_PrepareStartGame = 0x512EA0;

		*(int *)(*(int*)0x6CEC70 + 408) = menuSettingsValue;
		break;

	case D2V_8_2:
		dw_TheGamePtr = (int*)0x696CC0;
		dw_PrepareStartGame = 0x512E00;

		//MenuVideo->selected resolution index
		*(int *)(*(int*)0x6CEC90 + 408) = menuSettingsValue;
		break;
	}

	//run original function
	signed int*(*PrepareStartGame)(int*) = (signed int *(*)(int*))dw_PrepareStartGame;
	PrepareStartGame(dw_TheGamePtr);
}

void D2GIHookInjector::OnSetupUIOffsets(){
	switch (s_eCurrentD2Version) {
	case D2V_1_3:
		//call original function
		((void(*)(int))0x510750)(0);

		//fix sidebar positions
		//BackInfoX
		*(int*)0x6CDC7C = xres - 385;
		//MainSidebarX
		*(int*)0x6CDC74 = xres - 225;

		//fuel menu
		*(int *)(*(int *)(*(int*)0x6CDC0C + 0x38) + 0x2C) = (xres - 1024) / 2;
		//multiplayer menu
		*(int *)(*(int *)(*(int*)0x6CDC14 + 0x38) + 0x2C) = (xres - 1024) / 2;
		break;

	case D2V_8_1:
		//call original function
		((void(*)(int))0x510F20)(0);

		//fix sidebar positions
		//BackInfoX
		*(int*)0x6CED1C = xres - 385;
		//MainSidebarX
		*(int*)0x6CED14 = xres - 225;

		//fuel menu
		*(int *)(*(int *)(*(int*)0x6CECAC + 0x38) + 0x2C) = (xres - 1024) / 2;
		//multiplayer menu
		*(int *)(*(int *)(*(int*)0x6CECB4 + 0x38) + 0x2C) = (xres - 1024) / 2;
		break;

	case D2V_8_2:
		//call original function
		((void(*)(int))0x510E80)(0);

		//fix sidebar positions
		//BackInfoX
		*(int*)0x6CED3C = xres - 385;
		//MainSidebarX
		*(int*)0x6CED34 = xres - 225;

		//fuel menu
		*(int *)(*(int *)(*(int*)0x6CECCC + 0x38) + 0x2C) = (xres - 1024) / 2;
		//multiplayer menu
		*(int *)(*(int *)(*(int*)0x6CECD4 + 0x38) + 0x2C) = (xres - 1024) / 2;
		break;
	}
}

void D2GIHookInjector::OnInitClusters(){
	DWORD dw_InitClusters;

	switch (s_eCurrentD2Version) {
		case D2V_1_3:
			dw_InitClusters = 0x52A470;
			*(float *)(*(DWORD *)0x695C0C + 0x58) = ui_aspect * 1.2f;
			*(float *)(*(DWORD *)0x695C0C + 0x54) = 1.2f;
			break;

		case D2V_8_1:
			dw_InitClusters = 0x52AD30;
			*(float *)(*(DWORD *)0x696CAC + 0x58) = ui_aspect * 1.2f;
			*(float *)(*(DWORD *)0x696CAC + 0x54) = 1.2f;
			break;

		case D2V_8_2:
			dw_InitClusters = 0x52ACB0;
			//blockObserver->FOV
			*(float *)(*(DWORD *)0x696CCC + 0x58) = ui_aspect * 1.2f;
			*(float *)(*(DWORD *)0x696CCC + 0x54) = 1.2f;
			break;
	}

	//run original function
	void(*InitClusters)() = (void(*)())dw_InitClusters;
	InitClusters();
}

//main injection code
void D2GIHookInjector::InjectInterfacePatch() {
	xres = D2GIConfig::GetVideoWidth();
	yres = D2GIConfig::GetVideoHeight();

	float real_aspect = (float)xres / (float)yres;
	float aspect_rev  = (float)yres / (float)xres;


	//max in-game GUI textures size is 1600x1200, in other cases textures in esc16.res should be redrawn for higher resolutions
	//lower resolution if > 1600 with saving original aspect
	if (xres > 1600) {
		xres = 1600;
		yres = (int)((float)xres / real_aspect);
	}

	//resoultion should be 4:3 or 16:9, otherwise game will crash with "not enough texture memory error"
	menuSettingsValue = 306;

	//set correct pointers
	DWORD cmp1204addr;
	DWORD cmp800addr;
	DWORD pagerXaddr;
	DWORD pagerYaddr;
	DWORD panelXaddr;
	DWORD textXaddr;

	DWORD xresAddr;
	DWORD yresAddr;

	//function call pointers
	DWORD call_prepareGameAddr;
	DWORD call_setOffsetsAddr;
	DWORD call_initClustersAddr;

	switch (s_eCurrentD2Version) {

	case D2V_1_3:
		cmp1204addr = 0x5691ED;
		cmp800addr  = 0x569223;
		pagerXaddr  = 0x56924B;
		pagerYaddr  = 0x5691E8;
		panelXaddr  = 0x569259;
		textXaddr   = 0x569254;

		xresAddr    = 0x5127F1;
		yresAddr    = 0x5127EC;

		call_prepareGameAddr  = 0x510516;
		call_setOffsetsAddr   = 0x510732;
		call_initClustersAddr = 0x4E0505;
		break;

	case D2V_8_1:
		cmp1204addr = 0x569ADD;
		cmp800addr  = 0x569B13;
		pagerXaddr  = 0x569B3B;
		pagerYaddr  = 0x569AD8;
		panelXaddr  = 0x569B49;
		textXaddr   = 0x569B44;

		xresAddr = 0x512FC1;
		yresAddr = 0x512FBC;

		call_prepareGameAddr  = 0x510CE6;
		call_setOffsetsAddr   = 0x510F02;
		call_initClustersAddr = 0x4E05A5;
		break;
		
	case D2V_8_2:
		cmp1204addr = 0x569A6D;
		cmp800addr  = 0x569AA3;
		pagerXaddr  = 0x569ACB; // 250 default
		pagerYaddr  = 0x569A68; //  50 default
		panelXaddr  = 0x569AD9; //-399 default
		textXaddr   = 0x569AD4; //  70 default

		xresAddr = 0x512F21;
		yresAddr = 0x512F1C;

		call_prepareGameAddr  = 0x510C46;
		call_setOffsetsAddr   = 0x510E62;
		call_initClustersAddr = 0x4E0625;
		break;
	}

	//if aspect near 4:3
	if (aspect_rev > 0.7) {
		if (xres > 1024) {
			xres = 1600;
			yres = 1200;
			menuSettingsValue = 322;
		} else {
			xres = 1024;
			yres = 768;
		}
	} else {
		if (xres > 1280) {
			xres = 1600;
			yres = 900;
		} else {
			xres = 1280;
			yres = 720;

			CPatch::SetInt(cmp1204addr, 1285);
			CPatch::SetInt(cmp800addr,  1284);
			CPatch::SetInt(pagerXaddr,   936);
			CPatch::SetInt(pagerYaddr,    11);
			CPatch::SetInt(panelXaddr,  -225);
			CPatch::SetInt(textXaddr,    244);
		}
	}

	ui_aspect = (float)xres / yres;

	Logger::Log(TEXT("Current GUI res is %dx%d"), xres, yres);

	//replace default 1024x768 resolution to new
	CPatch::SetShort(xresAddr, xres);
	CPatch::SetShort(yresAddr, yres);

	bool hook_prepareGame = PatchCallOperation(call_prepareGameAddr,  (DWORD)OnPrepareStartGame);
	bool hook_setOffsets  = PatchCallOperation(call_setOffsetsAddr,   (DWORD)OnSetupUIOffsets);
	bool hook_initCluster = PatchCallOperation(call_initClustersAddr, (DWORD)OnInitClusters);

	if (!hook_prepareGame)
		Logger::Log(TEXT("Failed to inject function OnPrepareStartGame"));
	else if (!hook_setOffsets)
		Logger::Log(TEXT("Failed to inject function SetupSidebarOffsets"));
	else if (!hook_initCluster)
		Logger::Log(TEXT("Failed to inject function InitClusters"));
	else
		Logger::Log(TEXT("Successfully injected interface hooks"));
}


VOID D2GIHookInjector::InjectHooks()
{
	static DWORD c_adwSetupTransformsCalls[] =
	{
		0x5EACB2, 0x5EB682, 0x5EB622
	};
	static TCHAR* c_lpszVersionNames[] =
	{
		TEXT("1.3"),
		TEXT("8.1"),
		TEXT("8.2")
	};


	if (!D2GIConfig::HooksEnabled())
	{
		Logger::Log(TEXT("Hook injection is not enabled."));
		return;
	}

	Logger::Log(TEXT("Trying to inject hooks..."));

	s_eCurrentD2Version = DetectD2Version();
	if (s_eCurrentD2Version == D2V_UNKNOWN)
	{
		Logger::Log(TEXT("Current D2 executable version is unknown, injection aborted"));
		return;
	}

	Logger::Log(TEXT("Detected D2 version %s"), c_lpszVersionNames[s_eCurrentD2Version]);

	//Fix transforms
	bool transforms_hook  = PatchCallOperation(c_adwSetupTransformsCalls[s_eCurrentD2Version], (DWORD)SetupTransformsHook);

	if (transforms_hook)
		Logger::Log(TEXT("Successfully injected SetupTransforms hook"));
	else
		Logger::Log(TEXT("Unable to write process memory to inject SetupTransforms hook"));


	//Fix screenshot write function
	D2GIHookInjector::InjectScreenshotsPatch();

	//Fix interface aspect
	D2GIHookInjector::InjectInterfacePatch();
}
