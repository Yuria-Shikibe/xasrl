#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <meta>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "types.hpp"
#include "conversion.hpp"
#include "limits.hpp"

namespace srl {
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
            return memory_type_ == handler_key<MemTy>() && storage_type_ == handler_key<SrlTy>();
        }

        virtual error mem2srl(const void *mem, void *srl) = 0;
        virtual error srl2mem(void *mem, const void *srl) = 0;
    };

    template<typename MemTy, typename SrlTy>
    class typed_reference_handler : public context_reference_handler {
        static_assert(std::meta::is_object_type(^^MemTy) && std::meta::is_object_type(^^SrlTy));
        static_assert(std::meta::is_same_type(^^MemTy, std::meta::remove_cv(^^MemTy))
                      && std::meta::is_same_type(^^SrlTy, std::meta::remove_cv(^^SrlTy)));

        error mem2srl(const void *mem, void *srl) final {
            return this->to_storage(*static_cast<const MemTy *>(mem), *static_cast<SrlTy *>(srl));
        }

        error srl2mem(void *mem, const void *srl) final {
            return this->from_storage(*static_cast<MemTy *>(mem), *static_cast<const SrlTy *>(srl));
        }

    protected:
        typed_reference_handler() noexcept
            : context_reference_handler(std::in_place_type<MemTy>, std::in_place_type<SrlTy>) {}

        virtual error to_storage(const MemTy &mem, SrlTy &srl) = 0;
        virtual error from_storage(MemTy &mem, const SrlTy &srl) = 0;
    };

    struct serialization_config {
        duplicate_policy duplicates{duplicate_policy::deduplicate};
        decode_limits limits{};
    };

    class serialization_context {
    public:
        serialization_config config;

    private:
        std::unordered_map<const void *, context_reference_handler*> handlers_;

    public:
        serialization_context() = default;

        explicit serialization_context(const serialization_config& config) noexcept
            : config(config) {}

        // Handlers and their services must outlive all operations using this context.
        void register_handler(const void *id, context_reference_handler &handler) {
            if (id == nullptr) {
                throw std::invalid_argument("srl handler key must not be null");
            }
            if (!handlers_.emplace(id, &handler).second) {
                throw std::logic_error("srl handler key is already registered");
            }
        }

        template<typename MemTy, typename SrlTy>
        [[nodiscard]] context_reference_handler &get_handler(const void *id) const {
            const auto found = handlers_.find(id);
            if (found == handlers_.end()) {
                throw std::logic_error("srl context is missing a required handler");
            }
            auto &handler = *(found->second);
            if (!handler.matches<MemTy, SrlTy>()) {
                throw std::logic_error("srl handler memory/storage type mismatch");
            }
            return handler;
        }
    };
}
