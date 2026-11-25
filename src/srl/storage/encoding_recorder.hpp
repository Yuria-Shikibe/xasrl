#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <utility>

namespace xasrl {
    namespace detail {
        class encoding_session;
        class encoding_cursor;

        struct encoding_record_link {
            encoding_record_link *previous;
            encoding_record_link *next{};
            void (*destroy)(encoding_record_link *);
        };

        template<typename T>
        struct encoding_record : encoding_record_link {
            T value{};

            explicit encoding_record(encoding_record_link *previous)
                : encoding_record_link{previous, nullptr, [](encoding_record_link *link) {
                    std::destroy_at(static_cast<encoding_record *>(link));
                }} {}
        };
    }

    // Owns only the storage for encoding records. The resource is borrowed.
    class encoding_recorder {
        struct block {
            block *next{};
            std::byte *storage;
            std::size_t bytes;
        };

        std::pmr::memory_resource &resource_;
        block *first_{};
        block *current_{};
        block *last_{};
        std::size_t used_ = sizeof(block);
        std::size_t retained_{};
        bool active_{};

        friend class detail::encoding_session;

        void grow(std::size_t size, std::size_t alignment) {
            constexpr auto max_block = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
            const auto available = std::min(max_block, std::numeric_limits<std::size_t>::max() - retained_);
            const auto overhead = sizeof(block) + alignment - 1;
            if (overhead > available || size > available - overhead) {
                throw std::bad_alloc{};
            }
            const auto preferred = last_ ? std::min(last_->bytes, max_block / 2) * 2 : std::size_t{4096};
            const auto bytes = std::max(size + overhead, std::min(preferred, available));
            auto *storage = ::new (resource_.allocate(bytes, alignof(block))) std::byte[bytes];
            auto *next = ::new (storage) block{nullptr, storage, bytes};
            (last_ ? last_->next : first_) = next;
            current_ = last_ = next;
            retained_ += bytes;
        }

        void *allocate(std::size_t size, std::size_t alignment) {
            for (;;) {
                if (!current_) { grow(size, alignment); }
                void *space = current_->storage + used_;
                auto available = current_->bytes - used_;
                if (std::align(alignment, size, space, available)) {
                    used_ = current_->bytes - available + size;
                    return space;
                }
                current_ = current_->next;
                used_ = sizeof(block);
            }
        }

        void rewind() noexcept {
            current_ = first_;
            used_ = sizeof(block);
        }

        void free_blocks() {
            while (first_) {
                auto *chunk = first_;
                first_ = chunk->next;
                resource_.deallocate(chunk->storage, chunk->bytes, alignof(block));
            }
            current_ = last_ = nullptr;
            used_ = sizeof(block);
            retained_ = 0;
        }

    public:
        explicit encoding_recorder(std::pmr::memory_resource &resource) noexcept : resource_(resource) {}
        encoding_recorder(const encoding_recorder &) = delete;
        encoding_recorder &operator=(const encoding_recorder &) = delete;
        ~encoding_recorder() { free_blocks(); }

        [[nodiscard]] std::size_t retained_bytes() const noexcept { return retained_; }

        void release() {
            if (active_) { throw std::logic_error("encoding recorder is in use"); }
            free_blocks();
        }
    };

    namespace detail {
        class encoding_cursor {
            const encoding_record_link *next_{};

            friend class encoding_session;
            explicit encoding_cursor(const encoding_record_link *next) noexcept : next_(next) {}

        public:
            encoding_cursor() = default;

            // The schema drives both traversals, so no runtime type tags are needed.
            template<typename T>
            const T &read() noexcept {
                const auto *record = static_cast<const encoding_record<T> *>(next_);
                next_ = record->next;
                return record->value;
            }
        };

        class encoding_session {
            encoding_recorder *owner_;
            encoding_record_link *first_{};
            encoding_record_link *last_{};

        public:
            explicit encoding_session(encoding_recorder &owner) : owner_(&owner) {
                if (owner.active_) { throw std::logic_error("encoding recorder is in use"); }
                owner.active_ = true;
            }

            encoding_session(encoding_session &&other) noexcept
                : owner_(std::exchange(other.owner_, nullptr)), first_(other.first_), last_(other.last_) {}

            ~encoding_session() {
                if (!owner_) { return; }
                while (last_) {
                    auto *record = last_;
                    last_ = record->previous;
                    record->destroy(record);
                }
                owner_->rewind();
                owner_->active_ = false;
            }

            template<typename T>
            T &append() {
                using record_type = encoding_record<T>;
                auto *record = ::new (owner_->allocate(sizeof(record_type), alignof(record_type))) record_type(last_);
                (last_ ? last_->next : first_) = record;
                last_ = record;
                return record->value;
            }

            [[nodiscard]] encoding_cursor cursor() const noexcept { return encoding_cursor(first_); }
        };
    }
}
