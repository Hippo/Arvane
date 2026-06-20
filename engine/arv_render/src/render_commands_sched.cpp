#include "arv/render/render_commands.hpp"

#include "arv/sched/owner_thread.hpp"

namespace arv::render {
namespace {

class SchedRenderCommandOwnerThread final : public IRenderCommandOwnerThread {
public:
  explicit SchedRenderCommandOwnerThread(sched::OwnerThread& owner_thread) :
      owner_thread_(owner_thread) {
  }

  [[nodiscard]] usize owner_thread_hash() const noexcept override {
    return owner_thread_.owner_thread_hash();
  }

  [[nodiscard]] bool is_owner_thread() const noexcept override {
    return owner_thread_.is_owner_thread();
  }

  [[nodiscard]] bool post(std::function<void()> command) override {
    return owner_thread_.post(std::move(command));
  }

  void call(std::function<void()> command) override {
    owner_thread_.call(std::move(command));
  }

private:
  sched::OwnerThread& owner_thread_;
};

} // namespace

RenderCommandQueue::RenderCommandQueue(
    IRenderCommandTarget& target,
    sched::OwnerThread& owner_thread) : RenderCommandQueue(target) {
  attach_owner_thread(owner_thread);
}

void RenderCommandQueue::attach_owner_thread(sched::OwnerThread& owner_thread) {
  owned_owner_thread_ = std::make_unique<SchedRenderCommandOwnerThread>(owner_thread);
  owner_thread_ = owned_owner_thread_.get();
}

} // namespace arv::render
