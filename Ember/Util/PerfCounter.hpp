#pragma once

#include <vector>

#include "DirectXHeaders.hpp"
#include "Runtime.hpp"

#if defined( NDEBUG )
#define STRIP_META_INFO
#endif

namespace Ember
{

class PerfCounter
{
public:
  uint32_t constexpr static kSampleCount = 256;
  float constexpr static kMaxDeltaMs     = 1000.0 / 24.0; // Below 24fps, I'd rather slow down than jump.

private:
  D3D12_QUERY_DATA_PIPELINE_STATISTICS1 m_PipelineStats{};
  LARGE_INTEGER                         m_PrevQueryPerfCounter{};
  float                                 m_FrameTimeMs{ 0.0f };
  float                                 m_256FrameAvgBuffer[kSampleCount]{};
  float                                 m_BufferSumMs{ 0.0f };
  int                                   m_AvgBufferHead{ 0 };
  float                                 m_SampleCount{ 0.0f };

  // Query heap.
  std::vector<ComPtr<ID3D12QueryHeap>> m_QueryHeaps;
  std::vector<ComPtr<ID3D12Resource>>  m_QueryReadbackBuffers;

public:
  PerfCounter();
  PerfCounter(
      std::vector<ComPtr<ID3D12QueryHeap>> query_heaps, std::vector<ComPtr<ID3D12Resource>> query_readback_buffers );

  static void                Create( PerfCounter* perf_counter, ID3D12Device* device, uint32_t num_frames );

  void                       Tick();

  [[nodiscard]] float        GetAvgFrameTime() const;
  [[nodiscard]] float        GetDeltaMilliSeconds() const;
  [[nodiscard]] float const* GetDeltaValues() const;
  [[nodiscard]] D3D12_QUERY_DATA_PIPELINE_STATISTICS1 const& GetPipelineStats() const;

  void BeginQuery( ID3D12GraphicsCommandList* command_list, uint32_t frame_index ) const;
  void EndQuery( ID3D12GraphicsCommandList* command_list, uint32_t frame_index ) const;
  void UpdatePipelineStats( uint32_t frame_index );
};

} // namespace Ember
