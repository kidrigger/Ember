#pragma once

#include <array>
#include <mutex>
#include <queue>

#include "BindlessHandle.hpp"
#include "Buffer.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{

class BindlessManager
{
  class RabbitPullingFreeList
  {
    std::deque<uint32_t> m_Recycled{};
    uint32_t             m_MaxReached{ 0 };
    uint32_t             m_MaxAllowed{ 0 };

  public:
    RabbitPullingFreeList() = default;
    explicit RabbitPullingFreeList( uint32_t max_allowed );

    uint32_t Allocate();
    void     Free( uint32_t index );

    uint32_t InUse() const;
  };

  ComPtr<ID3D12Device2>        m_Device;

  RabbitPullingFreeList        m_ResourceFreeList;
  ComPtr<ID3D12DescriptorHeap> m_ResourceDescriptorHeap;
  std::mutex                   m_ResourceDescriptorLock;
  uint32_t                     m_ResourceDescriptorIncrement;

  RabbitPullingFreeList        m_SamplerFreeList;
  ComPtr<ID3D12DescriptorHeap> m_SamplerDescriptorHeap;
  std::mutex                   m_SamplerDescriptorLock;
  uint32_t                     m_SamplerDescriptorIncrement;

public:
  BindlessManager() = default;
  BindlessManager(
      ComPtr<ID3D12Device2>        device,
      ComPtr<ID3D12DescriptorHeap> resource_descriptor_heap,
      uint32_t                     resource_descriptor_increment,
      uint32_t                     max_resources,
      ComPtr<ID3D12DescriptorHeap> sampler_descriptor_heap,
      uint32_t                     sampler_descriptor_increment,
      uint32_t                     max_samplers );

  static void Create(
      BindlessManager* bindless, ComPtr<ID3D12Device2> device, uint32_t max_resources, uint32_t max_samplers );

  [[nodiscard]] CBVHandle CreateDescriptorHandle( D3D12_CONSTANT_BUFFER_VIEW_DESC const& cbv_desc );
  [[nodiscard]] SRVHandle CreateDescriptorHandle(
      ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc );
  [[nodiscard]] UAVHandle CreateDescriptorHandle(
      ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc );
  [[nodiscard]] UAVHandle CreateDescriptorHandle(
      ID3D12Resource* resource, ID3D12Resource* counter, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc );

  [[nodiscard]] SamplerHandle                        CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc );

  void                                               Free( SRVHandle handle );
  void                                               Free( UAVHandle handle );
  void                                               Free( CBVHandle handle );
  void                                               Free( SamplerHandle handle );

  [[nodiscard]] std::array<ID3D12DescriptorHeap*, 2> GetBindlessDescriptorHeaps() const;

  BindlessManager( BindlessManager const& other )                = delete;
  BindlessManager( BindlessManager&& other ) noexcept            = delete;
  BindlessManager& operator=( BindlessManager const& other )     = delete;
  BindlessManager& operator=( BindlessManager&& other ) noexcept = delete;
  ~BindlessManager();
};
} // namespace Ember
