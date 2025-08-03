#include "blob.hpp"

void Blob::release() {
  assert(owner_slab != nullptr && "BUG: double release");
  assert(slot_idx < owner_slab->capacity() && "slot_idx out of bounds!");
  if (owner_slab) {
    owner_slab->release_slot(slot_idx);
  }
  owner_slab = nullptr; // Prevent double release
}

Slab::Slab(size_t num_slots, size_t max_blob_size)
    : _num_slots(num_slots), _max_blob_size(max_blob_size), 
        _free_slots(num_slots)
{
    _data = std::make_unique<uint8_t[]>(num_slots * max_blob_size);
    for (size_t i = 0; i < num_slots; ++i) {
        _free_slots.push(i);
    }
}

// Allocate a slice: pops a free slot, returns pointer to region
// Returns nullptr if no space
cler::Result<Blob, cler::Error> Slab::take_slot() {
    size_t slot_idx;
    if (!_free_slots.try_pop(slot_idx)) {
        return cler::Error::ProcedureError;
    }
    uint8_t* ptr = _data.get() + (slot_idx * _max_blob_size);
    return Blob{ptr, _max_blob_size, slot_idx, this};
}

void Slab::release_slot(size_t slot_idx) {
    assert(_free_slots.try_push(slot_idx));
}