/**
 * @file datastructures/vec.c
 * @brief Vector with allocator support.
 */

#include "../private_api.h"

void ecs_vec_init(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t elem_count)
{
    ecs_vec_init_w_dbg_info(allocator, v, size, elem_count, NULL);
}

void ecs_vec_init_w_dbg_info(
    struct ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t elem_count,
    const char *type_name)
{
    (void)type_name;
    ecs_assert(size != 0, ECS_INVALID_PARAMETER, NULL);
    v->array = NULL;
    v->count = 0;
    if (elem_count) {
        if (allocator) {
            v->array = flecs_alloc_w_dbg_info(
                allocator, size * elem_count, type_name);
        } else {
            v->array = ecs_os_malloc(size * elem_count);
        }
    }
    v->size = elem_count;
#ifdef FLECS_SANITIZE
    v->elem_size = size;
    v->type_name = type_name;
#endif
}

void ecs_vec_init_if(
    ecs_vec_t *vec,
    ecs_size_t size)
{
    ecs_san_assert(!vec->elem_size || vec->elem_size == size, ECS_INVALID_PARAMETER, NULL);
    (void)vec;
    (void)size;
#ifdef FLECS_SANITIZE
    if (!vec->elem_size) {
        ecs_assert(vec->count == 0, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(vec->size == 0, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(vec->array == NULL, ECS_INTERNAL_ERROR, NULL);
        vec->elem_size = size;
    }
#endif
}

void ecs_vec_fini(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size)
{
    if (v->array) {
        ecs_san_assert(!size || size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
        if (allocator) {
            flecs_free(allocator, size * v->size, v->array);
        } else {
            ecs_os_free(v->array);
        }
        v->array = NULL;
        v->count = 0;
        v->size = 0;
    }
}

ecs_vec_t* ecs_vec_reset(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size)
{
    if (!v->size) {
        ecs_vec_init(allocator, v, size, 0);
    } else {
        ecs_san_assert(size == v->elem_size, ECS_INTERNAL_ERROR, NULL);
        ecs_vec_clear(v);
    }
    return v;
}

void ecs_vec_clear(
    ecs_vec_t *vec)
{
    vec->count = 0;
}

ecs_vec_t ecs_vec_copy(
    ecs_allocator_t *allocator,
    const ecs_vec_t *v,
    ecs_size_t size)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    void *array;
    if (allocator) {
        array = flecs_dup(allocator, size * v->size, v->array);
    } else {
        array = ecs_os_memdup(v->array, size * v->size);
    }
    return (ecs_vec_t) {
        .count = v->count,
        .size = v->size,
        .array = array
#ifdef FLECS_SANITIZE
        , .elem_size = size
#endif
    };
}

ecs_vec_t ecs_vec_copy_shrink(
    ecs_allocator_t *allocator,
    const ecs_vec_t *v,
    ecs_size_t size)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    int32_t count = v->count;
    void *array = NULL;
    if (count) {
        if (allocator) {
            array = flecs_dup(allocator, size * count, v->array);
        } else {
            array = ecs_os_memdup(v->array, size * count);
        }
    }
    return (ecs_vec_t) {
        .count = count,
        .size = count,
        .array = array
#ifdef FLECS_SANITIZE
        , .elem_size = size
#endif
    };
}

void ecs_vec_reclaim(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    int32_t count = v->count;
    if (count < v->size) {
        if (count) {
            /* Don't use realloc as it can return the same size buffer when the
             * new size is smaller than the existing size, which defeats the
             * purpose of reclaim. */
            if (allocator) {
                void *new_array = flecs_alloc(allocator, count * size);
                ecs_os_memcpy(new_array, v->array, size * count);
                flecs_free(allocator, v->size * size, v->array);
                v->array = new_array;
            } else {
                void *new_array = ecs_os_malloc(size * count);
                ecs_os_memcpy(new_array, v->array, size * count);
                ecs_os_free(v->array);
                v->array = new_array;
            }
            v->size = count;
        } else {
            ecs_vec_fini(allocator, v, size);
        }
    }
}

void ecs_vec_set_size(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t elem_count)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    if (v->size != elem_count) {
        if (elem_count < v->count) {
            elem_count = v->count;
        }

        elem_count = flecs_next_pow_of_2(elem_count);
        if (elem_count < 2) {
            elem_count = 2;
        }
        if (elem_count != v->size) {
            if (allocator) {
#ifdef FLECS_SANITIZE
                v->array = flecs_realloc_w_dbg_info(
                    allocator, size * elem_count, size * v->size, v->array,
                    v->type_name);
#else
                v->array = flecs_realloc(
                    allocator, size * elem_count, size * v->size, v->array);
#endif
            } else {
                v->array = ecs_os_realloc(v->array, size * elem_count);
            }
            v->size = elem_count;
        }
    }
}

void ecs_vec_set_min_size(
    struct ecs_allocator_t *allocator,
    ecs_vec_t *vec,
    ecs_size_t size,
    int32_t elem_count)
{
    if (elem_count > vec->size) {
        ecs_vec_set_size(allocator, vec, size, elem_count);
    }
}

void ecs_vec_set_min_count(
    struct ecs_allocator_t *allocator,
    ecs_vec_t *vec,
    ecs_size_t size,
    int32_t elem_count)
{
    ecs_vec_set_min_size(allocator, vec, size, elem_count);
    if (vec->count < elem_count) {
        vec->count = elem_count;
    }
}

void ecs_vec_set_min_count_zeromem(
    struct ecs_allocator_t *allocator,
    ecs_vec_t *vec,
    ecs_size_t size,
    int32_t elem_count)
{
    int32_t count = vec->count;
    if (count < elem_count) {
        ecs_vec_set_min_count(allocator, vec, size, elem_count);
        ecs_os_memset(ECS_ELEM(vec->array, size, count), 0,
            size * (elem_count - count));
    }
}

void ecs_vec_set_count(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t elem_count)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    if (v->count != elem_count) {
        if (v->size < elem_count) {
            ecs_vec_set_size(allocator, v, size, elem_count);
        }

        v->count = elem_count;
    }
}

void* ecs_vec_grow(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t elem_count)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(elem_count >= 0, ECS_INTERNAL_ERROR, NULL);
    int32_t count = v->count;
    ecs_vec_set_count(allocator, v, size, count + elem_count);
    return ECS_ELEM(v->array, size, count);
}

void* ecs_vec_append(
    ecs_allocator_t *allocator,
    ecs_vec_t *v,
    ecs_size_t size)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    int32_t count = v->count;
    if (v->size == count) {
        ecs_vec_set_size(allocator, v, size, count + 1);
    }
    v->count = count + 1;
    return ECS_ELEM(v->array, size, count);
}

void ecs_vec_remove(
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t index)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(index < v->count, ECS_OUT_OF_RANGE, NULL);
    if (index == --v->count) {
        return;
    }

    ecs_os_memcpy(
        ECS_ELEM(v->array, size, index),
        ECS_ELEM(v->array, size, v->count),
        size);
}

void ecs_vec_remove_last(
    ecs_vec_t *v)
{
    v->count --;
}

void ecs_vec_remove_ordered(
    ecs_vec_t *v,
    ecs_size_t size,
    int32_t index)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(index < v->count, ECS_OUT_OF_RANGE, NULL);

    int32_t new_count = --v->count;
    if (index == new_count) {
        return;
    }

    ecs_os_memmove(
        ECS_ELEM(v->array, size, index),
        ECS_ELEM(v->array, size, index + 1),
        size * (new_count - index));
}

int32_t ecs_vec_count(
    const ecs_vec_t *v)
{
    return v->count;
}

int32_t ecs_vec_size(
    const ecs_vec_t *v)
{
    return v->size;
}

void* ecs_vec_get(
    const ecs_vec_t *v,
    ecs_size_t size,
    int32_t index)
{
    ecs_san_assert(size == v->elem_size, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(index >= 0, ECS_OUT_OF_RANGE, NULL);
    ecs_assert(index < v->count, ECS_OUT_OF_RANGE, NULL);
    return ECS_ELEM(v->array, size, index);
}

void* ecs_vec_last(
    const ecs_vec_t *v,
    ecs_size_t size)
{
    ecs_san_assert(!v->elem_size || size == v->elem_size,
        ECS_INVALID_PARAMETER, NULL);
    return ECS_ELEM(v->array, size, v->count - 1);
}

void* ecs_vec_first(
    const ecs_vec_t *v)
{
    return v->array;
}
