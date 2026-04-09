#pragma once

#include "PresenceSource.h"

#include <vector>

namespace drpc {

class MockPresenceSource final : public PresenceSource {
public:
    explicit MockPresenceSource(std::vector<ActivityPreset> presets);

    SourceActivity Current() const override;
    bool Next() override;
    bool Previous() override;
    bool SupportsManualSelection() const override;
    std::wstring BuildMenuLabel() const override;
    std::wstring SourceStatus() const override;

private:
    std::vector<ActivityPreset> presets_;
    std::size_t currentIndex_ = 0;
};

}  // namespace drpc
