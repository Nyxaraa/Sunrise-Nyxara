#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace sunrise::state::build_data {

/**
 * Fixed row storage for one published table.
 * The caller must already hold the domain's Lock: exclusive to write, shared to read. Storage is
 * a member array, so the table lives in State for the whole process and nothing here allocates.
 * @tparam Row Trivially copyable published row.
 * @tparam Capacity Most rows the domain can publish.
 */
template <typename Row, std::size_t Capacity> class Table final {
public:
    /** Most rows this table accepts. */
    static constexpr std::size_t capacity = Capacity;

    /** Drops every row. Call under an exclusive hold. */
    void clear() noexcept {
        std::fill_n(rows_.begin(), count_, Row{});
        count_ = 0;
    }

    /**
     * Replaces every row. Call under an exclusive hold, and check the rows first.
     * @param rows Complete replacement set, already checked by the domain.
     * @return True when the rows fit fixed storage.
     */
    [[nodiscard]] bool replace(std::span<const Row> rows) noexcept {
        if (rows.size() > Capacity) {
            return false;
        }
        std::copy(rows.begin(), rows.end(), rows_.begin());
        if (count_ > rows.size()) {
            std::fill(rows_.begin() + static_cast<std::ptrdiff_t>(rows.size()),
                      rows_.begin() + static_cast<std::ptrdiff_t>(count_),
                      Row{});
        }
        // The count publishes the new rows, so it moves last.
        count_ = rows.size();
        return true;
    }

    /**
     * Clears the table, sizes it, and hands back writable storage. Call under an exclusive hold.
     * For a domain that places each row at a position the row itself names, not in input order.
     * No reader can see the sized but unwritten rows: writes hold exclusive, reads hold shared.
     * @param count Number of rows about to be written.
     * @return Storage for exactly that many rows, or an empty span when the count does not fit.
     */
    [[nodiscard]] std::span<Row> reset(std::size_t count) noexcept {
        if (count > Capacity) {
            return {};
        }
        std::fill_n(rows_.begin(), (std::max)(count_, count), Row{});
        count_ = count;
        return {rows_.data(), count_};
    }

    /**
     * Copies every row in publish order. Call under a shared hold.
     * @param output Caller-owned storage.
     * @param count Receives the copied row count, or zero when output is too small.
     * @return True when output can hold every row.
     */
    [[nodiscard]] bool snapshot(std::span<Row> output, std::size_t& count) const noexcept {
        count = 0;
        if (output.size() < count_) {
            return false;
        }
        std::copy_n(rows_.begin(), count_, output.begin());
        count = count_;
        return true;
    }

    /** @return Published rows in publish order. Call under a shared hold. */
    [[nodiscard]] std::span<const Row> rows() const noexcept {
        return {rows_.data(), count_};
    }

    /** @return Published row count. Call under a shared hold. */
    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }

private:
    std::array<Row, Capacity> rows_{};
    std::size_t count_{};
};

} // namespace sunrise::state::build_data
