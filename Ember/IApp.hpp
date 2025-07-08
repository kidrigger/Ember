#pragma once

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

  IApp();
  IApp( IApp const& other )                = delete;
  IApp( IApp&& other ) noexcept            = default;
  IApp& operator=( IApp const& other )     = delete;
  IApp& operator=( IApp&& other ) noexcept = default;
  virtual ~IApp();
};

} // namespace Ember
