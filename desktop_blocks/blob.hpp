#pragma once
#include "cler.hpp"
#include <memory>

struct Slab;

struct Blob {
    uint8_t* data;   // pointer to slab region
    size_t len;      // valid length
    size_t slot_idx; // slab index for recycling
    Slab* owner_slab;

    void release();
};

struct Slab {
    Slab(size_t num_slots, size_t max_blob_size);

    // Allocate a slice: pops a free slot, returns pointer to region
    // Returns nullptr if no space
    cler::Result<Blob, cler::Error> take_slot();
    void release_slot(size_t slot_idx);

    inline size_t capacity() const { return _num_slots; }
    inline size_t available_slots() const { return _free_slots.size(); }
    inline size_t max_blob_size() const { return _max_blob_size;}

private:
    size_t _num_slots;
    size_t _max_blob_size;
    std::unique_ptr<uint8_t[]> _data;
    cler::Channel<size_t> _free_slots;
};