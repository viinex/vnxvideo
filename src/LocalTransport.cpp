#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "RawSample.h"

#include <set>
#include <vector>
#include <thread>
#include <mutex>
#include <system_error>
#include <boost/asio.hpp>
#ifdef _WIN32
#include <boost/asio/windows/stream_handle.hpp>
#else
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#endif
#include <boost/bind.hpp>

#include "Win32Utils.h"

#pragma pack(push)
#pragma pack(1)
struct SRawSampleMsg {
    uint64_t pointer; // it's ptrdiff_t but we want 32 and 64-bit processes to be binary compatible wrt this struct
    uint64_t timestamp;
    int width;
    int height;
    EColorspace colorspace;
    int nplanes;
    int offsets[4];
    int strides[4];
    // total 4*16=64 bytes
};
#pragma pack(pop)

const uint64_t CMD_REQUEST = 0;
const uint64_t CMD_FREE = 1;
const uint64_t CMD_FREE_AND_REQUEST = 2;

#ifdef _WIN32
typedef boost::asio::windows::stream_handle pipe_t;
#else
typedef boost::asio::local::stream_protocol::socket pipe_t;
#endif

struct SConnection { // connection to a local transport client:
    pipe_t pipe; // communication pipe
    std::map<uint64_t, std::shared_ptr<VnxVideo::IRawSample> > samples; // samples held by that client
    uint64_t timestamp; // timestamp of the last sample sent to the client
    SRawSampleMsg out; // frames sent to the client
    uint64_t in[2]; // commands received from client: [0, _] -> request frame, [1, handle] -> free frame.
    int read;

#ifdef _WIN32
    SConnection(boost::asio::io_service& ios, pipe_t::native_handle_type handle)
        : pipe(ios, handle)
        , timestamp(0)
        , read(0)
    {
    }
#else
    SConnection(boost::asio::io_service& ios)
        : pipe(ios)
        , timestamp(0)
        , read(0)
    {
    }
#endif
};

#ifdef _WIN32
const std::string PIPE_PATH_PREFIX = "\\\\.\\pipe\\viinex_pipe_";
#else
const std::string PIPE_PATH_PREFIX = "/tmp/viinex_pipe_";
#endif

// OUTPUT video channel. A local named pipe server 
// which publishes a raw video stream and allows other 
// processes to subscribe to it using the CLocalVideoClients.
class CLocalVideoProvider : public VnxVideo::IRawProc {
private:

public:
    CLocalVideoProvider(const std::string& address, PShmAllocator allocator= PShmAllocator())
        : m_address(address)
        , m_allocator(allocator) 
        , m_timestamp(0)
        , m_shutdown(false)
#ifndef _WIN32
        , m_acceptor(m_ios)
#endif
    {
        if (!m_allocator.get()) {
            m_allocator.reset(CreateShmAllocator(address.c_str()));
        }
        auto pios(&m_ios);
        bind();
        listen();
        m_thread = std::move(std::thread([pios]() {pios->run(); }));
    }
#ifdef _WIN32
    void bind() {}
    void listen() {
        std::shared_ptr<SECURITY_ATTRIBUTES> psa(BuildSecurityAttributes777());

        pipe_t::native_handle_type pipeh = CreateNamedPipeA((PIPE_PATH_PREFIX + m_address).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE| PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 8192, 8192, 10000, psa.get());
        if (INVALID_HANDLE_VALUE == pipeh)
            throw std::system_error(GetLastError(), std::system_category());

        auto conn = std::make_shared<SConnection>(m_ios, pipeh);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connections.insert(conn);

        boost::asio::windows::overlapped_ptr op(m_ios, 
            boost::bind(&CLocalVideoProvider::acceptHandler, this, conn, _1, _2));
        BOOL ok = ConnectNamedPipe(conn->pipe.native_handle(), op.get());
        int err = GetLastError();
        if (!ok && err != ERROR_IO_PENDING) {
            op.complete(boost::system::error_code(err, boost::system::system_category()), 0);
        }
        else
            op.release();
    }
#else
    void bind(){
      std::string addr(PIPE_PATH_PREFIX + m_address);
        unlink(addr.c_str());
        boost::asio::local::stream_protocol::endpoint endpoint(addr);
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::local::stream_protocol::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();
    }
    void listen(){
        auto conn = std::make_shared<SConnection>(m_ios);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connections.insert(conn);

        m_acceptor.async_accept(conn->pipe,
                                boost::bind(&CLocalVideoProvider::acceptHandler,
                                            this, conn, _1, 0));
    }
#endif
    void acceptHandler(std::shared_ptr<SConnection> conn, const boost::system::error_code & ec, size_t n) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_shutdown) {
            return;
        }
        lock.unlock();
        listen(); // the connection is accepted, but we should start to listen for a new one
        if (!ec) {
            VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Accepted connection to local transport";
            scheduleRead(conn);
        }
        else {
            VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Listening for connection to local transport failed: " << ec;
        }
    }
    void scheduleRead(std::shared_ptr<SConnection> conn, bool cont=false) {
        if(!cont)
            conn->read = 0;
        boost::asio::async_read(conn->pipe,
            boost::asio::buffer((uint8_t*)(conn->in) + conn->read,
                sizeof(conn->in) - conn->read),
            boost::bind(&CLocalVideoProvider::readHandler, this, conn, _1, _2));
    }
    void readHandler(std::shared_ptr<SConnection> conn, const boost::system::error_code & ec, size_t n){
        if (ec){
            if (ec != boost::system::errc::operation_canceled) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "Error on reading from named pipe: " << ec;
                dropConnection(conn);
            }
            // otherwise it's canceled and we do nothing
        }
        else {
            conn->read += n;
            if (conn->read == sizeof(conn->in)) {
                const uint64_t command = conn->in[0];
                const uint64_t argument = conn->in[1];
                if (command == CMD_REQUEST)
                    requestFrame(conn);
                else if (command == CMD_FREE)
                    freeFrame(conn, argument);
                else if (command == CMD_FREE_AND_REQUEST) {
                    freeFrame(conn, argument);
                    requestFrame(conn);
                }
                else
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "Unknown command in local transport: " << conn->in[0];
                conn->read = 0;
                boost::asio::async_read(conn->pipe, boost::asio::buffer(conn->in, sizeof(conn->in)),
                    boost::bind(&CLocalVideoProvider::readHandler, this, conn, _1, _2));
            }
            else {
                scheduleRead(conn, true);
            }
        }
    }
    void requestFrame(std::shared_ptr<SConnection> conn) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_sample.get() && (conn->timestamp < m_timestamp))
            sendCurrentFrame(conn); // under the lock!
        else
            m_starving.insert(conn);
    }
    void freeFrame(std::shared_ptr<SConnection> conn, uint64_t frameId) {
        std::unique_lock<std::mutex> lock(m_mutex);
        conn->samples.erase(frameId);
    }
    void sendCurrentFrame(std::shared_ptr<SConnection> conn) { // under the lock!!!
        conn->timestamp = m_timestamp;

        SRawSampleMsg& m = conn->out;
        uint8_t* planes[4];
        m.timestamp = m_timestamp;
        m_sample->GetFormat(m.colorspace, m.width, m.height);
        m_sample->GetData(m.strides, planes);
        uint8_t* p0 = planes[0];
        for (int k = 0; k < 4; ++k)
            m.offsets[k] = planes[k] - p0;

        uint64_t pointer = m_allocator->FromPointer(planes[0]);
        m.pointer = pointer;
        conn->samples[pointer] = m_sample;

        boost::asio::async_write(conn->pipe, boost::asio::buffer(&conn->out, sizeof(conn->out)),
            boost::bind(&CLocalVideoProvider::writeHandler, this, conn, _1, _2));
    }
    void writeHandler(std::shared_ptr<SConnection> conn, const boost::system::error_code & ec, size_t n) {
        if (ec){
            if (ec != boost::system::errc::operation_canceled) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "Error on writing to named pipe: " << ec;
                dropConnection(conn);
            }
            // otherwise it's canceled and we do nothing
        }
        else if (n == sizeof(conn->out)) {
        }
    }

    void dropConnection(std::shared_ptr<SConnection> conn) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connections.erase(conn);
        m_starving.erase(conn);
        try {
            conn->pipe.cancel();
            conn->pipe.close();
        }
        catch(const std::exception&){
        }
    }

    ~CLocalVideoProvider() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_shutdown = true;
#ifndef _WIN32
        try {
            m_acceptor.cancel();
        }
        catch(const std::exception&){
        }
#endif
        for (auto p : m_connections) {
            if(p->pipe.is_open()){
                p->pipe.cancel();
                p->pipe.close();
            }
        }
        lock.unlock();
        if(m_thread.get_id()!=std::thread().get_id())
            m_thread.join();
    }
public:
    void SetFormat(EColorspace csp, int width, int height) {}
    void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sample.reset(sample->Dup());
        m_timestamp = timestamp;

        for (auto c = m_starving.begin(); c != m_starving.end();) {
            if ((*c)->timestamp < m_timestamp) {
                sendCurrentFrame(*c);
                c = m_starving.erase(c);
            }
            else
                ++c;
        }
    }
    void Flush() {}
private:
    boost::asio::io_service m_ios;
#ifndef _WIN32
    boost::asio::local::stream_protocol::acceptor m_acceptor;
#endif
    std::mutex m_mutex;
    std::set<std::shared_ptr<SConnection> > m_connections;
    std::set<std::shared_ptr<SConnection> > m_starving;
    std::thread m_thread;
    const std::string m_address;
    PShmAllocator m_allocator;

    std::shared_ptr<VnxVideo::IRawSample> m_sample;
    uint64_t m_timestamp;
    bool m_shutdown;
};

// INPUT video channel. A local named pipe client
// which acquires a raw video stream from a CLocalVideoProvider.
// In own process represents (makes a proxy to) the original video source.
class CLocalVideoClient : public VnxVideo::IVideoSource {
public:
    CLocalVideoClient(const std::string& address)
        : m_address(address)
        , m_timer(m_ios)
        , m_pipe(m_ios)

        , m_width(0)
        , m_height(0)
        , m_csp(EColorspace::EMF_NONE)

        , m_running(false)
    {
    }
#ifdef _WIN32
    void openPipe() {
        pipe_t::native_handle_type pipeh = CreateFileA((PIPE_PATH_PREFIX + m_address).c_str(),
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
        if (INVALID_HANDLE_VALUE == pipeh) {
            DWORD err = GetLastError();
            throw std::system_error(err, std::system_category());
        }
        m_pipe = pipe_t(m_ios, pipeh);
    }
#else
    void openPipe() {
        boost::asio::local::stream_protocol::endpoint endpoint(PIPE_PATH_PREFIX + m_address);
        pipe_t pipe(m_ios);
        pipe.connect(endpoint);
        m_pipe = std::move(pipe);
    }
#endif
    void openShm() {
        m_mapping.reset(CreateShmMapping(m_address.c_str()));
        m_shared.reset(new SFree());
        m_shared->mapping = m_mapping;
    }
  
    void prepareCommandBuffer() {
        m_commandBuffer.clear();
        std::unique_lock<std::mutex> lock(m_shared->mutex);
        for (uint64_t ptr : m_shared->free) {
            m_commandBuffer.push_back(CMD_FREE);
            m_commandBuffer.push_back(ptr);
        }
        m_shared->free.clear();
        m_commandBuffer.push_back(CMD_REQUEST);
        m_commandBuffer.push_back(0);
    }

    void connect() {
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CLocalVideoClient::connect: About to connect";
        openPipe();
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CLocalVideoClient::connect: Pipe/socket opened";
        openShm();
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CLocalVideoClient::connect: Shared memory segment opened";
        prepareCommandBuffer(); // it's always free what has to be freed, and then request
        boost::asio::async_write(m_pipe, boost::asio::buffer(m_commandBuffer),
            boost::bind(&CLocalVideoClient::writeHandler, this, _1, _2));
    }
    void writeHandler(const boost::system::error_code & ec, size_t n) {
        if (ec && ec != boost::system::errc::operation_canceled) {
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CLocalVideoClient::writeHandler: Error on writing to named pipe: " << ec;
            scheduleReconnect();
        }
        else if (n == m_commandBuffer.size()*sizeof(uint64_t)) {
            boost::asio::async_read(m_pipe, boost::asio::buffer(&m_sample, sizeof(m_sample)),
                boost::bind(&CLocalVideoClient::readHandler, this, _1, _2));
        }
    }
    void readHandler(const boost::system::error_code & ec, size_t n) {
        if (ec && ec != boost::system::errc::operation_canceled) {
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CLocalVideoClient::readHandler: Error on reading to named pipe: " << ec;
            scheduleReconnect();
        }
        else if (n == sizeof(m_sample)) {
            sendSample();
            prepareCommandBuffer();
            boost::asio::async_write(m_pipe, boost::asio::buffer(m_commandBuffer),
                boost::bind(&CLocalVideoClient::writeHandler, this, _1, _2));
        }
    }

    void scheduleReconnect() {
        try {
            m_pipe.close();
        }
        catch (const boost::system::system_error&) {
        }
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_running)
                return;
        }
        m_timer.expires_from_now(std::chrono::nanoseconds(1000000000));
        m_timer.async_wait([this](const boost::system::error_code & ec) {
            if (!ec) {
                try {
                    connect();
                }
                catch (const std::exception& e) {
                    VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CLocalVideoClient::scheduleReconnect: " << e.what();
                    scheduleReconnect();
                }
            }
        });
    }

    void sendSample() {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool formatUnchanged = (m_width == m_sample.width) 
            && (m_height == m_sample.height) && (m_csp == m_sample.colorspace);
        if (!formatUnchanged) {
            m_width = m_sample.width;
            m_height = m_sample.height;
            m_csp = m_sample.colorspace;
        }
        auto onFrame(m_onFrame);
        auto onFormat(m_onFormat);
        lock.unlock();
        if (!formatUnchanged)
            onFormat(m_csp, m_width, m_height);
        
        uint8_t *planes[4];
        memset(planes, 0, sizeof planes);
        // fill samples in here.
        uint8_t* pointer((uint8_t*)m_mapping->ToPointer(m_sample.pointer));
        for (int k = 0; k < 4; ++k) {
            planes[k] = pointer + m_sample.offsets[k];
        }

        // free the memory in dtor (return it to server) (not really return immediately but schedule to return)
        auto shared(m_shared);
        std::shared_ptr<void> sharedDtor((void*)m_sample.pointer, [shared](void* p) {
            uint64_t s = (uint64_t)p;
            std::unique_lock<std::mutex> lock(shared->mutex);
            shared->free.push_back(s);
        }); 
        CRawSample sample(m_csp, m_width, m_height, m_sample.strides, planes, sharedDtor);
        uint64_t timestamp = m_sample.timestamp;
        onFrame(&sample, timestamp);
    }

    ~CLocalVideoClient() {
        Stop();
    }
private:
    const std::string m_address;
    boost::asio::io_service m_ios;
    boost::asio::steady_timer m_timer;
    pipe_t m_pipe;
    std::shared_ptr<IShmMapping> m_mapping;
    bool m_running;
    std::mutex m_mutex;
    std::thread m_thread;

    // IDs of samples to be freed on next interaction with server.
    // access this vector with m_mutex locked.
    // Have to make it a separate sharedptr because samples can live longer than this.
    // For the same reason we put the shared pointer to shm mapping into the same struct
    struct SFree {
        std::shared_ptr<IShmMapping> mapping;
        std::mutex mutex;
        std::vector<uint64_t> free;
    };
    std::shared_ptr<SFree> m_shared;

    SRawSampleMsg m_sample; // frame received from the server
    // commands sent to the server: sequence of pairs: [0, _] -> request frame, [1, handle] -> free frame.
    std::vector<uint64_t> m_commandBuffer;

    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;

    int m_width;
    int m_height;
    EColorspace m_csp;
public: // IVideoSource
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }
    virtual void Run() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_running = true;
        }
        try {
            connect();
        }
        catch (const std::exception&) {
            scheduleReconnect();
        }
        auto pios(&m_ios);
        m_thread = std::move(std::thread([pios]() {pios->run(); }));
    }
    virtual void Stop() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_timer.cancel();
        try {
            m_pipe.cancel();
            m_pipe.close();
        }
        catch (const std::exception&) {
        }
        if (m_thread.get_id() != std::thread().get_id())
            m_thread.join();
    }

};

namespace VnxVideo {
    IVideoSource *CreateLocalVideoClient(const char* name) {
        return new CLocalVideoClient(name);
    }

    IRawProc *CreateLocalVideoProvider(const char* name) {
        return new CLocalVideoProvider(name, DupPreferredShmAllocator());
    }
}
