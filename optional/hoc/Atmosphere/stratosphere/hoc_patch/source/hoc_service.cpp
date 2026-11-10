// SPDX-License-Identifier: GPL-2.0-only
#include "hoc_service.hpp"
#include "hoc_service_core.hpp"
#include "oc/oc_loader.hpp"
#include "oc/pcv/pcv_hook_module.hpp"

namespace ams::hoc_patch {
    namespace {
        // One IPC dispatch thread owns this state and the OC engine's globals.
        constinit nexus_hoc::ServiceState g_state;

        Result Authenticate(const sf::ClientProcessId &pid, const sf::CopyHandle &process) {
            u64 handle_pid = 0, program_id = 0;
            R_TRY(svcGetProcessId(&handle_pid, process.GetOsHandle()));
            R_TRY(svcGetInfo(&program_id, InfoType_ProgramId, process.GetOsHandle(), 0));
            R_UNLESS(nexus_hoc::ServiceState::AuthorizedIdentity(pid.GetValue().value, handle_pid, program_id), sf::ResultPreconditionViolation());
            R_SUCCEED();
        }

        Result Reject() {
            const Result result = sf::ResultPreconditionViolation();
            g_state.RecordError(result.GetValue());
            return result;
        }
    }

    void InitializeService() {
        namespace engine = ldr::hoc;
        static_assert(sizeof(engine::CustomizeTable) <= 0x10000);
        static_assert(nexus_hoc::DataBytes == engine::pcv::PcvDataArenaSize);
        static_assert(nexus_hoc::CodeBytes == engine::pcv::PcvCodeArenaSize);
        static_assert(nexus_hoc::RuntimeBytes == engine::pcv::PcvRuntimeArenaSize);
        static_assert(nexus_hoc::PayloadOffset == engine::pcv::PcvHookPayloadOffset);
        AMS_ABORT_UNLESS(engine::C.cust[0] == 'C' && engine::C.cust[1] == 'U'
            && engine::C.cust[2] == 'S' && engine::C.cust[3] == 'T'
            && engine::C.custRev == CUST_REV && engine::C.kipVersion == KIP_VERSION);

        nexus_hoc::Descriptor descriptor{};
        descriptor.magic = nexus_hoc::Magic;
        descriptor.abi = nexus_hoc::Abi;
        descriptor.size = sizeof(descriptor);
        descriptor.engine_version = engine::C.kipVersion;
        descriptor.cust_revision = engine::C.custRev;
        descriptor.cust_size = sizeof(engine::CustomizeTable);
        descriptor.firmware = static_cast<u32>(hos::GetVersion());
        descriptor.flags = nexus_hoc::SupportsPcv | nexus_hoc::SupportsPtm;
        descriptor.data_bytes = nexus_hoc::DataBytes;
        descriptor.code_bytes = nexus_hoc::CodeBytes;
        descriptor.runtime_bytes = nexus_hoc::RuntimeBytes;
        descriptor.payload_offset = nexus_hoc::PayloadOffset;
        descriptor.max_image_bytes = nexus_hoc::MaxImageBytes;
        descriptor.generation = static_cast<u64>(os::GetSystemTick().GetInt64Value());
        if (!descriptor.generation) descriptor.generation = 1;
        crypto::GenerateSha256Hash(descriptor.settings_hash, sizeof(descriptor.settings_hash),
            const_cast<const engine::CustomizeTable *>(std::addressof(engine::C)), sizeof(engine::C));
        g_state.Initialize(descriptor);
    }

    Result Service::Hello(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                          const nexus_hoc::HelloRequest &request, sf::Out<nexus_hoc::Descriptor> out, sf::OutCopyHandle helper_process) {
        R_TRY(Authenticate(pid, process));
        if (!g_state.Hello(request, pid.GetValue().value)) return Reject();
        *out = g_state.Snapshot().descriptor;
        helper_process.SetValue(CUR_PROCESS_HANDLE, false);
        R_SUCCEED();
    }

    Result Service::Patch(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                          const nexus_hoc::PatchRequest &request, const sf::InMapAliasBuffer &input,
                          const sf::OutMapAliasBuffer &output, sf::Out<nexus_hoc::PatchReply> out) {
        R_TRY(Authenticate(pid, process));
        if (!g_state.CanPrepare(request, pid.GetValue().value,
            reinterpret_cast<uintptr_t>(input.GetPointer()), input.GetSize(),
            reinterpret_cast<uintptr_t>(output.GetPointer()), output.GetSize())) return Reject();
        u8 input_hash[32];
        crypto::GenerateSha256Hash(input_hash, sizeof(input_hash), input.GetPointer(), input.GetSize());
        if (std::memcmp(input_hash, request.input_hash, sizeof(input_hash)) != 0) return Reject();

        // From here, OC may change regulator state. Any engine abort or failed
        // commit is fatal to this boot; there is intentionally no stock fallback.
        g_state.BeginPrepare(request);
        std::memcpy(output.GetPointer(), input.GetPointer(), input.GetSize());
        const uintptr_t mapped = reinterpret_cast<uintptr_t>(output.GetPointer());
        if (request.kind == nexus_hoc::Pcv) {
            const uintptr_t data = mapped + request.module_size;
            const uintptr_t code = data + nexus_hoc::DataBytes;
            std::memset(reinterpret_cast<void *>(data), 0, nexus_hoc::ArenaBytes);
            ldr::hoc::pcv::InitializeHookModule(code);
            ldr::hoc::pcv::Patch(mapped, request.module_size, code + nexus_hoc::PayloadOffset,
                nexus_hoc::CodeBytes - nexus_hoc::PayloadOffset, request.target_address, data);
        } else {
            ldr::hoc::ptm::Patch(mapped, request.module_size);
        }
        u8 output_hash[32];
        crypto::GenerateSha256Hash(output_hash, sizeof(output_hash), output.GetPointer(), output.GetSize());
        g_state.CompletePrepare(output_hash);
        *out = g_state.Reply();
        R_SUCCEED();
    }

    Result Service::Commit(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                           const nexus_hoc::CommitRequest &request, sf::Out<nexus_hoc::PatchReply> out) {
        R_TRY(Authenticate(pid, process));
        if (!g_state.CanCommit(request, pid.GetValue().value)) return Reject();
        if (request.kind == nexus_hoc::Pcv) {
            const Result result = ldr::hoc::pcv::WriteKipLoadToIram();
            if (R_FAILED(result)) {
                g_state.FailCommit(result.GetValue());
                return result;
            }
        }
        g_state.CompleteCommit();
        *out = g_state.Reply();
        R_SUCCEED();
    }

    Result Service::GetStatus(sf::Out<nexus_hoc::Status> out) {
        *out = g_state.Snapshot();
        R_SUCCEED();
    }
}
