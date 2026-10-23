#include <cassert>
#include <cstring>

#include "ultramodern/rsp.hpp"

#if ULTRAMODERN_RSP_TASKS_COPIED
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>
#endif

static ultramodern::rsp::callbacks_t rsp_callbacks {};

ultramodern::rsp::SubmittedData ultramodern::rsp::submitted_data {};

void ultramodern::rsp::set_callbacks(const callbacks_t& callbacks) {
    rsp_callbacks = callbacks;
}

void ultramodern::rsp::init() {
    if (rsp_callbacks.init != nullptr) {
        rsp_callbacks.init();
    }
}

#if ULTRAMODERN_RSP_TASKS_COPIED
// An RSP task as the game submitted it: its OSTask and the RDRAM its data_ptr and data_size give (an audio task's
// command list), copied as the game starts it (task_submitted). A task the page runs late would read them after the game
// had written over them: a game expects its audio task done within the VI it starts in, as the RSP does it, and Super
// Mario 64's sound thread writes the next list but one into the same buffer and OSTask two VIs after the last, whether
// the task has run or not. Started late and still reading its list in chunks, the task read the start of one list and
// the rest of the next, whose commands wrote over aspMain's jump table in DMEM: it exited at an unhandled jump target.
// With the copy, a task runs on a whole list however late it runs.
struct SubmittedTask {
    const OSTask* original;
    OSTask task;
    uint32_t data_address;
    std::vector<uint8_t> data;
};

// The newest copy for each OSTask the game started a task with, the eight it used last. A task run late takes the
// newest copy of its OSTask, as it would have read the newest list from RDRAM: one that never runs (a runtime may
// complete a task unrun) leaves nothing behind, and a task whose copy is gone runs from RDRAM as before.
static struct {
    std::mutex mutex;
    std::shared_ptr<const SubmittedTask> tasks[8];
    size_t next = 0;
} submitted_tasks;
#endif

void ultramodern::rsp::task_submitted(RDRAM_ARG PTR(OSTask) task_) {
#if ULTRAMODERN_RSP_TASKS_COPIED
    const OSTask* task = TO_PTR(OSTask, task_);
    // A graphics task's OSTask goes to the renderer as a copy already (events.cpp, SpTaskAction)
    if (task->t.type == M_GFXTASK) {
        return;
    }
    // Its data in whole 8-byte units, as an RSP DMA reads them
    auto copy = std::make_shared<SubmittedTask>(SubmittedTask{ task, *task, 0, {} });
    const uint32_t data_start = task->t.data_ptr & 0xFFFFF8;
    const uint32_t data_end = ((task->t.data_ptr & 0xFFFFFF) + task->t.data_size + 7) & ~7u;
    if (task->t.data_size != 0 && data_end <= 0x800000) {
        copy->data_address = data_start;
        copy->data.assign(rdram + data_start, rdram + data_end);
    }
    std::lock_guard lock{ submitted_tasks.mutex };
    for (std::shared_ptr<const SubmittedTask>& submitted : submitted_tasks.tasks) {
        if (submitted != nullptr && submitted->original == task) {
            submitted = std::move(copy);
            return;
        }
    }
    submitted_tasks.tasks[submitted_tasks.next] = std::move(copy);
    submitted_tasks.next = (submitted_tasks.next + 1) % std::size(submitted_tasks.tasks);
#endif
}

bool ultramodern::rsp::run_task(RDRAM_ARG const OSTask* task) {
    assert(rsp_callbacks.run_task != nullptr);

#if ULTRAMODERN_RSP_TASKS_COPIED
    std::shared_ptr<const SubmittedTask> copy;
    {
        std::lock_guard lock{ submitted_tasks.mutex };
        for (const std::shared_ptr<const SubmittedTask>& submitted : submitted_tasks.tasks) {
            if (submitted != nullptr && submitted->original == task) {
                copy = submitted;
            }
        }
    }
    if (copy != nullptr) {
        submitted_data = { copy->data_address, static_cast<uint32_t>(copy->data.size()), const_cast<uint8_t*>(copy->data.data()) };
        const bool ran = rsp_callbacks.run_task(PASS_RDRAM &copy->task);
        submitted_data = {};
        return ran;
    }
#endif
    return rsp_callbacks.run_task(PASS_RDRAM task);
}
