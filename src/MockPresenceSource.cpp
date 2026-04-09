#include "MockPresenceSource.h"

#include <stdexcept>
#include <utility>

namespace drpc {

MockPresenceSource::MockPresenceSource(std::vector<ActivityPreset> presets) : presets_(std::move(presets)) {
    if (presets_.empty()) {
        throw std::invalid_argument("MockPresenceSource requires at least one preset");
    }
}

const ActivityPreset& MockPresenceSource::Current() const {
    return presets_.at(currentIndex_);
}

const ActivityPreset& MockPresenceSource::Next() {
    currentIndex_ = (currentIndex_ + 1) % presets_.size();
    return Current();
}

const ActivityPreset& MockPresenceSource::Previous() {
    currentIndex_ = currentIndex_ == 0 ? presets_.size() - 1 : currentIndex_ - 1;
    return Current();
}

std::size_t MockPresenceSource::CurrentIndex() const {
    return currentIndex_;
}

std::size_t MockPresenceSource::Count() const {
    return presets_.size();
}

}  // namespace drpc
