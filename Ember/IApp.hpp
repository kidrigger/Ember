#pragma once

#include <cstddef>
#include <string>

#include <Util/Runtime.hpp>

namespace Ember
{

class IApp
{
  static IApp* m_Instance;

public:
  virtual void LoadContent()   = 0;
  virtual void Update()        = 0;
  virtual void Render()        = 0;
  virtual void UnloadContent() = 0;
  virtual void Resize()        = 0;

  static IApp& Instance();

  explicit IApp( nullptr_t );
  IApp( IApp const& other )                = delete;
  IApp( IApp&& other ) noexcept            = default;
  IApp& operator=( IApp const& other )     = delete;
  IApp& operator=( IApp&& other ) noexcept = default;
  virtual ~IApp();
};

} // namespace Ember


void RegisterWindowClass( HINSTANCE instance_handle, const wchar_t* window_class_name );

HWND CreateWindow(
    wchar_t const* window_class_name,
    HINSTANCE      instance_handle,
    wchar_t const* window_title,
    uint32_t       width,
    uint32_t       height );
