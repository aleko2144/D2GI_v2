
#include "../common/common.h"
#include "../common/logger.h"
#include "../common/dir.h"

#include "../detours/CPatch.h"

#include "d2gi.h"
#include "d2gi_device.h"
#include "d2gi_hooks.h"
#include "d2gi_config.h"

#include "d2gi_common.h"

#include <gdiplus.h>
#pragma comment(lib, "GdiPlus.lib")

//GDI image formats
static const GUID bmp =
{ 0x557cf400, 0x1a04, 0x11d3,{ 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const GUID jpg =
{ 0x557cf401, 0x1a04, 0x11d3,{ 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static const GUID png =
{ 0x557cf406, 0x1a04, 0x11d3,{ 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

#define CALL_INSTRUCTION_SIZE 5
#define OPCODE_SIZE           1


D2GIHookInjector::D2VERSION D2GIHookInjector::s_eCurrentD2Version;


D2GI* D2GIHookInjector::ObtainD2GI()
{
	static DWORD c_adwDeviceAddresses[] =
	{
		0x00720908, 0x00720928
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
		0x005AE0E0, 0x005AE070
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
		0x400502EA, 0x4760F7AC
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

//https://firststeps.ru/mfc/winapi/r.php?158
//https://stackoverflow.com/questions/2802188/file-count-in-a-directory-using-c
void getScreenshotsCount() {
	int result = 0;
	char scr_path[128];
	strcpy(scr_path, D2GIConfig::GetScreenshotsPath());
	strcat(scr_path, "\\*");

	WIN32_FIND_DATAA find_data;
	HANDLE hndl = INVALID_HANDLE_VALUE;
	hndl = FindFirstFileA(scr_path, &find_data);

	if (hndl != INVALID_HANDLE_VALUE) {
		do {
			if (strstr(find_data.cFileName, "ddphoto")) {
				result++;
			}
		} while (FindNextFileA(hndl, &find_data) == TRUE);
		FindClose(hndl);
	}

	*(int*)0x6F34FC = result;
}

//struct tagBITMAPINFO *__cdecl sub_5E9EA0(int a1);
//struct tagBITMAPINFO *__cdecl WritePhotoToFile(FILE *a2)
//https://qna.habr.com/q/1071410
void __cdecl D2GIHookInjector::ScreenshotHook(void *a2) {
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	//Logger::Log(TEXT("ScreenshotHook->start"));

	D2GI* pD2GI = ObtainD2GI();

	if (pD2GI == NULL) {
		Logger::Warning(TEXT("ScreenshotHook->!pD2GI"));
		return;
	}

	int* screenshots_count = (int*)0x6F34FC;

	D3D9::IDirect3DDevice9* device = pD2GI->GetD3D9Device();
	D3D9::IDirect3DSurface9* buffer;
	D3D9::IDirect3DSurface9* backbuffer;
	D3D9::D3DSURFACE_DESC desc;

	device->GetRenderTarget(0, &backbuffer);
	backbuffer->GetDesc(&desc);

	device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3D9::D3DPOOL_SYSTEMMEM, &buffer, nullptr);
	device->GetRenderTargetData(backbuffer, buffer);

	HDC hdc;
	buffer->GetDC(&hdc);
	HDC c_hdc = CreateCompatibleDC(hdc);
	HBITMAP c_bmp = CreateCompatibleBitmap(hdc, desc.Width, desc.Height);

	SelectObject(c_hdc, c_bmp);

	BitBlt(c_hdc, 0, 0, desc.Width, desc.Height, hdc, 0, 0, SRCCOPY);
	HBITMAP hBitmap;
	hBitmap = (HBITMAP)SelectObject(c_hdc, c_bmp);
	Gdiplus::Bitmap bitmap(hBitmap, NULL);


	getScreenshotsCount();
	CreateDirectoryA(D2GIConfig::GetScreenshotsPath(), 0);

	char screenshot_path[255];
	//char* to const WCHAR*
	//https://ru.stackoverflow.com/questions/1457558/%D0%9F%D1%80%D0%BE%D0%B1%D0%BB%D0%B5%D0%BC%D0%B0-%D1%81-bitmam-save-%D0%B8-%D1%81%D1%82%D1%80%D0%BE%D0%BA%D0%B0%D0%BC%D0%B8
	int wchars_num;
	wchar_t* wstr_path;

	switch (D2GIConfig::GetScreenshotsFormat())
	{
		case IMG_PNG:
			sprintf(screenshot_path, "%s\\ddphoto%04d.png", D2GIConfig::GetScreenshotsPath(), *screenshots_count);
			
			wchars_num = MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, NULL, 0);
			wstr_path = new wchar_t[wchars_num];
			MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, wstr_path, wchars_num);
			
			bitmap.Save(wstr_path, &png);
			break;
		case IMG_JPG:
			sprintf(screenshot_path, "%s\\ddphoto%04d.jpg", D2GIConfig::GetScreenshotsPath(), *screenshots_count);
			
			wchars_num = MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, NULL, 0);
			wstr_path = new wchar_t[wchars_num];
			MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, wstr_path, wchars_num);
			
			bitmap.Save(wstr_path, &jpg);
			break;
		case IMG_BMP:
			sprintf(screenshot_path, "%s\\ddphoto%04d.bmp", D2GIConfig::GetScreenshotsPath(), *screenshots_count);
			
			wchars_num = MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, NULL, 0);
			wstr_path = new wchar_t[wchars_num];
			MultiByteToWideChar(CP_UTF8, 0, screenshot_path, -1, wstr_path, wchars_num);
			
			bitmap.Save(wstr_path, &bmp);
			break;
		default:
			break;
	}

	DeleteObject(hBitmap);

	Logger::Log(TEXT("Screenshot saved."));
}

//5DD850
//signed int __thiscall AdjustWin(CWinApp *this, int width, int height, eDepth a4, int flags)
//signed int __thiscall sub_5DD850(int this, int a2, int a3, int a4, unsigned int a5);

int   xres;
int   yres;
int   real_xres;
int   real_yres;
float real_aspect;
int   settings_res;

signed int _fastcall AdjustWin(DWORD* CWinApp, int width, int height, int depth, int flags) {
	typedef signed int(__thiscall * AdjustWin)(DWORD* CWinApp, int width, int height, int depth, int flags);
	return AdjustWin(0x5DD850)(CWinApp, width, height, depth, flags);
}

signed int *__cdecl PrepareStartGame() {
	typedef signed int * (__cdecl * PrepareStartGame)(DWORD* TheGamePtr);
	return PrepareStartGame(0x512E00)((DWORD*)0x696CC0);
}

void __cdecl SetupSideBarOffsets(int a1){
	typedef void(__cdecl * SetupSideBarOffsets)(int a1);
	return SetupSideBarOffsets(0x510E80)(a1);
}

//void __thiscall CabChan::Init(CabChan *this, TheGame *a2)
void _fastcall CabChan_Init() {
	typedef void(__thiscall * CabChan_Init)(DWORD* CabChan, DWORD* TheGame);
	return CabChan_Init(0x4DE310)((DWORD*)0x696CD0, (DWORD*)0x696CC0);
}

//sub_52ACB0
int sub_52ACB0() {
	typedef int(* sub_52ACB0)();
	return sub_52ACB0(0x52ACB0)();
}

void D2GIHookInjector::OnCall52ACB0() {
	sub_52ACB0();

	float* fov_x = (float *)(*(DWORD *)0x696CCC + 0x58);
	float* fov_y = (float *)(*(DWORD *)0x696CCC + 0x54);

	*fov_x = (float)real_xres / (float)real_yres * 1.2;
	*fov_y = 1.2;
}

void D2GIHookInjector::OnPrepareStartGame(){
	//overwrite selected resolution in the game settings to 1024x768
	// hex   int  &1F0
	//0x110 (274) (272) 640x480
	//0x120 (290) (288) 800x600
	//0x130 (306) (304) 1024x768
	//0x140 (322) (320) 1600x900

	*(int *)(*(int *)0x6CEC90 + 408) = settings_res;
	//run function
	PrepareStartGame();
}

void D2GIHookInjector::OnSetupSidebarOffsets(){
	//run original function
	SetupSideBarOffsets(0);

	//fix sidebar positions
	//BackInfo
	*(int*)0x6CED3C = xres - 385;
	//mainSideBarOffsetX	
	*(int*)0x6CED34 = xres - 225;
	//MenuGasSprite
	*(int *)(*(DWORD *)(*(DWORD*)0x6CECCC + 0x38) + 0x2C) = (xres - 1024) / 2;

	/*
		;right GUI textures
		;BackConnect  ?
		;BackGas      x
		;BackInfo     x
		;BackJob      x
		;BackOrder    x
		;BackSale     x
		;BackService  x
	*/
}

BOOL D2GIHookInjector::ResolutionsHook()
{
	real_xres = D2GIConfig::GetVideoWidth();
	real_yres = D2GIConfig::GetVideoHeight();

	xres = real_xres;
	yres = real_yres;

	real_aspect      = (float)xres / (float)yres;
	float aspect_rev = (float)yres / (float)xres;

	//max GUI textures size is 1600x1200, in other case textures should be redrawn for higher resolutions
	//lower resolution if > 1600 with saving original aspect
	if (xres > 1600) {
		xres = 1600;
		yres = (int)((float)xres / real_aspect);
	}
	//else if (yres > 1200) {
	//	yres = 1200;
	//	xres = (int)((float)yres * real_aspect);
	//}

	//resoultion should be 4:3 or 16:9, otherwise game will crash with "not enough texture memory error"
	settings_res = 306;

	//if aspect near 4:3
	if (aspect_rev > 0.7) {
		if (xres > 1024) {
			xres = 1600;
			yres = 1200;
			settings_res = 322;
		}
		else {
			xres = 1024;
			yres = 768;
		}

	//if aspect near 16:9
	} else {
		if (xres > 1280) {
			xres = 1600;
			yres = 900;
		} else {
			xres = 1280;
			yres = 720;

			//check if xres <= 1204
			CPatch::SetInt(0x569A6D, 1285);
			//check if xres <= 800
			CPatch::SetInt(0x569AA3, 1284);

			//set pager X
			CPatch::SetInt(0x569ACB, 936);
			//set pager Y
			CPatch::SetInt(0x569A68, 11);

			//set tablo X
			CPatch::SetInt(0x569AD9, -225);
			//set text X
			CPatch::SetInt(0x569AD4, 244);
		}
	}

	Logger::Log(TEXT("Current GUI res is %dx%d"), xres, yres);

	//replacing default screen resolutions in the game settings

	//640x480
	//CPatch::SetShort(0x512F6B, xres);
	//CPatch::SetShort(0x512F66, yres);
	//800x600
	//CPatch::SetShort(0x512F46, xres);
	//CPatch::SetShort(0x512F41, yres);
	//1024x768
	CPatch::SetShort(0x512F21, xres);
	CPatch::SetShort(0x512F1C, yres);
	//1600x1200
	//CPatch::SetShort(0x512EFC, xres);
	//CPatch::SetShort(0x512EF7, yres);

	return true;
}

VOID D2GIHookInjector::InjectHooks()
{
	static DWORD c_adwSetupTransformsCalls[] =
	{
		0x005EB682, 0x005EB622
	};
	static TCHAR* c_lpszVersionNames[] =
	{
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

	bool trs_hook = PatchCallOperation(c_adwSetupTransformsCalls[s_eCurrentD2Version], (DWORD)SetupTransformsHook);
	bool scr_hook = false;
	bool res_hook = false;
	//bool txr_hook = false;

	if (s_eCurrentD2Version == D2V_8_2) //только для 8.2 (king.exe от 10.09.2009)
	{
		scr_hook = PatchCallOperation(0x576452, (DWORD)ScreenshotHook);

		//патч скриншотов (CWinApp::MakeScreenshot) (0x5763D0) в 8.2
		//почему-то перехват самой ф-ии 0x5763D0 приводит к тому, что игра перестаёт реагировать
		//на нажатия клавиш, поэтому перехватил WritePhotoToFile (0x576452) и заглушил создание
		//папки screenshots и bmp-файла

		//CPatch взят из проекта D2InputWrapper от Voron295
		//https://github.com/Voron295/rignroll-dinput-wrapper/blob/main/D2DInputWrapper/CPatch.h

		//_mkdir(".\\screenshots");
		CPatch::Nop(0x57641E, 5);

		//v3 = fopen(&Dest, "wb");
		CPatch::Nop(0x57644A, 5);

		//fclose(v3);
		CPatch::Nop(0x576458, 5);

		if (scr_hook)
			Logger::Log(TEXT("Successfully injected screenshots hook"));
		else
			Logger::Log(TEXT("Unable to hook screenshots function"));


		//512E00 - функция, в которой задаётся разрешение экрана, которое влияет на интерфейс
		//512F74   call    AdjustWin
		//runtime debug params: AdjustWin(&g_WinApp, 0x400, 0x300, GAME_DEPTH16, 0x7E00);

		//res_hook = PatchCallOperation(0x512F19, (DWORD)ResolutionsHook);


		res_hook = PatchCallOperation(0x510C46, (DWORD)OnPrepareStartGame);
		res_hook = ResolutionsHook();
		res_hook = PatchCallOperation(0x510E62, (DWORD)OnSetupSidebarOffsets);
		res_hook = PatchCallOperation(0x4E0625, (DWORD)OnCall52ACB0);

		//if (res_hook)
		//	Logger::Log(TEXT("Successfully injected resolutions hook (interface scale)"));
		//else
		//	Logger::Log(TEXT("Unable to hook resoultions function"));
		
		//глушение ошибки о размере текстуры
		//CPatch::Nop(0x5DB437, 5);
		//если заглушить, то: 
		//"Необработанное исключение по адресу 0x005D781B в king.exe: 0xC0000005: нарушение прав доступа при чтении по адресу 0x00000000"
		//т.е. king.exe->LPDIRECTDRAWSURFACE7 __cdecl CreateSurfaceFromImageData(unsigned int width, int height, const void *imageData), это и есть 5D781B

		//0x5DB438	0x3	A4 79 FF 	D4 C8 F2
		//CPatch::SetShort(0x5DB438, 0xA4);
		//CPatch::SetShort(0x5DB438, 0xA4);
		//CPatch::SetShort(0x5DB438, 0xA4);


		//txr_hook = PatchCallOperation(0x4E0EA9, (DWORD)D2GUIHook);

		//if (gui_hook)
		//	Logger::Log(TEXT("Successfully injected GUI hook"));
		//else
		//	Logger::Log(TEXT("Unable to hook GUI"));
	}
	else {
		Logger::Log(TEXT("Screenshots hook working only with game version 8.2, injection aborted"));
	}

	if (trs_hook)
		Logger::Log(TEXT("Successfully injected SetupTransforms hook"));
	else
		Logger::Log(TEXT("Unable to write process memory to inject SetupTransforms hook"));
}
