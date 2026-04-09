#pragma once

#include "ActivityPreset.h"

#include <string>

namespace drpc {

struct SourceActivity {
    ActivityPreset preset;
    std::string identity;
    std::wstring label;
};

class PresenceSource {
public:
    virtual ~PresenceSource() = default;

    virtual SourceActivity Current() const = 0;
    virtual bool Next() = 0;
    virtual bool Previous() = 0;
    virtual bool SupportsManualSelection() const = 0;
    virtual std::wstring BuildMenuLabel() const = 0;
    virtual std::wstring SourceStatus() const = 0;
};

}  // namespace drpc
