// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "nexus_hoc_protocol.hpp"

namespace nexus_hoc {

// The IPC server runs one dispatch thread. This state is shared by every session,
// so reconnecting cannot reset a prepared transaction or replay hardware changes.
class ServiceState {
    Status m_status{};
    PatchReply m_pending{};
    std::uint64_t m_owner_pid = 0;
    std::uint64_t m_last_sequence = 0;
public:
    static bool AuthorizedIdentity(std::uint64_t client_pid, std::uint64_t handle_pid,
                                   std::uint64_t program_id) {
        return client_pid != 0 && client_pid == handle_pid && program_id == LoaderProgramId;
    }

    void Initialize(const Descriptor &descriptor) {
        m_status = {};
        m_status.descriptor = descriptor;
        m_pending = {};
        m_owner_pid = m_last_sequence = 0;
    }

    bool Hello(const HelloRequest &request, std::uint64_t caller_pid) {
        if (!ValidHello(request, m_status.descriptor.firmware) || caller_pid == 0
            || (m_owner_pid != 0 && m_owner_pid != caller_pid)) return false;
        m_owner_pid = caller_pid;
        return true;
    }

    bool CanPrepare(const PatchRequest &request, std::uint64_t caller_pid,
                    std::uintptr_t input, std::size_t input_size,
                    std::uintptr_t output, std::size_t output_size) const {
        if (caller_pid == 0 || caller_pid != m_owner_pid || request.sequence <= m_last_sequence
            || !ValidPatch(request, m_status.descriptor.generation, input_size, output_size)
            || input == 0 || output == 0 || input % PageSize || output % PageSize
            || input > UINTPTR_MAX - input_size || output > UINTPTR_MAX - output_size
            || !(input + input_size <= output || output + output_size <= input)) return false;
        if (m_status.pcv_phase == Failed || m_status.ptm_phase == Failed
            || m_status.pcv_phase == Prepared || m_status.ptm_phase == Prepared) return false;
        return request.kind == Pcv ? m_status.pcv_phase == Empty
            : m_status.pcv_phase == Committed && m_status.ptm_phase == Empty;
    }

    // Called only after authentication, dimensions and the source hash pass.
    // Failed is deliberately sticky until CompletePrepare: a patcher that does
    // not complete must never be treated as an untouched, retryable transaction.
    void BeginPrepare(const PatchRequest &request) {
        m_last_sequence = request.sequence;
        m_pending = {};
        m_pending.magic = Magic;
        m_pending.abi = Abi;
        m_pending.size = sizeof(m_pending);
        m_pending.kind = request.kind;
        m_pending.generation = request.generation;
        m_pending.sequence = request.sequence;
        m_pending.phase = Failed;
        m_pending.image_size = request.image_size;
        if (request.kind == Pcv) {
            m_status.pcv_phase = Failed;
            m_status.pcv_sequence = request.sequence;
            std::memcpy(m_status.pcv_module_id, request.module_id, 32);
            ++m_status.pcv_prepares;
        } else {
            m_status.ptm_phase = Failed;
            m_status.ptm_sequence = request.sequence;
            std::memcpy(m_status.ptm_module_id, request.module_id, 32);
            ++m_status.ptm_prepares;
        }
    }

    void CompletePrepare(const std::uint8_t output_hash[32]) {
        std::memcpy(m_pending.output_hash, output_hash, 32);
        m_pending.phase = Prepared;
        (m_pending.kind == Pcv ? m_status.pcv_phase : m_status.ptm_phase) = Prepared;
        m_status.last_error = 0;
    }

    bool CanCommit(const CommitRequest &request, std::uint64_t caller_pid) const {
        return caller_pid != 0 && caller_pid == m_owner_pid && ValidCommit(request, m_pending);
    }

    void CompleteCommit() {
        m_pending.phase = Committed;
        if (m_pending.kind == Pcv) {
            m_status.pcv_phase = Committed;
            ++m_status.pcv_commits;
        } else {
            m_status.ptm_phase = Committed;
            ++m_status.ptm_commits;
        }
        m_status.last_error = 0;
    }

    void FailCommit(std::uint32_t error) {
        m_pending.phase = Failed;
        (m_pending.kind == Pcv ? m_status.pcv_phase : m_status.ptm_phase) = Failed;
        m_status.last_error = error;
    }
    void RecordError(std::uint32_t error) { m_status.last_error = error; }
    const PatchReply &Reply() const { return m_pending; }
    const Status &Snapshot() const { return m_status; }
};
} // namespace nexus_hoc
