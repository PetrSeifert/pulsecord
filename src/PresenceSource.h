#pragma once

#include "ActivityPreset.h"

#include <optional>
#include <string>

namespace drpc {

enum class SourceActivityDisposition {
    Publish,
    Clear,
};

struct SourceActivity {
    std::optional<ActivityPreset> preset;
    std::string identity;
    std::wstring label;
    SourceActivityDisposition disposition = SourceActivityDisposition::Publish;
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
