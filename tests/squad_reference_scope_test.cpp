// Linux regression harness for the package-reference resolver. All identities are synthetic.
// Include the implementation to exercise its private resolver without widening the public API.
#include <cassert>
#include <iostream>
#include <openssl/sha.h>

#include "../Sunrise/src/client/content/activity/activity_sdk_squad_graph_edges.cpp"

namespace sunrise::middleware::crypto::sha256 {
bool hash(std::span<const std::byte> input, Digest& output) noexcept {
    return SHA256(reinterpret_cast<const unsigned char*>(input.data()),
                  input.size(),
                  reinterpret_cast<unsigned char*>(output.data()))
           != nullptr;
}
} // namespace sunrise::middleware::crypto::sha256

namespace squad = sunrise::client::content::activity::sdk_generation::squad_inventory;
namespace topology = sunrise::client::content::activity::sdk_generation::topology_inventory;
namespace detail = squad::detail;
namespace tables = sunrise::middleware::content::packages::tables;

struct Fixture {
    topology::Snapshot topology;
    squad::GraphSnapshot graph;
    std::unordered_map<detail::TargetKey, std::vector<std::uint32_t>, detail::TargetKeyHash>
        targets;
    std::unordered_map<std::uint32_t, std::uint32_t> rules;
    std::vector<std::vector<std::uint32_t>> scenarios{{0}, {1}};

    Fixture() {
        topology.objects.resize(2);
        topology.slots.resize(3);
        topology.slots[0].objectIndex = 0;
        topology.slots[0].slotType = 1;
        for (unsigned i = 1; i < 3; ++i) {
            topology.slots[i].objectIndex = i - 1;
            topology.slots[i].slotType = tables::kAuthoredSquadRuleSlotType;
        }
        graph.spawners.resize(1);
        graph.spawners[0].sourceDescriptorStatus = squad::SourceDescriptorStatus::exact;
        graph.spawners[0].sourceDescriptorRow = 0;
        graph.descriptors.resize(3);
        graph.descriptors[0].objectIndex = 0;
        graph.descriptors[0].slotIndex = 0;
        for (unsigned i = 1; i < 3; ++i) {
            auto& descriptor = graph.descriptors[i];
            descriptor.objectIndex = i - 1;
            descriptor.slotIndex = i;
            descriptor.configTag = 100 + i;
            descriptor.componentClass = tables::kAuthoredSquadRulePrimaryClass;
            rules.emplace(descriptor.configTag, i - 1);
        }
        targets[{123, tables::kAuthoredSquadRuleSlotType, 7}] = {1, 2};
    }

    squad::ReferenceResolutionStatus resolve(
        std::uint64_t raw = 123ULL
                            | (static_cast<std::uint64_t>(tables::kAuthoredSquadRuleSlotType) << 32)
                            | (7ULL << 48)) {
        std::map<detail::TargetGroup, detail::ExactTarget> exact;
        squad::ReferenceResolutionStatus status{};
        assert(detail::resolve_reference(
            topology, graph, 0, 0, raw, targets, rules, scenarios, exact, status));
        return status;
    }
};

int main() {
    using Status = squad::ReferenceResolutionStatus;
    {
        Fixture f;
        assert(f.resolve() == Status::exact);
        assert(f.graph.references[0].resolvedObjectRow == 0);
        assert(f.graph.referenceDescriptors.size() == 1);
        assert(f.graph.references[0].candidateDescriptors.count == 1);
        assert(f.graph.referenceDescriptors[0].resolvedTarget);
    }
    {
        Fixture f;
        f.graph.descriptors[2].componentClass = 0;
        assert(f.resolve()
               == Status::exact); // An unrelated scenario's wrong type cannot poison it.
    }
    {
        Fixture f;
        f.scenarios[1] = {0};
        assert(f.resolve() == Status::targetAmbiguous); // Keep real local ambiguity.
    }
    {
        Fixture f;
        f.scenarios[1] = {0};
        f.graph.descriptors[2].componentClass = 0;
        assert(f.resolve() == Status::targetDescriptorMismatch);
    }
    {
        Fixture f;
        f.targets.begin()->second = {2};
        assert(f.resolve() == Status::targetMissing); // No fallback to the arcade object.
        assert(f.graph.referenceDescriptors.empty());
    }
    {
        Fixture f;
        f.graph.spawners[0].sourceDescriptorStatus = squad::SourceDescriptorStatus::ambiguous;
        assert(f.resolve() == Status::sourceDescriptorAmbiguous);
    }
    {
        Fixture f;
        assert(f.resolve(~0ULL) == Status::invalidEncoding);
    }
    {
        Fixture f;
        f.scenarios[0] = {0, 2};
        f.scenarios[1] = {1, 2};
        assert(f.resolve()
               == Status::targetAmbiguous); // Shared objects retain overlapping contexts.
    }
    {
        Fixture f;
        f.graph.spawners[0].sourceDescriptorCandidates={0,2};
        f.graph.sourceDescriptorCandidates={{0,0},{0,2}};
        f.graph.descriptors[0].objectIndex=0;f.graph.descriptors[0].slotIndex=0;
        assert(detail::scoped_sources(f.graph,f.graph.spawners[0],f.scenarios).size()==2);
        f.scenarios[1]={0};
        assert(detail::scoped_sources(f.graph,f.graph.spawners[0],f.scenarios).empty());
        f.graph.descriptors[2].objectIndex=0;f.graph.descriptors[2].slotIndex=0;
        assert(detail::scoped_sources(f.graph,f.graph.spawners[0],f.scenarios).size()==1);
    }
    std::cout << "squad reference scope and reused-source regressions passed\n";
}
