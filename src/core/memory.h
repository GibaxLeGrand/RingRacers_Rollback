// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Ronald "Eidolon" Kinard
// Copyright (C) 2025 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#ifndef SRB2_CORE_MEMORY_H
#define SRB2_CORE_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus

#include <cstdint>

namespace srb2
{

/// Pool allocator; manages bulk allocations of same-size block. If the pool is full,
/// allocations will fail by returning nullptr.
class PoolAllocator final
{
	struct FreeBlock
	{
		FreeBlock* next;
	};
	struct ChunkFooter
	{
		ChunkFooter* next;
		void* start;
	};
	ChunkFooter* first_chunk_;
	ChunkFooter* spare_;       // chunks a restore took off the end, kept for reuse
	FreeBlock* head_;          // blocks handed back, most recent first
	uint8_t* fresh_;           // the next block of the last chunk never handed out
	uint8_t* fresh_end_;       // the end of that chunk's blocks
	size_t block_size_;
	size_t blocks_;
	size_t allocated_blocks_;
	int32_t tag_;

	ChunkFooter* allocate_chunk();
	ChunkFooter* last_chunk() const noexcept;

public:
	constexpr PoolAllocator(size_t block_size, size_t blocks, int32_t tag)
		: first_chunk_(nullptr)
		, spare_(nullptr)
		, head_(nullptr)
		, fresh_(nullptr)
		, fresh_end_(nullptr)
		, block_size_(block_size)
		, blocks_(blocks)
		, allocated_blocks_(0)
		, tag_(tag)
	{}
	PoolAllocator(const PoolAllocator&) = delete;
	PoolAllocator(PoolAllocator&&) noexcept = default;
	~PoolAllocator();

	PoolAllocator& operator=(const PoolAllocator&) = delete;
	PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

	void* allocate();
	void deallocate(void* p);
	constexpr size_t block_size() const noexcept { return block_size_; };
	constexpr size_t allocated_blocks() const noexcept { return allocated_blocks_; };
	constexpr size_t allocated_bytes() const noexcept { return allocated_blocks_ * block_size_; };
	constexpr size_t blocks_per_chunk() const noexcept { return blocks_; };
	size_t chunks() const noexcept;

	/// Snapshot support (WORLDWIDE.md 8.82). A pool's whole state is the
	/// contents of its chunks -- the free list is threaded through the free
	/// blocks themselves -- and the few fields above. The blocks past fresh_
	/// have never been handed out, so they need no copy: a snapshot is as large
	/// as the most the pool has ever held, not as its chunks.
	///
	/// restore() puts every block back as the snapshot had it, at its own
	/// address, and gives back the chunks grown since. It refuses (returns
	/// false, touching nothing) a snapshot of other chunks -- another level --
	/// or one that does not parse. What pointed into the pool from outside it
	/// is the caller's business.
	size_t snapshot_size() const noexcept;
	size_t snapshot(void* dst, size_t capacity) const noexcept;
	bool can_restore(const void* src, size_t length) const noexcept;
	bool restore(const void* src, size_t length) noexcept;

	void release();
};

} // namespace srb2


extern "C" {
#endif // __cpluspplus

/// @brief Allocate a block of memory with a lifespan of the current main-thread frame.
/// This function is NOT thread-safe, but the allocated memory may be used across threads.
/// @return a pointer to a block of memory aligned with libc malloc alignment, or null if allocation fails
void* Z_Frame_Alloc(size_t size);

/// @brief Resets per-frame memory. Not thread safe.
void Z_Frame_Reset(void);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // SRB2_CORE_MEMORY_H
