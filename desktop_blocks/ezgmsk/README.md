# EZGMSK Modulator Implementation Plan

## Overview
The EZGMSK modulator is designed to generate GMSK-modulated frames compatible with EasyLink radios and the existing ezgmsk_demod demodulator. The implementation follows the structure of liquid-dsp's gmskframegen while adapting to EZGMSK-specific requirements.

## Frame Structure
The EZGMSK frame consists of the following components:
1. **Preamble**: Configurable symbols for synchronization (user-defined pattern)
2. **Syncword**: User-defined symbol pattern for frame detection
3. **Header**: Frame metadata (length in bytes, user-configurable)
4. **Payload**: Variable-length data payload
5. **Tail**: Filter flush symbols (2*m symbols)

## Implementation Steps

### Step 1: Create _ezgmsk_mod.h
Define the C API following gmskframegen structure but adapted for EZGMSK:
- Define `ezgmsk_mod` opaque struct pointer
- Frame state enumeration
- Core functions:
  - `ezgmsk_mod_create_set()` - Create modulator with parameters
  - `ezgmsk_mod_destroy()` - Cleanup
  - `ezgmsk_mod_reset()` - Reset to initial state
  - `ezgmsk_mod_assemble()` - Assemble frame from header and payload
  - `ezgmsk_mod_execute()` - Generate modulated samples
  - `ezgmsk_mod_get_frame_len()` - Get total frame length in samples

### Step 2: Implement _ezgmsk_mod.c
Internal structure based on gmskframegen:
```c
struct ezgmsk_mod_s {
    // GMSK modulator
    gmskmod mod;
    unsigned int k;             // samples/symbol
    unsigned int m;             // filter delay (symbols)
    float BT;                   // bandwidth-time product
    
    // Frame parameters
    unsigned int preamble_len;  // preamble length in symbols
    unsigned char* preamble;    // preamble pattern
    unsigned int syncword_len;  // syncword length in symbols
    unsigned char* syncword;    // syncword pattern
    unsigned int header_len;    // header length in bytes
    unsigned int payload_len;   // payload length in bytes
    unsigned int tail_len;      // tail symbols (2*m)
    
    // Frame state machine
    enum {
        STATE_UNASSEMBLED,
        STATE_PREAMBLE,
        STATE_SYNCWORD,
        STATE_HEADER,
        STATE_PAYLOAD,
        STATE_TAIL
    } state;
    
    // Buffers
    unsigned char* header_enc;  // encoded header
    unsigned char* payload_enc; // encoded payload
    complex float* buf_sym;     // output symbol buffer
    unsigned int buf_idx;       // buffer index
    unsigned int symbol_counter;
};
```

Key implementation details:
- Adapt state machine for EZGMSK frame structure
- Remove liquid-dsp specific encoding (CRC, FEC) from header/payload
- Use raw binary data for header and payload
- Implement configurable preamble and syncword generation
- Generate GMSK symbols using liquid's gmskmod

### Step 3: Update ezgmsk_mod.hpp
Integrate the C library into the C++ block:
- Add `ezgmsk::ezgmsk_mod` member variable
- Initialize in constructor with frame parameters
- Implement `send_header()` and `send_payload()` methods
- In `procedure()`:
  - Assemble frames from queued data
  - Generate modulated samples
  - Write to output channel

## Parameters
Key configuration parameters matching the demodulator:
- `k`: Samples per symbol (typically 2)
- `m`: Filter delay in symbols (typically 3)
- `BT`: Bandwidth-time product (typically 0.5)
- `preamble_symbols_len`: Length of preamble in symbols
- `syncword_symbols`: Syncword pattern (binary symbols)
- `syncword_symbols_len`: Length of syncword in symbols
- `header_bytes_len`: Header length in bytes

## Important: Preamble Pattern
The modulator automatically generates the standard alternating bit preamble pattern (0,1,0,1,0,1...) that is compatible with the EZGMSK demodulator. You only need to specify the preamble length when creating the modulator.

## Differences from gmskframegen
1. **No built-in encoding**: EZGMSK uses raw data without CRC/FEC encoding
2. **Fixed preamble pattern**: Automatically generates alternating bits (0,1,0,1...) matching demodulator expectations
3. **Syncword support**: Additional syncword field between preamble and header
4. **Simplified header**: No protocol version or encoding fields
5. **Direct symbol input**: Syncword specified as symbols, not bits

## Next Steps
1. Implement _ezgmsk_mod.h with complete API
2. Implement _ezgmsk_mod.c following this design
3. Update CMakeLists.txt to build the library
4. Complete ezgmsk_mod.hpp integration