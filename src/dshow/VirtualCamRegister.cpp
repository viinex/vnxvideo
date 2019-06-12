//////////////////////////////////////////////////////////////////////////
//  This file contains routines to register / Unregister the 
//  Directshow filter 'Virtual Cam'
//  We do not use the inbuilt BaseClasses routines as we need to register as
//  a capture source
//////////////////////////////////////////////////////////////////////////
#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "advapi32")
#pragma comment(lib, "winmm")
#pragma comment(lib, "ole32")
#pragma comment(lib, "oleaut32")

//#ifdef _DEBUG
//#pragma comment(lib, "strmbasd")
//#else
//#pragma comment(lib, "strmbase")
//#endif


#include <streams.h>
#include <olectl.h>
#include <initguid.h>
#include <dllsetup.h>
#include "VirtualCam.h"

#define CreateComObject(clsid, iid, var) CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, iid, (void **)&var);

STDAPI AMovieSetupRegisterServer(CLSID   clsServer, LPCWSTR szDescription, LPCWSTR szFileName, LPCWSTR szThreadingModel = L"Both", LPCWSTR szServerType = L"InprocServer32");
STDAPI AMovieSetupUnregisterServer(CLSID clsServer);


// {BCC0A910-2B96-43F5-B3A6-3C5B9A2B802B}
DEFINE_GUID(CLSID_ViinexVirtualCamera,
    0xbcc0a910, 0x2b96, 0x43f5, 0xb3, 0xa6, 0x3c, 0x5b, 0x9a, 0x2b, 0x80, 0x2b);


const AMOVIESETUP_MEDIATYPE AMSMediaTypesVCam =
{
    &MEDIATYPE_Video,
    &MEDIASUBTYPE_NULL
};

const AMOVIESETUP_PIN AMSPinVCam =
{
    L"Output",             // Pin string name
    FALSE,                 // Is it rendered
    TRUE,                  // Is it an output
    FALSE,                 // Can we have none
    FALSE,                 // Can we have many
    &CLSID_NULL,           // Connects to filter
    NULL,                  // Connects to pin
    1,                     // Number of types
    &AMSMediaTypesVCam      // Pin Media types
};

const AMOVIESETUP_FILTER AMSFilterVCam =
{
    &CLSID_ViinexVirtualCamera, // Filter CLSID
    VIRTUALCAM_NAMEW,           // String name
    MERIT_DO_NOT_USE,           // Filter merit
    1,                          // Number pins
    &AMSPinVCam                 // Pin details
};

CFactoryTemplate g_Templates[] =
{
    {
        VIRTUALCAM_NAMEW,
        &CLSID_ViinexVirtualCamera,
        CVCam::CreateInstance,
        NULL,
        &AMSFilterVCam
    },
    
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI RegisterFilters(BOOL bRegister)
{
    HRESULT hr = NOERROR;
    WCHAR achFileName[MAX_PATH];
    char achTemp[MAX_PATH];
    ASSERT(g_hInst != 0);

    if (0 == GetModuleFileNameA(g_hInst, achTemp, sizeof(achTemp)))
        return AmHresultFromWin32(GetLastError());

    MultiByteToWideChar(CP_ACP, 0L, achTemp, lstrlenA(achTemp) + 1,
        achFileName, NUMELMS(achFileName));

    hr = CoInitialize(0);
    if (bRegister)
    {
        hr = AMovieSetupRegisterServer(CLSID_ViinexVirtualCamera, VIRTUALCAM_NAMEW, achFileName, L"Both", L"InprocServer32");
    }

    if (SUCCEEDED(hr))
    {
        IFilterMapper2 *fm = 0;
        hr = CreateComObject(CLSID_FilterMapper2, IID_IFilterMapper2, fm);
        if (SUCCEEDED(hr))
        {
            if (bRegister)
            {
                IMoniker *pMoniker = 0;
                REGFILTER2 rf2;
                rf2.dwVersion = 1;
                rf2.dwMerit = MERIT_DO_NOT_USE;
                rf2.cPins = 1;
                rf2.rgPins = &AMSPinVCam;
                hr = fm->RegisterFilter(CLSID_ViinexVirtualCamera, VIRTUALCAM_NAMEW, &pMoniker, &CLSID_VideoInputDeviceCategory, NULL, &rf2);
            }
            else
            {
                hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, 0, CLSID_ViinexVirtualCamera);
            }
        }

        // release interface
        //
        if (fm)
            fm->Release();
    }

    if (SUCCEEDED(hr) && !bRegister)
        hr = AMovieSetupUnregisterServer(CLSID_ViinexVirtualCamera);

    CoFreeUnusedLibraries();
    CoUninitialize();
    return hr;
}

STDAPI DllRegisterServer()
{
    return RegisterFilters(TRUE);
}

STDAPI DllUnregisterServer()
{
    return RegisterFilters(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  dwReason, LPVOID lpReserved)
{
    return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}
