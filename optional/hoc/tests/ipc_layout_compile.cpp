// SPDX-License-Identifier: GPL-2.0-only
// Compile with the real target SDK; these inspect the production method types.
#include "../Atmosphere/stratosphere/hoc_patch/source/hoc_service.hpp"

template<typename> struct MethodMetadata;
template<typename Class, typename Return, typename... Args>
struct MethodMetadata<Return (Class::*)(Args...)> : ams::sf::impl::CommandMetaInfo<Args...> {};

using Hello = MethodMetadata<decltype(&ams::hoc_patch::Service::Hello)>;
using Patch = MethodMetadata<decltype(&ams::hoc_patch::Service::Patch)>;
using Commit = MethodMetadata<decltype(&ams::hoc_patch::Service::Commit)>;
using Status = MethodMetadata<decltype(&ams::hoc_patch::Service::GetStatus)>;

static_assert(Hello::InDataSize == sizeof(nexus_hoc::AuthenticatedInput<nexus_hoc::HelloRequest>) && Hello::InDataSize == 40);
static_assert(Patch::InDataSize == sizeof(nexus_hoc::AuthenticatedInput<nexus_hoc::PatchRequest>) && Patch::InDataSize == 136);
static_assert(Commit::InDataSize == sizeof(nexus_hoc::AuthenticatedInput<nexus_hoc::CommitRequest>) && Commit::InDataSize == 72);
static_assert(Hello::InDataOffsets[0] == 0 && Hello::InDataOffsets[1] == 8);
static_assert(Patch::InDataOffsets[0] == 0 && Patch::InDataOffsets[1] == 8);
static_assert(Commit::InDataOffsets[0] == 0 && Commit::InDataOffsets[1] == 8);
static_assert(Hello::HasInProcessIdHolder && Patch::HasInProcessIdHolder && Commit::HasInProcessIdHolder);
static_assert(Hello::NumInCopyHandles == 1 && Hello::NumOutCopyHandles == 1);
static_assert(Patch::NumInCopyHandles == 1 && Patch::NumOutCopyHandles == 0);
static_assert(Commit::NumInCopyHandles == 1 && Commit::NumOutCopyHandles == 0);
static_assert(Patch::NumInHipcMapAliasBuffers == 1 && Patch::NumOutHipcMapAliasBuffers == 1);
static_assert(Patch::NumBuffers == 2 && Hello::NumBuffers == 0 && Commit::NumBuffers == 0);
static_assert(Hello::OutDataSize == sizeof(nexus_hoc::Descriptor));
static_assert(Patch::OutDataSize == sizeof(nexus_hoc::PatchReply));
static_assert(Commit::OutDataSize == sizeof(nexus_hoc::PatchReply));
static_assert(Status::InDataSize == 0 && Status::OutDataSize == sizeof(nexus_hoc::Status));
static_assert(!Status::HasInProcessIdHolder && Status::NumInCopyHandles == 0 && Status::NumOutCopyHandles == 0);
