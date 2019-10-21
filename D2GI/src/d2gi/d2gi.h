#pragma once

#include <windows.h>

#include "../d3d9.h"

#include "d2gi_ddraw.h"


enum RENDERSTATE
{
	RS_UNKNOWN,
	RS_BACKBUFFER_STREAMING,
	RS_PRIMARY_SURFACE_BLITTING,
	RS_BACKBUFFER_BLITTING,
};


class D2GISystemMemorySurface;
class D2GIPrimarySingleSurface;
class D2GIBackBufferSurface;
class D2GIPaletteBlitter;
class D2GITexture;


class D2GI
{
	D2GIDirectDraw* m_pDirectDrawProxy;

	HMODULE m_hD3D9Lib;
	D3D9::IDirect3D9* m_pD3D9;
	D3D9::IDirect3DDevice9* m_pDev;

	HWND m_hWnd;
	DWORD m_dwOriginalWidth, m_dwOriginalHeight, m_dwOriginalBPP;

	RENDERSTATE m_eRenderState;

	D2GIPaletteBlitter* m_pPaletteBlitter;

	VOID LoadD3D9Library();
	VOID ResetD3D9Device();
	VOID ReleaseResources();
	VOID LoadResources();
public:
	D2GI();
	~D2GI();

	D2GIDirectDraw* GetDirectDrawProxy() { return m_pDirectDrawProxy; }
	D3D9::IDirect3D9* GetD3D9() { return m_pD3D9; }
	D3D9::IDirect3DDevice9* GetD3D9Device() { return m_pDev; }
	DWORD GetOriginalWidth() { return m_dwOriginalWidth; }
	DWORD GetOriginalHeight() { return m_dwOriginalHeight; }
	DWORD GetOriginalBPP() { return m_dwOriginalBPP; }

	VOID OnDirectDrawReleased();
	VOID OnCooperativeLevelSet(HWND, DWORD);
	VOID OnDisplayModeSet(DWORD, DWORD, DWORD, DWORD dwFlags);
	VOID OnViewportSet(D3D7::LPD3DVIEWPORT7);
	VOID OnFlip();
	VOID OnBackBufferLock();
	VOID OnSysMemSurfaceBltOnPrimarySingle(D2GISystemMemorySurface*, RECT*, D2GIPrimarySingleSurface*, RECT*);
	VOID OnClear(DWORD dwCount, D3D7::LPD3DRECT lpRects, DWORD dwFlags, D3D7::D3DCOLOR col, D3D7::D3DVALUE z, DWORD dwStencil);
	VOID OnLightEnable(DWORD, BOOL);
	VOID OnSysMemSurfaceBltOnBackBuffer(D2GISystemMemorySurface*, RECT*, D2GIBackBufferSurface*, RECT*);
	VOID OnSysMemSurfaceBltOnTexture(D2GISystemMemorySurface*, RECT*, D2GITexture*, RECT*);
	VOID OnSceneBegin();
	VOID OnSceneEnd();
};
