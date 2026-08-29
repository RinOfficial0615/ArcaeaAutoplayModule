#include "manager/network/NetworkHandlerSnapshot.hpp"

#include <algorithm>

namespace arc_helper::network {

std::shared_ptr<const HandlerSnapshot> HandlerSnapshot::Empty() {
    static const std::shared_ptr<const HandlerSnapshot> empty =
        std::make_shared<const HandlerSnapshot>();
    return empty;
}

bool HandlerSnapshot::Contains(std::string_view name, HandlerFn fn) const {
    return std::ranges::any_of(entries_, [name, fn](const Entry &entry) {
        return entry.fn == fn || (!name.empty() && entry.name == name);
    });
}

std::shared_ptr<const HandlerSnapshot> HandlerSnapshot::With(Entry entry) const {
    auto next = std::make_shared<HandlerSnapshot>();
    next->entries_ = entries_;
    next->entries_.push_back(entry);
    std::ranges::stable_sort(next->entries_, [](const Entry &left, const Entry &right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        return left.register_order < right.register_order;
    });
    return next;
}

} // namespace arc_helper::network
