#pragma once

// PandaAsync: minimal background job scheduler.
//
// Model: an owning Scheduler runs a fixed worker pool over priority FIFO queues.
// A job has two phases:
//   - work     runs on a worker thread (pure compute / IO / decode, plain data only).
//   - complete runs on the registered main thread, drained by processCompletions().
// Cancellation is cooperative: work polls Context::isCanceled(); a canceled job never
// runs complete. The module depends only on the C++ standard library.

#include <PandaAsync/Context.hpp>
#include <PandaAsync/JobHandle.hpp>
#include <PandaAsync/Scheduler.hpp>
#include <PandaAsync/Types.hpp>
