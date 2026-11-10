// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stratosphere.hpp>
#include "nexus_hoc_protocol.hpp"

#define NEXUS_HOC_SERVICE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 0, Result, Hello, (const sf::ClientProcessId &pid, sf::CopyHandle &&process, const nexus_hoc::HelloRequest &request, sf::Out<nexus_hoc::Descriptor> out, sf::OutCopyHandle helper_process), (pid, std::move(process), request, out, helper_process)) \
    AMS_SF_METHOD_INFO(C, H, 1, Result, Patch, (const sf::ClientProcessId &pid, sf::CopyHandle &&process, const nexus_hoc::PatchRequest &request, const sf::InMapAliasBuffer &input, const sf::OutMapAliasBuffer &output, sf::Out<nexus_hoc::PatchReply> out), (pid, std::move(process), request, input, output, out)) \
    AMS_SF_METHOD_INFO(C, H, 2, Result, Commit, (const sf::ClientProcessId &pid, sf::CopyHandle &&process, const nexus_hoc::CommitRequest &request, sf::Out<nexus_hoc::PatchReply> out), (pid, std::move(process), request, out)) \
    AMS_SF_METHOD_INFO(C, H, 3, Result, GetStatus, (sf::Out<nexus_hoc::Status> out), (out))

AMS_SF_DEFINE_INTERFACE(ams::hoc_patch::impl, IService, NEXUS_HOC_SERVICE_INFO, 0x31434F48)

namespace ams::hoc_patch {
    void InitializeService();
    class Service {
    public:
        Result Hello(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                     const nexus_hoc::HelloRequest &request, sf::Out<nexus_hoc::Descriptor> out, sf::OutCopyHandle helper_process);
        Result Patch(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                     const nexus_hoc::PatchRequest &request, const sf::InMapAliasBuffer &input,
                     const sf::OutMapAliasBuffer &output, sf::Out<nexus_hoc::PatchReply> out);
        Result Commit(const sf::ClientProcessId &pid, sf::CopyHandle &&process,
                      const nexus_hoc::CommitRequest &request, sf::Out<nexus_hoc::PatchReply> out);
        Result GetStatus(sf::Out<nexus_hoc::Status> out);
    };
    static_assert(impl::IsIService<Service>);
}
