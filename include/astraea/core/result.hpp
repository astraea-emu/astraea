#pragma once

#include <cstddef>
#include <utility>
#include <variant>

namespace astraea::core {

template <typename T, typename E>
class [[nodiscard]] Result {
public:
    [[nodiscard]] static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    [[nodiscard]] static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T& value() & {
        return std::get<0>(storage_);
    }

    [[nodiscard]] const T& value() const& {
        return std::get<0>(storage_);
    }

    [[nodiscard]] T&& value() && {
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(storage_);
    }

    [[nodiscard]] const E& error() const& {
        return std::get<1>(storage_);
    }

    [[nodiscard]] T* operator->() {
        return &std::get<0>(storage_);
    }

    [[nodiscard]] const T* operator->() const {
        return &std::get<0>(storage_);
    }

    [[nodiscard]] T& operator*() & {
        return std::get<0>(storage_);
    }

    [[nodiscard]] const T& operator*() const& {
        return std::get<0>(storage_);
    }

private:
    template <std::size_t Index, typename U>
    Result(std::in_place_index_t<Index> index, U&& value)
        : storage_(index, std::forward<U>(value)) {}

    std::variant<T, E> storage_;
};

}  // namespace astraea::core
