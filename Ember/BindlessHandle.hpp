#pragma once

#include "Util/Runtime.hpp"

namespace Ember
{

class BindlessHandle
{
public:
  BindlessHandle() = default;
  [[nodiscard]] bool IsNull() const noexcept;

protected:
  explicit BindlessHandle( uint32_t index );

  [[nodiscard]] uint32_t GetInner() const
  {
    return m_Handle;
  }

private:
  constexpr static uint32_t kInvalid{ UINT32_MAX };
  uint32_t                  m_Handle{ kInvalid };
};

// Guarantee size matches the 32bit (DWORD) constants.
static_assert( sizeof( BindlessHandle ) == sizeof( DWORD32 ) );

#define TYPED_BINDLESS_HANDLE( Type )                                                                                  \
  class Type##Handle : public BindlessHandle                                                                           \
  {                                                                                                                    \
  public:                                                                                                              \
    Type##Handle() = default;                                                                                          \
                                                                                                                       \
    explicit operator UINT() const                                                                                     \
    {                                                                                                                  \
      return GetInner();                                                                                               \
    }                                                                                                                  \
                                                                                                                       \
    explicit operator bool() const                                                                                     \
    {                                                                                                                  \
      return not IsNull();                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
  protected:                                                                                                           \
    friend class BindlessManager;                                                                                      \
                                                                                                                       \
    explicit Type##Handle( uint32_t const index ) : BindlessHandle{ index }                                            \
    {}                                                                                                                 \
  };                                                                                                                   \
  static_assert( sizeof( Type##Handle ) == sizeof( DWORD32 ) )

TYPED_BINDLESS_HANDLE( SRV );
TYPED_BINDLESS_HANDLE( UAV );
TYPED_BINDLESS_HANDLE( CBV );
TYPED_BINDLESS_HANDLE( Sampler );

} // namespace Ember
