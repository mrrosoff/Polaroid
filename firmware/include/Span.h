#pragma once

// The xtensa toolchain is GCC 8.4; libstdc++ only grew <span> in GCC 10. Every
// use in this firmware is a dynamic-extent view over bytes or chars, so this
// supplies that subset and nothing else. It lands in namespace std, and
// __has_include means it vanishes the moment the toolchain ships the real one —
// the native test envs already take that branch.

#if __has_include(<span>)
#include <span>
#else

#include <cstddef>
#include <type_traits>

namespace std {

template <typename T>
class span;

namespace polaroid_span_detail {
template <typename T>
struct is_span : false_type {};
template <typename T>
struct is_span<span<T>> : true_type {};
}  // namespace polaroid_span_detail

template <typename T>
class span {
  public:
    using element_type = T;
    using value_type = remove_cv_t<T>;
    using size_type = size_t;
    using pointer = T *;
    using reference = T &;
    using iterator = T *;

    constexpr span() noexcept : data_(nullptr), size_(0) {}
    constexpr span(pointer ptr, size_type count) noexcept : data_(ptr), size_(count) {}

    template <size_t N>
    constexpr span(element_type (&arr)[N]) noexcept : data_(arr), size_(N) {}

    // Any container exposing data()/size() whose pointer converts to ours. The
    // static_cast in the defaulted template argument is the constraint: it
    // fails substitution for a const container viewed as mutable.
    template <typename C,
              typename = enable_if_t<!polaroid_span_detail::is_span<decay_t<C>>::value>,
              typename = decltype(static_cast<pointer>(declval<C &>().data()))>
    constexpr span(C &&c) noexcept : data_(c.data()), size_(c.size()) {}

    // span<T> -> span<const T>. The array-pointer form is the qualification
    // conversion check; a plain is_convertible<U*, T*> would also admit
    // derived-to-base, which is not a valid span conversion.
    template <typename U, typename = enable_if_t<is_convertible<U (*)[], T (*)[]>::value>>
    constexpr span(const span<U> &other) noexcept : data_(other.data()), size_(other.size()) {}

    [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept {
        return size_ * sizeof(element_type);
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr reference operator[](size_type i) const { return data_[i]; }
    [[nodiscard]] constexpr reference front() const { return data_[0]; }
    [[nodiscard]] constexpr reference back() const { return data_[size_ - 1]; }

    [[nodiscard]] constexpr iterator begin() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator end() const noexcept { return data_ + size_; }

    [[nodiscard]] constexpr span<T> first(size_type n) const { return span<T>(data_, n); }
    [[nodiscard]] constexpr span<T> last(size_type n) const {
        return span<T>(data_ + (size_ - n), n);
    }
    [[nodiscard]] constexpr span<T> subspan(size_type offset, size_type n) const {
        return span<T>(data_ + offset, n);
    }

  private:
    pointer data_;
    size_type size_;
};

}  // namespace std

#endif
