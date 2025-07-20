#pragma once

#include <array>
#include <mutex>
#include <queue>

#include "BindlessHandle.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{

class BindlessManager
{
  class RabbitPullingFreeList
  {
    std::queue<uint32_t> m_Recycled{};
    uint32_t             m_MaxReached{ 0 };
    uint32_t             m_MaxAllowed{ 0 };

  public:
    RabbitPullingFreeList() = default;
    explicit RabbitPullingFreeList( uint32_t max_allowed );

    uint32_t Allocate();
    void     Free( uint32_t index );
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

  SRVHandle CreateDescriptorHandle( ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc );
  UAVHandle CreateDescriptorHandle( ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc );
  UAVHandle CreateDescriptorHandle(
      ID3D12Resource* resource, ID3D12Resource* counter, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc );

  SamplerHandle                        CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc );

  std::array<ID3D12DescriptorHeap*, 2> GetBindlessDescriptorHeaps() const;
};
} // namespace Ember
