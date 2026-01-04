#include "Input.hpp"

Ember::Input& Ember::Input::Instance()
{
  static Input instance;
  return instance;
}

bool Ember::Input::IsLeftMouseDown() const
{
  return m_IsLeftMouseDown;
}

void Ember::Input::SetLeftMouseDown( bool const v )
{
  m_IsLeftMouseDown = v;
}

bool Ember::Input::IsLeftMouseReleased() const
{
  return not m_IsLeftMouseDown and m_PrevLeftMouseDown;
}

bool Ember::Input::IsRightMouseDown() const
{
  return m_IsRightMouseDown;
}

void Ember::Input::SetRightMouseDown( bool const v )
{
  m_IsRightMouseDown = v;
}

DirectX::XMUINT2 Ember::Input::GetMousePosition() const
{
  return m_MousePosition;
}

void Ember::Input::SetMousePosition( DirectX::XMUINT2 const mouse_position )
{
  m_MousePosition = mouse_position;
}

bool Ember::Input::IsPressed( char const c )
{
  return m_KeyPress[c] & kPressedBit;
}

bool Ember::Input::IsJustPressed( char const c )
{
  byte const val = m_KeyPress[c];
  return val & kPressedBit and not( val & kPrevPressedBit );
}

bool Ember::Input::IsJustReleased( char const c )
{
  byte const val = m_KeyPress[c];
  return val & kPrevPressedBit and not( val & kPressedBit );
}

void Ember::Input::SetDown( char const c, bool const down )
{
  if ( down )
  {
    m_KeyPress[c] |= kPressedBit;
  }
  else
  {
    m_KeyPress[c] &= ~kPressedBit;
  }
}

void Ember::Input::Update()
{
  m_PrevLeftMouseDown = m_IsLeftMouseDown;
  for ( auto& v : m_KeyPress.Values() )
  {
    v = ( byte )( ( v << 1 ) | v );
  }
}
