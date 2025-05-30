/**
 * @file addons/stats.c
 * @brief Stats addon.
 */

#include "stats.h"

#include "../system/system.h"
#include "../pipeline/pipeline.h"

#ifdef FLECS_STATS

#define ECS_GAUGE_RECORD(m, t, value)\
    flecs_gauge_record(m, t, (ecs_float_t)(value))

#define ECS_COUNTER_RECORD(m, t, value)\
    flecs_counter_record(m, t, (double)(value))

#define ECS_METRIC_FIRST(stats)\
    ECS_CAST(ecs_metric_t*, ECS_OFFSET(&stats->first_, ECS_SIZEOF(int64_t)))

#define ECS_METRIC_LAST(stats)\
    ECS_CAST(ecs_metric_t*, ECS_OFFSET(&stats->last_, -ECS_SIZEOF(ecs_metric_t)))

static
int32_t t_next(
    int32_t t)
{
    return (t + 1) % ECS_STAT_WINDOW;
}

static
int32_t t_prev(
    int32_t t)
{
    return (t - 1 + ECS_STAT_WINDOW) % ECS_STAT_WINDOW;
}

static
void flecs_gauge_record(
    ecs_metric_t *m,
    int32_t t,
    ecs_float_t value)
{
    m->gauge.avg[t] = value;
    m->gauge.min[t] = value;
    m->gauge.max[t] = value;
}

static
double flecs_counter_record(
    ecs_metric_t *m,
    int32_t t,
    double value)
{
    int32_t tp = t_prev(t);
    double prev = m->counter.value[tp];
    m->counter.value[t] = value;
    double gauge_value = value - prev;
    if (gauge_value < 0) {
        gauge_value = 0; /* Counters are monotonically increasing */
    }
    flecs_gauge_record(m, t, (ecs_float_t)gauge_value);
    return gauge_value;
}

static
void flecs_metric_print(
    const char *name,
    ecs_float_t value)
{
    ecs_size_t len = ecs_os_strlen(name);
    ecs_trace("%s: %*s %.2f", name, 32 - len, "", (double)value);
}

static
void flecs_gauge_print(
    const char *name,
    int32_t t,
    const ecs_metric_t *m)
{
    flecs_metric_print(name, m->gauge.avg[t]);
}

static
void flecs_counter_print(
    const char *name,
    int32_t t,
    const ecs_metric_t *m)
{
    flecs_metric_print(name, m->counter.rate.avg[t]);
}

void ecs_metric_reduce(
    ecs_metric_t *dst,
    const ecs_metric_t *src,
    int32_t t_dst,
    int32_t t_src)
{
    ecs_check(dst != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(src != NULL, ECS_INVALID_PARAMETER, NULL);

    bool min_set = false;
    dst->gauge.avg[t_dst] = 0;
    dst->gauge.min[t_dst] = 0;
    dst->gauge.max[t_dst] = 0;

    ecs_float_t fwindow = (ecs_float_t)ECS_STAT_WINDOW;

    int32_t i;
    for (i = 0; i < ECS_STAT_WINDOW; i ++) {
        int32_t t = (t_src + i) % ECS_STAT_WINDOW;
        dst->gauge.avg[t_dst] += src->gauge.avg[t] / fwindow;

        if (!min_set || (src->gauge.min[t] < dst->gauge.min[t_dst])) {
            dst->gauge.min[t_dst] = src->gauge.min[t];
            min_set = true;
        }
        if ((src->gauge.max[t] > dst->gauge.max[t_dst])) {
            dst->gauge.max[t_dst] = src->gauge.max[t];
        }
    }

    dst->counter.value[t_dst] = src->counter.value[t_src];

error:
    return;
}

void ecs_metric_reduce_last(
    ecs_metric_t *m,
    int32_t prev,
    int32_t count)
{
    ecs_check(m != NULL, ECS_INVALID_PARAMETER, NULL);
    int32_t t = t_next(prev);

    if (m->gauge.min[t] < m->gauge.min[prev]) {
        m->gauge.min[prev] = m->gauge.min[t];
    }

    if (m->gauge.max[t] > m->gauge.max[prev]) {
        m->gauge.max[prev] = m->gauge.max[t];
    }

    ecs_float_t fcount = (ecs_float_t)(count + 1);
    ecs_float_t cur = m->gauge.avg[prev];
    ecs_float_t next = m->gauge.avg[t];

    cur *= ((fcount - 1) / fcount);
    next *= 1 / fcount;

    m->gauge.avg[prev] = cur + next;
    m->counter.value[prev] = m->counter.value[t];

error:
    return;
}

void ecs_metric_copy(
    ecs_metric_t *m,
    int32_t dst,
    int32_t src)
{
    ecs_check(m != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(dst != src, ECS_INVALID_PARAMETER, NULL);

    m->gauge.avg[dst] = m->gauge.avg[src];
    m->gauge.min[dst] = m->gauge.min[src];
    m->gauge.max[dst] = m->gauge.max[src];
    m->counter.value[dst] = m->counter.value[src];

error:
    return;
}

static
void flecs_stats_reduce(
    ecs_metric_t *dst_cur,
    ecs_metric_t *dst_last,
    ecs_metric_t *src_cur,
    int32_t t_dst,
    int32_t t_src)
{
    for (; dst_cur <= dst_last; dst_cur ++, src_cur ++) {
        ecs_metric_reduce(dst_cur, src_cur, t_dst, t_src);
    }
}

static
void flecs_stats_reduce_last(
    ecs_metric_t *dst_cur,
    ecs_metric_t *dst_last,
    ecs_metric_t *src_cur,
    int32_t t_dst,
    int32_t t_src,
    int32_t count)
{
    int32_t t_dst_next = t_next(t_dst);
    for (; dst_cur <= dst_last; dst_cur ++, src_cur ++) {
        /* Reduce into previous value */
        ecs_metric_reduce_last(dst_cur, t_dst, count);

        /* Restore old value */
        dst_cur->gauge.avg[t_dst_next] = src_cur->gauge.avg[t_src];
        dst_cur->gauge.min[t_dst_next] = src_cur->gauge.min[t_src];
        dst_cur->gauge.max[t_dst_next] = src_cur->gauge.max[t_src];
        dst_cur->counter.value[t_dst_next] = src_cur->counter.value[t_src];
    }
}

static
void flecs_stats_repeat_last(
    ecs_metric_t *cur,
    ecs_metric_t *last,
    int32_t t)
{
    int32_t prev = t_prev(t);
    for (; cur <= last; cur ++) {
        ecs_metric_copy(cur, t, prev);
    }
}

static
void flecs_stats_copy_last(
    ecs_metric_t *dst_cur,
    ecs_metric_t *dst_last,
    ecs_metric_t *src_cur,
    int32_t t_dst,
    int32_t t_src)
{
    for (; dst_cur <= dst_last; dst_cur ++, src_cur ++) {
        dst_cur->gauge.avg[t_dst] = src_cur->gauge.avg[t_src];
        dst_cur->gauge.min[t_dst] = src_cur->gauge.min[t_src];
        dst_cur->gauge.max[t_dst] = src_cur->gauge.max[t_src];
        dst_cur->counter.value[t_dst] = src_cur->counter.value[t_src];
    }
}

void ecs_world_stats_get(
    const ecs_world_t *world,
    ecs_world_stats_t *s)
{
    ecs_check(world != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(s != NULL, ECS_INVALID_PARAMETER, NULL);

    world = ecs_get_world(world);

    int32_t t = s->t = t_next(s->t);

    double delta_frame_count = 
    ECS_COUNTER_RECORD(&s->frame.frame_count, t, world->info.frame_count_total);
    ECS_COUNTER_RECORD(&s->frame.merge_count, t, world->info.merge_count_total);
    ECS_COUNTER_RECORD(&s->frame.rematch_count, t, world->info.rematch_count_total);
    ECS_COUNTER_RECORD(&s->frame.pipeline_build_count, t, world->info.pipeline_build_count_total);
    ECS_COUNTER_RECORD(&s->frame.systems_ran, t, world->info.systems_ran_frame);
    ECS_COUNTER_RECORD(&s->frame.observers_ran, t, world->info.observers_ran_frame);
    ECS_COUNTER_RECORD(&s->frame.event_emit_count, t, world->event_id);

    double delta_world_time = 
    ECS_COUNTER_RECORD(&s->performance.world_time_raw, t, world->info.world_time_total_raw);
    ECS_COUNTER_RECORD(&s->performance.world_time, t, world->info.world_time_total);
    ECS_COUNTER_RECORD(&s->performance.frame_time, t, world->info.frame_time_total);
    ECS_COUNTER_RECORD(&s->performance.system_time, t, world->info.system_time_total);
    ECS_COUNTER_RECORD(&s->performance.emit_time, t, world->info.emit_time_total);
    ECS_COUNTER_RECORD(&s->performance.merge_time, t, world->info.merge_time_total);
    ECS_COUNTER_RECORD(&s->performance.rematch_time, t, world->info.rematch_time_total);
    ECS_GAUGE_RECORD(&s->performance.delta_time, t, delta_world_time);
    if (ECS_NEQZERO(delta_world_time) && ECS_NEQZERO(delta_frame_count)) {
        ECS_GAUGE_RECORD(&s->performance.fps, t, (double)1 / (delta_world_time / (double)delta_frame_count));
    } else {
        ECS_GAUGE_RECORD(&s->performance.fps, t, 0);
    }

    ECS_GAUGE_RECORD(&s->entities.count, t, flecs_entities_count(world));
    ECS_GAUGE_RECORD(&s->entities.not_alive_count, t, flecs_entities_not_alive_count(world));

    ECS_GAUGE_RECORD(&s->components.tag_count, t, world->info.tag_id_count);
    ECS_GAUGE_RECORD(&s->components.component_count, t, world->info.component_id_count);
    ECS_GAUGE_RECORD(&s->components.pair_count, t, world->info.pair_id_count);
    ECS_GAUGE_RECORD(&s->components.type_count, t, ecs_map_count(&world->type_info));
    ECS_COUNTER_RECORD(&s->components.create_count, t, world->info.id_create_total);
    ECS_COUNTER_RECORD(&s->components.delete_count, t, world->info.id_delete_total);

    ECS_GAUGE_RECORD(&s->queries.query_count, t, ecs_count_id(world, EcsQuery));
    ECS_GAUGE_RECORD(&s->queries.observer_count, t, ecs_count_id(world, EcsObserver));
    if (ecs_is_alive(world, EcsSystem)) {
        ECS_GAUGE_RECORD(&s->queries.system_count, t, ecs_count_id(world, EcsSystem));
    }
    ECS_COUNTER_RECORD(&s->tables.create_count, t, world->info.table_create_total);
    ECS_COUNTER_RECORD(&s->tables.delete_count, t, world->info.table_delete_total);
    ECS_GAUGE_RECORD(&s->tables.count, t, world->info.table_count);

    ECS_COUNTER_RECORD(&s->commands.add_count, t, world->info.cmd.add_count);
    ECS_COUNTER_RECORD(&s->commands.remove_count, t, world->info.cmd.remove_count);
    ECS_COUNTER_RECORD(&s->commands.delete_count, t, world->info.cmd.delete_count);
    ECS_COUNTER_RECORD(&s->commands.clear_count, t, world->info.cmd.clear_count);
    ECS_COUNTER_RECORD(&s->commands.set_count, t, world->info.cmd.set_count);
    ECS_COUNTER_RECORD(&s->commands.ensure_count, t, world->info.cmd.ensure_count);
    ECS_COUNTER_RECORD(&s->commands.modified_count, t, world->info.cmd.modified_count);
    ECS_COUNTER_RECORD(&s->commands.other_count, t, world->info.cmd.other_count);
    ECS_COUNTER_RECORD(&s->commands.discard_count, t, world->info.cmd.discard_count);
    ECS_COUNTER_RECORD(&s->commands.batched_entity_count, t, world->info.cmd.batched_entity_count);
    ECS_COUNTER_RECORD(&s->commands.batched_count, t, world->info.cmd.batched_command_count);

    int64_t outstanding_allocs = ecs_os_api_malloc_count + 
        ecs_os_api_calloc_count - ecs_os_api_free_count;
    ECS_COUNTER_RECORD(&s->memory.alloc_count, t, ecs_os_api_malloc_count + ecs_os_api_calloc_count);
    ECS_COUNTER_RECORD(&s->memory.realloc_count, t, ecs_os_api_realloc_count);
    ECS_COUNTER_RECORD(&s->memory.free_count, t, ecs_os_api_free_count);
    ECS_GAUGE_RECORD(&s->memory.outstanding_alloc_count, t, outstanding_allocs);

    outstanding_allocs = ecs_block_allocator_alloc_count - ecs_block_allocator_free_count;
    ECS_COUNTER_RECORD(&s->memory.block_alloc_count, t, ecs_block_allocator_alloc_count);
    ECS_COUNTER_RECORD(&s->memory.block_free_count, t, ecs_block_allocator_free_count);
    ECS_GAUGE_RECORD(&s->memory.block_outstanding_alloc_count, t, outstanding_allocs);

    outstanding_allocs = ecs_stack_allocator_alloc_count - ecs_stack_allocator_free_count;
    ECS_COUNTER_RECORD(&s->memory.stack_alloc_count, t, ecs_stack_allocator_alloc_count);
    ECS_COUNTER_RECORD(&s->memory.stack_free_count, t, ecs_stack_allocator_free_count);
    ECS_GAUGE_RECORD(&s->memory.stack_outstanding_alloc_count, t, outstanding_allocs);

#ifdef FLECS_HTTP
    ECS_COUNTER_RECORD(&s->http.request_received_count, t, ecs_http_request_received_count);
    ECS_COUNTER_RECORD(&s->http.request_invalid_count, t, ecs_http_request_invalid_count);
    ECS_COUNTER_RECORD(&s->http.request_handled_ok_count, t, ecs_http_request_handled_ok_count);
    ECS_COUNTER_RECORD(&s->http.request_handled_error_count, t, ecs_http_request_handled_error_count);
    ECS_COUNTER_RECORD(&s->http.request_not_handled_count, t, ecs_http_request_not_handled_count);
    ECS_COUNTER_RECORD(&s->http.request_preflight_count, t, ecs_http_request_preflight_count);
    ECS_COUNTER_RECORD(&s->http.send_ok_count, t, ecs_http_send_ok_count);
    ECS_COUNTER_RECORD(&s->http.send_error_count, t, ecs_http_send_error_count);
    ECS_COUNTER_RECORD(&s->http.busy_count, t, ecs_http_busy_count);
#endif

error:
    return;
}

void ecs_world_stats_reduce(
    ecs_world_stats_t *dst,
    const ecs_world_stats_t *src)
{
    flecs_stats_reduce(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), (dst->t = t_next(dst->t)), src->t);
}

void ecs_world_stats_reduce_last(
    ecs_world_stats_t *dst,
    const ecs_world_stats_t *src,
    int32_t count)
{
    flecs_stats_reduce_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), (dst->t = t_prev(dst->t)), src->t, count);
}

void ecs_world_stats_repeat_last(
    ecs_world_stats_t *stats)
{
    flecs_stats_repeat_last(ECS_METRIC_FIRST(stats), ECS_METRIC_LAST(stats),
        (stats->t = t_next(stats->t)));
}

void ecs_world_stats_copy_last(
    ecs_world_stats_t *dst,
    const ecs_world_stats_t *src)
{
    flecs_stats_copy_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst),
        ECS_METRIC_FIRST(src), dst->t, t_next(src->t));
}

void ecs_query_stats_get(
    const ecs_world_t *world,
    const ecs_query_t *query,
    ecs_query_stats_t *s)
{
    ecs_check(query != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(s != NULL, ECS_INVALID_PARAMETER, NULL);
    (void)world;

    int32_t t = s->t = t_next(s->t);    
    ecs_query_count_t counts = ecs_query_count(query);
    ECS_GAUGE_RECORD(&s->result_count, t, counts.results);
    ECS_GAUGE_RECORD(&s->matched_table_count, t, counts.tables);
    ECS_GAUGE_RECORD(&s->matched_entity_count, t, counts.entities);

error:
    return;
}

void ecs_query_cache_stats_reduce(
    ecs_query_stats_t *dst,
    const ecs_query_stats_t *src)
{
    flecs_stats_reduce(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), (dst->t = t_next(dst->t)), src->t);
}

void ecs_query_cache_stats_reduce_last(
    ecs_query_stats_t *dst,
    const ecs_query_stats_t *src,
    int32_t count)
{
    flecs_stats_reduce_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), (dst->t = t_prev(dst->t)), src->t, count);
}

void ecs_query_cache_stats_repeat_last(
    ecs_query_stats_t *stats)
{
    flecs_stats_repeat_last(ECS_METRIC_FIRST(stats), ECS_METRIC_LAST(stats),
        (stats->t = t_next(stats->t)));
}

void ecs_query_cache_stats_copy_last(
    ecs_query_stats_t *dst,
    const ecs_query_stats_t *src)
{
    flecs_stats_copy_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst),
        ECS_METRIC_FIRST(src), dst->t, t_next(src->t));
}

#ifdef FLECS_SYSTEM

bool ecs_system_stats_get(
    const ecs_world_t *world,
    ecs_entity_t system,
    ecs_system_stats_t *s)
{
    ecs_check(world != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(s != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(system != 0, ECS_INVALID_PARAMETER, NULL);

    world = ecs_get_world(world);

    const ecs_system_t *ptr = flecs_poly_get(world, system, ecs_system_t);
    if (!ptr) {
        return false;
    }

    ecs_query_stats_get(world, ptr->query, &s->query);
    int32_t t = s->query.t;

    ECS_COUNTER_RECORD(&s->time_spent, t, ptr->time_spent);

    s->task = !(ptr->query->flags & EcsQueryMatchThis);

    return true;
error:
    return false;
}

void ecs_system_stats_reduce(
    ecs_system_stats_t *dst,
    const ecs_system_stats_t *src)
{
    ecs_query_cache_stats_reduce(&dst->query, &src->query);
    dst->task = src->task;
    flecs_stats_reduce(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), dst->query.t, src->query.t);
}

void ecs_system_stats_reduce_last(
    ecs_system_stats_t *dst,
    const ecs_system_stats_t *src,
    int32_t count)
{
    ecs_query_cache_stats_reduce_last(&dst->query, &src->query, count);
    dst->task = src->task;
    flecs_stats_reduce_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst), 
        ECS_METRIC_FIRST(src), dst->query.t, src->query.t, count);
}

void ecs_system_stats_repeat_last(
    ecs_system_stats_t *stats)
{
    ecs_query_cache_stats_repeat_last(&stats->query);
    flecs_stats_repeat_last(ECS_METRIC_FIRST(stats), ECS_METRIC_LAST(stats),
        (stats->query.t));
}

void ecs_system_stats_copy_last(
    ecs_system_stats_t *dst,
    const ecs_system_stats_t *src)
{
    ecs_query_cache_stats_copy_last(&dst->query, &src->query);
    dst->task = src->task;
    flecs_stats_copy_last(ECS_METRIC_FIRST(dst), ECS_METRIC_LAST(dst),
        ECS_METRIC_FIRST(src), dst->query.t, t_next(src->query.t));
}

#endif

#ifdef FLECS_PIPELINE

bool ecs_pipeline_stats_get(
    ecs_world_t *stage,
    ecs_entity_t pipeline,
    ecs_pipeline_stats_t *s)
{
    ecs_check(stage != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(s != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(pipeline != 0, ECS_INVALID_PARAMETER, NULL);

    const ecs_world_t *world = ecs_get_world(stage);
    const EcsPipeline *pqc = ecs_get(world, pipeline, EcsPipeline);
    if (!pqc) {
        return false;
    }
    ecs_pipeline_state_t *pq = pqc->state;
    ecs_assert(pq != NULL, ECS_INTERNAL_ERROR, NULL);

    int32_t sys_count = 0, active_sys_count = 0;

    /* Count number of active systems */
    ecs_iter_t it = ecs_query_iter(stage, pq->query);
    while (ecs_query_next(&it)) {
        if (flecs_component_get_table(pq->cr_inactive, it.table) != NULL) {
            continue;
        }
        active_sys_count += it.count;
    }

    /* Count total number of systems in pipeline */
    it = ecs_query_iter(stage, pq->query);
    while (ecs_query_next(&it)) {
        sys_count += it.count;
    }   

    /* Also count synchronization points */
    ecs_vec_t *ops = &pq->ops;
    ecs_pipeline_op_t *op = ecs_vec_first_t(ops, ecs_pipeline_op_t);
    ecs_pipeline_op_t *op_last = ecs_vec_last_t(ops, ecs_pipeline_op_t);
    int32_t pip_count = active_sys_count + ecs_vec_count(ops);

    if (!sys_count) {
        return false;
    }

    if (op) {
        ecs_entity_t *systems = NULL;
        if (pip_count) {
            ecs_vec_init_if_t(&s->systems, ecs_entity_t);
            ecs_vec_set_count_t(NULL, &s->systems, ecs_entity_t, pip_count);
            systems = ecs_vec_first_t(&s->systems, ecs_entity_t);

            /* Populate systems vector, keep track of sync points */
            it = ecs_query_iter(stage, pq->query);
            
            int32_t i, i_system = 0, ran_since_merge = 0;
            while (ecs_query_next(&it)) {
                if (flecs_component_get_table(pq->cr_inactive, it.table) != NULL) {
                    continue;
                }

                for (i = 0; i < it.count; i ++) {
                    systems[i_system ++] = it.entities[i];
                    ran_since_merge ++;
                    if (op != op_last && ran_since_merge == op->count) {
                        ran_since_merge = 0;
                        op++;
                        systems[i_system ++] = 0; /* 0 indicates a merge point */
                    }
                }
            }

            systems[i_system ++] = 0; /* Last merge */
            ecs_assert(pip_count == i_system, ECS_INTERNAL_ERROR, NULL);
        } else {
            ecs_vec_fini_t(NULL, &s->systems, ecs_entity_t);
        }

        /* Get sync point statistics */
        int32_t i, count = ecs_vec_count(ops);
        if (count) {
            ecs_vec_init_if_t(&s->sync_points, ecs_sync_stats_t);
            ecs_vec_set_min_count_zeromem_t(NULL, &s->sync_points, ecs_sync_stats_t, count);
            op = ecs_vec_first_t(ops, ecs_pipeline_op_t);

            for (i = 0; i < count; i ++) {
                ecs_pipeline_op_t *cur = &op[i];
                ecs_sync_stats_t *el = ecs_vec_get_t(&s->sync_points, 
                    ecs_sync_stats_t, i);

                ECS_COUNTER_RECORD(&el->time_spent, s->t, cur->time_spent);
                ECS_COUNTER_RECORD(&el->commands_enqueued, s->t, 
                    cur->commands_enqueued);

                el->system_count = cur->count;
                el->multi_threaded = cur->multi_threaded;
                el->immediate = cur->immediate;
            }
        }
    }

    s->t = t_next(s->t);

    return true;
error:
    return false;
}

void ecs_pipeline_stats_fini(
    ecs_pipeline_stats_t *stats)
{
    ecs_vec_fini_t(NULL, &stats->systems, ecs_entity_t);
    ecs_vec_fini_t(NULL, &stats->sync_points, ecs_sync_stats_t);
}

void ecs_pipeline_stats_reduce(
    ecs_pipeline_stats_t *dst,
    const ecs_pipeline_stats_t *src)
{
    int32_t system_count = ecs_vec_count(&src->systems);
    ecs_vec_init_if_t(&dst->systems, ecs_entity_t);
    ecs_vec_set_count_t(NULL, &dst->systems, ecs_entity_t, system_count);
    ecs_entity_t *dst_systems = ecs_vec_first_t(&dst->systems, ecs_entity_t);
    ecs_entity_t *src_systems = ecs_vec_first_t(&src->systems, ecs_entity_t);
    ecs_os_memcpy_n(dst_systems, src_systems, ecs_entity_t, system_count);

    int32_t i, sync_count = ecs_vec_count(&src->sync_points);
    ecs_vec_init_if_t(&dst->sync_points, ecs_sync_stats_t);
    ecs_vec_set_min_count_zeromem_t(NULL, &dst->sync_points, ecs_sync_stats_t, sync_count);
    ecs_sync_stats_t *dst_syncs = ecs_vec_first_t(&dst->sync_points, ecs_sync_stats_t);
    ecs_sync_stats_t *src_syncs = ecs_vec_first_t(&src->sync_points, ecs_sync_stats_t);
    for (i = 0; i < sync_count; i ++) {
        ecs_sync_stats_t *dst_el = &dst_syncs[i];
        ecs_sync_stats_t *src_el = &src_syncs[i];
        flecs_stats_reduce(ECS_METRIC_FIRST(dst_el), ECS_METRIC_LAST(dst_el),
            ECS_METRIC_FIRST(src_el), dst->t, src->t);
        dst_el->system_count = src_el->system_count;
        dst_el->multi_threaded = src_el->multi_threaded;
        dst_el->immediate = src_el->immediate;
    }

    dst->t = t_next(dst->t);
}

void ecs_pipeline_stats_reduce_last(
    ecs_pipeline_stats_t *dst,
    const ecs_pipeline_stats_t *src,
    int32_t count)
{
    int32_t i, sync_count = ecs_vec_count(&src->sync_points);
    ecs_sync_stats_t *dst_syncs = ecs_vec_first_t(&dst->sync_points, ecs_sync_stats_t);
    ecs_sync_stats_t *src_syncs = ecs_vec_first_t(&src->sync_points, ecs_sync_stats_t);

    for (i = 0; i < sync_count; i ++) {
        ecs_sync_stats_t *dst_el = &dst_syncs[i];
        ecs_sync_stats_t *src_el = &src_syncs[i];
        flecs_stats_reduce_last(ECS_METRIC_FIRST(dst_el), ECS_METRIC_LAST(dst_el),
            ECS_METRIC_FIRST(src_el), dst->t, src->t, count);
        dst_el->system_count = src_el->system_count;
        dst_el->multi_threaded = src_el->multi_threaded;
        dst_el->immediate = src_el->immediate;
    }

    dst->t = t_prev(dst->t);
}

void ecs_pipeline_stats_repeat_last(
    ecs_pipeline_stats_t *stats)
{
    int32_t i, sync_count = ecs_vec_count(&stats->sync_points);
    ecs_sync_stats_t *syncs = ecs_vec_first_t(&stats->sync_points, ecs_sync_stats_t);

    for (i = 0; i < sync_count; i ++) {
        ecs_sync_stats_t *el = &syncs[i];
        flecs_stats_repeat_last(ECS_METRIC_FIRST(el), ECS_METRIC_LAST(el),
            (stats->t));
    }

    stats->t = t_next(stats->t);
}

void ecs_pipeline_stats_copy_last(
    ecs_pipeline_stats_t *dst,
    const ecs_pipeline_stats_t *src)
{
    int32_t i, sync_count = ecs_vec_count(&src->sync_points);
    ecs_vec_init_if_t(&dst->sync_points, ecs_sync_stats_t);
    ecs_vec_set_min_count_zeromem_t(NULL, &dst->sync_points, ecs_sync_stats_t, sync_count);
    ecs_sync_stats_t *dst_syncs = ecs_vec_first_t(&dst->sync_points, ecs_sync_stats_t);
    ecs_sync_stats_t *src_syncs = ecs_vec_first_t(&src->sync_points, ecs_sync_stats_t);

    for (i = 0; i < sync_count; i ++) {
        ecs_sync_stats_t *dst_el = &dst_syncs[i];
        ecs_sync_stats_t *src_el = &src_syncs[i];
        flecs_stats_copy_last(ECS_METRIC_FIRST(dst_el), ECS_METRIC_LAST(dst_el),
            ECS_METRIC_FIRST(src_el), dst->t, t_next(src->t));
        dst_el->system_count = src_el->system_count;
        dst_el->multi_threaded = src_el->multi_threaded;
        dst_el->immediate = src_el->immediate;
    }
}

#endif

void ecs_world_stats_log(
    const ecs_world_t *world,
    const ecs_world_stats_t *s)
{
    int32_t t = s->t;

    ecs_check(world != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(s != NULL, ECS_INVALID_PARAMETER, NULL);

    world = ecs_get_world(world);    
    
    flecs_counter_print("Frame", t, &s->frame.frame_count);
    ecs_trace("-------------------------------------");
    flecs_counter_print("pipeline rebuilds", t, &s->frame.pipeline_build_count);
    flecs_counter_print("systems ran", t, &s->frame.systems_ran);
    ecs_trace("");
    flecs_metric_print("target FPS", (ecs_float_t)world->info.target_fps);
    flecs_metric_print("time scale", (ecs_float_t)world->info.time_scale);
    ecs_trace("");
    flecs_gauge_print("actual FPS", t, &s->performance.fps);
    flecs_counter_print("frame time", t, &s->performance.frame_time);
    flecs_counter_print("system time", t, &s->performance.system_time);
    flecs_counter_print("merge time", t, &s->performance.merge_time);
    flecs_counter_print("simulation time elapsed", t, &s->performance.world_time);
    ecs_trace("");
    flecs_gauge_print("tag id count", t, &s->components.tag_count);
    flecs_gauge_print("component id count", t, &s->components.component_count);
    flecs_gauge_print("pair id count", t, &s->components.pair_count);
    flecs_gauge_print("type count", t, &s->components.type_count);
    flecs_counter_print("id create count", t, &s->components.create_count);
    flecs_counter_print("id delete count", t, &s->components.delete_count);
    ecs_trace("");
    flecs_gauge_print("alive entity count", t, &s->entities.count);
    flecs_gauge_print("not alive entity count", t, &s->entities.not_alive_count);
    ecs_trace("");
    flecs_gauge_print("query count", t, &s->queries.query_count);
    flecs_gauge_print("observer count", t, &s->queries.observer_count);
    flecs_gauge_print("system count", t, &s->queries.system_count);
    ecs_trace("");
    flecs_gauge_print("table count", t, &s->tables.count);
    flecs_gauge_print("empty table count", t, &s->tables.empty_count);
    flecs_counter_print("table create count", t, &s->tables.create_count);
    flecs_counter_print("table delete count", t, &s->tables.delete_count);
    ecs_trace("");
    flecs_counter_print("add commands", t, &s->commands.add_count);
    flecs_counter_print("remove commands", t, &s->commands.remove_count);
    flecs_counter_print("delete commands", t, &s->commands.delete_count);
    flecs_counter_print("clear commands", t, &s->commands.clear_count);
    flecs_counter_print("set commands", t, &s->commands.set_count);
    flecs_counter_print("ensure commands", t, &s->commands.ensure_count);
    flecs_counter_print("modified commands", t, &s->commands.modified_count);
    flecs_counter_print("other commands", t, &s->commands.other_count);
    flecs_counter_print("discarded commands", t, &s->commands.discard_count);
    flecs_counter_print("batched entities", t, &s->commands.batched_entity_count);
    flecs_counter_print("batched commands", t, &s->commands.batched_count);
    ecs_trace("");
    
error:
    return;
}

#endif
