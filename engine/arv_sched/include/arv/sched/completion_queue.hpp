#pragma once

#include "arv/core/completion_queue.hpp"

namespace arv::sched {

template<typename T>
using SpscQueue = core::SpscQueue<T>;

template<typename T>
using CompletionQueue = core::CompletionQueue<T>;

} // namespace arv::sched
