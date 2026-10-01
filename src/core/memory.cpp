// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Ronald "Eidolon" Kinard
// Copyright (C) 2025 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#include "memory.h"

#include <cstdint>
#include <cstring>

#include "../cxxutil.hpp"
#include "../z_zone.h"
#include "../lua_script.h"

using namespace srb2;

namespace
{

class LinearMemory
{
	size_t size_;
	size_t height_;
	void* memory_;

public:
	constexpr explicit LinearMemory(size_t size) noexcept;

	void* allocate(size_t size);
	void reset() noexcept;
};

constexpr LinearMemory::LinearMemory(size_t size) noexcept : size_(size), height_{0}, memory_{nullptr} {}

void* LinearMemory::allocate(size_t size)
{
	size_t aligned_size = (size + 15) & ~15;
	if (height_ + aligned_size > size_)
	{
		throw std::bad_alloc();
	}

	if (memory_ == nullptr)
	{
		memory_ = Z_Malloc(size_, PU_STATIC, nullptr);
	}

	void* ptr = (void*)((uintptr_t)(memory_) + height_);
	height_ += aligned_size;
	return ptr;
}

void LinearMemory::reset() noexcept
{
	height_ = 0;
}

} // namespace

PoolAllocator::~PoolAllocator()
{
	release();
}

constexpr static size_t nearest_multiple(size_t v, size_t divisor)
{
	return (v + (divisor - 1)) & ~(divisor - 1);
}

// A chunk's blocks are no longer linked into the free list when the chunk is
// made: the ones never handed out are taken in order from fresh_ instead
// (WORLDWIDE.md 8.82). Blocks come out in the same order as before -- the
// ones handed back first, most recent first, then the never-used ones in
// address order -- but the pool now knows where its used blocks end, which is
// what a snapshot needs to copy and no more.
PoolAllocator::ChunkFooter* PoolAllocator::allocate_chunk()
{
	if (spare_ != nullptr)
	{
		ChunkFooter* footer = spare_;
		spare_ = footer->next;
		footer->next = nullptr;
		return footer;
	}

	uint8_t* chunk = (uint8_t*)Z_Malloc(nearest_multiple(blocks_ * block_size_, alignof(ChunkFooter)) + sizeof(ChunkFooter), tag_, nullptr);
	ChunkFooter* footer = (ChunkFooter*)(chunk + (blocks_ * block_size_));
	footer->next = nullptr;
	footer->start = (void*)chunk;
	return footer;
}

PoolAllocator::ChunkFooter* PoolAllocator::last_chunk() const noexcept
{
	ChunkFooter* last = first_chunk_;
	while (last != nullptr && last->next != nullptr)
	{
		last = last->next;
	}
	return last;
}

void* PoolAllocator::allocate()
{
	void* ret;

	if (head_ != nullptr)
	{
		ret = head_;
		head_ = head_->next;
	}
	else
	{
		if (fresh_ == fresh_end_)
		{
			// Every block handed out at least once: another chunk at the end.
			ChunkFooter* new_chunk = allocate_chunk();
			ChunkFooter* last = last_chunk();

			if (last == nullptr)
				first_chunk_ = new_chunk;
			else
				last->next = new_chunk;

			fresh_ = (uint8_t*)new_chunk->start;
			fresh_end_ = fresh_ + (blocks_ * block_size_);
		}

		ret = fresh_;
		fresh_ += block_size_;
	}

	// A block a restore handed back -- or a chunk it kept aside -- may still be
	// known to Lua as the object that stood there before the restore took it
	// away. deallocate() forgets a block as it frees it; a restore frees
	// nothing one block at a time, so the block is forgotten here instead.
	// Nothing to find for a block that went through deallocate() already.
	LUA_InvalidateUserdata(ret);

	allocated_blocks_++;
	return ret;
}

void PoolAllocator::deallocate(void* p)
{
	// Required in case this block is reused
	LUA_InvalidateUserdata(p);

	FreeBlock* block = reinterpret_cast<FreeBlock*>(p);
	block->next = head_;
	head_ = block;
}

size_t PoolAllocator::chunks() const noexcept
{
	size_t n = 0;
	for (const ChunkFooter* i = first_chunk_; i != nullptr; i = i->next)
	{
		n++;
	}
	return n;
}

namespace
{

// A pool's snapshot: this header, the start of each chunk it covers (to
// refuse a snapshot of chunks that are not these), then each chunk's used
// blocks -- the whole chunk, except for the last, whose blocks stop at fresh.
struct PoolSnapshotHeader
{
	uint32_t magic;
	uint32_t chunks;
	size_t block_size;
	size_t blocks;
	void* head;
	size_t allocated;
	uint8_t* fresh;
	uint8_t* fresh_end;
};

constexpr uint32_t kPoolSnapshotMagic = 0x4C4F4F50; // "POOL"

} // namespace

size_t PoolAllocator::snapshot_size() const noexcept
{
	size_t size = sizeof(PoolSnapshotHeader);
	for (const ChunkFooter* i = first_chunk_; i != nullptr; i = i->next)
	{
		size += sizeof(void*);
		size += (i->next != nullptr) ? (blocks_ * block_size_) : (size_t)(fresh_ - (uint8_t*)i->start);
	}
	return size;
}

size_t PoolAllocator::snapshot(void* dst, size_t capacity) const noexcept
{
	const size_t size = snapshot_size();
	uint8_t* p = (uint8_t*)dst;
	PoolSnapshotHeader h;

	if (size > capacity)
	{
		return 0;
	}

	// Zeroed first: any padding goes into the snapshot too, and two snapshots
	// of the same pool must compare equal byte for byte.
	memset(&h, 0, sizeof h);
	h.magic = kPoolSnapshotMagic;
	h.chunks = (uint32_t)chunks();
	h.block_size = block_size_;
	h.blocks = blocks_;
	h.head = head_;
	h.allocated = allocated_blocks_;
	h.fresh = fresh_;
	h.fresh_end = fresh_end_;
	memcpy(p, &h, sizeof h);
	p += sizeof h;

	for (const ChunkFooter* i = first_chunk_; i != nullptr; i = i->next)
	{
		memcpy(p, &i->start, sizeof(void*));
		p += sizeof(void*);
	}

	for (const ChunkFooter* i = first_chunk_; i != nullptr; i = i->next)
	{
		const size_t used = (i->next != nullptr) ? (blocks_ * block_size_) : (size_t)(fresh_ - (uint8_t*)i->start);
		memcpy(p, i->start, used);
		p += used;
	}

	return size;
}

/// Whether restore() would take this snapshot, without writing anything: a
/// caller restoring several pools checks them all first, so that it never
/// leaves some restored and others not.
bool PoolAllocator::can_restore(const void* src, size_t length) const noexcept
{
	const uint8_t* p = (const uint8_t*)src;
	const size_t chunkbytes = blocks_ * block_size_;
	PoolSnapshotHeader h;
	size_t expect;
	uint32_t k;

	if (src == nullptr || length < sizeof h)
	{
		return false;
	}

	memcpy(&h, p, sizeof h);

	if (h.magic != kPoolSnapshotMagic || h.block_size != block_size_ || h.blocks != blocks_
		|| h.chunks > chunks())
	{
		return false;
	}

	expect = sizeof h + (h.chunks * sizeof(void*));
	if (length < expect)
	{
		return false;
	}

	{
		const ChunkFooter* c = first_chunk_;

		for (k = 0; k < h.chunks; k++, c = c->next)
		{
			void* start;
			memcpy(&start, p + sizeof h + (k * sizeof(void*)), sizeof start);

			if (start != c->start)
			{
				return false; // other chunks: another level's pool
			}

			if (k + 1 < h.chunks)
			{
				expect += chunkbytes;
			}
			else
			{
				const uint8_t* first = (const uint8_t*)c->start;

				if (h.fresh < first || h.fresh > first + chunkbytes || h.fresh_end != first + chunkbytes
					|| ((size_t)(h.fresh - first) % block_size_) != 0)
				{
					return false;
				}

				expect += (size_t)(h.fresh - first);
			}
		}
	}

	if (h.chunks == 0 && (h.head != nullptr || h.fresh != nullptr || h.allocated != 0))
	{
		return false;
	}

	return (length == expect);
}

bool PoolAllocator::restore(const void* src, size_t length) noexcept
{
	const uint8_t* p = (const uint8_t*)src;
	const size_t chunkbytes = blocks_ * block_size_;
	PoolSnapshotHeader h;
	uint32_t k;

	// Everything is checked before anything is written.
	if (can_restore(src, length) == false)
	{
		return false;
	}

	memcpy(&h, p, sizeof h);

	// Every covered chunk as the snapshot had it.
	{
		const uint8_t* data = p + sizeof h + (h.chunks * sizeof(void*));
		ChunkFooter* c = first_chunk_;
		ChunkFooter* last_kept = nullptr;

		for (k = 0; k < h.chunks; k++)
		{
			const size_t used = (k + 1 < h.chunks) ? chunkbytes : (size_t)(h.fresh - (uint8_t*)c->start);

			memcpy(c->start, data, used);
			data += used;
			last_kept = c;
			c = c->next;
		}

		// Chunks grown since: nothing in the restored pool reaches them. Kept
		// aside rather than freed, so a pool that grows and is restored every
		// pass does not allocate a chunk every pass; their blocks are forgotten
		// by Lua as they are handed out again (allocate()).
		if (last_kept != nullptr)
		{
			last_kept->next = nullptr;
		}
		else
		{
			first_chunk_ = nullptr;
		}

		while (c != nullptr)
		{
			ChunkFooter* next = c->next;
			c->next = spare_;
			spare_ = c;
			c = next;
		}
	}

	head_ = (FreeBlock*)h.head;
	allocated_blocks_ = h.allocated;
	fresh_ = h.fresh;
	fresh_end_ = h.fresh_end;
	return true;
}

void PoolAllocator::release()
{
	ChunkFooter* next = nullptr;
	for (int list = 0; list < 2; list++)
	{
		for (ChunkFooter* i = (list == 0) ? first_chunk_ : spare_; i != nullptr; i = next)
		{
			uint8_t *chunk = (uint8_t*)i->start;
			for (size_t j = 0; j < blocks_; j++)
			{
				// Invalidate all blocks that possibly weren't passed to deallocate
				LUA_InvalidateUserdata(chunk + (j * block_size_));
			}
			next = i->next;
			Z_Free(i->start);
		}
	}

	first_chunk_ = nullptr;
	spare_ = nullptr;
	head_ = nullptr;
	fresh_ = nullptr;
	fresh_end_ = nullptr;
	allocated_blocks_ = 0;
}

static LinearMemory g_frame_memory {4 * 1024 * 1024};

void* Z_Frame_Alloc(size_t size)
{
	return g_frame_memory.allocate(size);
}

void Z_Frame_Reset()
{
	g_frame_memory.reset();
}
