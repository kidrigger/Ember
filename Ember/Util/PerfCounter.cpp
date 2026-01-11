#include "PerfCounter.hpp"

#include <algorithm>

#include "DataUtil.hpp"
#include "HelperUtils.hpp"

Ember::PerfCounter::PerfCounter()
{
  ZeroMemory( m_256FrameAvgBuffer, 256 * sizeof( m_256FrameAvgBuffer[0] ) );
  ::QueryPerformanceCounter( &m_PrevQueryPerfCounter );
}

Ember::PerfCounter::PerfCounter(
    std::vector<ComPtr<ID3D12QueryHeap>> query_heaps, std::vector<ComPtr<ID3D12Resource>> query_readback_buffers )
  : m_QueryHeaps{ std::move( query_heaps ) }, m_QueryReadbackBuffers{ std::move( query_readback_buffers ) }
{
  ZeroMemory( m_256FrameAvgBuffer, U32ByteSizeOf( m_256FrameAvgBuffer ) );
  ::QueryPerformanceCounter( &m_PrevQueryPerfCounter );

  ZeroMemory( &m_PipelineStats, sizeof( m_PipelineStats ) );
}

void Ember::PerfCounter::Create( PerfCounter* perf_counter, ID3D12Device* device, uint32_t const num_frames )
{
  uint32_t constexpr kQueriesPerFrame = 1;
  std::vector<ComPtr<ID3D12QueryHeap>> query_heaps;
  std::vector<ComPtr<ID3D12Resource>>  query_readback_buffers;

  // Skip creating resources if we are stripping meta info.
#if not defined( STRIP_META_INFO )

  D3D12_QUERY_HEAP_DESC const qh_desc = {
    .Type     = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1,
    .Count    = kQueriesPerFrame,
    .NodeMask = 0,
  };
  query_heaps.resize( num_frames );
  for ( auto& qh : query_heaps )
  {
    ERR_ABORT( device->CreateQueryHeap( &qh_desc, IID_PPV_ARGS( &qh ) ) );
  }

  query_readback_buffers.resize( num_frames );

  CD3DX12_RESOURCE_DESC const buffer_desc =
      CD3DX12_RESOURCE_DESC::Buffer( sizeof( D3D12_QUERY_DATA_PIPELINE_STATISTICS1 ) * kQueriesPerFrame );
  CD3DX12_HEAP_PROPERTIES const heap_props = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_READBACK );

  for ( auto& qrb : query_readback_buffers )
  {
    ERR_ABORT( device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &buffer_desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS( &qrb ) ) );
  }

#endif

  new ( perf_counter ) PerfCounter{ std::move( query_heaps ), std::move( query_readback_buffers ) };
}

void Ember::PerfCounter::Tick()
{
  LARGE_INTEGER perf_counter, freq;
  ::QueryPerformanceCounter( &perf_counter ); // Always returns true on Win XP or later.
  ::QueryPerformanceFrequency( &freq );       // Always returns true on Win XP or later.

  m_FrameTimeMs  = ( float )( ( double )( 1000 * ( perf_counter.QuadPart - m_PrevQueryPerfCounter.QuadPart ) ) /
                             ( double )freq.QuadPart );

  m_BufferSumMs -= m_256FrameAvgBuffer[m_AvgBufferHead];
  m_BufferSumMs += m_FrameTimeMs;
  m_256FrameAvgBuffer[m_AvgBufferHead++]  = m_FrameTimeMs;
  m_AvgBufferHead                        %= 256;

  m_SampleCount                           = std::min( m_SampleCount + 1.0f, ( float )kSampleCount );

  m_PrevQueryPerfCounter                  = perf_counter;
}

float Ember::PerfCounter::GetAvgFrameTime() const
{
  return m_BufferSumMs / m_SampleCount;
}

float Ember::PerfCounter::GetDeltaMilliSeconds() const
{
  return std::clamp( m_FrameTimeMs, 0.0f, kMaxDeltaMs );
}

D3D12_QUERY_DATA_PIPELINE_STATISTICS1 const& Ember::PerfCounter::GetPipelineStats() const
{
  return m_PipelineStats;
}

void Ember::PerfCounter::GatherPipelineStatistics()
{
  m_GatherPipelineStatistics = kPipelineStatsFrameGatherCount;
}

void Ember::PerfCounter::BeginQuery( ID3D12GraphicsCommandList* command_list, uint32_t const frame_index ) const
{
#if not defined( STRIP_META_INFO )
  if ( m_GatherPipelineStatistics )
  {
    command_list->BeginQuery( m_QueryHeaps[frame_index].Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0 );
  }
#endif
}

void Ember::PerfCounter::EndQuery( ID3D12GraphicsCommandList* command_list, uint32_t const frame_index ) const
{
#if not defined( STRIP_META_INFO )
  if ( m_GatherPipelineStatistics )
  {
    command_list->EndQuery( m_QueryHeaps[frame_index].Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0 );
    command_list->ResolveQueryData(
        m_QueryHeaps[frame_index].Get(),
        D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
        0,
        1,
        m_QueryReadbackBuffers[frame_index].Get(),
        0 );
  }
#endif
}

void Ember::PerfCounter::UpdatePipelineStats( uint32_t const frame_index )
{
#if not defined( STRIP_META_INFO )
  if ( m_GatherPipelineStatistics )
  {
    ASSERT( frame_index < m_QueryReadbackBuffers.size() );

    // Map the readback buffer and copy the stats.
    D3D12_RANGE read_range{ 0, sizeof( D3D12_QUERY_DATA_PIPELINE_STATISTICS1 ) };
    void*       data = nullptr;
    if ( SUCCEEDED( m_QueryReadbackBuffers[frame_index]->Map( 0, &read_range, &data ) ) )
    {
      memcpy( &m_PipelineStats, data, sizeof( D3D12_QUERY_DATA_PIPELINE_STATISTICS1 ) );
      m_QueryReadbackBuffers[frame_index]->Unmap( 0, nullptr );
    }
    m_GatherPipelineStatistics--;
  }
#endif
}

float const* Ember::PerfCounter::GetDeltaValues() const
{
  return m_256FrameAvgBuffer;
}
