#include "arrowlane/buffer.hpp"

namespace arrowlane {

Result<Buffer> buffer_slice(const std::shared_ptr<const std::vector<std::byte>>& owner,
                            std::size_t offset, std::size_t length) {
    if (!owner) {
        return std::unexpected(make_error(Code::Invalid, "buffer owner is null"));
    }
    if (offset > owner->size() || length > owner->size() - offset) {
        return std::unexpected(make_error(
            Code::TruncatedFrame,
            "buffer slice " + std::to_string(offset) + "+" + std::to_string(length) +
                " exceeds " + std::to_string(owner->size())));
    }
    return Buffer(owner, offset, length);
}

}  // namespace arrowlane
