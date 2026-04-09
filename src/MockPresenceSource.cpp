#include "MockPresenceSource.h"

#include "Config.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace drpc {

MockPresenceSource::MockPresenceSource(std::vector<ActivityPreset> presets) : presets_(std::move(presets)) {
    if (presets_.empty()) {
        throw std::invalid_argument("MockPresenceSource requires at least one preset");
    }
}

SourceActivity MockPresenceSource::Current() const {
    const auto& preset = presets_.at(currentIndex_);
    return SourceActivity{
        .preset = preset,
        .identity = "mock:" + std::to_string(currentIndex_),
        .label = ToWide(preset.name),
        .disposition = SourceActivityDisposition::Publish,
    };
}

bool MockPresenceSource::Next() {
    currentIndex_ = (currentIndex_ + 1) % presets_.size();
    return true;
}

bool MockPresenceSource::Previous() {
    currentIndex_ = currentIndex_ == 0 ? presets_.size() - 1 : currentIndex_ - 1;
    return true;
}

bool MockPresenceSource::SupportsManualSelection() const {
    return true;
}

std::wstring MockPresenceSource::BuildMenuLabel() const {
    std::wostringstream label;
    label << L"Preset " << (currentIndex_ + 1) << L"/" << presets_.size() << L": " << ToWide(presets_.at(currentIndex_).name);
    return label.str();
}

std::wstring MockPresenceSource::SourceStatus() const {
    return L"Mock source";
}

}  // namespace drpc
