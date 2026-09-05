#include <array>
#include <cassert>
#include <vector>

#include "../Sunrise/src/client/hooks/network/investment/lore_visibility_patch.h"
namespace lore = sunrise::client::hooks::network::investment::lore;

// Model the decoded instructions with arbitrary unlock reads, including a completely fresh save.
bool evaluate(std::span<const lore::Instruction> code, int value) {
    std::vector<int> stack;
    for (auto instruction : code) {
        const auto op = instruction.opcode;
        if (op == 1 || op == 10)
            stack.push_back(value);
        else if (op == 11)
            stack.push_back(static_cast<int>(instruction.operand));
        else if (op == 2) {
            assert(!stack.empty());
            stack.back() = !stack.back();
        } else {
            assert(stack.size() >= 2);
            int b = stack.back();
            stack.pop_back();
            int a = stack.back();
            stack.pop_back();
            assert(op == 4 || op == 8);
            stack.push_back(op == 4 ? (a && b) : (a == b));
        }
    }
    assert(stack.size() == 1);
    return stack.back() != 0;
}
int main() {
    assert(lore::kTargets.size() == 36);
    for (const auto& target : lore::kTargets) {
        std::vector<lore::Instruction> code;
        switch (target.shape) {
        case lore::Shape::constant:
            code = {{11, 1}};
            break;
        case lore::Shape::eva:
            code = {{10, 10343}, {2, 0}};
            break;
        case lore::Shape::confessions:
            code = {{1, 8702}, {2, 0}};
            break;
        case lore::Shape::chronicon:
            for (unsigned i = 0; i < 15; ++i) {
                code.push_back({10, 10615 + i});
                code.push_back({11, 0});
                code.push_back({8, UINT32_MAX});
            }
            for (unsigned i = 0; i < 14; ++i)
                code.push_back({4, UINT32_MAX});
            break;
        }
        const auto original = code;
        lore::Instruction output{};
        assert(lore::replacement(target.shape, code, output));
        code[0] = output;
        for (int value : {0, 1, 2, 100})
            assert(!evaluate(code, value));
        for (std::size_t i = 1; i < code.size(); ++i)
            assert(code[i] == original[i]);
        // Refuse changed schema, wrong operands, and already-modified conditions.
        assert(!lore::replacement(target.shape, code, output));
        code = original;
        code.back().operand ^= 1;
        assert(!lore::replacement(target.shape, code, output));
        assert(!lore::replacement(target.shape, {}, output));
    }
    // No chapter completion, reward, or redemption fields occur in the target plan.
    for (const auto& target : lore::kTargets) {
        assert(target.node ? target.field == 64 : (target.field == 120 || target.field == 136));
    }
}
