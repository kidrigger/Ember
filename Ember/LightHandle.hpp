#pragma once

#include <compare>

namespace Ember
{

class LightHandle
{
  uint16_t static constexpr kInvalid{ UINT16_MAX };

  uint16_t m_Inner{ kInvalid };
  uint16_t m_Generation{ kInvalid };

public:
  LightHandle() = default;
  LightHandle( uint16_t inner, uint16_t generation );

  [[nodiscard]] uint16_t GetIndex() const;
  [[nodiscard]] uint16_t GetGeneration() const;

  auto                   operator<=>( LightHandle const& ) const = default;
};

class OmniLightHandle : public LightHandle
{
public:
  OmniLightHandle() = default;
  OmniLightHandle( uint16_t const inner, uint16_t const generation );
};

class DirLightHandle : public LightHandle
{
public:
  DirLightHandle() = default;
  DirLightHandle( uint16_t const inner, uint16_t const generation );
};

} // namespace Ember
