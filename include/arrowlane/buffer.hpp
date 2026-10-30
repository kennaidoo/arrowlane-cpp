#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "arrowlane/error.hpp"

namespace arrowlane {

class Buffer {
public:
    Buffer() = default;

    explicit Buffer(std::vector<std::byte> owned)
        : owner_(std::make_shared<const std::vector<std::byte>>(std::move(owned))),
          view_(*owner_) {}

    Buffer(std::shared_ptr<const std::vector<std::byte>> owner, std::size_t offset,
           std::size_t length)
        : owner_(std::move(owner)) {
        assert(owner_ != nullptr);
        // Do not form a pointer from a null data() when the slice is empty.
        if (owner_ && length != 0) {
            view_ = std::span<const std::byte>(owner_->data() + offset, length);
        }
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return view_; }
    [[nodiscard]] std::byte operator[](std::size_t index) const noexcept {
        return view_[index];
    }
    [[nodiscard]] std::size_t size() const noexcept { return view_.size(); }
    [[nodiscard]] bool empty() const noexcept { return view_.empty(); }
    [[nodiscard]] const std::shared_ptr<const std::vector<std::byte>>& owner() const
        noexcept {
        return owner_;
    }

private:
    std::shared_ptr<const std::vector<std::byte>> owner_;
    std::span<const std::byte> view_;
};

// Bounds checked slice. A caller must never build a span from unchecked offsets.
[[nodiscard]] Result<Buffer> buffer_slice(
    const std::shared_ptr<const std::vector<std::byte>>& owner, std::size_t offset,
    std::size_t length);

}  // namespace arrowlane
