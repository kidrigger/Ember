#pragma once

#include <cstdint>

class FrameGraphResource final {
  int32_t m_Id{ -1 };

  public:
  FrameGraphResource() = default;
  FrameGraphResource(const uint32_t id) : m_Id{ static_cast<int32_t>(id) } {}

  operator int32_t() const { return m_Id; }

  bool valid() const
  {
    return m_Id >= 0;
  }
};
