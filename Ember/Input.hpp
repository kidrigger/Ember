#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class Input
{
  uint32_t constexpr static kPressedBit     = 0x1;
  uint32_t constexpr static kPrevPressedBit = 0x2;

  DirectX::XMUINT2    m_MousePosition{ 0, 0 };
  bool                m_PrevLeftMouseDown{ false };
  bool                m_IsLeftMouseDown{ false };
  bool                m_IsRightMouseDown{ false };

  FlatMap<char, byte> m_KeyPress;

  Input() = default;

public:
  static Input&    Instance();

  DirectX::XMUINT2 GetMousePosition() const;
  void             SetMousePosition( DirectX::XMUINT2 mouse_position );
  bool             IsLeftMouseDown() const;
  void             SetLeftMouseDown( bool v );
  bool             IsLeftMouseReleased() const;
  bool             IsRightMouseDown() const;
  void             SetRightMouseDown( bool v );
  bool             IsPressed( char c );
  bool             IsJustPressed( char c );
  bool             IsJustReleased( char c );
  void             SetDown( char c, bool down );
  void             Update();
};

} // namespace Ember
