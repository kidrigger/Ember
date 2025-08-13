#pragma once

#include <compare>

#include "Util/Runtime.hpp"

namespace Ember
{

class OmniLightHandle
{
  uint16_t static constexpr kInvalid{ UINT16_MAX };

  uint16_t m_Inner{ kInvalid };
  uint16_t m_Generation{ kInvalid };

public:
  OmniLightHandle() = default;
  explicit OmniLightHandle( uint16_t inner, uint16_t generation );

  [[nodiscard]] uint16_t GetIndex() const;
  [[nodiscard]] uint16_t GetGeneration() const;

  std::strong_ordering   operator<=>( OmniLightHandle const& ) const;
};

} // namespace Ember
