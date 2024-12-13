
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
	}
	else {
		Logger::Log(TEXT("Screenshots hook working only with game version 8.2, injection aborted"));
	}

	if (trs_hook)
		Logger::Log(TEXT("Successfully injected SetupTransforms hook"));
	else
		Logger::Log(TEXT("Unable to write process memory to inject SetupTransforms hook"));
}
