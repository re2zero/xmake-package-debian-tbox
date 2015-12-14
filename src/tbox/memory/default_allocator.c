/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2015, ruki All rights reserved.
 *
 * @author      ruki
 * @file        default_allocator.c
 * @ingroup     memory
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "allocator"
#define TB_TRACE_MODULE_DEBUG           (0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "allocator.h"
#include "small_allocator.h"
#include "large_allocator.h"
#include "default_allocator.h"
#include "impl/prefix.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the default allocator type
typedef struct __tb_default_allocator_t
{
    // the base
    tb_allocator_t          base;

    // the large allocator
    tb_allocator_ref_t      large_allocator;

    // the small allocator
    tb_allocator_ref_t      small_allocator;

}tb_default_allocator_t, *tb_default_allocator_ref_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
static tb_void_t tb_default_allocator_exit(tb_allocator_ref_t self)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return(allocator);

    // enter
    tb_spinlock_enter(&allocator->base.lock);

    // exit small allocator
    if (allocator->small_allocator) tb_allocator_exit(allocator->small_allocator);
    allocator->small_allocator = tb_null;

    // leave
    tb_spinlock_leave(&allocator->base.lock);

    // exit lock
    tb_spinlock_exit(&allocator->base.lock);

    // exit allocator
    if (allocator->large_allocator) tb_allocator_large_free(allocator->large_allocator, allocator);
}
static tb_pointer_t tb_default_allocator_malloc(tb_allocator_ref_t self, tb_size_t size __tb_debug_decl__)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return_val(allocator, tb_null);

    // check
    tb_assert_and_check_return_val(allocator->large_allocator && allocator->small_allocator && size, tb_null);

    // done
    return size <= TB_SMALL_ALLOCATOR_DATA_MAXN? tb_allocator_malloc_(allocator->small_allocator, size __tb_debug_args__) : tb_allocator_large_malloc_(allocator->large_allocator, size, tb_null __tb_debug_args__);
}
static tb_pointer_t tb_default_allocator_ralloc(tb_allocator_ref_t self, tb_pointer_t data, tb_size_t size __tb_debug_decl__)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return_val(allocator, tb_null);

    // check
    tb_assert_and_check_return_val(allocator && allocator->large_allocator && allocator->small_allocator && size, tb_null);

    // done
    tb_pointer_t data_new = tb_null;
    do
    {
        // no data?
        if (!data)
        {
            // malloc it directly
            data_new = size <= TB_SMALL_ALLOCATOR_DATA_MAXN? tb_allocator_malloc_(allocator->small_allocator, size __tb_debug_args__) : tb_allocator_large_malloc_(allocator->large_allocator, size, tb_null __tb_debug_args__);
            break;
        }

        // the data head
        tb_pool_data_head_t* data_head = &(((tb_pool_data_head_t*)data)[-1]);
        tb_assertf_break(data_head->debug.magic == TB_POOL_DATA_MAGIC, "ralloc invalid data: %p", data);
        tb_assert_and_check_break(data_head->size);

        // small => small
        if (data_head->size <= TB_SMALL_ALLOCATOR_DATA_MAXN && size <= TB_SMALL_ALLOCATOR_DATA_MAXN)
            data_new = tb_allocator_ralloc_(allocator->small_allocator, data, size __tb_debug_args__);
        // small => large
        else if (data_head->size <= TB_SMALL_ALLOCATOR_DATA_MAXN)
        {
            // make the new data
            data_new = tb_allocator_large_malloc_(allocator->large_allocator, size, tb_null __tb_debug_args__);
            tb_assert_and_check_break(data_new);

            // copy the old data
            tb_memcpy_(data_new, data, tb_min(data_head->size, size));

            // free the old data
            tb_allocator_free_(allocator->small_allocator, data __tb_debug_args__);
        }
        // large => small
        else if (size <= TB_SMALL_ALLOCATOR_DATA_MAXN)
        {
            // make the new data
            data_new = tb_allocator_malloc_(allocator->small_allocator, size __tb_debug_args__);
            tb_assert_and_check_break(data_new);

            // copy the old data
            tb_memcpy_(data_new, data, tb_min(data_head->size, size));

            // free the old data
            tb_allocator_large_free_(allocator->large_allocator, data __tb_debug_args__);
        }
        // large => large
        else data_new = tb_allocator_large_ralloc_(allocator->large_allocator, data, size, tb_null __tb_debug_args__);

    } while (0);

    // ok?
    return data_new;
}
static tb_bool_t tb_default_allocator_free(tb_allocator_ref_t self, tb_pointer_t data __tb_debug_decl__)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return_val(allocator, tb_false);

    // check
    tb_assert_and_check_return_val(allocator->large_allocator && allocator->small_allocator && data, tb_false);

    // done
    tb_bool_t ok = tb_false;
    do
    {
        // the data head
        tb_pool_data_head_t* data_head = &(((tb_pool_data_head_t*)data)[-1]);
        tb_assertf_break(data_head->debug.magic == TB_POOL_DATA_MAGIC, "free invalid data: %p", data);

        // free it
        ok = (data_head->size <= TB_SMALL_ALLOCATOR_DATA_MAXN)? tb_allocator_free_(allocator->small_allocator, data __tb_debug_args__) : tb_allocator_large_free_(allocator->large_allocator, data __tb_debug_args__);

    } while (0);

    // ok?
    return ok;
}
static tb_void_t tb_default_allocator_clear(tb_allocator_ref_t self)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return(allocator);

    // check
    tb_assert_and_check_return(allocator->large_allocator && allocator->small_allocator);

    // clear allocator
    tb_allocator_clear(allocator->small_allocator);
    tb_allocator_clear(allocator->large_allocator);
}
#ifdef __tb_debug__
static tb_void_t tb_default_allocator_dump(tb_allocator_ref_t self)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return(allocator);

    // check
    tb_assert_and_check_return(allocator->large_allocator && allocator->small_allocator);

    // dump allocator
    tb_allocator_dump(allocator->small_allocator);
}
#endif
static tb_handle_t tb_default_allocator_instance_init(tb_cpointer_t* ppriv)
{
    // check
    tb_check_return_val(ppriv, tb_null);

    // the data and size
    tb_value_ref_t  tuple = (tb_value_ref_t)*ppriv;
    tb_byte_t*      data = (tb_byte_t*)tuple[0].ptr;
    tb_size_t       size = tuple[1].ul;

    // clear the private data first
    *ppriv = tb_null;

    // done
    tb_bool_t               ok = tb_false;
    tb_allocator_ref_t      allocator = tb_null;
    tb_allocator_ref_t      large_allocator = tb_null;
    do
    {
        /* init the page first
         *
         * because this allocator may be called before tb_init()
         */
        if (!tb_page_init()) break ;

        /* init the native memory first
         *
         * because this allocator may be called before tb_init()
         */
        if (!tb_native_memory_init()) break ;

        // init large allocator
        large_allocator = tb_large_allocator_init(data, size);
        tb_assert_and_check_break(large_allocator);

        // init allocator
        allocator = tb_default_allocator_init(large_allocator);
        tb_assert_and_check_break(allocator);

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // exit large allocator
        if (large_allocator) tb_allocator_exit(large_allocator);
        large_allocator = tb_null;
    }

    // ok?
    return (tb_handle_t)allocator;
}
static tb_void_t tb_default_allocator_instance_exit(tb_handle_t self, tb_cpointer_t priv)
{
    // check
    tb_default_allocator_ref_t allocator = (tb_default_allocator_ref_t)self;
    tb_assert_and_check_return(allocator);

    // the large allocator
    tb_allocator_ref_t large_allocator = allocator->large_allocator;
    tb_assert_and_check_return(large_allocator);

#ifdef __tb_debug__
    // dump allocator
    if (allocator) tb_allocator_dump((tb_allocator_ref_t)allocator);
#endif
 
    // exit allocator
    if (allocator) tb_allocator_exit((tb_allocator_ref_t)allocator);
    allocator = tb_null;

#ifdef __tb_debug__
    // dump large allocator
    tb_allocator_dump(large_allocator);
#endif

    // exit large allocator
    tb_allocator_exit(large_allocator);
    large_allocator = tb_null;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_allocator_ref_t tb_default_allocator(tb_byte_t* data, tb_size_t size)
{
    /* init singleton first
     *
     * because this allocator may be called before tb_init()
     */
    if (!tb_singleton_init()) return tb_null;

    // init tuple
    tb_value_t tuple[2];
    tuple[0].ptr    = (tb_pointer_t)data;
    tuple[1].ul     = size;

    // get it
    return (tb_allocator_ref_t)tb_singleton_instance(TB_SINGLETON_TYPE_DEFAULT_ALLOCATOR, tb_default_allocator_instance_init, tb_default_allocator_instance_exit, tb_null, tuple);
}
tb_allocator_ref_t tb_default_allocator_init(tb_allocator_ref_t large_allocator)
{
    // check
    tb_assert_and_check_return_val(large_allocator, tb_null);

    // done
    tb_bool_t                   ok = tb_false;
    tb_default_allocator_ref_t  allocator = tb_null;
    do
    {
        // make allocator
        allocator = (tb_default_allocator_ref_t)tb_allocator_large_malloc0(large_allocator, sizeof(tb_default_allocator_t), tb_null);
        tb_assert_and_check_break(allocator);

        // init base
        allocator->base.type            = TB_ALLOCATOR_DEFAULT;
        allocator->base.malloc          = tb_default_allocator_malloc;
        allocator->base.ralloc          = tb_default_allocator_ralloc;
        allocator->base.free            = tb_default_allocator_free;
        allocator->base.exit            = tb_default_allocator_exit;
        allocator->base.clear           = tb_default_allocator_clear;
#ifdef __tb_debug__
        allocator->base.dump            = tb_default_allocator_dump;
#endif

        // init lock
        if (!tb_spinlock_init(&allocator->base.lock)) break;

        // init allocator
        allocator->large_allocator = large_allocator;
        allocator->small_allocator = tb_small_allocator_init(large_allocator);
        tb_assert_and_check_break(allocator->small_allocator);

        // register lock profiler
#ifdef TB_LOCK_PROFILER_ENABLE
        tb_lock_profiler_register(tb_lock_profiler(), (tb_pointer_t)&allocator->base.lock, TB_TRACE_MODULE_NAME);
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        if (allocator) tb_default_allocator_exit((tb_allocator_ref_t)allocator);
        allocator = tb_null;
    }

    // ok?
    return (tb_allocator_ref_t)allocator;
}

