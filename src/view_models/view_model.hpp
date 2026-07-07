#pragma once

#include "core/ir_remote_router.hpp"
#include "core/ir_remote_types.hpp"

namespace ir_remote {

class ViewModel {
public:
    explicit ViewModel(IRRemoteRouter& router) : _router(router)
    {
    }
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&)            = delete;
    ViewModel& operator=(const ViewModel&) = delete;

    virtual PageId pageId() const = 0;
    virtual void onEnter()
    {
    }
    virtual void onExit()
    {
    }
    virtual void onKey(uint32_t key)
    {
        (void)key;
    }
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }

protected:
    IRRemoteRouter& _router;
};

}  // namespace ir_remote
