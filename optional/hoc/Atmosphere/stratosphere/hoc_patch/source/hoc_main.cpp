// SPDX-License-Identifier: GPL-2.0-only
#include "hoc_service.hpp"

namespace ams {
    namespace hoc_patch {
        namespace {
            alignas(0x40) constinit u8 g_heap_memory[32_KB];
            lmem::HeapHandle g_heap;

            struct ServerOptions {
                static constexpr size_t PointerBufferSize = 0x200;
                static constexpr size_t MaxDomains = 0;
                static constexpr size_t MaxDomainObjects = 0;
                static constexpr bool CanDeferInvokeRequest = false;
                static constexpr bool CanManageMitmServers = false;
            };
            constexpr size_t MaxSessions = 4;
            sf::hipc::ServerManager<1, ServerOptions, MaxSessions> g_server_manager;
            constinit sf::UnmanagedServiceObject<impl::IService, Service> g_service;

            void InitializeHeap() {
                g_heap = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory), lmem::CreateOption_None);
            }
            void *Allocate(size_t size) { return lmem::AllocateFromExpHeap(g_heap, size); }
            void Deallocate(void *p) { lmem::FreeToExpHeap(g_heap, p); }

            void Run() {
                InitializeService();
                Handle port = INVALID_HANDLE;
                R_ABORT_UNLESS(svcManageNamedPort(&port, nexus_hoc::ServiceName, MaxSessions));
                g_server_manager.RegisterObjectForServer(g_service.GetShared(), port);
                // A single dispatcher serializes every session and OC invocation.
                g_server_manager.LoopProcess();
            }
        }
    }

    namespace init {
        void InitializeSystemModule() {
            hoc_patch::InitializeHeap();
            R_ABORT_UNLESS(sm::Initialize());
            spl::Initialize();
            ams::CheckApiVersion();
            // No filesystem, settings, PCV, Loader or SD dependency here. I2C is
            // acquired by the unchanged engine only when Loader prepares PCV.
        }
        void FinalizeSystemModule() { }
        void Startup() { }
    }

    void NORETURN Exit(int rc) {
        AMS_UNUSED(rc);
        AMS_ABORT("Exit called by HOC patch service");
    }
    void Main() {
        os::SetThreadNamePointer(os::GetCurrentThread(), "NexusHOC.Main");
        hoc_patch::Run();
        AMS_ASSUME(false);
    }
}

void *operator new(size_t size) { return ams::hoc_patch::Allocate(size); }
void *operator new(size_t size, const std::nothrow_t &) { return ams::hoc_patch::Allocate(size); }
void operator delete(void *p) { ams::hoc_patch::Deallocate(p); }
void operator delete(void *p, size_t) { ams::hoc_patch::Deallocate(p); }
