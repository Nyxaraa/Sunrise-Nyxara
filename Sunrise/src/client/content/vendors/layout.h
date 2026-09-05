#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::vendors {

/** Tag of the installed vendor index blob, which names every vendor definition. */
inline constexpr std::uint32_t kIndexRootTag = 0x8131931DU;

/** A vendor definition holds its installed array descriptor here. */
inline constexpr std::size_t kInstalledArrayDescriptor = 32;
/** A vendor definition holds its sale array descriptor here. */
inline constexpr std::size_t kSaleArrayDescriptor = 48;
/** A vendor definition holds its unnamed third array descriptor here. */
inline constexpr std::size_t kThirdArrayDescriptor = 80;
/** Raw reset interval. Its unit, epoch and scope are open, so it is stored unconverted. */
inline constexpr std::size_t kResetIntervalOffset = 20;
/** Raw reset phase, paired with the interval. */
inline constexpr std::size_t kResetPhaseOffset = 24;

/** Sale row expression array descriptor. */
inline constexpr std::size_t kSaleExpression8Offset = 8;
/** Sale row nested-record array descriptor. */
inline constexpr std::size_t kSaleNestedRecordOffset = 32;
/** Sale row main item-definition index. */
inline constexpr std::size_t kSaleItemIndexOffset = 70;
/** Sale row vendor category index. */
inline constexpr std::size_t kSaleCategoryIndexOffset = 100;
/** Sale row scalar with no closed consumer. */
inline constexpr std::size_t kSaleRaw104Offset = 104;
/** Sale row scalar with no closed consumer. */
inline constexpr std::size_t kSaleRaw108Offset = 108;
/** Sale row second expression array descriptor. */
inline constexpr std::size_t kSaleExpression120Offset = 120;
/** Sale row array descriptor with no closed consumer. */
inline constexpr std::size_t kSaleCount136Offset = 136;
/** Sale row feature branch byte. */
inline constexpr std::size_t kSaleFeatureBranchOffset = 154;
/** Sale row inline expression array descriptor. */
inline constexpr std::size_t kSaleExpression160Offset = 160;
/** Sale row scalar with no closed consumer. It is not the field the runtime selector reads. */
inline constexpr std::size_t kSaleRaw172Offset = 172;
/** Sale row secondary item-definition index. */
inline constexpr std::size_t kSaleSecondaryItemOffset = 176;

} // namespace sunrise::client::content::vendors
