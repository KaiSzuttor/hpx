//  Copyright (c) 2006, Giovanni P. Deretta
//  Copyright (c) 2007-2013 Hartmut Kaiser
//
//  This code may be used under either of the following two licences:
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE. OF SUCH DAMAGE.
//
//  Or:
//
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/config.hpp>

#include <hpx/runtime/threads/coroutines/coroutine.hpp>
#include <hpx/runtime/threads/coroutines/detail/coroutine_impl.hpp>
#include <hpx/runtime/threads/coroutines/detail/coroutine_self.hpp>
#include <hpx/runtime/threads/thread_data_fwd.hpp>
#include <hpx/util/assert.hpp>

#include <cstddef>
#include <exception>
#include <utility>

namespace hpx { namespace threads { namespace coroutines { namespace detail
{
    ///////////////////////////////////////////////////////////////////////////
    namespace
    {
        struct reset_self_on_exit
        {
            reset_self_on_exit(coroutine_self* val,
                    coroutine_self* old_val = nullptr)
              : old_self(old_val)
            {
                coroutine_self::set_self(val);
            }

            ~reset_self_on_exit()
            {
                coroutine_self::set_self(old_self);
            }

            coroutine_self* old_self;
        };
    }

#if defined(HPX_DEBUG)
    coroutine_impl::~coroutine_impl()
    {
        HPX_ASSERT(!m_fun);   // functor should have been reset by now
    }
#endif

    void coroutine_impl::operator()() noexcept
    {
        typedef super_type::context_exit_status context_exit_status;
        context_exit_status status = super_type::ctx_exited_return;

        // yield value once the thread function has finished executing
        result_type result_last(
            thread_state_enum::terminated, invalid_thread_id);

        // loop as long this coroutine has been rebound
        do
        {
            std::exception_ptr tinfo;
            try
            {
                {
                    coroutine_self* old_self = coroutine_self::get_self();
                    coroutine_self self(this, old_self);
                    reset_self_on_exit on_exit(&self, old_self);

                    result_last = m_fun(*this->args());
                    HPX_ASSERT(result_last.first == thread_state_enum::terminated);
                }

                // return value to other side of the fence
                this->bind_result(result_last);
            }
            catch (...) {
                status = super_type::ctx_exited_abnormally;
                tinfo = std::current_exception();
            }

            this->reset();
            this->do_return(status, std::move(tinfo));
        } while (this->m_state == super_type::ctx_running);

        // should not get here, never
        HPX_ASSERT(this->m_state == super_type::ctx_running);
    }
}}}}
