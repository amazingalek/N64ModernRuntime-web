#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include "ultramodern/displaylists.hpp"

// On a console the RSP reads a display list within a few ms of the game sending it, long before the game's next load
// from ROM lands, so a game may load over commands its last list runs: one loads its title screen's textures over its
// logos' model the frame after their last list. Here the renderer reads a list on the graphics thread, which on a slow
// GPU can be a frame or more behind the game, and would take what the load left there for commands. So the renderer
// marks in displaylist_reads() the RDRAM it reads each list's commands from (RT64's interpreter, given the array by
// RecompFrontend's glue, which counts the lists read), librecomp counts the graphics tasks sent, and a load from ROM
// onto commands one of the last lists read ran waits until the renderer has read every list sent before it, on
// whichever thread the load is (the one that loads over a list needn't be the one that sends it). A load anywhere else,
// as a sound's samples, goes at once.

#if ULTRAMODERN_LOADS_WAIT_FOR_DISPLAY_LISTS

namespace {
    // RDRAM's 8 MB in 256-byte units.
    constexpr uint32_t unit_shift = 8;
    constexpr uint32_t units = 0x800000 >> unit_shift;

    // Commands one of the last this many lists the renderer read ran are ones the lists it hasn't read yet may run.
    constexpr uint32_t recent_lists = 8;

    // [0] the lists the renderer has read in full; [1 + unit] the number of the last list it read commands from there
    // in (counting from 1, so 0 is none; displaylists.hpp).
    std::atomic<uint32_t> reads[1 + units];
    std::atomic<uint32_t> lists_sent{ 0 };
}

std::atomic<uint32_t>* ultramodern::displaylist_reads() {
    return reads;
}

void ultramodern::displaylist_sent() {
    lists_sent.fetch_add(1, std::memory_order_relaxed);
}

void ultramodern::displaylist_read() {
    reads[0].fetch_add(1, std::memory_order_release);
}

void ultramodern::wait_for_displaylists_read(uint32_t rdram_address, uint32_t size) {
    uint32_t sent = lists_sent.load(std::memory_order_relaxed);
    uint32_t read = reads[0].load(std::memory_order_acquire);
    uint32_t first = rdram_address >> unit_shift;
    if (read >= sent || size == 0 || first >= units) {
        return;
    }

    uint32_t last = std::min((rdram_address + size - 1) >> unit_shift, units - 1);
    bool over_commands = false;
    for (uint32_t unit = first; unit <= last && !over_commands; unit++) {
        uint32_t list = reads[1 + unit].load(std::memory_order_relaxed);
        over_commands = (list != 0) && (list + recent_lists > read);
    }

    if (!over_commands) {
        return;
    }

    // At most 2 s, after which the lists still unread count as read, so a renderer that stopped reading them (or reads
    // fewer than are sent) costs one wait, not one per load.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (reads[0].load(std::memory_order_acquire) < sent) {
        if (std::chrono::steady_clock::now() >= deadline) {
            lists_sent.store(reads[0].load(std::memory_order_acquire), std::memory_order_relaxed);
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

#else

std::atomic<uint32_t>* ultramodern::displaylist_reads() {
    return nullptr;
}

void ultramodern::displaylist_sent() {}

void ultramodern::displaylist_read() {}

void ultramodern::wait_for_displaylists_read(uint32_t, uint32_t) {}

#endif
