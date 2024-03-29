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
        if (m_surfPrimary != nullptr)
            return;
        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        HRESULT hr = m_ddraw->CreateSurface(&ddsd, &m_surfPrimary, 0);
        checkHRESULT(hr, "could not create primary surface");
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "Primary surface created";
    }
    void createBackbufferSurface(int width, int height)
    {
        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        ddsd.dwWidth = width;
        ddsd.dwHeight = height;
        ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        ddsd.ddpfPixelFormat.dwSize = sizeof ddsd.ddpfPixelFormat;
        ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;

        // target surface is always planar 4:2:0 (YVYU)
        // http://www.fourcc.org/pixel-format/yuv-yv12/
        ddsd.ddpfPixelFormat.dwYUVBitCount = 12;
        ddsd.ddpfPixelFormat.dwFourCC = mmioFOURCC('Y', 'V', '1', '2');

        HRESULT hr = m_ddraw->CreateSurface(&ddsd, &m_surfBackbufferYUV, 0);

        if (hr == DDERR_INVALIDPIXELFORMAT) {
            VNXVIDEO_LOG(VNXLOG_WARNING, "displaywin32") << "Pixel format YV12 not supported for back buffer";
            DDSURFACEDESC ddsd;
            memset(&ddsd, 0, sizeof ddsd);
            ddsd.dwSize = sizeof ddsd;
            ddsd.dwWidth = width;
            ddsd.dwHeight = height;
            ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT;
            hr = m_ddraw->CreateSurface(&ddsd, &m_surfBackbufferRGB, 0);

            memset(&ddpfRGB, 0, sizeof ddpfRGB);
            ddpfRGB.dwSize = sizeof ddpfRGB;
            m_surfBackbufferRGB->GetPixelFormat(&ddpfRGB);
        }

        checkHRESULT(hr, "could not create backbuffer surface");

        std::string format("unknown");
        if (m_surfBackbufferYUV)
            format = "YUV 4:2:0 planar";
        else if (m_surfBackbufferRGB && ddpfRGB.dwFourCC==0) {
            format = "RGB" + std::to_string(ddpfRGB.dwRGBBitCount);
        }

        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "Backbuffer surface created, format " << format;
    }
public:
    CDisplaySink(int width, int height, const char* caption, std::function<void(void)> onClose)
        : m_ddraw(0)
        , m_surfBackbufferYUV(0)
        , m_surfBackbufferRGB(0)
        , m_surfPrimary(0)
        , m_hwnd(0)
        , m_caption(std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(caption))
        , m_csp(EMF_NONE)
        , m_formatInvalidated(true)
        , m_sizeInvalidated(true)
        , m_surfacesLost(false)
        , m_onClose(onClose)
    {
        if(!helper().DirectDrawCreate)
            throw std::runtime_error("could not get the address of DirectDrawCreate function in ddraw.dll");
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "DirectDrawCreate symbol found in ddraw.dll";
        HRESULT hr;
        hr = helper().DirectDrawCreate(0, &m_ddraw, 0);
        checkHRESULT(hr, "could not DirectDrawCreate()");
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "DirectDrawCreate() succeeded";


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
    CComPtr<IDirectDrawSurface> m_surfBackbufferYUV;
    CComPtr<IDirectDrawSurface> m_surfBackbufferRGB;
    DDPIXELFORMAT ddpfRGB;

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

    bool m_surfacesLost;

    VnxVideo::PRawSample m_sample;

    std::function<void(void)> m_onClose;
private:
    void onWindowGeometry() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sizeInvalidated = true;
    }
    bool restoreSurfaces(std::unique_lock<std::mutex>& lock) {
        if (!lock)
            throw std::logic_error("lock should be acquired in restoreSurfaces");
        if (!m_surfacesLost)
            return true;
        auto primary(m_surfPrimary);
        auto backbuffer(m_surfBackbufferYUV ? m_surfBackbufferYUV : m_surfBackbufferRGB);
        if (!primary || !backbuffer)
            return false;
        lock.unlock();
        HRESULT hr;
        hr=primary->Restore();
        if (FAILED(hr))
            return false;
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "Primary surface restored";
        hr = backbuffer->Restore();
        if (FAILED(hr))
            return false;
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "Backbuffer surface restored";
        lock.lock();
        m_surfacesLost = false;
        return true;
    }
    void onPaint() {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_formatInvalidated && m_csp != EMF_NONE) {
            createSurfaces(lock);
            m_formatInvalidated = false;
        }

        if (m_surfacesLost)
            if (!restoreSurfaces(lock))
                return;

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
        auto backbuffer(m_surfBackbufferYUV ? m_surfBackbufferYUV : m_surfBackbufferRGB);

        bool blit=updateBackbufferSurface(lock);
            
        if (primary && backbuffer && blit) {
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

            HRESULT hr;
            if ((hr=primary->IsLost()) != DD_OK) {
                lock.lock();
                m_surfacesLost = true;
                VNXVIDEO_LOG(VNXLOG_INFO, "displaywin32") << "IsLost returned non-success on primary surface, hr = " << std::hex << hr;
                return;
            }
            hr = primary->Blt(&vp, backbuffer, 0, DDBLT_WAIT, 0);
            if (FAILED(hr)) {
                if (hr == DDERR_SURFACELOST) {
                    lock.lock();
                    m_surfacesLost = true;
                    VNXVIDEO_LOG(VNXLOG_INFO, "displaywin32") << "DDERR_SURFACELOST on attempt to Blt to primary surface";
                }
                else
                    VNXVIDEO_LOG(VNXLOG_WARNING, "displaywin32") << "Could not Blt to primary surface: HRESULT=" << std::hex << hr;
            }
        }
        //or primary->Flip(backbuffer, DDFLIP_WAIT); // this is for the fullscreen
    }
    void onClose() {
        m_onClose();
    }
    bool updateBackbufferSurface(std::unique_lock<std::mutex>& lock) {
        if (!lock.owns_lock())
            throw std::logic_error("lock should be acquired");

        if (m_surfacesLost)
            if (!restoreSurfaces(lock))
                return false;

        VnxVideo::PRawSample sample(m_sample);
        auto backbuffer(m_surfBackbufferYUV ? m_surfBackbufferYUV : m_surfBackbufferRGB);
        bool yuv = !!m_surfBackbufferYUV;
        auto csp(m_csp);
        auto width(m_width);
        auto height(m_height);
        if (nullptr == sample.get() || !backbuffer)
            return false;

        lock.unlock();
        DDSURFACEDESC ddsd;
        memset(&ddsd, 0, sizeof ddsd);
        ddsd.dwSize = sizeof ddsd;
        HRESULT hr;
        int srcStrides[4];
        uint8_t* srcPlanes[4];
        sample->GetData(srcStrides, srcPlanes);
        if ((hr=backbuffer->IsLost()) != DD_OK) {
            lock.lock();
            m_surfacesLost = true;
            VNXVIDEO_LOG(VNXLOG_INFO, "displaywin32") << "IsLost returned non-success on backbuffer, hr=" << std::hex << hr;
            return false;
        }
        hr = backbuffer->Lock(0, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, 0);
        if (SUCCEEDED(hr))
        {
            if (yuv) {
                int dstStrides[4];
                uint8_t* dstPlanes[4];
                dstStrides[0] = ddsd.lPitch;
                dstStrides[1] = ddsd.lPitch / 2;
                dstStrides[2] = ddsd.lPitch / 2;
                dstPlanes[0] = (uint8_t*)ddsd.lpSurface;
                dstPlanes[2] = dstPlanes[0] + dstStrides[0] * ddsd.dwHeight;
                dstPlanes[1] = dstPlanes[2] + dstStrides[2] * ddsd.dwHeight / 2;

                CRawSample::CopyRawToI420(width, height, csp, srcPlanes, srcStrides, dstPlanes, dstStrides);
            }
            else { // rgb
                CRawSample::CopyI420ToRGB(width, height, srcPlanes, srcStrides, ddpfRGB.dwAlphaBitDepth, (uint8_t*)ddsd.lpSurface, ddsd.lPitch);
            }

            backbuffer->Unlock(ddsd.lpSurface);
            return true;
        }
        else{
            if (hr == DDERR_SURFACELOST) {
                lock.lock();
                m_surfacesLost = true;
                VNXVIDEO_LOG(VNXLOG_INFO, "displaywin32") << "DDERR_SURFACELOST on attempt to lock surface";
            }
            else
                VNXVIDEO_LOG(VNXLOG_WARNING, "displaywin32") << "Could not lock surface: HRESULT=" << std::hex << hr;
            return false;
        }
    }
private:
    void run(int windowWidth, int windowHeight)
    {
        try {
            createWindow(windowWidth, windowHeight);
            messageLoop();
        }
        catch (const std::exception& e) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "displaywin32") << "Unhandled exception in display run loop: " << e.what();
        }
    }

    void createWindow(int windowWidth, int windowHeight) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_hwnd = CreateWindowW(WINDOW_CLASS_NAME,
            m_caption.c_str(),
            WS_OVERLAPPEDWINDOW, 100, 100, 100 + windowWidth, 100 + windowHeight, 0, 0, 0, 0);
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

        HRESULT hr = m_ddraw->SetCooperativeLevel(m_hwnd, DDSCL_NORMAL);
        checkHRESULT(hr, "could not SetCooperativeLevel()");
        VNXVIDEO_LOG(VNXLOG_DEBUG, "displaywin32") << "SetCooperativeLevel() succeeded";

        m_condition.notify_all();

        lock.unlock();
        ShowWindow(m_hwnd, SW_SHOW);
    }

    void createSurfaces(std::unique_lock<std::mutex>& lock){
        createPrimarySurface();
        createBackbufferSurface(m_width, m_height);

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
        if (!vnxvideo_emf_is_video(csp))
            return;
        std::unique_lock<std::mutex> lock(m_mutex);
        m_csp = csp;
        m_width = width;
        m_height = height;
        m_formatInvalidated = true;
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        ERawMediaFormat emf;
        int x, y;
        sample->GetFormat(emf, x, y);
        if (m_csp!=emf) // ignore audio
            return;
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sample.reset(sample->Dup());
        HWND hwnd(m_hwnd);
        if (lock.owns_lock())
            lock.unlock();
        if(hwnd)
            PostMessage(hwnd, WM_PAINT, 0, (LPARAM)this);
    }
    virtual void Flush() {

    }
public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        try
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
                if (displaySink)
                    displaySink->onWindowGeometry();
                break;
            }
            }
        }
        catch (const std::exception& e) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "displaywin32") << "Unhandled exception in WindowProc: " << e.what();
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
