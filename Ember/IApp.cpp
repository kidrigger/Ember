#include "IApp.hpp"

#include "Util/HelperUtils.hpp"

Ember::IApp* Ember::IApp::m_Instance = nullptr;

Ember::IApp& Ember::IApp::Instance()
{
  ASSERT( m_Instance );
  return *m_Instance;
}

Ember::IApp::IApp( nullptr_t )
{
  ASSERT_M( not m_Instance, "Only one instance of App allowed at once" );
  m_Instance = this;
}

Ember::IApp::~IApp()
{
  if ( m_Instance == this ) m_Instance = nullptr;
}
