#pragma once

#include "ActivityPreset.h"

#include <cstddef>

namespace drpc {

class PresenceSource {
public:
    virtual ~PresenceSource() = default;

    virtual const ActivityPreset& Current() const = 0;
    virtual const ActivityPreset& Next() = 0;
    virtual const ActivityPreset& Previous() = 0;
    virtual std::size_t CurrentIndex() const = 0;
    virtual std::size_t Count() const = 0;
};

}  // namespace drpc
