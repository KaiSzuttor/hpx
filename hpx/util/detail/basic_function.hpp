//  Copyright (c) 2011 Thomas Heller
//  Copyright (c) 2013 Hartmut Kaiser
//  Copyright (c) 2014 Agustin Berge
//  Copyright (c) 2017 Google
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef HPX_UTIL_DETAIL_BASIC_FUNCTION_HPP
#define HPX_UTIL_DETAIL_BASIC_FUNCTION_HPP

#include <hpx/config.hpp>
#include <hpx/runtime/serialization/serialization_fwd.hpp>
#include <hpx/traits/get_function_address.hpp>
#include <hpx/traits/get_function_annotation.hpp>
#include <hpx/traits/is_callable.hpp>
#include <hpx/util/assert.hpp>
#include <hpx/util/detail/empty_function.hpp>
#include <hpx/util/detail/vtable/serializable_function_vtable.hpp>
#include <hpx/util/detail/vtable/serializable_vtable.hpp>
#include <hpx/util/detail/vtable/function_vtable.hpp>
#include <hpx/util/detail/vtable/unique_function_vtable.hpp>
#include <hpx/util/detail/vtable/vtable.hpp>

#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace hpx { namespace util { namespace detail
{
    static const std::size_t function_storage_size = 3 * sizeof(void*);

    ///////////////////////////////////////////////////////////////////////////
    template <typename Sig, bool Copyable>
    class function_base;

    template <typename F>
    HPX_CONSTEXPR bool is_empty_function(F* fp) noexcept
    {
        return fp == nullptr;
    }

    template <typename T, typename C>
    HPX_CONSTEXPR bool is_empty_function(T C::*mp) noexcept
    {
        return mp == nullptr;
    }

    template <typename Sig, bool Copyable>
    static bool is_empty_function_impl(
        function_base<Sig, Copyable> const* f) noexcept
    {
        return f->empty();
    }

    static HPX_CONSTEXPR bool is_empty_function_impl(...) noexcept
    {
        return false;
    }

    template <typename F>
    HPX_CONSTEXPR bool is_empty_function(F const& f) noexcept
    {
        return detail::is_empty_function_impl(&f);
    }

    ///////////////////////////////////////////////////////////////////////////
    template <bool Copyable, typename R, typename ...Ts>
    class function_base<R(Ts...), Copyable>
    {
        using vtable = typename std::conditional<
                Copyable,
                detail::function_vtable<R(Ts...)>,
                detail::unique_function_vtable<R(Ts...)>
            >::type;

    public:
        HPX_CONSTEXPR function_base() noexcept
          : vptr(detail::get_empty_function_vtable<vtable>())
          , object(nullptr)
          , storage_init()
        {}

        function_base(function_base const& other)
          : vptr(other.vptr)
          , object(other.object)
        {
            if (other.object != nullptr)
            {
                object = vptr->copy(
                    storage, detail::function_storage_size,
                    other.object, /*destroy*/false);
            }
        }

        function_base(function_base&& other) noexcept
          : vptr(other.vptr)
          , object(other.object)
        {
            if (object == &other.storage)
            {
                std::memcpy(storage, other.storage, function_storage_size);
                object = &storage;
            }
            other.vptr = detail::get_empty_function_vtable<vtable>();
            other.object = nullptr;
        }

        ~function_base()
        {
            destroy();
        }

        function_base& operator=(function_base const& other)
        {
            if (vptr == other.vptr)
            {
                if (this != &other && object)
                {
                    HPX_ASSERT(other.object != nullptr);
                    // reuse object storage
                    object = vptr->copy(
                        object, -1,
                        other.object, /*destroy*/true);
                }
            } else {
                destroy();
                vptr = other.vptr;
                if (other.object != nullptr)
                {
                    object = vptr->copy(
                        storage, detail::function_storage_size,
                        other.object, /*destroy*/false);
                } else {
                    object = nullptr;
                }
            }
            return *this;
        }

        function_base& operator=(function_base&& other) noexcept
        {
            if (this != &other)
            {
                swap(other);
                other.reset();
            }
            return *this;
        }

        void assign(std::nullptr_t) noexcept
        {
            reset();
        }

        template <typename F>
        void assign(F&& f)
        {
            using T = typename std::decay<F>::type;
            static_assert(!Copyable ||
                std::is_constructible<T, T const&>::value,
                "F shall be CopyConstructible");

            if (!detail::is_empty_function(f))
            {
                vtable const* f_vptr = get_vtable<T>();
                void* buffer = nullptr;
                if (vptr == f_vptr)
                {
                    HPX_ASSERT(object != nullptr);
                    // reuse object storage
                    buffer = object;
                    vtable::template get<T>(object).~T();
                } else {
                    destroy();
                    vptr = f_vptr;
                    buffer = vtable::template allocate<T>(
                        storage, function_storage_size);
                }
                object = ::new (buffer) T(std::forward<F>(f));
            } else {
                reset();
            }
        }

        void destroy() noexcept
        {
            if (object != nullptr)
            {
                vptr->deallocate(
                    object, function_storage_size,
                    /*destroy*/true);
            }
        }

        void reset() noexcept
        {
            destroy();
            vptr = detail::get_empty_function_vtable<vtable>();
            object = nullptr;
        }

        void swap(function_base& f) noexcept
        {
            std::swap(vptr, f.vptr);
            std::swap(object, f.object);
            std::swap(storage, f.storage);
            if (object == &f.storage)
                object = &storage;
            if (f.object == &storage)
                f.object = &f.storage;
        }

        bool empty() const noexcept
        {
            return object == nullptr;
        }

        explicit operator bool() const noexcept
        {
            return !empty();
        }

        template <typename T>
        T* target() noexcept
        {
            using target_type = typename std::remove_cv<T>::type;

            static_assert(
                traits::is_invocable_r<R, target_type&, Ts...>::value
              , "T shall be Callable with the function signature");

            vtable const* f_vptr = get_vtable<target_type>();
            if (vptr != f_vptr || empty())
                return nullptr;

            return &vtable::template get<target_type>(object);
        }

        template <typename T>
        T const* target() const noexcept
        {
            using target_type = typename std::remove_cv<T>::type;

            static_assert(
                traits::is_invocable_r<R, target_type&, Ts...>::value
              , "T shall be Callable with the function signature");

            vtable const* f_vptr = get_vtable<target_type>();
            if (vptr != f_vptr || empty())
                return nullptr;

            return &vtable::template get<target_type>(object);
        }

        HPX_FORCEINLINE R operator()(Ts... vs) const
        {
            return vptr->invoke(object, std::forward<Ts>(vs)...);
        }

        std::size_t get_function_address() const
        {
#if defined(HPX_HAVE_THREAD_DESCRIPTION)
            return vptr->get_function_address(object);
#else
            return 0;
#endif
        }

        char const* get_function_annotation() const
        {
#if defined(HPX_HAVE_THREAD_DESCRIPTION)
            return vptr->get_function_annotation(object);
#else
            return nullptr;
#endif
        }

        util::itt::string_handle get_function_annotation_itt() const
        {
#if HPX_HAVE_ITTNOTIFY != 0 && !defined(HPX_HAVE_APEX)
            return vptr->get_function_annotation_itt(object);
#else
            static util::itt::string_handle sh;
            return sh;
#endif
        }

    private:
        template <typename T>
        static vtable const* get_vtable() noexcept
        {
            return detail::get_vtable<vtable, T>();
        }

    protected:
        vtable const *vptr;
        void* object;
        union {
            char storage_init;
            mutable unsigned char storage[function_storage_size];
        };
    };

    ///////////////////////////////////////////////////////////////////////////
    template <typename Sig, bool Copyable, bool Serializable>
    class basic_function;

    template <bool Copyable, typename R, typename ...Ts>
    class basic_function<R(Ts...), Copyable, true>
      : public function_base<R(Ts...), Copyable>
    {
        using vtable = typename std::conditional<
                Copyable,
                detail::function_vtable<R(Ts...)>,
                detail::unique_function_vtable<R(Ts...)>
            >::type;
        using serializable_vtable = serializable_function_vtable<vtable>;
        using base_type = function_base<R(Ts...), Copyable>;

    public:
        HPX_CONSTEXPR basic_function() noexcept
          : base_type()
          , serializable_vptr(nullptr)
        {}

        template <typename F>
        void assign(F&& f)
        {
            using target_type = typename std::decay<F>::type;

            base_type::assign(std::forward<F>(f));
            if (!base_type::empty())
            {
                serializable_vptr = get_serializable_vtable<target_type>();
            }
        }

        void swap(basic_function& f) noexcept
        {
            base_type::swap(f);
            std::swap(serializable_vptr, f.serializable_vptr);
        }

    private:
        friend class hpx::serialization::access;

        void save(serialization::output_archive& ar, unsigned const version) const
        {
            bool const is_empty = base_type::empty();
            ar << is_empty;
            if (!is_empty)
            {
                std::string const name = serializable_vptr->name;
                ar << name;

                serializable_vptr->save_object(object, ar, version);
            }
        }

        void load(serialization::input_archive& ar, unsigned const version)
        {
            base_type::reset();

            bool is_empty = false;
            ar >> is_empty;
            if (!is_empty)
            {
                std::string name;
                ar >> name;
                serializable_vptr = detail::get_serializable_vtable<vtable>(name);

                vptr = serializable_vptr->vptr;
                object = serializable_vptr->load_object(
                    storage, function_storage_size, ar, version);
            }
        }

        HPX_SERIALIZATION_SPLIT_MEMBER()

        template <typename T>
        static serializable_vtable const* get_serializable_vtable() noexcept
        {
            return detail::get_serializable_vtable<vtable, T>();
        }

    protected:
        using base_type::vptr;
        using base_type::object;
        using base_type::storage;
        serializable_vtable const* serializable_vptr;
    };

    template <bool Copyable, typename R, typename ...Ts>
    class basic_function<R(Ts...), Copyable, false>
      : public function_base<R(Ts...), Copyable>
    {};
}}}

#endif
