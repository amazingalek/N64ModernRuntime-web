#ifndef __ULTRAMODERN_DISPLAYLISTS_HPP__
#define __ULTRAMODERN_DISPLAYLISTS_HPP__

#include <atomic>
#include <cstdint>

// With ULTRAMODERN_LOADS_WAIT_FOR_DISPLAY_LISTS (on with Emscripten; displaylists.cpp says why), a load from ROM onto
// commands of a display list the renderer hasn't read yet waits until it has. Without it these do nothing.
namespace ultramodern {
    // Where the renderer marks the RDRAM it reads display list commands from: [0] the lists read in full (counted by
    // displaylist_read), [1 + physical address / 256] the number of the last list that read a command from those 256
    // bytes of RDRAM's first 8 MB ([0] + 1 while it is read). nullptr without the option.
    std::atomic<uint32_t>* displaylist_reads();
    // A graphics task sent to the renderer.
    void displaylist_sent();
    // The renderer has read the oldest list sent to it.
    void displaylist_read();
    // Waits, at most 2 s, until the renderer has read every list sent so far if a load to RDRAM at this physical
    // address and size would land on commands of one of the last lists it read.
    void wait_for_displaylists_read(uint32_t rdram_address, uint32_t size);
}

#endif
