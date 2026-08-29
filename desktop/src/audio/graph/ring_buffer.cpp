// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// The template class RingBuffer is header-only; this translation unit exists to
// host explicit instantiations that the linker can reference from other TUs,
// avoiding template-bloat in every TU that includes the header.
//
// If you link against libarrow-audio-core (or whatever the domain library is
// called) you get these instantiations for free.  If you need a different
// (Frames, Channels) pair at link time, just include the header directly — the
// template is always available.

#include "audio/graph/ring_buffer.hpp"

namespace arrow::audio {

// Explicit instantiations for the ring sizes used by the playback graph.
// The playback graph allocates SpscPcmRing(capacity, channels) dynamically, so
// this file also satisfies the linker requirement for any RingBuffer<…, …> that
// the build references.

template class RingBuffer<512, 2>;
template class RingBuffer<512, 6>;
template class RingBuffer<512, 8>;

template class RingBuffer<2048, 2>;
template class RingBuffer<2048, 6>;
template class RingBuffer<2048, 8>;

template class RingBuffer<4096, 2>;
template class RingBuffer<4096, 6>;
template class RingBuffer<4096, 8>;

template class RingBuffer<16384, 2>;

}  // namespace arrow::audio
