#pragma once

#include <concepts>
#include <type_traits>
#include "BindlessManager.hpp"
#include "DeviceHandle.hpp"

namespace Ember
{

class ScopedHandlePair
{
  BindlessManager* m_Bindless{ nullptr };
  SRVHandle        m_SRV;
  UAVHandle        m_UAV;

public:
  ScopedHandlePair() = default;
  ScopedHandlePair( BindlessManager* bindless, SRVHandle srv, UAVHandle uav = {} );

  [[nodiscard]] SRVHandle GetSRV() const noexcept;
  [[nodiscard]] UAVHandle GetUAV() const noexcept;

  ScopedHandlePair( ScopedHandlePair&& other ) noexcept;
  ScopedHandlePair& operator=( ScopedHandlePair&& other ) noexcept;
  ScopedHandlePair( ScopedHandlePair const& other )            = delete;
  ScopedHandlePair& operator=( ScopedHandlePair const& other ) = delete;
  ~ScopedHandlePair();
};


} // namespace Ember
