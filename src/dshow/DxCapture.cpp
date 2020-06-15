#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <codecvt>

#include <atlbase.h>
#include <dshow.h>
#include <windows.h>

#include <vnxvideo/json.hpp>
#include <vnxvideo/jget.h>

using json = nlohmann::json;

// https://github.com/Microsoft/Windows-classic-samples/tree/master/Samples/Win7Samples/multimedia/directshow/baseclasses
// also available as a separate repository at https://github.com/viinex/ambase
#include <streams.h>

#include <vnxvideo/vnxvideoimpl.h>
#include <vnxvideo/vnxvideologimpl.h>

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "winmm.lib")
#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif

#include <ippcc.h>
#include <ippi.h>

#include "../RawSample.h"

#ifdef WITH_IDS_UEYE
#include "uEyeCaptureInterface.h"
#endif

class CCoInitialize
{
public:
    CCoInitialize()
    {
        if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)))
            throw std::runtime_error("CoInitializeEx failed");
    }
    ~CCoInitialize()
    {
        CoUninitialize();
    }
};


std::string hexstr(HRESULT hr) {
    char buf[16];
    sprintf_s(buf, "%08x", hr);
    return buf;
}

class CDxRawSample : public VnxVideo::IRawSample
{
private:
    EColorspace m_csp;
    int m_width;
    int m_height;
    CComPtr<IMediaSample> m_sample;
    std::shared_ptr<uint8_t> m_data; // I420, if original colorspace is not planar or YUV
    int m_size;

    int m_nplanes;
    int m_strides[4];
    ptrdiff_t m_offsets[4];
public:
    CDxRawSample(EColorspace csp, int width, int height, EColorspace cspOrig, int bppOrig, IMediaSample* sample, PShmAllocator allocator)
        :m_csp(csp)
        ,m_width(width)
        ,m_height(height)
        ,m_sample(sample)
        ,m_size(0)
        ,m_nplanes(0)
    {
        if (cspOrig != csp || allocator.get()!=nullptr) {
            if (csp != EMF_I420)
                throw std::logic_error("cannot convert format to anything but I420");
            m_size = (m_width*m_height * 3) / 2;
            if (allocator.get())
                m_data = allocator->Alloc(m_size);
            else
                m_data.reset((uint8_t*)malloc(m_size), free);
            uint8_t *dataOrig;
            m_sample->GetPointer(&dataOrig);
            copyRawToI420(m_data.get(), cspOrig, width, height, width*bppOrig / 8, dataOrig);
        }
        CRawSample::FillStridesOffsets(m_csp, m_width, m_height, m_nplanes, m_strides, m_offsets, false);
    }
    void GetFormat(EColorspace &csp, int &width, int &height) {
        csp = m_csp;
        width = m_width;
        height = m_height;
        if (m_csp == EMF_YV12) { // output YV12 as I420, just swap the U and V planes later
            csp = EMF_I420;
        }
    }
    void GetData(unsigned char* &data, int& size) {
        if (m_data.get() != nullptr && m_size != 0) {
            data = m_data.get();
            size = m_size;
        }
        else {
            HRESULT hr = m_sample->GetPointer(&data);
            size = m_sample->GetActualDataLength();
            if (FAILED(hr))
                throw std::runtime_error("IMediaSample::GetPointer failed: "+hexstr(hr));
        }
    }
    void GetData(int* strides, uint8_t** planes) {
        if(m_nplanes==0)
            throw std::logic_error("unsupported format, this should not have happened");
        uint8_t* ptr;
        int sz;
        GetData(ptr, sz);
        for (int k = 0; k < m_nplanes; ++k) {
            strides[k] = m_strides[k];
            planes[k] = ptr + m_offsets[k];
        }
        if (m_csp == EMF_YV12) { // we pretend that it's I420 because it's cheap to do so here
            strides[1] = m_strides[2];
            planes[1] = ptr + m_offsets[2];
            strides[2] = m_strides[1];
            planes[2] = ptr + m_offsets[1];
        }
    }
    VnxVideo::IRawSample* Dup() {
        return new CDxRawSample(*this); // copy constructor will do as CComPtr copy ctor provides behaviour we need
    }
private:
    static void copyRawToI420(uint8_t* dest, EColorspace emf,
        int width, int height, int stride, const uint8_t* source)
    {
        int offsetY = 0;
        int pitchY = width;
        int offsetU = width*height;
        int pitchU = width / 2;
        int offsetV = width*height * 5 / 4;
        int pitchV = width / 2;

        IppiSize roi = { width, height };
        Ipp8u* dst[3] = {
            dest + offsetY,
            dest + offsetU,
            dest + offsetV
        };
        int dst_strides[3] = { pitchY, pitchU, pitchV };

        // set src strides in some naive assumptions
        int src_strides[3] = { stride, stride / 2, stride / 2 };
        const uint8_t* src[3] = { source, source + src_strides[0] * height,
            source + src_strides[0] * height + src_strides[1] * height / 2 };
        if (emf == EMF_YVU9)
        {
            // never ever tested
            src_strides[1] = width / 16;
            src_strides[2] = width / 16;
            src[1] = src[0] + width*height;
            src[2] = src[1] + width*height / 16;
        }

        CRawSample::CopyRawToI420(width, height, emf, src, src_strides, dst, dst_strides);

        if (emf == EMF_RGB32 || emf == EMF_RGB24 || emf == EMF_RGB16)
        {
            ippiMirror_8u_C1IR(dst[0], dst_strides[0], roi, ippAxsHorizontal);
            IppiSize roic = { width / 2,height / 2 };
            ippiMirror_8u_C1IR(dst[1], dst_strides[1], roic, ippAxsHorizontal);
            ippiMirror_8u_C1IR(dst[2], dst_strides[2], roic, ippAxsHorizontal);
        }
    }
};

struct __declspec(uuid("{A6F334E2-F3ED-44be-9C07-F42029E05C4E}")) CVideoSinkClassId;
class CVideoSink : public CBaseVideoRenderer
{
private:
    HRESULT hr;
    PShmAllocator m_allocator;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    EColorspace m_cspOrig;
    int m_bppOrig;
    int m_width;
    int m_height;
public:
    CVideoSink(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame, PShmAllocator allocator = PShmAllocator())
        : CBaseVideoRenderer(__uuidof(CVideoSinkClassId), _T("Video sink"), 0, ((hr = S_OK), &hr))
        , m_onFormat(onFormat)
        , m_onFrame(onFrame)
        , m_csp(EMF_NONE)
        , m_allocator(allocator)
    {
        if (FAILED(hr))
            throw std::runtime_error("CBaseVideoRenderer ctor failed: " + hexstr(hr));
    }
    HRESULT CBaseRenderer::DoRenderSample(IMediaSample *sample)
    {
        REFERENCE_TIME startTs, endTs;
        HRESULT hr = sample->GetTime(&startTs, &endTs);
        if (FAILED(hr))
            return E_FAIL;
        CDxRawSample s(m_csp, m_width, m_height, m_cspOrig, m_bppOrig, sample, m_allocator);
        try
        {
            startTs /= 10000; // 100 nanoseconds to milliseconds which Viinex does expect
            m_onFrame(&s, startTs);
            return S_OK;
        }
        catch (...)
        {
            return E_FAIL;
        }
    }
    HRESULT CBaseRenderer::CheckMediaType(const CMediaType* media)
    {
        // check for YUV
        // http://msdn.microsoft.com/en-us/library/Aa904813
        // https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd391027(v=vs.85).aspx
        GUID guid = *media->Subtype();
        char sguid[64];
        snprintf(sguid, 64, "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        VNXVIDEO_LOG(VNXLOG_DEBUG, "dxcapture") << "Checking for media subtype " << sguid;

        // planar formats supported by x264
        if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_IYUV))
        {
            m_csp = m_cspOrig = EMF_I420;
        }
        else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_YV12))
        {
            m_csp = m_cspOrig = EMF_YV12;
        }
        else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_NV12))
        {
            m_csp = m_cspOrig = EMF_NV12;
        }
        // planar or packed or not-YUV formats not supported by x264
        else
        {
            m_csp = EMF_I420; // will perform conversion to I420
            if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_YUYV) ||
                IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_YUY2))
            {
                m_cspOrig = EMF_YUY2;
            }
            else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_UYVY))
            {
                m_cspOrig = EMF_UYVY;
            }
            else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_YVU9))
            {
                m_cspOrig = EMF_YVU9;
            }
            else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_RGB32))
            {
                m_cspOrig = EMF_RGB32;
            }
            else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_RGB24))
            {
                m_cspOrig = EMF_RGB24;
            }
            else if (IsEqualGUID(*media->Subtype(), MEDIASUBTYPE_RGB565))
            {
                m_cspOrig = EMF_RGB16;
            }
            else
                return E_FAIL;
        }

        VIDEOINFO* vi = (VIDEOINFO *)media->Format();
        m_width = vi->bmiHeader.biWidth;
        m_height = vi->bmiHeader.biHeight;
        m_bppOrig = vi->bmiHeader.biBitCount;

        m_onFormat(m_csp, m_width, m_height);

        return S_OK;

    }
    virtual ~CVideoSink()
    {
        VNXVIDEO_LOG(VNXLOG_DEBUG, "dxcapture") <<  "CVideoSink destroyed";
    }
};

class CVideoDevice : public VnxVideo::IVideoSource
{
public:
    CVideoDevice(CComPtr<IBaseFilter> device, CComPtr<IPin> pin, PShmAllocator allocator = PShmAllocator())
        : m_device(device)
        , m_pin(pin)
        , m_allocator(allocator)
    {
        HRESULT hr;
        hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC, IID_IGraphBuilder, (void**)&m_graph);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not create Graph Builder: " << shr;
            throw std::runtime_error("Could not create Graph Builder: " + shr);
        }

        hr = m_graph->QueryInterface(IID_IMediaControl, (void**)&m_mediaControl);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not obtain media control: " << hr;
            throw std::runtime_error("Could not obtain media control: " + shr);
        }

        hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC, IID_ICaptureGraphBuilder2, (void**)&m_capture);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not create an instance of ICaptureGraphBuilder2: " << hr;
            throw std::runtime_error("Could not create an instance of ICaptureGraphBuilder2: " + shr);
        }

        hr = m_capture->SetFiltergraph(m_graph);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not ICaptureGraphBuilder2::SetFiltergraph: " << shr;
            throw std::runtime_error("Could not ICaptureGraphBuilder2::SetFiltergraph: " + shr);
        }

        hr = m_graph->AddFilter(m_device, L"Video Capture");
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not add selected capture device to the filter graph: " << shr;
            throw std::runtime_error("Could not add selected capture device to the filter graph: " + shr);
        }
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame)
    {
        HRESULT hr;
        CComPtr<IBaseFilter> sink(new CVideoSink(onFormat, onFrame, m_allocator));
        hr = m_graph->AddFilter(sink, L"Video render sink");
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not add sink to filter graph: " << shr;
            throw std::runtime_error("Could not add sink to filter graph: " + shr);
        }
        hr = m_capture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, m_pin?((IUnknown*)m_pin):((IUnknown*)m_device), 0, sink);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not RenderStream: " << shr;
            throw std::runtime_error("Could not RenderStream: " + shr);
        }
    }
    virtual void Run()
    {
        HRESULT hr = m_mediaControl->Run();
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not run the graph: " << shr;
            throw std::runtime_error("Could not run the graph: " + shr);
        }
    }
    virtual void Stop()
    {
        HRESULT hr = m_mediaControl->Stop();
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not stop the graph: " << shr;
            throw std::runtime_error("Could not stop the graph: " + shr);
        }
    }
    ~CVideoDevice()
    {
        m_mediaControl->StopWhenReady();
    }
private:
    CCoInitialize m_coInitialize;
    CComPtr<IBaseFilter> m_device;
    CComPtr<IPin> m_pin;

    CComPtr<IGraphBuilder> m_graph;
    CComPtr<IMediaControl> m_mediaControl;
    CComPtr<ICaptureGraphBuilder2> m_capture;
    PShmAllocator m_allocator;
};

class CVideoDeviceManager : public VnxVideo::IVideoDeviceManager
{
public:
    virtual void EnumerateDevices(bool details, VnxVideo::TDevices& dev)
    {
        TMonikers deviceMonikers;
        CollectVideoSourceMonikers(deviceMonikers);
        dev.clear();
        for (TMonikers::iterator it = deviceMonikers.begin(); it != deviceMonikers.end(); ++it)
        {
            try {
                std::string path, fname;
                VnxVideo::TCapabilities caps;
                GetDeviceName(*it, path, fname);
                if (details) {
                    GetDeviceCaps(*it, caps);
                }
                dev[path] = std::make_pair(fname, caps);
            }
            catch (const std::runtime_error& e) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Encountered an exception while enumerating devices: " << e.what();
            }
        }
    }
    VnxVideo::IVideoSource *CreateVideoSource(const VnxVideo::TUniqueName& path, const std::string& mode)
    {
        TMonikers deviceMonikers;
        CollectVideoSourceMonikers(deviceMonikers);

        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        CComPtr<IMoniker> m(GetMonikerByDevicePath(deviceMonikers, conv.from_bytes(path)));
        if (0 == m) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not find device by path " << path;
            throw std::runtime_error("Could not find device " + path);
        }
        CComPtr<IBaseFilter> bf(GetBaseFilter(m));
        json jm;
        std::stringstream ss(mode);
        ss >> jm;
        CComPtr<IPin> pin=SetMode(bf, jm);
        return new CVideoDevice(bf, pin, DupPreferredShmAllocator());
    }
private:
    CCoInitialize m_coInitialize;
    typedef std::vector<CComPtr<IMoniker> > TMonikers;

    void CollectVideoSourceMonikers(TMonikers& deviceMonikers)
    {
        CComPtr<ICreateDevEnum> devEnum(0);

        HRESULT hr;

        hr = CoCreateInstance(CLSID_SystemDeviceEnum, 0, CLSCTX_INPROC, IID_ICreateDevEnum, (void **)&devEnum);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "CoCreateInstance for device enumerator failed: " << shr;
            throw std::runtime_error("CoCreateInstance for device enumerator failed: " + shr);
        }

        CComPtr<IEnumMoniker> classEnum(0);

        hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &classEnum, 0);
        if (FAILED(hr)) {
            auto shr = hexstr(hr);
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "CreateClassEnumerator for video input devices failed: " << shr;
            throw std::runtime_error("CreateClassEnumerator for video input devices failed: " + shr);
        }

        deviceMonikers.clear();

        if (!classEnum)
            return;
        CComPtr<IMoniker> moniker;
        ULONG fetched;
        while ((S_OK == classEnum->Next(1, &moniker, &fetched)) && (fetched > 0)) {
            deviceMonikers.push_back(moniker);
            moniker.Release();
        }
    }

    CComPtr<IMoniker> GetMonikerByDevicePath(const TMonikers& deviceMonikers, const std::wstring& p)
    {
        for (TMonikers::const_iterator it = deviceMonikers.begin(); it != deviceMonikers.end(); ++it)
        {
            HRESULT hr;
            CComPtr<IPropertyBag> bag;
            hr = (*it)->BindToStorage(0, 0, IID_IPropertyBag, (void **)&bag);
            if (FAILED(hr))
                throw std::runtime_error("Could not IMoniker::BindToStorage");
            VARIANT var;
            var.vt = VT_BSTR;
            hr = bag->Read(L"DevicePath", &var, 0);
            if (FAILED(hr)) {
                std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
                throw std::runtime_error("IPropertyBag::Read failed for DevicePath property on device "
                    + conv.to_bytes(p) + ": error code is " + hexstr(hr) + ".");
            }
            if (p == var.bstrVal)
            {
                SysFreeString(var.bstrVal);
                return *it;
            }
            else
                SysFreeString(var.bstrVal);
        }
        return CComPtr<IMoniker>();
    }
    void GetDeviceName(IMoniker* moniker, VnxVideo::TUniqueName& unique, VnxVideo::TFriendlyName& friendly) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        HRESULT hr;
        CComPtr<IPropertyBag> bag;
        hr = moniker->BindToStorage(0, 0, IID_IPropertyBag, (void **)&bag);
        if (FAILED(hr))
            throw std::runtime_error("Could not IMoniker::BindToStorage: " + hexstr(hr));
        VARIANT var;
        var.vt = VT_BSTR;
        hr = bag->Read(L"DevicePath", &var, 0);
        if (FAILED(hr)) {
            throw std::runtime_error("IPropertyBag::Read failed for DevicePath property on device "
                + unique + ": error code is " + hexstr(hr) + ".");
        }
        unique = conv.to_bytes(var.bstrVal);
        SysFreeString(var.bstrVal);
        hr = bag->Read(L"FriendlyName", &var, 0);
        if (FAILED(hr))
            throw std::runtime_error("IPropertyBag::Read failed: " + hexstr(hr) +". No friendly name provided?");
        friendly = conv.to_bytes(var.bstrVal);
        SysFreeString(var.bstrVal);

    }
    void GetDeviceCaps(IMoniker* moniker, VnxVideo::TCapabilities& caps)
    {
        HRESULT hr;
        CComPtr<IBaseFilter> bf(GetBaseFilter(moniker));
        if (0 == bf)
            throw std::runtime_error("Could not get IBaseFilter from device moniker");
        caps.clear();

        CComPtr<IEnumPins> ep;
        hr = bf->EnumPins(&ep);
        if (FAILED(hr))
            throw std::runtime_error("Could not EnumPins: " + hexstr(hr));
        CComPtr<IPin> pin;
        while (S_OK == ep->Next(1, &pin, 0))
        {
            EnumPinCaps(pin, caps);
            pin.Release();
        }
    }
    CComPtr<IPin> SetMode(IBaseFilter* bf, const json& mode)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;

        std::wstring pinid(conv.from_bytes(jget<std::string>(mode, "pin")));

        CComPtr<IEnumPins> ep;
        HRESULT hr = bf->EnumPins(&ep);
        if (FAILED(hr))
            throw std::runtime_error("Could not EnumPins: " + hexstr(hr));
        for (CComPtr<IPin> pin; S_OK == ep->Next(1, &pin, 0); pin.Release())
        {
            if (GetPinId(pin) != pinid)
                continue;

            SetModeFormat(pin, mode);
            SetModeCapture(bf, pin, mode);

            return pin;
        }
        return CComPtr<IPin>();
    }

    bool SetModeFormat(CComPtr<IPin> pin, const json& mode) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        HRESULT hr;

        int bpp;
        int format(biCompressionFromString(jget<std::string>(mode, "colorspace"),bpp));
        int width = 0, height = 0;
        auto jsz = mode.find("size");
        if (jsz != mode.end()) {
            width = jsz.value().at(0).begin().value().get<int>();
            height = jsz.value().at(1).begin().value().get<int>();
        }
        int period((int)round(1000000.0 / jget<float>(mode, "fps")));

        CComPtr<IAMStreamConfig> cfg(AMStreamConfigFromPin(pin));
        if (!cfg)
            return false;

        int count, size;
        hr = cfg->GetNumberOfCapabilities(&count, &size);
        if (FAILED(hr))
            throw std::runtime_error("Could not GetNumberOfCapabilities: " + hexstr(hr));
        std::vector<BYTE> cap(size);
        std::shared_ptr<AM_MEDIA_TYPE> bestAmt;
        unsigned int score = 0xffffffff;
        for (int k = 0; k < count; ++k)
        {
            AM_MEDIA_TYPE* amt;
            cfg->GetStreamCaps(k, &amt, &cap[0]);

            BITMAPINFOHEADER *bih = 0;
            REFERENCE_TIME *fr = 0;
            if (IsEqualGUID(amt->formattype, FORMAT_VideoInfo))
            {
                VIDEOINFOHEADER *v = (VIDEOINFOHEADER*)amt->pbFormat;
                fr = &v->AvgTimePerFrame;
                bih = &v->bmiHeader;
            }
            if (!!bih && !!fr)
            {
                if (true
                    && bih->biCompression == format
                    && ((0 != format) || (bih->biBitCount == bpp))
                    && bih->biWidth == width
                    && abs(bih->biHeight) == height
                    && true)
                {
                    unsigned int newscore = abs((int)(*fr - period));
                    if (newscore < score)
                    {
                        bestAmt = std::shared_ptr<AM_MEDIA_TYPE>(amt, DeleteMediaType);
                        amt = 0;
                        score = newscore;
                    }
                }
            }
            DeleteMediaType(amt);
        }
        if (bestAmt.get())
        {
            hr = cfg->SetFormat(bestAmt.get());
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not set format " << mode << ": hr=" << hexstr(hr);
                return false;
            }
            else
                return true;
        }
        else {
            VNXVIDEO_LOG(VNXLOG_ERROR, "dxcapture") << "Could not find appropriate format " << mode;
            return false;
        }
    }

    void SetModeCapture(IBaseFilter* filter, IPin* pin, const json& mode) {
        HRESULT hr;
        CComPtr<IAMCameraControl> cameraControl;
        CComPtr<IAMVideoControl> videoControl;
        hr = filter->QueryInterface(IID_IAMCameraControl, (void**)&cameraControl);
        if(nullptr==cameraControl)
            hr = pin->QueryInterface(IID_IAMCameraControl, (void**)&cameraControl);
        
        hr = filter->QueryInterface(IID_IAMVideoControl, (void**)&videoControl);
#ifdef WITH_IDS_UEYE
        CComPtr<IuEyeCaptureEx> ueyeCaptureEx; // filter
        CComPtr<IuEyeCapturePin> ueyeCapturePin; //pin

        CComPtr<IuEyeAutoParameter> ueyeAutoParameter;
        CComPtr<IuEyeAOI> ueyeAOI;
        CComPtr<IuEyeResample> ueyeResample;


        hr = filter->QueryInterface(IID_IuEyeCaptureEx, (void**)&ueyeCaptureEx);
        hr = pin->QueryInterface(IID_IuEyeCapturePin, (void**)&ueyeCapturePin);

        hr = filter->QueryInterface(IID_IuEyeAutoParameter, (void**)&ueyeAutoParameter);
        hr = filter->QueryInterface(IID_IuEyeAOI, (void**)&ueyeAOI);
        hr = pin->QueryInterface(IID_IuEyeResample, (void**)&ueyeResample);

        if (ueyeCaptureEx != nullptr) {
            hr = ueyeCaptureEx->ResetDefaults();
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not reset uEye camera settings to drivers defaults: " << hexstr(hr);
            }
        }
        if (ueyeAOI != nullptr && ueyeResample != nullptr) {
            ueyeSetAoiAndBinning(ueyeAOI, ueyeResample, mode);
        }

        bool gainBoost;
        if (mjget<bool>(mode, "gain_boost", gainBoost) && ueyeCaptureEx != nullptr) {
            hr = ueyeCaptureEx->SetGainBoost(gainBoost ? 1 : 0);
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set gain boost: " << hexstr(hr);
            }
        }

        int pixelClock;
        if (mjget<int>(mode, "pixel_clock", pixelClock) && ueyeCapturePin != nullptr) {
            hr = ueyeCapturePin->SetPixelClock(pixelClock);
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set pixel clock: " << hexstr(hr);
            }
        }
#endif

        if (nullptr != videoControl) {
            bool flipHorizontal = false;
            bool flipVertical = false;
            long flags = 0;
            hr = videoControl->GetMode(pin, &flags);
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not get video control flags: " << hexstr(hr);
            }
            if (mjget<bool>(mode, "flip_horizontal", flipHorizontal)) {
                if (flipHorizontal)
                    flags |= VideoControlFlag_FlipHorizontal;
                else
                    flags &= ~VideoControlFlag_FlipHorizontal;
            }
            if (mjget<bool>(mode, "flip_vertical", flipVertical)) {
                if (flipVertical)
                    flags |= VideoControlFlag_FlipVertical;
                else
                    flags &= ~VideoControlFlag_FlipVertical;
            }
            hr = videoControl->SetMode(pin, flags);
            if (FAILED(hr)) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set video control flags: " << hexstr(hr);
            }
        }

        auto exposureIt = mode.find("exposure");
        if (exposureIt != mode.end()) {
            if (exposureIt->is_number()) {
                float exposure = exposureIt->get<float>();
                long ne=0, xe=0, is=0, de=0, fl=0;
#ifdef WITH_IDS_UEYE
                if (ueyeCapturePin != nullptr) {
                    hr=ueyeCapturePin->GetExposureRange(&ne, &xe, &is);
                    if (FAILED(hr)) {
                        VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not GetExposureRange: " << hexstr(hr);
                    }
                    if (xe > ne && is > 0) {
                        hr = ueyeCapturePin->SetExposureTime(ne + is*((long)floor(exposure*float(xe - ne) / float(is))));
                        if (FAILED(hr)) {
                            VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set exposure: " << hexstr(hr);
                        }
                    }
                } else 
#endif
                if (cameraControl) {
                    cameraControl->GetRange(CameraControl_Exposure, &ne, &xe, &is, &de, &fl);
                    if (xe > ne && is > 0) {
                        hr = cameraControl->Set(CameraControl_Exposure, ne + is*((long)floor(exposure*float(xe - ne) / float(is))), CameraControl_Flags_Manual);
                        if (FAILED(hr)) {
                            VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set exposure: " << hexstr(hr);
                        }
                    }
                }
            }
            else if (exposureIt->is_string() && "auto" == exposureIt->get<std::string>()) {
                if (cameraControl != nullptr) {
                    hr = cameraControl->Set(CameraControl_Exposure, 0, CameraControl_Flags_Auto);
                    if (FAILED(hr)) {
                        VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set auto exposure" << hexstr(hr);
                    }
                }
            }
        }

#ifdef WITH_IDS_UEYE
        std::string awb;
        if (mjget<std::string>(mode, "awb", awb) && ueyeAutoParameter != nullptr) {
            hr = S_OK;
            if (awb == "off") {
                hr = ueyeAutoParameter->AutoParameter_SetEnableAWB(IS_AUTOPARAMETER_DISABLE);
                if (FAILED(hr)) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not disable AWB: " << hexstr(hr);
                }
            }
            else if (awb == "greyworld") {
                hr = ueyeAutoParameter->AutoParameter_SetAWBType(IS_AWB_GREYWORLD);
                if (FAILED(hr)) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not select AWB type GREYWORLD: " << hexstr(hr);
                }
                hr = ueyeAutoParameter->AutoParameter_SetEnableAWB(IS_AUTOPARAMETER_ENABLE);
                if (FAILED(hr)) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not enable AWB: " << hexstr(hr);
                }
            }
            else if (awb == "kelvin") {
                hr = ueyeAutoParameter->AutoParameter_SetAWBType(IS_AWB_COLOR_TEMPERATURE);
                if (FAILED(hr)) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not select AWB type GREYWORLD: " << hexstr(hr);
                }
                hr = ueyeAutoParameter->AutoParameter_SetEnableAWB(IS_AUTOPARAMETER_ENABLE);
                if (FAILED(hr)) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not enable AWB: " << hexstr(hr);
                }
            }
        }
#endif

    }

#ifdef WITH_IDS_UEYE
    static void ueyeSetAoiAndBinning(IuEyeAOI* ueyeAOI, IuEyeResample* ueyeResample, const json &mode) {
        static const int binModeY[] =
        { IS_BINNING_DISABLE
        , IS_BINNING_2X_VERTICAL
        , IS_BINNING_3X_VERTICAL
        , IS_BINNING_4X_VERTICAL
        , IS_BINNING_5X_VERTICAL
        , IS_BINNING_6X_VERTICAL
        , IS_BINNING_8X_VERTICAL
        , IS_BINNING_8X_VERTICAL
        , IS_BINNING_16X_VERTICAL };
        static const int binModeX[] =
        { IS_BINNING_DISABLE
        , IS_BINNING_2X_HORIZONTAL
        , IS_BINNING_3X_HORIZONTAL
        , IS_BINNING_4X_HORIZONTAL
        , IS_BINNING_5X_HORIZONTAL
        , IS_BINNING_6X_HORIZONTAL
        , IS_BINNING_8X_HORIZONTAL
        , IS_BINNING_8X_HORIZONTAL
        , IS_BINNING_16X_HORIZONTAL };
        static const int binFactor[] = { 1,2,3,4,5,6,8,8,16 };

        HRESULT hr;
        // set aoi to full matrix size
        RECT full;
        int minx, miny, maxx, maxy;
        ueyeAOI->AOI_GetMinMaxSizeX(&minx, &maxx);
        ueyeAOI->AOI_GetMinMaxSizeY(&miny, &maxy);
        full.right = maxx;
        full.bottom = maxy;
        ueyeAOI->AOI_GetMinMaxPosX(&minx, &maxx);
        ueyeAOI->AOI_GetMinMaxPosY(&miny, &maxy);
        full.left = minx;
        full.top = miny;
        //hr = ueyeAOI->AOI_SetImageAOI(full);
        //if (FAILED(hr)) {
        //    VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not set the AOI to full matrix size: " << hexstr(hr);
        //}

        // pick up binning mode to match the requested size
        int width = 0, height = 0;
        auto jsz = mode.find("size");
        if (jsz != mode.end()) {
            width = jsz.value().at(0).begin().value().get<int>();
            height = jsz.value().at(1).begin().value().get<int>();
        }
        if (width <= 0 && height <= 0)
            throw std::runtime_error("requested image width or height is zero or less");
        // disable subsampling and set binning so that resulting image is of closest possible size
        // to what is requested, but not less than requested in each dimension
        float binx = float(full.right - full.left) / width;
        float biny = float(full.bottom - full.top) / height;
        int ibinx = (int)floor(binx);
        int ibiny = (int)floor(biny);
        ibinx = std::max<int>(1, std::min<int>(9, ibinx));
        ibiny = std::max<int>(1, std::min<int>(9, ibiny));

        int pbx = binFactor[ibinx - 1];
        int pby = binFactor[ibiny - 1];

        int imgw = (full.right - full.left) / pbx;
        int imgh = (full.bottom - full.top) / pby;
        int dx = (imgw - width)*pbx / 2;
        int dy = (imgh - height)*pby / 2;
        RECT aoi = full;
        aoi.left += dx;
        aoi.right -= dx;
        aoi.top += dy;
        aoi.bottom -= dy;
        VNXVIDEO_LOG(VNXLOG_DEBUG, "dxcapture") << "Setting AOI to ("
            << aoi.left << ',' << aoi.top << ")-(" << aoi.right << ',' << aoi.bottom
            << ')';
        hr = ueyeAOI->AOI_SetImageAOI(aoi);
        if (FAILED(hr)) {
            VNXVIDEO_LOG(VNXLOG_WARNING, "dxcapture") << "Could not adjust AOI: " << hexstr(hr);
        }

        ueyeResample->Subsampling_SetMode(IS_SUBSAMPLING_DISABLE);
        const int binMode = binModeX[ibinx - 1] | binModeY[ibiny - 1];
        hr = ueyeResample->Binning_SetMode(binMode);
        // and adjust AOI (shrink it) so that resulting image size exactly matches requested size
        ////int imgw = 0, imgh = 0;
        ////ueyeResample->Binning_GetImageWidth(&imgw);
        ////ueyeResample->Binning_GetImageHeight(&imgh);
        unsigned long bv = 1, bh = 1;
        ueyeResample->Binning_GetHorizontalResolution(&bh);
        ueyeResample->Binning_GetVerticalResolution(&bv);
        VNXVIDEO_LOG(VNXLOG_DEBUG, "dxcapture") << "Binning factors set to " << bh << 'x' << bv;
    }
#endif

    CComPtr<IAMStreamConfig> AMStreamConfigFromPin(IPin* pin)
    {
        CComPtr<IAMStreamConfig> cfg;
        HRESULT hr = pin->QueryInterface(IID_IAMStreamConfig, (void**)&cfg);
        if (FAILED(hr))
        {
            //_log_ << "Could not get IAMStreamConfig from pin, hr=" << hr;
            return 0;
        }
        if (0 == cfg)
            throw std::runtime_error("Could not get IAMStreamConfig from pin (null was returned)");
        return cfg;
    }

    std::wstring GetPinId(IPin* pin)
    {
        LPWSTR pinid;
        HRESULT hr = pin->QueryId(&pinid);
        if (FAILED(hr))
            throw std::runtime_error("Could not IPin::QueryId");
        std::wstring res(pinid);
        CoTaskMemFree(pinid);
        return res;
    }
    void EnumPinCaps(IPin* pin, VnxVideo::TCapabilities& caps)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        HRESULT hr;
        std::wstring pinId(GetPinId(pin));
        CComPtr<IAMStreamConfig> cfg(AMStreamConfigFromPin(pin));
        if (!cfg)
        {
            //_log_ << "Could not get IAMStreamConfig from pin" << pinId;
            return;
        }
        int count, size;
        hr = cfg->GetNumberOfCapabilities(&count, &size);
        if (FAILED(hr))
            throw std::runtime_error("Could not GetNumberOfCapabilities");
        std::vector<BYTE> cap(size);
        for (int k = 0; k < count; ++k)
        {
            AM_MEDIA_TYPE* amt;
            cfg->GetStreamCaps(k, &amt, &cap[0]);

            BITMAPINFOHEADER *bih = 0;
            REFERENCE_TIME *fr = 0;
            if (IsEqualGUID(amt->formattype, FORMAT_VideoInfo))
            {
                VIDEOINFOHEADER *v = (VIDEOINFOHEADER*)amt->pbFormat;
                fr = &v->AvgTimePerFrame;
                bih = &v->bmiHeader;
            }
            /*else if (IsEqualGUID(amt->formattype, FORMAT_VideoInfo2))
            {
                VIDEOINFOHEADER2 *v = (VIDEOINFOHEADER2*) amt->pbFormat;
                fr = &v->AvgTimePerFrame;
                bih = &v->bmiHeader;
            }*/
            if (!!bih && !!fr)
            {
                std::stringstream ss;
                json j;
                j["pin"] = conv.to_bytes(pinId);
                j["colorspace"] = biCompressionToString(bih->biCompression, bih->biBitCount);
                j["size"] = { bih->biWidth, abs(bih->biHeight) };
                j["framerate"] = round(10000000.0 / float(*fr));
                /*
                ss << pinId << ' '
                    << bih->biCompression << ' '
                    << bih->biBitCount << ' '
                    << bih->biPlanes << ' '
                    << bih->biWidth << ' '
                    << abs(bih->biHeight) << ' '
                    << *fr;
                    */
                ss << j;
                caps.push_back(ss.str());
            }
            DeleteMediaType(amt);
        }
    }

    static std::string biCompressionToString(DWORD c, int bpp = 0) {
        switch (c) {
        case 0: return "RGB"+std::to_string(bpp);
        case 0x59455247: return "GREY";
        case 0x56595559: return "YUY2";// "YUYV";
        case 0x59565955: return "UYVY";
        case 0x32595559: return "YUY2";
        case 0x32315659: return "YV12";
        case 0x39555659: return "YVU9";
        case 0x56555949: return "IYUV";
        case 0x30323449: return "I420";
        //case 0x47504a4d: return "MJPEG";
        default: 
            char s[16];
            snprintf(s, 16, "0x%08x", c);
            return s;
        }
    }
    static DWORD biCompressionFromString(const std::string &s, int& bpp) {
        if (s.substr(0,3) == "RGB") {
            bpp = atoi(s.substr(3).c_str());
            return 0;
        }
        else if (s == "GREY") return 0x59455247;
        else if (s == "YUYV") return 0x56595559;
        else if (s == "UYVY") return 0x59565955;
        else if (s == "YUY2") return 0x32595559;
        else if (s == "YV12") return 0x32315659;
        else if (s == "YVU9") return 0x39555659;
        else if (s == "IYUV") return 0x56555949;
        else if (s == "I420") return 0x30323449;
        else {
            DWORD res = 0;
            if (sscanf_s(s.c_str(), "0x%x", &res))
                return res;
            else
                throw std::runtime_error("cannot parse bih->biCompression string representation");
        }
    }

    CComPtr<IBaseFilter> GetBaseFilter(IMoniker* moniker)
    {
        HRESULT hr;

        CComPtr<IBaseFilter> filter(0);
        // WARNING: this crap apparently leaks. instant release of filter does not release all allocated memory.
        // therefore don't use it repeatedly. only use it when really needed.
        hr = moniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&filter);
        if (FAILED(hr))
            throw std::runtime_error("BindToObject on a device moniker failed: "+hexstr(hr));
        
        return filter;
    }
};

namespace VnxVideo
{
    IVideoDeviceManager *CreateVideoDeviceManager_DirectShow()
    {
        return new CVideoDeviceManager();
    }
}
