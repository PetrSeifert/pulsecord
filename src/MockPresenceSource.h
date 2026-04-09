#pragma once

#include "PresenceSource.h"

#include <vector>

namespace drpc {

class MockPresenceSource final : public PresenceSource {
public:
    explicit MockPresenceSource(std::vector<ActivityPreset> presets);

    const ActivityPreset& Current() const override;
    const ActivityPreset& Next() override;
    const ActivityPreset& Previous() override;
    std::size_t CurrentIndex() const override;
    std::size_t Count() const override;

private:
    std::vector<ActivityPreset> presets_;
    std::size_t currentIndex_ = 0;
};

}  // namespace drpc
