#include <boost/interprocess/managed_heap_memory.hpp>
#ifdef _MSC_VER
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <windows.h>
#else
#include <boost/interprocess/managed_shared_memory.hpp>
#endif
#include <algorithm>

#include "RawSample.h"
#include "vnxvideologimpl.h"

#include "Win32Utils.h"

#ifdef _WIN32
#include <aclapi.h>
#endif

class CPrivateAllocator : public IAllocator {
public:
    virtual std::shared_ptr<uint8_t> Alloc(int size) {
        std::shared_ptr<uint8_t> res;
#ifdef _MSC_VER
        res.reset((uint8_t*)_aligned_malloc(size, 16), _aligned_free);
#else
        res.reset((uint8_t*)aligned_alloc(16, size), free); // LOL. spent two days on this: args swapped WRT MS impl
#endif
        return res;
    }
} g_privateAllocatorImpl;

IAllocator* const g_privateAllocator(&g_privateAllocatorImpl);

#ifdef _WIN32
typedef boost::interprocess::managed_windows_shared_memory TManagedSharedMemory;
const std::string ShmNamePrefix("Global\\viinex_shm_");
#else
typedef boost::interprocess::managed_shared_memory TManagedSharedMemory;
const std::string ShmNamePrefix("viinex_shm_");
#endif


class CShmAllocator : public IShmAllocator {
public:
    CShmAllocator(const char* name) 
    {
        const uint32_t maxSize = 1024 * 1024 * 32;
        const std::string mappingName(ShmNamePrefix + name);
#ifndef _WIN32
		boost::interprocess::shared_memory_object::remove(mappingName.c_str());
#endif
        m_heap.reset(new TManagedSharedMemory(boost::interprocess::open_or_create, mappingName.c_str(), maxSize));
#ifdef _WIN32
        DWORD res = SetNamedSecurityInfoA(const_cast<char*>(mappingName.c_str()),
            SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
            NULL, NULL, BuildDacl777().get(), NULL);
#endif
    }
    virtual std::shared_ptr<uint8_t> Alloc(int size) {
        std::shared_ptr<uint8_t> res;
        try {
            auto heap(m_heap);
            res.reset((uint8_t*)m_heap->allocate_aligned(size, 16), [heap](void* ptr) { heap->deallocate(ptr); });
        }
        catch (const boost::interprocess::bad_alloc& e) {
            // just return empty pointer
            VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "CShmAllocator::Alloc() failed: " << e.what();
        }
        return res;
    }
    virtual uint64_t FromPointer(void* ptr) {
        if (!m_heap->belongs_to_segment(ptr)) {
            throw std::runtime_error("CShmAllocator::FromPointer(): given pointer does not belong to this allocator");
        }
        return (uint64_t)m_heap->get_handle_from_address(ptr);
    }
    virtual void* ToPointer(uint64_t offset) {
        return m_heap->get_address_from_handle((ptrdiff_t)offset);
    }
private:
    std::shared_ptr<TManagedSharedMemory> m_heap;
};

class CShmMapping : public IShmMapping {
public:
    CShmMapping(const char* name)
        :m_heap(new TManagedSharedMemory(boost::interprocess::open_read_only, (ShmNamePrefix + name).c_str()))
    {
    }
    virtual uint64_t FromPointer(void* ptr) {
        return (uint64_t)m_heap->get_handle_from_address(ptr);
    }
    virtual void* ToPointer(uint64_t offset) {
        return m_heap->get_address_from_handle((ptrdiff_t)offset);
    }
private:
    std::shared_ptr<TManagedSharedMemory> m_heap;
};


IShmAllocator *CreateShmAllocator(const char* name) {
    return new CShmAllocator(name);
}

IShmMapping* CreateShmMapping(const char* name) {
    return new CShmMapping(name);
}

thread_local PShmAllocator* g_preferredAllocator=nullptr;
class CWithPreferredAllocator {
public:
    CWithPreferredAllocator(PShmAllocator allocator)
        : m_allocator(allocator)
        , m_prev(g_preferredAllocator) {
        g_preferredAllocator = &m_allocator;
    }
    ~CWithPreferredAllocator() {
        g_preferredAllocator = m_prev;
    }
private:
    PShmAllocator m_allocator;
    PShmAllocator* m_prev;
};

void WithPreferredShmAllocator(PShmAllocator allocator, std::function<void(void)> action) {
    CWithPreferredAllocator x(allocator);
    action();
}
PShmAllocator GetPreferredShmAllocator() {
    if (g_preferredAllocator != nullptr)
        return *g_preferredAllocator;
    else
        return PShmAllocator();
}

namespace VnxVideo {
    void WithPreferredShmAllocator(const char* name, std::function<void(void)> action) {
        WithPreferredShmAllocator(PShmAllocator(CreateShmAllocator(name)), action);
    }
}
