#pragma optimize("",off)
// originated from
// https://github.com/rdp/open-source-directshow-video-capture-demo-filter/blob/master/vcam_vs_2010_demo_video_capture_project/vcam_vs_2010/Filters.cpp

#include <streams.h>
#include <stdio.h>
#include <olectl.h>
#include <dvdmedia.h>
#include "VirtualCam.h"

#include "../RawSample.h"

extern "C" {
#include <libswscale/swscale.h>
}

//////////////////////////////////////////////////////////////////////////
//  CVCam is the source filter which masquerades as a capture device
//////////////////////////////////////////////////////////////////////////
CUnknown * WINAPI CVCam::CreateInstance(LPUNKNOWN lpunk, HRESULT *phr)
{
    ASSERT(phr);
    try {
        CUnknown* punk = new CVCam(lpunk, phr);
        return punk;
    }
    catch (const std::exception&) {
        if (!*phr)
            *phr = E_FAIL;
        return nullptr;
    }
}

CVCam::CVCam(LPUNKNOWN lpunk, HRESULT *phr) :
    CSource(NAME(VIRTUALCAM_NAME), lpunk, CLSID_ViinexVirtualCamera)
{
    ASSERT(phr);
    CAutoLock cAutoLock(&m_cStateLock);
    // Create the one and only output pin
    m_paStreams = (CSourceStream **) new CVCamStream*[1];
    m_paStreams[0] = new CVCamStream(phr, this, L"Output");
}

HRESULT CVCam::QueryInterface(REFIID riid, void **ppv)
{
    //Forward request for IAMStreamConfig & IKsPropertySet to the pin
    if (riid == _uuidof(IAMStreamConfig) || riid == _uuidof(IKsPropertySet))
        return m_paStreams[0]->QueryInterface(riid, ppv);
    else
        return CSource::QueryInterface(riid, ppv);
}

//////////////////////////////////////////////////////////////////////////
// CVCamStream is the one and only output pin of CVCam which handles 
// all the stuff.
//////////////////////////////////////////////////////////////////////////
CVCamStream::CVCamStream(HRESULT *phr, CVCam *pParent, LPCWSTR pPinName) :
    CSourceStream(NAME(VIRTUALCAM_NAME), phr, pParent, pPinName), m_pParent(pParent)
    ,m_csp(EMF_NONE),m_width(0),m_height(0),m_timestamp(0),m_timestamp0(0)
{
    m_source.reset(VnxVideo::CreateLocalVideoClient("rend0"));
    m_source->Subscribe([this](EColorspace csp, int width, int height) {this->onFormat(csp, width, height); },
        [this](VnxVideo::IRawSample* sample, uint64_t timestamp) {this->onFrame(sample, timestamp); });
    m_source->Run();

    m_rgb = false;

    m_mt.InitMediaType();
    m_mtrgb.InitMediaType();
    m_mtyuv.InitMediaType();
    InitMediaType(true, &m_mtrgb, nullptr);
    InitMediaType(false, &m_mtyuv, nullptr);
    InitMediaType(false, &m_mt, nullptr);
}

void CVCamStream::onFormat(EColorspace csp, int width, int height) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_csp = csp;
    m_width = width;
    m_height = height;
    m_condition.notify_all();
}
void CVCamStream::onFrame(VnxVideo::IRawSample* sample, uint64_t timestamp) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_sample.reset(sample->Dup());
    m_timestamp = timestamp;
    m_condition.notify_all();
}


CVCamStream::~CVCamStream()
{
    m_source->Stop();
}

HRESULT CVCamStream::QueryInterface(REFIID riid, void **ppv)
{
    // Standard OLE stuff
    if (riid == _uuidof(IAMStreamConfig))
        *ppv = (IAMStreamConfig*)this;
    else if (riid == _uuidof(IKsPropertySet))
        *ppv = (IKsPropertySet*)this;
    else
        return CSourceStream::QueryInterface(riid, ppv);

    AddRef();
    return S_OK;
}


//////////////////////////////////////////////////////////////////////////
//  This is the routine where we create the data being output by the Virtual
//  Camera device.
//////////////////////////////////////////////////////////////////////////

HRESULT CVCamStream::FillBuffer(IMediaSample *pms)
{
    REFERENCE_TIME rtNow;

    REFERENCE_TIME avgFrameTime = ((VIDEOINFOHEADER*)m_mt.pbFormat)->AvgTimePerFrame;

    rtNow = m_rtLastTime;
    m_rtLastTime += avgFrameTime;

    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_sample.get() == nullptr)
        m_condition.wait_for(lock, std::chrono::milliseconds(2000));
    if (!m_sample.get())
        return -1;
    VnxVideo::PRawSample sample(m_sample);
    uint64_t timestamp(m_timestamp);
    if (0 == m_timestamp0) {
        m_timestamp0 = m_timestamp;
        CRefTime graphNow;
        m_pParent->StreamTime(graphNow);
        m_graphTimestamp0 = graphNow;
    }
    m_sample.reset();
    int width(m_width);
    int height(m_height);
    lock.unlock();

    int strides[4];
    uint8_t* planes[4];
    sample->GetData(strides, planes);

    BYTE *pData;
    long lDataLen;
    pms->GetPointer(&pData);
    lDataLen = pms->GetSize();

    if (m_rgb) {
        if (!m_swsc) {
            m_swsc.reset(sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                width, height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
        }
        uint8_t* dstplanes[4] = { pData + width * (height - 1) * 3,0,0,0 };
        int dststrides[4] = { -width * 3,0,0,0 };
        int res = sws_scale(m_swsc.get(), planes, strides, 0, height, dstplanes, dststrides);
        if (res <= 0) {

        }

    }
    else {
        int dststrides[4] = { m_width, m_width / 2, m_width / 2, 0 };
        ptrdiff_t dstoffsets[4] = { 0, m_width * m_height, m_width * m_height * 5 / 4, 0 };

        uint8_t* dstplanes[4];
        for (int k = 0; k < 3; ++k)
            dstplanes[k] = pData + dstoffsets[k];

        CRawSample::CopyRawToI420(width, height, EMF_I420, planes, strides, dstplanes, dststrides);
    }

    REFERENCE_TIME now = (timestamp - m_timestamp0) * 10000; // milliseconds to 100 nanoseconds
    REFERENCE_TIME endThisFrame = now + avgFrameTime;
    pms->SetTime((REFERENCE_TIME *)&now, &endThisFrame);
    pms->SetSyncPoint(TRUE);
    
    pms->SetDiscontinuity(FALSE);

    return NOERROR;
} // FillBuffer


  //
  // Notify
  // Ignore quality management messages sent from the downstream filter
STDMETHODIMP CVCamStream::Notify(IBaseFilter * pSender, Quality q)
{
    return E_NOTIMPL;
} // Notify

  //////////////////////////////////////////////////////////////////////////
  // This is called when the output format has been negotiated
  //////////////////////////////////////////////////////////////////////////
HRESULT CVCamStream::SetMediaType(const CMediaType *pmt)
{
    if (*pmt == m_mtrgb)
        m_rgb = true;
    else if (*pmt == m_mtyuv)
        m_rgb = false;
    else
        return VFW_E_TYPE_NOT_ACCEPTED;
    DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->Format());
    HRESULT hr = CSourceStream::SetMediaType(pmt);
    return hr;
}

// See Directshow help topic for IAMStreamConfig for details on this method
HRESULT CVCamStream::GetMediaType(int iPosition, CMediaType* pmt)
{
    if (iPosition == 0)
    {
        *pmt = m_mtyuv;
        return S_OK;
    }
    else if (iPosition == 1) 
    {
        *pmt = m_mtrgb;
        return S_OK;
    }
    else if (iPosition > 0) 
        return VFW_S_NO_MORE_ITEMS;
    else 
        return E_INVALIDARG;
}

void initMediaTypeCommon(int width, int height, CMediaType* pmt) {
    DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));

    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = width;
    pvi->bmiHeader.biHeight = height;
    pvi->bmiHeader.biClrImportant = 0;

    pvi->AvgTimePerFrame = 333333; // 100ns

    SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
    SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

    pmt->SetType(&MEDIATYPE_Video);
    pmt->SetFormatType(&FORMAT_VideoInfo);
    pmt->SetTemporalCompression(FALSE);
}

void initMediaTypeRGB(int width, int height, CMediaType* pmt) {
    DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    pvi->bmiHeader.biCompression = BI_RGB;
    pvi->bmiHeader.biBitCount = 24;
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biSizeImage = width * height * 3;
    pmt->SetSubtype(&MEDIASUBTYPE_RGB24);
}

void initMediaTypeYUV(int width, int height, CMediaType* pmt) {
    DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    pvi->bmiHeader.biCompression = MAKEFOURCC('I', 'Y', 'U', 'V');
    pvi->bmiHeader.biBitCount = 12;
    pvi->bmiHeader.biPlanes = 3;
    pvi->bmiHeader.biSizeImage = width * height * 3 / 2;
    pmt->SetSubtype(&MEDIASUBTYPE_IYUV);
}


void CVCamStream::InitMediaType(bool rgb, CMediaType* pmt, BYTE* pSCC)
{
    int width, height;
    EColorspace csp;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_csp != EMF_I420)
            m_condition.wait_for(lock, std::chrono::milliseconds(2000));
        if (m_csp != EMF_I420)
            throw std::runtime_error("Source colorspace other than I420 not supported");
        csp = m_csp;
        width = m_width;
        height = m_height;
    }

    initMediaTypeCommon(width, height, pmt);
    if (rgb)
        initMediaTypeRGB(width, height, pmt);
    else
        initMediaTypeYUV(width, height, pmt);

    DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);

    if (!pSCC)
        return;

    DECLARE_PTR(VIDEO_STREAM_CONFIG_CAPS, pvscc, pSCC);

    pvscc->guid = FORMAT_VideoInfo;
    pvscc->VideoStandard = AnalogVideo_None;
    pvscc->InputSize.cx = width;
    pvscc->InputSize.cy = height;
    pvscc->MinCroppingSize.cx = width;
    pvscc->MinCroppingSize.cy = height;
    pvscc->MaxCroppingSize.cx = width;
    pvscc->MaxCroppingSize.cy = height;
    pvscc->CropGranularityX = 80;
    pvscc->CropGranularityY = 60;
    pvscc->CropAlignX = 0;
    pvscc->CropAlignY = 0;

    pvscc->MinOutputSize.cx = width;
    pvscc->MinOutputSize.cy = height;
    pvscc->MaxOutputSize.cx = width;
    pvscc->MaxOutputSize.cy = height;
    pvscc->OutputGranularityX = 0;
    pvscc->OutputGranularityY = 0;
    pvscc->StretchTapsX = 0;
    pvscc->StretchTapsY = 0;
    pvscc->ShrinkTapsX = 0;
    pvscc->ShrinkTapsY = 0;
    pvscc->MinFrameInterval = 200000;   //50 fps
    pvscc->MaxFrameInterval = 50000000; // 0.2 fps
    pvscc->MinBitsPerSecond = (80 * 60 * 3 * 8) / 5;
    pvscc->MaxBitsPerSecond = 640 * 480 * 3 * 8 * 50;
}

  // This method is called to see if a given output format is supported
HRESULT CVCamStream::CheckMediaType(const CMediaType *pMediaType)
{
    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(pMediaType->Format());
    if (*pMediaType != m_mtrgb && *pMediaType != m_mtyuv)
        return E_INVALIDARG;
    return S_OK;
} // CheckMediaType

  // This method is called after the pins are connected to allocate buffers to stream data
HRESULT CVCamStream::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties)
{
    CAutoLock cAutoLock(m_pFilter->pStateLock());
    HRESULT hr = NOERROR;

    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)m_mt.Format();
    pProperties->cBuffers = 1;
    pProperties->cbBuffer = pvi->bmiHeader.biSizeImage;

    ALLOCATOR_PROPERTIES Actual;
    hr = pAlloc->SetProperties(pProperties, &Actual);

    if (FAILED(hr)) return hr;
    if (Actual.cbBuffer < pProperties->cbBuffer) return E_FAIL;

    return NOERROR;
} // DecideBufferSize

  // Called when graph is run
HRESULT CVCamStream::OnThreadCreate()
{
    m_rtLastTime = 0;
    return NOERROR;
} // OnThreadCreate


  //////////////////////////////////////////////////////////////////////////
  //  IAMStreamConfig
  //////////////////////////////////////////////////////////////////////////

HRESULT STDMETHODCALLTYPE CVCamStream::SetFormat(AM_MEDIA_TYPE *pmt)
{
    m_mt = *pmt;
    IPin* pin=nullptr;
    ConnectedTo(&pin);
    if (pin)
    {
        IFilterGraph *pGraph = m_pParent->GetGraph();
        pGraph->Reconnect(this);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetFormat(AM_MEDIA_TYPE **ppmt)
{
    *ppmt = CreateMediaType(&m_mt);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetNumberOfCapabilities(int *piCount, int *piSize)
{
    *piCount = 2;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC)
{
    try {
        if (iIndex == 0) {
            *pmt = CreateMediaType(&m_mtyuv);
            InitMediaType(false, (CMediaType*)*pmt, pSCC);
        }
        else if (iIndex == 1) {
            *pmt = CreateMediaType(&m_mtrgb);
            InitMediaType(true, (CMediaType*)*pmt, pSCC);
        }
        return S_OK;
    }
    catch (const std::exception&) {
        DeleteMediaType(*pmt);
        *pmt = nullptr;
        return E_FAIL;
    }
}

//////////////////////////////////////////////////////////////////////////
// IKsPropertySet
//////////////////////////////////////////////////////////////////////////


HRESULT CVCamStream::Set(REFGUID guidPropSet, DWORD dwID, void *pInstanceData,
    DWORD cbInstanceData, void *pPropData, DWORD cbPropData)
{// Set: Cannot set any properties.
    return E_NOTIMPL;
}

// Get: Return the pin category (our only property). 
HRESULT CVCamStream::Get(
    REFGUID guidPropSet,   // Which property set.
    DWORD dwPropID,        // Which property in that set.
    void *pInstanceData,   // Instance data (ignore).
    DWORD cbInstanceData,  // Size of the instance data (ignore).
    void *pPropData,       // Buffer to receive the property data.
    DWORD cbPropData,      // Size of the buffer.
    DWORD *pcbReturned     // Return the size of the property.
)
{
    if (guidPropSet != AMPROPSETID_Pin)             return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY)        return E_PROP_ID_UNSUPPORTED;
    if (pPropData == NULL && pcbReturned == NULL)   return E_POINTER;

    if (pcbReturned) *pcbReturned = sizeof(GUID);
    if (pPropData == NULL)          return S_OK; // Caller just wants to know the size. 
    if (cbPropData < sizeof(GUID))  return E_UNEXPECTED;// The buffer is too small.

    *(GUID *)pPropData = PIN_CATEGORY_CAPTURE;
    return S_OK;
}

// QuerySupported: Query whether the pin supports the specified property.
HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport)
{
    if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    // We support getting this property, but not setting it.
    if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
    return S_OK;
}
