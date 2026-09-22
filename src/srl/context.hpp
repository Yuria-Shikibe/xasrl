#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <memory>
#include <meta>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "wire.hpp"

namespace srl {
    namespace detail {
        template<typename T>
        inline unsigned char identity_token{};
    }

    template<typename Tag>
    [[nodiscard]] constexpr const void *handler_key() noexcept {
        return &detail::identity_token<std::remove_cvref_t<Tag>>;
    }

    struct conversion_tag {
        std::meta::info target_type;
        const void *handler_id = nullptr;
    };

    class context_reference_handler {
        const void *const memory_type_;
        const void *const storage_type_;

    protected:
        template<typename MemTy, typename SrlTy>
        explicit context_reference_handler(std::in_place_type_t<MemTy>,
                                           std::in_place_type_t<SrlTy>) noexcept
            : memory_type_(handler_key<MemTy>()), storage_type_(handler_key<SrlTy>()) {}

    public:
        virtual ~context_reference_handler() = default;

        template<typename MemTy, typename SrlTy>
        [[nodiscard]] bool matches() const noexcept {
            return memory_type_ == handler_key<MemTy>()
                   && storage_type_ == handler_key<SrlTy>();
        }

        virtual std::expected<void, error> mem2srl(const void *mem, void *srl) = 0;
        virtual std::expected<void, error> srl2mem(void *mem, const void *srl) = 0;
    };

    template<typename MemTy, typename SrlTy>
    class typed_reference_handler : public context_reference_handler {
        static_assert(std::is_object_v<MemTy> && std::is_object_v<SrlTy>);
        static_assert(std::same_as<MemTy, std::remove_cv_t<MemTy>>
                      && std::same_as<SrlTy, std::remove_cv_t<SrlTy>>);

        std::expected<void, error> mem2srl(const void *mem, void *srl) final {
            return to_storage(*static_cast<const MemTy *>(mem), *static_cast<SrlTy *>(srl));
        }

        std::expected<void, error> srl2mem(void *mem, const void *srl) final {
            return from_storage(*static_cast<MemTy *>(mem), *static_cast<const SrlTy *>(srl));
        }

    protected:
        typed_reference_handler() noexcept
            : context_reference_handler(std::in_place_type<MemTy>, std::in_place_type<SrlTy>) {}

        virtual std::expected<void, error> to_storage(const MemTy &mem, SrlTy &srl) = 0;
        virtual std::expected<void, error> from_storage(MemTy &mem, const SrlTy &srl) = 0;
    };

    class serialization_context {
        std::unordered_map<const void *, std::reference_wrapper<context_reference_handler>> handlers_;

    public:
        // Handlers and their services must outlive all operations using this context.
        void register_handler(const void *id, context_reference_handler &handler) {
            if (id == nullptr) {
                throw std::invalid_argument("srl handler key must not be null");
            }
            if (!handlers_.emplace(id, std::ref(handler)).second) {
                throw std::logic_error("srl handler key is already registered");
            }
        }

        template<typename MemTy, typename SrlTy>
        [[nodiscard]] context_reference_handler &get_handler(const void *id) const {
            const auto found = handlers_.find(id);
            if (found == handlers_.end()) {
                throw std::logic_error("srl context is missing a required handler");
            }
            auto &handler = found->second.get();
            if (!handler.template matches<MemTy, SrlTy>()) {
                throw std::logic_error("srl handler memory/storage type mismatch");
            }
            return handler;
        }
    };
}
