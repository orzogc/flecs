/**
 * @file observable.c
 * @brief Observable implementation.
 * 
 * The observable implementation contains functions that find the set of 
 * observers to invoke for an event. The code also contains the implementation
 * of a reachable id cache, which is used to speedup event propagation when
 * relationships are added/removed to/from entities.
 */

#include "private_api.h"

void flecs_observable_init(
    ecs_observable_t *observable)
{
    flecs_sparse_init_t(&observable->events, NULL, NULL, ecs_event_record_t);
    observable->on_add.event = EcsOnAdd;
    observable->on_remove.event = EcsOnRemove;
    observable->on_set.event = EcsOnSet;
}

void flecs_observable_fini(
    ecs_observable_t *observable)
{
    ecs_assert(!ecs_map_is_init(&observable->on_add.event_ids), 
        ECS_INTERNAL_ERROR, NULL);
    ecs_assert(!ecs_map_is_init(&observable->on_remove.event_ids), 
        ECS_INTERNAL_ERROR, NULL);
    ecs_assert(!ecs_map_is_init(&observable->on_set.event_ids), 
        ECS_INTERNAL_ERROR, NULL);

    ecs_sparse_t *events = &observable->events;
    int32_t i, count = flecs_sparse_count(events);
    for (i = 0; i < count; i ++) {
        ecs_event_record_t *er = 
            flecs_sparse_get_dense_t(events, ecs_event_record_t, i);
        ecs_assert(er != NULL, ECS_INTERNAL_ERROR, NULL);
        (void)er;

        /* All observers should've unregistered by now */
        ecs_assert(!ecs_map_is_init(&er->event_ids), 
            ECS_INTERNAL_ERROR, NULL);
    }

    flecs_sparse_fini(&observable->events);
}

ecs_event_record_t* flecs_event_record_get(
    const ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);
    
    /* Builtin events*/
    if      (event == EcsOnAdd)    return ECS_CONST_CAST(ecs_event_record_t*, &o->on_add);
    else if (event == EcsOnRemove) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_remove);
    else if (event == EcsOnSet)    return ECS_CONST_CAST(ecs_event_record_t*, &o->on_set);
    else if (event == EcsWildcard) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_wildcard);

    /* User events */
    return flecs_sparse_get_t(&o->events, ecs_event_record_t, event);
}

ecs_event_record_t* flecs_event_record_ensure(
    ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_event_record_t *er = flecs_event_record_get(o, event);
    if (er) {
        return er;
    }
    er = flecs_sparse_get_t(&o->events, ecs_event_record_t, event);
    if (!er) {
        er = flecs_sparse_ensure_t(&o->events, ecs_event_record_t, event, NULL);
    }
    er->event = event;
    return er;
}

static
const ecs_event_record_t* flecs_event_record_get_if(
    const ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);

    const ecs_event_record_t *er = flecs_event_record_get(o, event);
    if (er) {
        if (ecs_map_is_init(&er->event_ids)) {
            return er;
        }
        if (er->any) {
            return er;
        }
        if (er->wildcard) {
            return er;
        }
        if (er->wildcard_pair) {
            return er;
        }
    }

    return NULL;
}

ecs_event_id_record_t* flecs_event_id_record_get(
    const ecs_event_record_t *er,
    ecs_id_t id)
{
    if (!er) {
        return NULL;
    }

    if (id == EcsAny)                                  return er->any;
    else if (id == EcsWildcard)                        return er->wildcard;
    else if (id == ecs_pair(EcsWildcard, EcsWildcard)) return er->wildcard_pair;
    else {
        if (ecs_map_is_init(&er->event_ids)) {
            return ecs_map_get_deref(&er->event_ids, ecs_event_id_record_t, id);
        }
        return NULL;
    }
}

static
ecs_event_id_record_t* flecs_event_id_record_get_if(
    const ecs_event_record_t *er,
    ecs_id_t id)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get(er, id);
    if (!ider) {
        return NULL;
    }

    if (ider->observer_count) {
        return ider;
    }

    return NULL;
}

ecs_event_id_record_t* flecs_event_id_record_ensure(
    ecs_world_t *world,
    ecs_event_record_t *er,
    ecs_id_t id)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get(er, id);
    if (ider) {
        return ider;
    }

    ider = ecs_os_calloc_t(ecs_event_id_record_t);

    if (id == EcsAny) {
        return er->any = ider;
    } else if (id == EcsWildcard) {
        return er->wildcard = ider;
    } else if (id == ecs_pair(EcsWildcard, EcsWildcard)) {
        return er->wildcard_pair = ider;
    }

    ecs_map_init_w_params_if(&er->event_ids, &world->allocators.ptr);
    ecs_map_insert_ptr(&er->event_ids, id, ider);
    return ider;
}

void flecs_event_id_record_remove(
    ecs_event_record_t *er,
    ecs_id_t id)
{
    if (id == EcsAny) {
        er->any = NULL;
    } else if (id == EcsWildcard) {
        er->wildcard = NULL;
    } else if (id == ecs_pair(EcsWildcard, EcsWildcard)) {
        er->wildcard_pair = NULL;
    } else {
        ecs_map_remove(&er->event_ids, id);
        if (!ecs_map_count(&er->event_ids)) {
            ecs_map_fini(&er->event_ids);
        }
    }
}

static
int32_t flecs_event_observers_get(
    const ecs_event_record_t *er,
    ecs_id_t id,
    ecs_event_id_record_t **iders)
{
    if (!er) {
        return 0;
    }

    /* Populate array with observer sets matching the id */
    int32_t count = 0;

    if (id != EcsAny) {
        iders[0] = flecs_event_id_record_get_if(er, EcsAny);
        count += iders[count] != 0;
    }

    iders[count] = flecs_event_id_record_get_if(er, id);
    count += iders[count] != 0;

    if (id != EcsAny) {
        if (ECS_IS_PAIR(id)) {
            ecs_id_t id_fwc = ecs_pair(EcsWildcard, ECS_PAIR_SECOND(id));
            ecs_id_t id_swc = ecs_pair(ECS_PAIR_FIRST(id), EcsWildcard);
            ecs_id_t id_pwc = ecs_pair(EcsWildcard, EcsWildcard);
            if (id_fwc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_fwc);
                count += iders[count] != 0;
            }
            if (id_swc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_swc);
                count += iders[count] != 0;
            }
            if (id_pwc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_pwc);
                count += iders[count] != 0;
            }
        } else if (id != EcsWildcard) {
            iders[count] = flecs_event_id_record_get_if(er, EcsWildcard);
            count += iders[count] != 0;
        }
    }

    return count;
}

bool flecs_observers_exist(
    ecs_observable_t *observable,
    ecs_id_t id,
    ecs_entity_t event)
{
    const ecs_event_record_t *er = flecs_event_record_get_if(observable, event);
    if (!er) {
        return false;
    }

    return flecs_event_id_record_get_if(er, id) != NULL;
}

static
void flecs_emit_propagate(
    ecs_world_t *world,
    ecs_iter_t *it,
    ecs_component_record_t *cr,
    ecs_component_record_t *tgt_cr,
    ecs_entity_t trav,
    ecs_event_id_record_t **iders,
    int32_t ider_count);

static
void flecs_emit_propagate_id(
    ecs_world_t *world,
    ecs_iter_t *it,
    ecs_component_record_t *cr,
    ecs_component_record_t *cur,
    ecs_entity_t trav,
    ecs_event_id_record_t **iders,
    int32_t ider_count)
{
    ecs_table_cache_iter_t idt;
    if (!flecs_table_cache_all_iter(&cur->cache, &idt)) {
        return;
    }

    const ecs_table_record_t *tr;
    int32_t event_cur = it->event_cur;
    while ((tr = flecs_table_cache_next(&idt, ecs_table_record_t))) {
        ecs_table_t *table = tr->hdr.table;
        if (!ecs_table_count(table)) {
            continue;
        }

        bool owned = flecs_component_get_table(cr, table) != NULL;

        int32_t e, entity_count = ecs_table_count(table);
        it->table = table;
        it->other_table = NULL;
        it->offset = 0;
        it->count = entity_count;
        it->up_fields = 1;
        if (entity_count) {
            it->entities = ecs_table_entities(table);
        }

        int32_t ider_i;
        for (ider_i = 0; ider_i < ider_count; ider_i ++) {
            ecs_event_id_record_t *ider = iders[ider_i];
            flecs_observers_invoke(world, &ider->up, it, table, trav);

            if (!owned) {
                /* Owned takes precedence */
                flecs_observers_invoke(world, &ider->self_up, it, table, trav);
            }
        }

        if (!table->_->traversable_count) {
            continue;
        }

        const ecs_entity_t *entities = ecs_table_entities(table);
        for (e = 0; e < entity_count; e ++) {
            ecs_record_t *r = flecs_entities_get(world, entities[e]);
            ecs_assert(r != NULL, ECS_INTERNAL_ERROR, NULL);
            ecs_component_record_t *cr_t = r->cr;
            if (cr_t) {
                /* Only notify for entities that are used in pairs with
                 * traversable relationships */
                flecs_emit_propagate(world, it, cr, cr_t, trav,
                    iders, ider_count);
            }
        }
    }

    it->event_cur = event_cur;
    it->up_fields = 0;
}

static
void flecs_emit_propagate(
    ecs_world_t *world,
    ecs_iter_t *it,
    ecs_component_record_t *cr,
    ecs_component_record_t *tgt_cr,
    ecs_entity_t propagate_trav,
    ecs_event_id_record_t **iders,
    int32_t ider_count)
{
    ecs_assert(tgt_cr != NULL, ECS_INTERNAL_ERROR, NULL);    

    if (ecs_should_log_3()) {
        char *idstr = ecs_id_str(world, tgt_cr->id);
        ecs_dbg_3("propagate events/invalidate cache for %s", idstr);
        ecs_os_free(idstr);
    }

    ecs_log_push_3();

    /* Propagate to records of traversable relationships */
    ecs_component_record_t *cur = tgt_cr;
    while ((cur = flecs_component_trav_next(cur))) {
        cur->pair->reachable.generation ++; /* Invalidate cache */

        /* Get traversed relationship */
        ecs_entity_t trav = ECS_PAIR_FIRST(cur->id);
        if (propagate_trav && propagate_trav != trav) {
            if (propagate_trav != EcsIsA) {
                continue;
            }
        }

        flecs_emit_propagate_id(
            world, it, cr, cur, trav, iders, ider_count);
    }

    ecs_log_pop_3();
}

static
void flecs_emit_propagate_invalidate_tables(
    ecs_world_t *world,
    ecs_component_record_t *tgt_cr)
{
    ecs_assert(tgt_cr != NULL, ECS_INTERNAL_ERROR, NULL);

    if (ecs_should_log_3()) {
        char *idstr = ecs_id_str(world, tgt_cr->id);
        ecs_dbg_3("invalidate reachable cache for %s", idstr);
        ecs_os_free(idstr);
    }

    /* Invalidate records of traversable relationships */
    ecs_component_record_t *cur = tgt_cr;
    while ((cur = flecs_component_trav_next(cur))) {
        ecs_reachable_cache_t *rc = &cur->pair->reachable;
        if (rc->current != rc->generation) {
            /* Subtree is already marked invalid */
            continue;
        }

        rc->generation ++;

        ecs_table_cache_iter_t idt;
        if (!flecs_table_cache_all_iter(&cur->cache, &idt)) {
            continue;
        }

        const ecs_table_record_t *tr;
        while ((tr = flecs_table_cache_next(&idt, ecs_table_record_t))) {
            ecs_table_t *table = tr->hdr.table;
            if (!table->_->traversable_count) {
                continue;
            }

            int32_t e, entity_count = ecs_table_count(table);
            const ecs_entity_t *entities = ecs_table_entities(table);

            for (e = 0; e < entity_count; e ++) {
                ecs_record_t *r = flecs_entities_get(world, entities[e]);
                ecs_component_record_t *cr_t = r->cr;
                if (cr_t) {
                    /* Only notify for entities that are used in pairs with
                     * traversable relationships */
                    flecs_emit_propagate_invalidate_tables(world, cr_t);
                }
            }
        }
    }
}

void flecs_emit_propagate_invalidate(
    ecs_world_t *world,
    ecs_table_t *table,
    int32_t offset,
    int32_t count)
{
    const ecs_entity_t *entities = &ecs_table_entities(table)[offset];
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_record_t *record = flecs_entities_get(world, entities[i]);
        if (!record) {
            /* If the event is emitted after a bulk operation, it's possible
             * that it hasn't been populated with entities yet. */
            continue;
        }

        ecs_component_record_t *cr_t = record->cr;
        if (cr_t) {
            /* Event is used as target in traversable relationship, propagate */
            flecs_emit_propagate_invalidate_tables(world, cr_t);
        }
    }
}

static
void flecs_propagate_entities(
    ecs_world_t *world,
    ecs_iter_t *it,
    ecs_component_record_t *cr,
    const ecs_entity_t *entities,
    int32_t count,
    ecs_entity_t src,
    ecs_event_id_record_t **iders,
    int32_t ider_count)
{
    if (!count) {
        return;
    }

    ecs_entity_t old_src = it->sources[0];
    ecs_table_t *old_table = it->table;
    ecs_table_t *old_other_table = it->other_table;
    const ecs_entity_t *old_entities = it->entities;
    int32_t old_count = it->count;
    int32_t old_offset = it->offset;

    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_record_t *record = flecs_entities_get(world, entities[i]);
        if (!record) {
            /* If the event is emitted after a bulk operation, it's possible
             * that it hasn't been populated with entities yet. */
            continue;
        }

        ecs_component_record_t *cr_t = record->cr;
        if (cr_t) {
            /* Entity is used as target in traversable pairs, propagate */
            ecs_entity_t e = src ? src : entities[i];
            it->sources[0] = e;
            flecs_emit_propagate(
                world, it, cr, cr_t, 0, iders, ider_count);
        }
    }
    
    it->table = old_table;
    it->other_table = old_other_table;
    it->entities = old_entities;
    it->count = old_count;
    it->offset = old_offset;
    it->sources[0] = old_src;
}

static
void flecs_emit_forward_up(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_component_record_t *cr,
    ecs_vec_t *stack,
    ecs_vec_t *reachable_ids,
    int32_t depth);

static
void flecs_emit_forward_id(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_component_record_t *cr,
    ecs_entity_t tgt,
    ecs_table_t *tgt_table,
    int32_t column,
    ecs_entity_t trav)
{
    ecs_id_t id = cr->id;
    ecs_entity_t event = er ? er->event : 0;
    bool inherit = trav == EcsIsA;
    bool may_override = inherit && (event == EcsOnAdd) && (emit_ids->count > 1);
    ecs_event_id_record_t *iders[5];
    ecs_event_id_record_t *iders_onset[5];

    /* Skip id if there are no observers for it */
    int32_t ider_i, ider_count = flecs_event_observers_get(er, id, iders);
    int32_t ider_onset_i, ider_onset_count = 0;
    if (er_onset) {
        ider_onset_count = flecs_event_observers_get(
            er_onset, id, iders_onset);
    }

    if (!may_override && (!ider_count && !ider_onset_count)) {
        return;
    }

    ecs_entity_t old_src = it->sources[0];

    it->ids[0] = id;
    it->sources[0] = tgt;
    it->event_id = id;
    ECS_CONST_CAST(int32_t*, it->sizes)[0] = 0; /* safe, owned by observer */
    it->up_fields = 1;

    int32_t storage_i = ecs_table_type_to_column_index(tgt_table, column);
    if (storage_i != -1) {
        ecs_assert(cr->type_info != NULL, ECS_INTERNAL_ERROR, NULL);
        ecs_column_t *c = &tgt_table->data.columns[storage_i];
        it->trs[0] = &tgt_table->_->records[column];
        ECS_CONST_CAST(int32_t*, it->sizes)[0] = c->ti->size; /* safe, see above */
    }

    const ecs_table_record_t *tr = flecs_component_get_table(cr, table);
    bool owned = tr != NULL;

    for (ider_i = 0; ider_i < ider_count; ider_i ++) {
        ecs_event_id_record_t *ider = iders[ider_i];
        flecs_observers_invoke(world, &ider->up, it, table, trav);

        /* Owned takes precedence */
        if (!owned) {
            flecs_observers_invoke(world, &ider->self_up, it, table, trav);
        }
    }

    /* Emit OnSet events for newly inherited components */
    if (storage_i != -1) {
        if (ider_onset_count) {
            it->event = er_onset->event;

            for (ider_onset_i = 0; ider_onset_i < ider_onset_count; ider_onset_i ++) {
                ecs_event_id_record_t *ider = iders_onset[ider_onset_i];
                flecs_observers_invoke(world, &ider->up, it, table, trav);

                /* Owned takes precedence */
                if (!owned) {
                    flecs_observers_invoke(
                        world, &ider->self_up, it, table, trav);
                }
            }

            it->event = event;
        }
    }

    it->sources[0] = old_src;
    it->up_fields = 0;
}

static
void flecs_emit_forward_and_cache_id(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_component_record_t *cr,
    ecs_entity_t tgt,
    ecs_record_t *tgt_record,
    ecs_table_t *tgt_table,
    const ecs_table_record_t *tgt_tr,
    int32_t column,
    ecs_vec_t *reachable_ids,
    ecs_entity_t trav)
{
    /* Cache forwarded id for (rel, tgt) pair */
    ecs_reachable_elem_t *elem = ecs_vec_append_t(&world->allocator,
        reachable_ids, ecs_reachable_elem_t);
    elem->tr = tgt_tr;
    elem->record = tgt_record;
    elem->src = tgt;
    elem->id = cr->id;
#ifndef FLECS_NDEBUG
    elem->table = tgt_table;
#endif
    ecs_assert(tgt_table == tgt_record->table, ECS_INTERNAL_ERROR, NULL);

    flecs_emit_forward_id(world, er, er_onset, emit_ids, it, table, cr,
        tgt, tgt_table, column, trav);
}

static
int32_t flecs_emit_stack_at(
    ecs_vec_t *stack,
    ecs_component_record_t *cr)
{
    int32_t sp = 0, stack_count = ecs_vec_count(stack);
    ecs_table_t **stack_elems = ecs_vec_first(stack);

    for (sp = 0; sp < stack_count; sp ++) {
        ecs_table_t *elem = stack_elems[sp];
        if (flecs_component_get_table(cr, elem)) {
            break;
        }
    }

    return sp;
}

static
bool flecs_emit_stack_has(
    ecs_vec_t *stack,
    ecs_component_record_t *cr)
{
    return flecs_emit_stack_at(stack, cr) != ecs_vec_count(stack);
}

static
void flecs_emit_forward_cached_ids(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_reachable_cache_t *rc,
    ecs_vec_t *reachable_ids,
    ecs_vec_t *stack,
    ecs_entity_t trav)
{
    ecs_reachable_elem_t *elems = ecs_vec_first_t(&rc->ids, 
        ecs_reachable_elem_t);
    int32_t i, count = ecs_vec_count(&rc->ids);
    for (i = 0; i < count; i ++) {
        ecs_reachable_elem_t *rc_elem = &elems[i];
        const ecs_table_record_t *rc_tr = rc_elem->tr;
        ecs_assert(rc_tr != NULL, ECS_INTERNAL_ERROR, NULL);
        ecs_component_record_t *rc_cr = rc_tr->hdr.cr;
        ecs_record_t *rc_record = rc_elem->record;

        ecs_assert(rc_cr->id == rc_elem->id, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(rc_record != NULL, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(flecs_entities_get(world, rc_elem->src) == 
            rc_record, ECS_INTERNAL_ERROR, NULL);
        ecs_dbg_assert(rc_record->table == rc_elem->table, 
            ECS_INTERNAL_ERROR, NULL);

        if (flecs_emit_stack_has(stack, rc_cr)) {
            continue;
        }

        flecs_emit_forward_and_cache_id(world, er, er_onset, emit_ids,
            it, table, rc_cr, rc_elem->src,
                rc_record, rc_record->table, rc_tr, rc_tr->index,
                    reachable_ids, trav);
    }
}

static
void flecs_emit_dump_cache(
    ecs_world_t *world,
    const ecs_vec_t *vec)
{
    ecs_reachable_elem_t *elems = ecs_vec_first_t(vec, ecs_reachable_elem_t);
    for (int i = 0; i < ecs_vec_count(vec); i ++) {
        ecs_reachable_elem_t *elem = &elems[i];
        char *idstr = ecs_id_str(world, elem->id);
        char *estr = ecs_id_str(world, elem->src);
        #ifndef FLECS_NDEBUG
        ecs_table_t *table = elem->table;
        #else
        ecs_table_t *table = NULL;
        #endif
        (void)table;
        ecs_dbg_3("- id: %s (%u), src: %s (%u), table: %p", 
            idstr, (uint32_t)elem->id,
            estr, (uint32_t)elem->src,
            table);
        ecs_os_free(idstr);
        ecs_os_free(estr);
    }
    if (!ecs_vec_count(vec)) {
        ecs_dbg_3("- no entries");
    }
}

static
void flecs_emit_forward_table_up(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_entity_t tgt,
    ecs_table_t *tgt_table,
    ecs_record_t *tgt_record,
    ecs_component_record_t *tgt_cr,
    ecs_vec_t *stack,
    ecs_vec_t *reachable_ids,
    int32_t depth)
{
    ecs_allocator_t *a = &world->allocator;
    int32_t i, id_count = tgt_table->type.count;
    ecs_id_t *ids = tgt_table->type.array;
    int32_t rc_child_offset = ecs_vec_count(reachable_ids);
    int32_t stack_count = ecs_vec_count(stack);

    /* If tgt_cr is out of sync but is not the current component record being updated,
     * keep track so that we can update two records for the cost of one. */
    ecs_assert(tgt_cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_reachable_cache_t *rc = &tgt_cr->pair->reachable;
    bool parent_revalidate = (reachable_ids != &rc->ids) && 
        (rc->current != rc->generation);
    if (parent_revalidate) {
        ecs_vec_reset_t(a, &rc->ids, ecs_reachable_elem_t);
    }

    if (ecs_should_log_3()) {
        char *idstr = ecs_id_str(world, tgt_cr->id);
        ecs_dbg_3("forward events from %s", idstr);
        ecs_os_free(idstr);
    }
    ecs_log_push_3();

    /* Function may have to copy values from overridden components if an IsA
     * relationship was added together with other components. */
    ecs_entity_t trav = ECS_PAIR_FIRST(tgt_cr->id);
    bool inherit = trav == EcsIsA;

    for (i = 0; i < id_count; i ++) {
        ecs_id_t id = ids[i];
        ecs_table_record_t *tgt_tr = &tgt_table->_->records[i];
        ecs_component_record_t *cr = tgt_tr->hdr.cr;
        if (inherit && !(cr->flags & EcsIdOnInstantiateInherit)) {
            continue;
        }

        if (cr == tgt_cr) {
            char *idstr = ecs_id_str(world, cr->id);
            ecs_assert(cr != tgt_cr, ECS_CYCLE_DETECTED, idstr);
            ecs_os_free(idstr);
            return;
        }

        /* Id has the same relationship, traverse to find ids for forwarding */
        if (ECS_PAIR_FIRST(id) == trav || ECS_PAIR_FIRST(id) == EcsIsA) {
            ecs_table_t **t = ecs_vec_append_t(&world->allocator, stack, 
                ecs_table_t*);
            t[0] = tgt_table;

            ecs_assert(cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
            ecs_reachable_cache_t *cr_rc = &cr->pair->reachable;
            if (cr_rc->current == cr_rc->generation) {
                /* Cache hit, use cached ids to prevent traversing the same
                 * hierarchy multiple times. This especially speeds up code 
                 * where (deep) hierarchies are created. */
                if (ecs_should_log_3()) {
                    char *idstr = ecs_id_str(world, id);
                    ecs_dbg_3("forward cached for %s", idstr);
                    ecs_os_free(idstr);
                }
                ecs_log_push_3();
                flecs_emit_forward_cached_ids(world, er, er_onset, emit_ids, it,
                    table, cr_rc, reachable_ids, stack, trav);
                ecs_log_pop_3();
            } else {
                /* Cache is dirty, traverse upwards */
                do {
                    flecs_emit_forward_up(world, er, er_onset, emit_ids, it, 
                        table, cr, stack, reachable_ids, depth);
                    if (++i >= id_count) {
                        break;
                    }

                    id = ids[i];
                    if (ECS_PAIR_FIRST(id) != trav) {
                        break;
                    }
                } while (true);
            }

            ecs_vec_remove_last(stack);
            continue;
        }

        int32_t stack_at = flecs_emit_stack_at(stack, cr);
        if (parent_revalidate && (stack_at == (stack_count - 1))) {
            /* If parent component record needs to be revalidated, add id */
            ecs_reachable_elem_t *elem = ecs_vec_append_t(a, &rc->ids, 
                ecs_reachable_elem_t);
            elem->tr = tgt_tr;
            elem->record = tgt_record;
            elem->src = tgt;
            elem->id = cr->id;
#ifndef FLECS_NDEBUG
            elem->table = tgt_table;
#endif
        }

        /* Skip id if it's masked by a lower table in the tree */
        if (stack_at != stack_count) {
            continue;
        }

        flecs_emit_forward_and_cache_id(world, er, er_onset, emit_ids, it,
            table, cr, tgt, tgt_record, tgt_table, tgt_tr, i, 
                reachable_ids, trav);
    }

    if (parent_revalidate) {
        /* If this is not the current cache being updated, but it's marked
         * as out of date, use intermediate results to populate cache. */
        int32_t rc_parent_offset = ecs_vec_count(&rc->ids);

        /* Only add ids that were added for this table */
        int32_t count = ecs_vec_count(reachable_ids);
        count -= rc_child_offset;

        /* Append ids to any ids that already were added /*/
        if (count) {
            ecs_vec_grow_t(a, &rc->ids, ecs_reachable_elem_t, count);
            ecs_reachable_elem_t *dst = ecs_vec_get_t(&rc->ids, 
                ecs_reachable_elem_t, rc_parent_offset);
            ecs_reachable_elem_t *src = ecs_vec_get_t(reachable_ids,
                ecs_reachable_elem_t, rc_child_offset);
            ecs_os_memcpy_n(dst, src, ecs_reachable_elem_t, count);
        }

        rc->current = rc->generation;

        if (ecs_should_log_3()) {
            char *idstr = ecs_id_str(world, tgt_cr->id);
            ecs_dbg_3("cache revalidated for %s:", idstr);
            ecs_os_free(idstr);
            flecs_emit_dump_cache(world, &rc->ids);
        }
    }

    ecs_log_pop_3();
}

static
void flecs_emit_forward_up(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_component_record_t *cr,
    ecs_vec_t *stack,
    ecs_vec_t *reachable_ids,
    int32_t depth)
{
    if (depth >= FLECS_DAG_DEPTH_MAX) {
        char *idstr = ecs_id_str(world, cr->id);
        ecs_assert(depth < FLECS_DAG_DEPTH_MAX, ECS_CYCLE_DETECTED, idstr);
        ecs_os_free(idstr);
        return;
    }

    ecs_id_t id = cr->id;
    ecs_entity_t tgt = ECS_PAIR_SECOND(id);
    tgt = flecs_entities_get_alive(world, tgt);
    ecs_assert(tgt != 0, ECS_INTERNAL_ERROR, NULL);
    ecs_record_t *tgt_record = flecs_entities_try(world, tgt);
    ecs_table_t *tgt_table;
    if (!tgt_record || !(tgt_table = tgt_record->table)) {
        return;
    }

    flecs_emit_forward_table_up(world, er, er_onset, emit_ids, it, table, 
        tgt, tgt_table, tgt_record, cr, stack, reachable_ids, depth + 1);
}

static
void flecs_emit_forward(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    const ecs_type_t *emit_ids,
    ecs_iter_t *it,
    ecs_table_t *table,
    ecs_component_record_t *cr)
{
    ecs_assert(cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_reachable_cache_t *rc = &cr->pair->reachable;

    if (rc->current != rc->generation) {
        /* Cache miss, iterate the tree to find ids to forward */
        if (ecs_should_log_3()) {
            char *idstr = ecs_id_str(world, cr->id);
            ecs_dbg_3("reachable cache miss for %s", idstr);
            ecs_os_free(idstr);
        }
        ecs_log_push_3();

        ecs_vec_t stack;
        ecs_vec_init_t(&world->allocator, &stack, ecs_table_t*, 0);
        ecs_vec_reset_t(&world->allocator, &rc->ids, ecs_reachable_elem_t);
        flecs_emit_forward_up(world, er, er_onset, emit_ids, it, table, 
            cr, &stack, &rc->ids, 0);
        it->sources[0] = 0;
        ecs_vec_fini_t(&world->allocator, &stack, ecs_table_t*);

        if (it->event == EcsOnAdd || it->event == EcsOnRemove) {
            /* Only OnAdd/OnRemove events can validate top-level cache, which
             * is for the id for which the event is emitted. 
             * The reason for this is that we don't want to validate the cache
             * while the administration for the mutated entity isn't up to 
             * date yet. */
            rc->current = rc->generation;
        }

        if (ecs_should_log_3()) {
            ecs_dbg_3("cache after rebuild:");
            flecs_emit_dump_cache(world, &rc->ids);
        }

        ecs_log_pop_3();
    } else {
        /* Cache hit, use cached values instead of walking the tree */
        if (ecs_should_log_3()) {
            char *idstr = ecs_id_str(world, cr->id);
            ecs_dbg_3("reachable cache hit for %s", idstr);
            ecs_os_free(idstr);
            flecs_emit_dump_cache(world, &rc->ids);
        }

        ecs_entity_t trav = ECS_PAIR_FIRST(cr->id);
        ecs_reachable_elem_t *elems = ecs_vec_first_t(&rc->ids, 
            ecs_reachable_elem_t);
        int32_t i, count = ecs_vec_count(&rc->ids);
        for (i = 0; i < count; i ++) {
            ecs_reachable_elem_t *elem = &elems[i];
            const ecs_table_record_t *tr = elem->tr;
            ecs_assert(tr != NULL, ECS_INTERNAL_ERROR, NULL);
            ecs_component_record_t *rc_cr = tr->hdr.cr;
            ecs_record_t *r = elem->record;

            ecs_assert(rc_cr->id == elem->id, ECS_INTERNAL_ERROR, NULL);
            ecs_assert(r != NULL, ECS_INTERNAL_ERROR, NULL);
            ecs_assert(flecs_entities_get(world, elem->src) == r,
                ECS_INTERNAL_ERROR, NULL);
            ecs_dbg_assert(r->table == elem->table, ECS_INTERNAL_ERROR, NULL);

            flecs_emit_forward_id(world, er, er_onset, emit_ids, it, table,
                rc_cr, elem->src, r->table, tr->index, trav);
        }
    }

    /* Propagate events for new reachable ids downwards */
    if (table->_->traversable_count) {
        int32_t i;
        const ecs_entity_t *entities = ecs_table_entities(table);
        entities = ECS_ELEM_T(entities, ecs_entity_t, it->offset);
        for (i = 0; i < it->count; i ++) {
            ecs_record_t *r = flecs_entities_get(world, entities[i]);
            if (r->cr) {
                break;
            }
        }

        if (i != it->count) {
            ecs_reachable_elem_t *elems = ecs_vec_first_t(&rc->ids, 
                ecs_reachable_elem_t);
            int32_t count = ecs_vec_count(&rc->ids);
            for (i = 0; i < count; i ++) {
                ecs_reachable_elem_t *elem = &elems[i];
                const ecs_table_record_t *tr = elem->tr;
                ecs_assert(tr != NULL, ECS_INTERNAL_ERROR, NULL);
                ecs_component_record_t *rc_cr = tr->hdr.cr;
                ecs_record_t *r = elem->record;

                ecs_assert(rc_cr->id == elem->id, ECS_INTERNAL_ERROR, NULL);
                ecs_assert(r != NULL, ECS_INTERNAL_ERROR, NULL);
                ecs_assert(flecs_entities_get(world, elem->src) == r,
                    ECS_INTERNAL_ERROR, NULL);
                ecs_dbg_assert(r->table == elem->table, ECS_INTERNAL_ERROR, NULL);
                (void)r;

                /* If entities already have the component, don't propagate */
                if (flecs_component_get_table(rc_cr, it->table)) {
                    continue;
                }

                ecs_event_id_record_t *iders[5] = {0};
                int32_t ider_count = flecs_event_observers_get(
                    er, rc_cr->id, iders);

                flecs_propagate_entities(world, it, rc_cr, it->entities, 
                    it->count, elem->src, iders, ider_count);
            }
        }
    }
}

static
void flecs_emit_on_set_for_override_on_add(
    ecs_world_t *world,
    const ecs_event_record_t *er_onset,
    int32_t evtx,
    ecs_iter_t *it,
    ecs_id_t id,
    ecs_component_record_t *cr,
    ecs_table_t *table)
{
    (void)evtx;

    ecs_ref_t storage;
    const ecs_ref_t *o = flecs_table_get_override(
        world, table, id, cr, &storage);
    if (!o) {
        return;
    }

    /* Table has override for component. If this overrides a
     * component that was already reachable for the table we 
     * don't need to emit since the value didn't change. */
    ecs_entity_t base = o->entity;

    ecs_table_t *other = it->other_table;
    if (other) {
        if (ecs_table_has_id(world, other, ecs_pair(EcsIsA, base))) {
            /* If previous table already had (IsA, base), entity already 
             * inherited the component, so no new value needs to be emitted. */
            return;
        }
    }

    ecs_event_id_record_t *iders_set[5] = {0};
    int32_t ider_set_i, ider_set_count = 
        flecs_event_observers_get(er_onset, id, iders_set);
    if (!ider_set_count) {
        /* No OnSet observers for component */
        return;
    }

    it->ids[0] = id;
    it->event_id = id;
    it->trs[0] = flecs_component_get_table(cr, table);
    it->sources[0] = 0;

    /* Invoke OnSet observers for new inherited component value. */
    for (ider_set_i = 0; ider_set_i < ider_set_count; ider_set_i ++) {
        ecs_event_id_record_t *ider = iders_set[ider_set_i];
        flecs_observers_invoke(world, &ider->self, it, table, 0);
        ecs_assert(it->event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
        flecs_observers_invoke(world, &ider->self_up, it, table, 0);
        ecs_assert(it->event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
    }
}

static
void flecs_emit_on_set_for_override_on_remove(
    ecs_world_t *world,
    const ecs_event_record_t *er_onset,
    int32_t evtx,
    ecs_iter_t *it,
    ecs_id_t id,
    ecs_component_record_t *cr,
    ecs_table_t *table)
{
    (void)evtx;

    ecs_ref_t storage;
    const ecs_ref_t *o = flecs_table_get_override(world, table, id, cr, &storage);
    if (!o) {
        return;
    }

    ecs_event_id_record_t *iders_set[5] = {0};
    int32_t ider_set_i, ider_set_count = 
        flecs_event_observers_get(er_onset, id, iders_set);
    if (!ider_set_count) {
        /* No OnSet observers for component */
        return;
    }

    /* We're removing, so emit an OnSet for the base component. */
    ecs_entity_t base = o->entity;
    ecs_assert(base != 0, ECS_INTERNAL_ERROR,  NULL);
    ecs_record_t *base_r = flecs_entities_get(world, base);
    const ecs_table_record_t *base_tr = 
        flecs_component_get_table(cr, base_r->table);

    it->ids[0] = id;
    it->event_id = id;
    it->sources[0] = base;
    it->trs[0] = base_tr;
    it->up_fields = 1;

    /* Invoke OnSet observers for previous inherited component value. */
    for (ider_set_i = 0; ider_set_i < ider_set_count; ider_set_i ++) {
        ecs_event_id_record_t *ider = iders_set[ider_set_i];
        flecs_observers_invoke(world, &ider->self_up, it, table, EcsIsA);
        ecs_assert(it->event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
        flecs_observers_invoke(world, &ider->up, it, table, EcsIsA);
        ecs_assert(it->event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
    }
}

/* The emit function is responsible for finding and invoking the observers 
 * matching the emitted event. The function is also capable of forwarding events
 * for newly reachable ids (after adding a relationship) and propagating events
 * downwards. Both capabilities are not just useful in application logic, but
 * are also an important building block for keeping query caches in sync. */
void flecs_emit(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_event_desc_t *desc)
{
    flecs_poly_assert(world, ecs_world_t);
    ecs_check(desc != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->event != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->event != EcsWildcard, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->ids != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->ids->count != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->table != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->observable != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_os_perf_trace_push("flecs.emit");

    ecs_time_t t = {0};
    bool measure_time = world->flags & EcsWorldMeasureSystemTime;
    if (measure_time) {
        ecs_time_measure(&t);
    }

    const ecs_type_t *ids = desc->ids;
    ecs_entity_t event = desc->event;
    ecs_table_t *table = desc->table, *other_table = desc->other_table;
    int32_t offset = desc->offset;
    int32_t i, count = desc->count;
    ecs_flags32_t table_flags = table->flags;

    /* Deferring cannot be suspended for observers */
    int32_t defer = world->stages[0]->defer;
    if (defer < 0) {
        world->stages[0]->defer *= -1;
    }

    /* Table events are emitted for internal table operations only, and do not
     * provide component data and/or entity ids. */
    bool table_event = desc->flags & EcsEventTableOnly;
    if (!count && !table_event) {
        /* If no count is provided, forward event for all entities in table */
        count = ecs_table_count(table) - offset;
    }

    /* The world event id is used to determine if an observer has already been
     * triggered for an event. Observers for multiple components are split up
     * into multiple observers for a single component, and this counter is used
     * to make sure a multi observer only triggers once, even if multiple of its
     * single-component observers trigger. */
    int32_t evtx = ++world->event_id;

    ecs_id_t ids_cache = 0;
    ecs_size_t sizes_cache = 0;
    const ecs_table_record_t* trs_cache = 0;
    ecs_entity_t sources_cache = 0;

    ecs_iter_t it = {
        .world = stage,
        .real_world = world,
        .event = event,
        .event_cur = evtx,
        .table = table,
        .field_count = 1,
        .ids = &ids_cache,
        .sizes = &sizes_cache,
        .trs = (const ecs_table_record_t**)&trs_cache,
        .sources = &sources_cache,
        .other_table = other_table,
        .offset = offset,
        .count = count,
        .param = ECS_CONST_CAST(void*, desc->param),
        .flags = desc->flags | EcsIterIsValid
    };

    ecs_observable_t *observable = flecs_get_observable(desc->observable);
    ecs_check(observable != NULL, ECS_INVALID_PARAMETER, NULL);

    /* Event records contain all observers for a specific event. In addition to
     * the emitted event, also request data for the Wildcard event (for 
     * observers subscribing to the wildcard event), OnSet events. The
     * latter to are used for automatically emitting OnSet events for 
     * inherited components, for example when an IsA relationship is added to an
     * entity. This doesn't add much overhead, as fetching records is cheap for
     * builtin event types. */
    const ecs_event_record_t *er = flecs_event_record_get_if(observable, event);
    const ecs_event_record_t *wcer = flecs_event_record_get_if(observable, EcsWildcard);
    const ecs_event_record_t *er_onset = flecs_event_record_get_if(observable, EcsOnSet);

    if (count) {
        it.entities = &ecs_table_entities(table)[offset];
    }

    int32_t id_count = ids->count;
    ecs_id_t *id_array = ids->array;
    bool do_on_set = !(desc->flags & EcsEventNoOnSet);

    /* When we add an (IsA, b) pair we need to emit OnSet events for any new 
     * component values that are reachable through the instance, either 
     * inherited or overridden. OnSet events for inherited components are 
     * emitted by the event forwarding logic. For overriding, we only need to 
     * emit an OnSet if both the IsA pair and the component were added in the
     * same event. If a new override is added for an existing base component,
     * it changes the ownership of the component, but not the value, so no OnSet
     * is needed. */
    bool can_override_on_add = count && 
        do_on_set &&
        (event == EcsOnAdd) &&
        (table_flags & EcsTableHasIsA);

    /* If we remove an override, this reexposes the component from the base. 
     * Since the override could have a different value from the base, this 
     * effectively changes the value of the component for the entity, so an
     * OnSet event must be emitted. */
    bool can_override_on_remove = count &&
        do_on_set &&
        (event == EcsOnRemove) &&
        (it.other_table) &&
        (it.other_table->flags & EcsTableHasIsA);

    /* When a new (traversable) relationship is added (emitting an OnAdd/OnRemove
     * event) this will cause the components of the target entity to be 
     * propagated to the source entity. This makes it possible for observers to
     * get notified of any new reachable components though the relationship. */
    bool can_forward = event != EcsOnSet;

    /* Does table has observed entities */
    bool has_observed = table_flags & EcsTableHasTraversable;

    ecs_event_id_record_t *iders[5] = {0};
    ecs_table_record_t dummy_tr;

    if (count && can_forward && has_observed) {
        flecs_emit_propagate_invalidate(world, table, offset, count);
    }

repeat_event:
    /* This is the core event logic, which is executed for each event. By 
     * default this is just the event kind from the ecs_event_desc_t struct, but
     * can also include the Wildcard and UnSet events. The latter is emitted as
     * counterpart to OnSet, for any removed ids associated with data. */
    for (i = 0; i < id_count; i ++) {
        /* Emit event for each id passed to the function. In most cases this 
         * will just be one id, like a component that was added, removed or set.
         * In some cases events are emitted for multiple ids.
         * 
         * One example is when an id was added with a "With" property, or 
         * inheriting from a prefab with overrides. In these cases an entity is 
         * moved directly to the archetype with the additional components. */
        ecs_id_t id = id_array[i];

        /* If id is wildcard this could be a remove(Rel, *) call for a 
         * DontFragment component (for regular components this gets handled by
         * the table graph which returns a vector with removed ids).
         * This will be handled at a higher level than flecs_emit, so we can 
         * ignore the wildcard */
        if (id != EcsAny && ecs_id_is_wildcard(id)) {
            continue;
        }

        int32_t ider_i, ider_count = 0;
        ecs_component_record_t *cr = flecs_components_get(world, id);
        ecs_assert(cr != NULL, ECS_INTERNAL_ERROR, NULL);
        ecs_flags32_t cr_flags = cr->flags;

        /* Check if this id is a pair of an traversable relationship. If so, we 
         * may have to forward ids from the pair's target. */
        bool is_pair = ECS_IS_PAIR(id);
        if (can_forward && is_pair) {
            if (is_pair && (cr_flags & EcsIdTraversable)) {
                const ecs_event_record_t *er_fwd = NULL;
                if (ECS_PAIR_FIRST(id) == EcsIsA) {
                    if (event == EcsOnAdd) {
                        if (!world->stages[0]->base) {
                            /* Adding an IsA relationship can trigger prefab
                             * instantiation, which can instantiate prefab 
                             * hierarchies for the entity to which the 
                             * relationship was added. */
                            ecs_entity_t tgt = ECS_PAIR_SECOND(id);

                            /* Setting this value prevents flecs_instantiate 
                             * from being called recursively, in case prefab
                             * children also have IsA relationships. */
                            world->stages[0]->base = tgt;
                            const ecs_entity_t *instances = 
                                ecs_table_entities(table);
                            int32_t e;

                            for (e = 0; e < count; e ++) {
                                flecs_instantiate(
                                    world, tgt, instances[offset + e], NULL);
                            }

                            world->stages[0]->base = 0;
                        }

                        /* Adding an IsA relationship will emit OnSet events for
                         * any new reachable components. */
                        er_fwd = er_onset;
                    }
                }

                /* Forward events for components from pair target */
                flecs_emit_forward(world, er, er_fwd, ids, &it, table, cr);
                ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
            }
        }

        if (er) {
            /* Get observer sets for id. There can be multiple sets of matching
             * observers, in case an observer matches for wildcard ids. For
             * example, both observers for (ChildOf, p) and (ChildOf, *) would
             * match an event for (ChildOf, p). */
            ider_count = flecs_event_observers_get(er, id, iders);
            cr = cr ? cr : flecs_components_get(world, id);
            ecs_assert(cr != NULL, ECS_INTERNAL_ERROR, NULL);
        }

        if (!ider_count && !(can_override_on_add || can_override_on_remove)) {
            /* If nothing more to do for this id, early out */
            continue;
        }

        ecs_assert(cr != NULL, ECS_INTERNAL_ERROR, NULL);
        const ecs_table_record_t *tr = flecs_component_get_table(cr, table);
        dummy_tr = (ecs_table_record_t){
            .hdr.cr = cr,
            .hdr.table = table,
            .index = -1,
            .column = -1,
            .count = 0
        };

        bool dont_fragment = cr_flags & EcsIdDontFragment;
        if (!dont_fragment && id != EcsAny) {
            if (tr == NULL) {
                /* When a single batch contains multiple add's for an exclusive
                * relationship, it's possible that an id was in the added list
                * that is no longer available for the entity. */
                continue;
            }
        } else {
            /* When matching Any the table may not have a record for it */
            tr = &dummy_tr;
        }

        it.trs[0] = tr;
        ECS_CONST_CAST(int32_t*, it.sizes)[0] = 0; /* safe, owned by observer */
        it.event_id = id;
        it.ids[0] = id;

        /* Actually invoke observers for this event/id */
        for (ider_i = 0; ider_i < ider_count; ider_i ++) {
            ecs_event_id_record_t *ider = iders[ider_i];
            flecs_observers_invoke(world, &ider->self, &it, table, 0);
            ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
            flecs_observers_invoke(world, &ider->self_up, &it, table, 0);
            ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
        }

        if (!ider_count || !count || !has_observed) {
            continue;
        }

        /* The table->traversable_count value indicates if the table contains any
         * entities that are used as targets of traversable relationships. If the
         * entity/entities for which the event was generated is used as such a
         * target, events must be propagated downwards. */
        flecs_propagate_entities(
            world, &it, cr, it.entities, count, 0, iders, ider_count);
    }

    can_forward = false; /* Don't forward twice */

    if (wcer && er != wcer) {
        /* Repeat event loop for Wildcard event */
        er = wcer;
        it.event = event;
        goto repeat_event;
    }

    /* Invoke OnSet observers for component overrides if necessary */
    if (count && (can_override_on_add|can_override_on_remove)) {
        for (i = 0; i < id_count; i ++) {
            ecs_id_t id = id_array[i];

            bool non_trivial_set = true;
            if (id < FLECS_HI_COMPONENT_ID) {
                non_trivial_set = world->non_trivial_set[id];
            }

            if (non_trivial_set) {
                ecs_component_record_t *cr = flecs_components_get(world, id);
                ecs_assert(cr != NULL, ECS_INTERNAL_ERROR, NULL);
                const ecs_type_info_t *ti = cr->type_info;;
                ecs_flags32_t cr_flags = cr->flags;

                /* Can only override components that don't have DontInherit trait. */
                bool id_can_override_on_add = can_override_on_add;
                bool id_can_override_on_remove = can_override_on_remove;
                id_can_override_on_add &= !(cr_flags & EcsIdOnInstantiateDontInherit);
                id_can_override_on_remove &= !(cr_flags & EcsIdOnInstantiateDontInherit);
                id_can_override_on_add &= ti != NULL;
                id_can_override_on_remove &= ti != NULL;

                if (id_can_override_on_add) {
                    flecs_emit_on_set_for_override_on_add(
                        world, er_onset, evtx, &it, id, cr, table);
                } else if (id_can_override_on_remove) {
                    flecs_emit_on_set_for_override_on_remove(
                        world, er_onset, evtx, &it, id, cr, table);
                }
            }
        }
    }

error:
    world->stages[0]->defer = defer;

    ecs_os_perf_trace_pop("flecs.emit");

    if (measure_time) {
        world->info.emit_time_total += (ecs_ftime_t)ecs_time_measure(&t);
    }
    return;
}

void ecs_emit(
    ecs_world_t *stage,
    ecs_event_desc_t *desc)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(stage));
    ecs_check(desc != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(!(desc->param && desc->const_param), ECS_INVALID_PARAMETER, 
        "cannot set param and const_param at the same time");

    if (desc->entity) {
        ecs_assert(desc->table == NULL, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(desc->offset == 0, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(desc->count == 0, ECS_INVALID_PARAMETER, NULL);
        ecs_record_t *r = flecs_entities_get(world, desc->entity);
        desc->table = r->table;
        desc->offset = ECS_RECORD_TO_ROW(r->row);
        desc->count = 1;
    }

    if (!desc->observable) {
        desc->observable = world;
    }

    ecs_type_t default_ids = (ecs_type_t){ 
        .count = 1, 
        .array = (ecs_id_t[]){ EcsAny }
    };

    if (!desc->ids || !desc->ids->count) {
        desc->ids = &default_ids;
    }

    if (desc->const_param) {
        desc->param = ECS_CONST_CAST(void*, desc->const_param);
        desc->const_param = NULL;
    }

    ecs_defer_begin(world);
    flecs_emit(world, stage, desc);
    ecs_defer_end(world);

    if (desc->ids == &default_ids) {
        desc->ids = NULL;
    }
error:
    return;
}

void ecs_enqueue(
    ecs_world_t *world,
    ecs_event_desc_t *desc)
{
    if (!ecs_is_deferred(world)) {
        ecs_emit(world, desc);
        return;
    }

    ecs_stage_t *stage = flecs_stage_from_world(&world);
    flecs_enqueue(world, stage, desc);
}
