#pragma once

#include "Util/Runtime.hpp"

namespace Ember
{

class DeviceHandle
{
public:
  DeviceHandle() = default;
  [[nodiscard]] bool IsNull() const noexcept;

protected:
  explicit DeviceHandle( uint32_t index );

  [[nodiscard]] uint32_t GetInner() const
  {
    return m_Handle;
  }

private:
  constexpr static uint32_t kInvalid{ UINT32_MAX };
  uint32_t                  m_Handle{ kInvalid };
};

// Guarantee size matches the 32bit (DWORD) constants.
static_assert( sizeof( DeviceHandle ) == sizeof( DWORD32 ) );

#define TYPED_HANDLE( Type, Owner )                                                                                    \
  class Type##Handle : public DeviceHandle                                                                             \
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
    friend class Owner;                                                                                                \
                                                                                                                       \
    explicit Type##Handle( uint32_t const index ) : DeviceHandle{ index }                                              \
    {}                                                                                                                 \
  };                                                                                                                   \
  static_assert( sizeof( Type##Handle ) == sizeof( DWORD32 ) )

TYPED_HANDLE( SRV, BindlessManager );
TYPED_HANDLE( UAV, BindlessManager );
TYPED_HANDLE( CBV, BindlessManager );
TYPED_HANDLE( Sampler, BindlessManager );
TYPED_HANDLE( RawDescriptor, BindlessManager );
TYPED_HANDLE( Material, MaterialManager );
TYPED_HANDLE( Geometry, GeometryManager );

} // namespace Ember
