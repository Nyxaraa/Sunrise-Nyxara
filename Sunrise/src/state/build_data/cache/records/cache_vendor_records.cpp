#include "codec.h"

namespace sunrise::state::build_data::cache::records {

/** Encodes one vendor index row. */
bool encode(const vendors::IndexEntry& value, VendorIndexRecord& record) noexcept {
    record = {};
    record.definitionHash = value.definitionHash;
    record.definitionTag = value.definitionTag;
    record.index = value.index;
    return true;
}

/** Decodes one vendor index row. */
bool decode(const VendorIndexRecord& record, vendors::IndexEntry& value) noexcept {
    value = {};
    if (record.reserved != 0) {
        return false;
    }
    value = {record.definitionHash, record.definitionTag, record.index};
    return true;
}

/** Encodes one vendor definition and its flat-bank ranges. */
bool encode(const vendors::Definition& value, VendorDefinitionRecord& record) noexcept {
    record = {};
    record.definitionHash = value.definitionHash;
    record.definitionTag = value.definitionTag;
    record.definitionClass = value.definitionClass;
    record.definitionSize = value.definitionSize;
    record.installedRowBase = value.installedRowBase;
    record.installedRowClass = value.installedRowClass;
    record.saleRowBase = value.saleRowBase;
    record.saleRowClass = value.saleRowClass;
    record.thirdRowBase = value.thirdRowBase;
    record.thirdRowClass = value.thirdRowClass;
    record.saleRowOffset = value.saleRowOffset;
    record.installedRowOffset = value.installedRowOffset;
    record.resetIntervalRaw = value.resetIntervalRaw;
    record.resetPhaseRaw = value.resetPhaseRaw;
    record.index = value.index;
    record.installedCount = value.installedCount;
    record.saleCount = value.saleCount;
    record.thirdCount = value.thirdCount;
    return true;
}

/** Decodes one vendor definition and its flat-bank ranges. */
bool decode(const VendorDefinitionRecord& record, vendors::Definition& value) noexcept {
    value = {};
    // The catalog checks every range against the whole domain. Only the class is checked here.
    // A row of another class is not a vendor definition, whatever its ranges say.
    if (record.definitionClass != vendors::kDefinitionClass) {
        return false;
    }
    value.definitionHash = record.definitionHash;
    value.definitionTag = record.definitionTag;
    value.definitionClass = record.definitionClass;
    value.definitionSize = record.definitionSize;
    value.installedRowBase = record.installedRowBase;
    value.installedRowClass = record.installedRowClass;
    value.saleRowBase = record.saleRowBase;
    value.saleRowClass = record.saleRowClass;
    value.thirdRowBase = record.thirdRowBase;
    value.thirdRowClass = record.thirdRowClass;
    value.saleRowOffset = record.saleRowOffset;
    value.installedRowOffset = record.installedRowOffset;
    value.resetIntervalRaw = record.resetIntervalRaw;
    value.resetPhaseRaw = record.resetPhaseRaw;
    value.index = record.index;
    value.installedCount = record.installedCount;
    value.saleCount = record.saleCount;
    value.thirdCount = record.thirdCount;
    return true;
}

/** Encodes one vendor sale row. */
bool encode(const vendors::SaleRow& value, VendorSaleRowRecord& record) noexcept {
    record = {};
    record.vendorIndex = value.vendorIndex;
    record.rowIndex = value.rowIndex;
    record.itemIndex = value.itemIndex;
    record.secondaryItemIndex = value.secondaryItemIndex;
    record.categoryIndex = value.categoryIndex;
    record.raw104 = value.raw104;
    record.raw108 = value.raw108;
    record.raw172 = value.raw172;
    record.expressionCount8 = value.expressionCount8;
    record.nestedRecordCount = value.nestedRecordCount;
    record.expressionCount120 = value.expressionCount120;
    record.count136 = value.count136;
    record.expressionCount160 = value.expressionCount160;
    record.featureBranch = value.featureBranch;
    return true;
}

/** Decodes one vendor sale row. */
bool decode(const VendorSaleRowRecord& record, vendors::SaleRow& value) noexcept {
    value = {};
    if (record.reserved != decltype(record.reserved){}) {
        return false;
    }
    value.vendorIndex = record.vendorIndex;
    value.rowIndex = record.rowIndex;
    value.itemIndex = record.itemIndex;
    value.secondaryItemIndex = record.secondaryItemIndex;
    value.categoryIndex = record.categoryIndex;
    value.raw104 = record.raw104;
    value.raw108 = record.raw108;
    value.raw172 = record.raw172;
    value.expressionCount8 = record.expressionCount8;
    value.nestedRecordCount = record.nestedRecordCount;
    value.expressionCount120 = record.expressionCount120;
    value.count136 = record.count136;
    value.expressionCount160 = record.expressionCount160;
    value.featureBranch = record.featureBranch;
    return true;
}

/** Encodes one vendor installed row. */
bool encode(const vendors::InstalledRow& value, VendorInstalledRowRecord& record) noexcept {
    record = {};
    record.vendorIndex = value.vendorIndex;
    record.rowIndex = value.rowIndex;
    record.raw = value.raw;
    return true;
}

/** Decodes one vendor installed row. */
bool decode(const VendorInstalledRowRecord& record, vendors::InstalledRow& value) noexcept {
    value = {record.vendorIndex, record.rowIndex, record.raw};
    return true;
}

} // namespace sunrise::state::build_data::cache::records
