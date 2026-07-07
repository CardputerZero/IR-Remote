#include "core/ir_remote_router.hpp"

namespace ir_remote {

void IRRemoteRouter::replace(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _current_page.set(page);
}

void IRRemoteRouter::push(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _history.push_back(_current_page.get());
    _current_page.set(page);
}

void IRRemoteRouter::back()
{
    if (_history.empty()) {
        replace(PageId::Remote);
        return;
    }

    PageId previous = _history.back();
    _history.pop_back();
    _current_page.set(previous);
}

}  // namespace ir_remote
