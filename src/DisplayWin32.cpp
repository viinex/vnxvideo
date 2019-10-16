#ifdef _WIN32
#include <iostream>
#include <stdexcept>
#include <codecvt>
#include <thread>
#include <mutex>

#include <windowsx.h>
#include <ddraw.h>
#include <atlbase.h>
#include <atlcom.h>

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "RawSample.h"

#include "resource.h"

const wchar_t* const WINDOW_CLASS_NAME = L"VIINEX_VIDEO_DISPLAY_WINDOW";

// for things that need to be performed once
class CHelper {
public:
    CHelper();
    typedef HRESULT(WINAPI * DirectDrawCreate_t)(GUID FAR *lpGUID, LPDIRECTDRAW FAR *lplpDD, IUnknown FAR *pUnkOuter);
    DirectDrawCreate_t DirectDrawCreate;
};

CHelper& helper() {
    // as the matter of fact we do want lazy initialization for this
    static CHelper helper;
    return helper;
}

void checkHRESULT(HRESULT hr, const char* message) {
    std::stringstream ss;
    ss << message << ", HRESULT=" << std::hex << hr;
    if (FAILED(hr))
        throw std::runtime_error(ss.str().c_str());
}

class CDisplaySink: public VnxVideo::IRawProc
{
private:
    void createPrimarySurface()
    {
        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        HRESULT hr = m_ddraw->CreateSurface(&ddsd, &m_surfPrimary, 0);
        checkHRESULT(hr, "could not create primary surface");
    }
    void createOverlaySurface(int width, int height)
    {
        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        ddsd.dwWidth = width;
        ddsd.dwHeight = height;
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        //ddsd.ddsCaps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY;
        ddsd.ddpfPixelFormat.dwSize = sizeof ddsd.ddpfPixelFormat;
        ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;

        // target surface is always planar 4:2:0 (YVYU)
        // http://www.fourcc.org/pixel-format/yuv-yv12/
        ddsd.ddpfPixelFormat.dwYUVBitCount = 12;
        ddsd.ddpfPixelFormat.dwFourCC = mmioFOURCC('Y', 'V', '1', '2');

        HRESULT hr = m_ddraw->CreateSurface(&ddsd, &m_surfOverlay, 0);
        checkHRESULT(hr, "could not create overlay surface");
    }
public:
    CDisplaySink(int width, int height, const char* caption, std::function<void(void)> onClose)
        : m_ddraw(0)
        , m_surfOverlay(0)
        , m_surfPrimary(0)
        , m_hwnd(0)
        , m_caption(std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(caption))
        , m_csp(EMF_NONE)
        , m_formatInvalidated(true)
        , m_sizeInvalidated(true)
        , m_onClose(onClose)
    {
        if(!helper().DirectDrawCreate)
            throw std::runtime_error("could not get the address of DirectDrawCreate function in ddraw.dll");
        HRESULT hr;
        hr = helper().DirectDrawCreate(0, &m_ddraw, 0);
        checkHRESULT(hr, "could not DirectDrawCreate()");

        std::unique_lock<std::mutex> lock(m_mutex);
        m_continue = true;
        m_thread = std::move(std::thread(std::bind(&CDisplaySink::run, this, width, height)));

        while (0 == m_hwnd && m_continue)
            m_condition.wait(lock);
    }
    ~CDisplaySink()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_continue = false;
        PostMessage(m_hwnd, WM_QUIT, 0, (LPARAM)this);
        //PostThreadMessage(GetThreadId(m_thread.native_handle()), WM_QUIT, 0, (LPARAM)this);
        m_condition.notify_all();
        lock.unlock();
        if (m_thread.get_id() != std::thread().get_id())
            m_thread.join();
    }

private:
    CComPtr<IDirectDraw> m_ddraw;
    CComPtr<IDirectDrawSurface> m_surfPrimary;
    CComPtr<IDirectDrawSurface> m_surfOverlay;
    CComPtr<IDirectDrawClipper> m_clipper;
    HWND m_hwnd;

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_thread;
    bool m_continue;

    const std::wstring m_caption;
    // that's width and height of picture, not window
    EColorspace m_csp;
    int m_width; 
    int m_height;
    bool m_formatInvalidated;

    bool m_sizeInvalidated; // viewport size was changed.

    VnxVideo::PRawSample m_sample;

    std::function<void(void)> m_onClose;
private:
    void onWindowGeometry() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sizeInvalidated = true;
    }
    void onPaint() {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_formatInvalidated && m_csp != EMF_NONE) {
            createSurfaces(lock);
            m_formatInvalidated = false;
        }

        RECT cr;
        GetClientRect(m_hwnd, &cr);
        POINT p; p.x = p.y = 0;
        ClientToScreen(m_hwnd, &p);
        cr.left += p.x; cr.right += p.x;
        cr.top += p.y; cr.bottom += p.y;

        RECT vp;
        vp = cr;

        // correct viewport area to preserve original aspect ratio
        const int crw = cr.right - cr.left;
        const int crh = cr.bottom - cr.top;

        if (m_width == 0 || m_height == 0 || crw == 0 || crh == 0) // nothing or nowhere to display
            return;

        if (crw*m_height > m_width*crh) { // window is too wide
            // fit image height, compute excess width
            const int vpw = (crh*m_width) / m_height;
            const int exw = crw - vpw;
            const int margin = exw / 2;
            vp.left += margin;
            vp.right -= margin;
        }
        else { // window is too tall
            // fit image width, compute excess height
            const int vph = (crw*m_height) / m_width;
            const int exh = crh - vph;
            const int margin = exh / 2;
            vp.top += margin;
            vp.bottom -= margin;
        }

        bool clearBackground = m_sizeInvalidated;
        m_sizeInvalidated = false;

        auto primary(m_surfPrimary);
        auto overlay(m_surfOverlay);

        updateOverlaySurface(lock); // lock is unlocked after that
        if (primary && overlay) {
            if (clearBackground) {
                // color fill clear by means of GDI
                // because the priomary->Blt with color fill flags causes an access violation.
                // somewhere in the deep of nvidia dlls.
                HDC hdc = GetWindowDC(m_hwnd);
                RECT r;
                GetClientRect(m_hwnd, &r);
                FillRect(hdc, &r, GetStockBrush(DKGRAY_BRUSH));
                ReleaseDC(m_hwnd, hdc);
            }

            primary->Blt(&vp, overlay, 0, DDBLT_WAIT, 0);
        }
        //or primary->Flip(overlay, DDFLIP_WAIT); // this is for the fullscreen
    }
    void onClose() {
        m_onClose();
    }
    void updateOverlaySurface(std::unique_lock<std::mutex>& lock) {
        if (!lock.owns_lock())
            throw std::logic_error("lock should be acquired");
        VnxVideo::PRawSample sample(m_sample);
        auto overlay(m_surfOverlay);
        auto csp(m_csp);
        auto width(m_width);
        auto height(m_height);
        lock.unlock();
        if (nullptr == sample.get() || !overlay)
            return;

        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        HRESULT hr;
        int srcStrides[4];
        uint8_t* srcPlanes[4];
        sample->GetData(srcStrides, srcPlanes);
        hr = overlay->Lock(0, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, 0);
        if (SUCCEEDED(hr))
        {
            int dstStrides[4];
            uint8_t* dstPlanes[4];
            dstStrides[0] = ddsd.lPitch;
            dstStrides[1] = ddsd.lPitch / 2;
            dstStrides[2] = ddsd.lPitch / 2;
            dstPlanes[0] = (uint8_t*)ddsd.lpSurface;
            dstPlanes[2] = dstPlanes[0] + dstStrides[0] * ddsd.dwHeight;
            dstPlanes[1] = dstPlanes[2] + dstStrides[2] * ddsd.dwHeight/2;

            CRawSample::CopyRawToI420(width, height, csp, srcPlanes, srcStrides, dstPlanes, dstStrides);

            overlay->Unlock(ddsd.lpSurface);
        }
    }
private:
    void run(int windowWidth, int windowHeight)
    {
        createWindow(windowWidth, windowHeight);
        messageLoop();
    }

    void createWindow(int windowWidth, int windowHeight) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_hwnd = CreateWindowW(WINDOW_CLASS_NAME,
            m_caption.c_str(),
            WS_OVERLAPPEDWINDOW, 100, 100, 100 + windowWidth, 100 + windowHeight, 0, 0, 0, 0);
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

        HRESULT hr = m_ddraw->SetCooperativeLevel(m_hwnd, DDSCL_NORMAL);
        checkHRESULT(hr, "could not SetCooperativeLevel()");

        m_condition.notify_all();

        lock.unlock();
        ShowWindow(m_hwnd, SW_SHOW);
    }

    void createSurfaces(std::unique_lock<std::mutex>& lock){
        createPrimarySurface();
        createOverlaySurface(m_width, m_height);

        HRESULT hr = m_ddraw->CreateClipper(0, &m_clipper, 0);
        checkHRESULT(hr, "could not CreateClipper()");
        m_clipper->SetHWnd(0, m_hwnd);
        m_surfPrimary->SetClipper(m_clipper);
    }
    void messageLoop() {
        MSG msg;
        int ret;
        while (0 != (ret = GetMessage(&msg, 0, 0, 0)))
        {
            if (-1 == ret)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
public:
    virtual void SetFormat(EColorspace csp, int width, int height) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_csp = csp;
        m_width = width;
        m_height = height;
        m_formatInvalidated = true;
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sample.reset(sample->Dup());
        HWND hwnd(m_hwnd);
        updateOverlaySurface(lock); // lock is unlocked after that
        if(hwnd)
            PostMessage(hwnd, WM_PAINT, 0, (LPARAM)this);
    }
    virtual void Flush() {

    }
public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CLOSE:
        case WM_DESTROY: {
            CDisplaySink* displaySink = reinterpret_cast<CDisplaySink*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (displaySink)
                displaySink->onClose();
            PostQuitMessage(0);
            break;
        }
        case WM_PAINT: {
            CDisplaySink* displaySink = reinterpret_cast<CDisplaySink*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (displaySink)
                displaySink->onPaint();
            break;
        }
        case WM_SIZE:
        case WM_MOVE:
        case WM_MOVING: {
            CDisplaySink* displaySink = reinterpret_cast<CDisplaySink*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if(displaySink)
                displaySink->onWindowGeometry();
            break;
        }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

};

CHelper::CHelper() : DirectDrawCreate(nullptr) {
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)&CDisplaySink::WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = 0;
    wc.hIcon = LoadIcon(GetModuleHandle(L"vnxvideo.dll"), MAKEINTRESOURCEW(IDI_VIINEX));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClass(&wc);

    // Interesting, Microsoft.... :/
    auto ddraw_dll = LoadLibraryW(L"ddraw.dll");
    if (ddraw_dll) {
        auto procptr = GetProcAddress(ddraw_dll, "DirectDrawCreate");
        if (procptr) {
            DirectDrawCreate = reinterpret_cast<DirectDrawCreate_t>(procptr);
        }
    }
}


namespace VnxVideo {
    IRawProc *CreateDisplay(int width, int height, const char* name, std::function<void(void)> onClose) {
        return new CDisplaySink(width, height, name, onClose);
    }
}
#endif
