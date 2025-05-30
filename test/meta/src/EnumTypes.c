#include <meta.h>

static
void meta_test_enum(
    ecs_world_t *world, 
    ecs_entity_t t,
    int32_t count) 
{
    const EcsComponent *ct = ecs_get(world, t, EcsComponent);
    test_assert(ct != NULL);
    test_int(ct->size, ECS_SIZEOF(ecs_i32_t));
    test_int(ct->alignment, ECS_ALIGNOF(ecs_i32_t));

    const EcsType *mt = ecs_get(world, t, EcsType);
    test_assert(mt != NULL);
    test_assert(mt->kind == EcsEnumType);

    const EcsEnum *et = ecs_get(world, t, EcsEnum);
    test_assert(et != NULL);
    test_int(ecs_map_count(&et->constants), count);
}

static
void meta_test_constant(
    ecs_world_t *world, 
    ecs_entity_t t, 
    const char *name,
    int32_t value,
    int32_t order)
{
    ecs_entity_t m = ecs_lookup_child(world, t, name);
    test_assert(m != 0);
    test_assert(ecs_has_id(world, m, EcsConstant) || 
        ecs_has_pair(world, m, EcsConstant, EcsWildcard));

    const EcsEnum *et = ecs_get(world, t, EcsEnum);
    test_assert(et != NULL);

    ecs_map_iter_t it = ecs_map_iter(&et->constants);
    bool constant_found = false;
    while (ecs_map_next(&it)) {
        ecs_bitmask_constant_t *c = ecs_map_ptr(&it);
        ecs_map_key_t key = ecs_map_key(&it);

        test_int(c->value, key);
        test_assert(c->constant != 0);
        test_str(c->name, ecs_get_name(world, c->constant));

        if (!ecs_os_strcmp(c->name, name)) {
            test_int(c->value, value);

            const ecs_i32_t *vptr = ecs_get_pair_second(world, c->constant, 
                EcsConstant, ecs_i32_t);
            if (vptr) {
                test_int(*vptr, value);
            }

            constant_found = true;
        }
    }

    test_assert(constant_found == true);

    ecs_enum_constant_t* constants = ecs_vec_first(&et->ordered_constants);
    test_assert(constants != NULL);
    int32_t i, count = ecs_vec_count(&et->ordered_constants);
    for (i = 0; i < count; i++) {
        if (constants[i].value == value) {
            test_int(i, order);
            break;
        }
    }
}

void EnumTypes_enum_1_constant(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}
        }
    });

    test_assert(e != 0);

    meta_test_enum(world, e, 1);
    meta_test_constant(world, e, "Red", 0, 0);

    ecs_fini(world);
}

void EnumTypes_enum_2_constants(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}
        }
    });

    test_assert(e != 0);

    meta_test_enum(world, e, 2);
    meta_test_constant(world, e, "Red", 0, 0);
    meta_test_constant(world, e, "Blue", 1, 1);

    ecs_fini(world);
}

void EnumTypes_enum_3_constants(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}, {"Green"}
        }
    });

    test_assert(e != 0);

    meta_test_enum(world, e, 3);
    meta_test_constant(world, e, "Red", 0, 0);
    meta_test_constant(world, e, "Blue", 1, 1);
    meta_test_constant(world, e, "Green", 2, 2);

    ecs_fini(world);
}

void EnumTypes_enum_3_constants_manual_values(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red", 3}, {"Blue", 2}, {"Green", 1}
        }
    });

    test_assert(e != 0);

    meta_test_enum(world, e, 3);
    meta_test_constant(world, e, "Red", 3, 0);
    meta_test_constant(world, e, "Blue", 2, 1);
    meta_test_constant(world, e, "Green", 1, 2);

    ecs_fini(world);
}

void EnumTypes_struct_w_enum(void) {
    typedef struct {
        ecs_bool_t before;
        ecs_i32_t v;
        ecs_bool_t after;
    } T;

    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}, {"Green"}
        }
    });

    test_assert(e != 0);

    ecs_entity_t t = ecs_struct_init(world, &(ecs_struct_desc_t){
        .entity = ecs_entity(world, {.name = "T"}),
        .members = {
            {"before", ecs_id(ecs_bool_t)},
            {"v", e},
            {"after", ecs_id(ecs_bool_t)}
        }
    });

    test_assert(t != 0);
    test_str(ecs_get_name(world, t), "T");
    
    meta_test_struct(world, t, T);
    meta_test_member(world, t, T, before, ecs_id(ecs_bool_t), 1);
    meta_test_member(world, t, T, v, e, 1);
    meta_test_member(world, t, T, after, ecs_id(ecs_bool_t), 1);

    ecs_fini(world);
}

void EnumTypes_zero_initialized(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}, {"Green"}
        }
    });

    test_assert(e != 0);
    test_assert(ecs_has(world, e, EcsComponent));

    ecs_entity_t ent = ecs_new(world);
    ecs_add_id(world, ent, e);

    const int32_t *ptr = ecs_get_id(world, ent, e);
    test_assert(ptr != NULL);
    test_int(*ptr, 0);

    ecs_fini(world);
}

void EnumTypes_enum_relation(void) {
    ecs_world_t *world = ecs_init();

    enum Color {
        Red, Blue, Green
    };

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}, {"Green"}
        }
    });

    test_assert(e != 0);
    test_assert(ecs_has(world, e, EcsComponent));
    test_assert(ecs_has_id(world, e, EcsExclusive));
    test_assert(ecs_has_id(world, e, EcsPairIsTag));

    ecs_entity_t red = ecs_lookup_child(world, e, "Red");
    ecs_entity_t green = ecs_lookup_child(world, e, "Green");
    ecs_entity_t blue = ecs_lookup_child(world, e, "Blue");

    test_assert(red != 0);
    test_assert(green != 0);
    test_assert(blue != 0);

    test_assert(ecs_get_typeid(world, ecs_pair(e, red)) == 0);

    ecs_entity_t ent = ecs_new(world);
    ecs_add_pair(world, ent, e, red);
    test_assert( ecs_has_pair(world, ent, e, red));

    ecs_add_pair(world, ent, e, green);
    test_assert( ecs_has_pair(world, ent, e, green));
    test_assert( !ecs_has_pair(world, ent, e, red));

    ecs_add_pair(world, ent, e, blue);
    test_assert( ecs_has_pair(world, ent, e, blue));
    test_assert( !ecs_has_pair(world, ent, e, green));
    test_assert( !ecs_has_pair(world, ent, e, red));

    ecs_fini(world);
}

void EnumTypes_enum_w_short_notation(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t e = ecs_enum(world, {
        .constants = {
            {"Red"}, {"Blue"}, {"Green"}
        }
    });

    test_assert(e != 0);
    test_assert(ecs_has(world, e, EcsEnum));

    ecs_fini(world);
}

void EnumTypes_constant_w_name_prefix(void) {
    // Implement testcase
}

void EnumTypes_constant_w_type_prefix(void) {
    // Implement testcase
}

void EnumTypes_constant_w_name_type_prefix(void) {
    // Implement testcase
}


int enum_modified_calls = 0;

static
void enum_modified(ecs_iter_t *it) {
    enum_modified_calls ++;
}

/* Checks that observers watching enum changes are notified */
void EnumTypes_enum_modified_event(void) {
    ecs_world_t *world = ecs_init();

    ecs_observer(world, {
        .query.terms[0] = { .id = ecs_id(EcsEnum) },
        .events = {EcsOnSet},
        .callback = enum_modified
    });

    ecs_entity_t e = ecs_enum_init(world, &(ecs_enum_desc_t){
        .constants = {
            {"Red"}, {"Blue"}
        }
    });

    test_assert(e != 0);
    /* must receive two calls, one for each enum member added */
    test_int(enum_modified_calls, 3); 

    /* run-time add a new member constant to the enum: */
    ecs_entity_t old_scope = ecs_set_scope(world, e);
    ecs_entity_t c = ecs_entity(world, {
        .name = "Orange"
    });
    ecs_add_id(world, c, EcsConstant);
    ecs_set_scope(world, old_scope);

    /* check if observer was called after adding */
    /* a new member constant */
    test_int(enum_modified_calls, 4); 

    meta_test_enum(world, e, 3);
    meta_test_constant(world, e, "Red", 0, 0);
    meta_test_constant(world, e, "Blue", 1, 1);
    meta_test_constant(world, e, "Orange", 2, 2);

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_i8(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_i8_t),
        .constants = {
            {"Red"}, {"Blue"}, 
            {"Green", .value = INT8_MAX }, {"Yellow", .value = INT8_MIN }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, 1);
    test_int(c->alignment, 1);

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_i8_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i8_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i8_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT8_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value, INT8_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i8_t);
        test_assert(v != NULL);
        test_int(*v, INT8_MAX);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT8_MIN);
        test_assert(ec != NULL);
        test_str(ec->name, "Yellow");
        test_int(ec->value, INT8_MIN);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Yellow");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i8_t);
        test_assert(v != NULL);
        test_int(*v, INT8_MIN);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_i16(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_i16_t),
        .constants = {
            {"Red"}, {"Blue"}, 
            {"Green", .value = INT16_MAX }, {"Yellow", .value = INT16_MIN }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_i16_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_i16_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_i16_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i16_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i16_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT16_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value, INT16_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i16_t);
        test_assert(v != NULL);
        test_int(*v, INT16_MAX);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT16_MIN);
        test_assert(ec != NULL);
        test_str(ec->name, "Yellow");
        test_int(ec->value, INT16_MIN);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Yellow");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i16_t);
        test_assert(v != NULL);
        test_int(*v, INT16_MIN);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_i32(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_i32_t),
        .constants = {
            {"Red"}, {"Blue"}, 
            {"Green", .value = INT32_MAX }, {"Yellow", .value = INT32_MIN }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_i32_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_i32_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_i32_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i32_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i32_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT32_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value, INT32_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i32_t);
        test_assert(v != NULL);
        test_int(*v, INT32_MAX);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT32_MIN);
        test_assert(ec != NULL);
        test_str(ec->name, "Yellow");
        test_int(ec->value, INT32_MIN);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Yellow");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i32_t);
        test_assert(v != NULL);
        test_int(*v, INT32_MIN);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_i64(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_i64_t),
        .constants = {
            {"Red"}, {"Blue"}, 
            {"Green", .value = INT64_MAX }, {"Yellow", .value = INT64_MIN }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_i64_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_i64_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_i64_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i64_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i64_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT64_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value, INT64_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i64_t);
        test_assert(v != NULL);
        test_int(*v, INT64_MAX);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, INT64_MIN);
        test_assert(ec != NULL);
        test_str(ec->name, "Yellow");
        test_int(ec->value, INT64_MIN);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Yellow");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_i64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i64_t);
        test_assert(v != NULL);
        test_int(*v, INT64_MIN);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_u8(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_u8_t),
        .constants = {
            {"Red"}, {"Blue"}, {"Green", .value_unsigned = UINT8_MAX }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_u8_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_u8_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_u8_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value_unsigned, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u8_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value_unsigned, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u8_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, UINT8_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value_unsigned, UINT8_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u8_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u8_t);
        test_assert(v != NULL);
        test_int(*v, UINT8_MAX);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_u16(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_u16_t),
        .constants = {
            {"Red"}, {"Blue"}, {"Green", .value_unsigned = UINT16_MAX }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_u16_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_u16_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_u16_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value_unsigned, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u16_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value_unsigned, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u16_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, UINT16_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value_unsigned, UINT16_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u16_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u16_t);
        test_assert(v != NULL);
        test_int(*v, UINT16_MAX);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_u32(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_u32_t),
        .constants = {
            {"Red"}, {"Blue"}, {"Green", .value_unsigned = UINT32_MAX }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_u32_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_u32_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_u32_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value_unsigned, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u32_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value_unsigned, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u32_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, UINT32_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value_unsigned, UINT32_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u32_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u32_t);
        test_assert(v != NULL);
        test_int(*v, UINT32_MAX);
    }

    ecs_fini(world);
}

void EnumTypes_enum_w_underlying_u64(void) {
    ecs_world_t *world = ecs_init();

    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .underlying_type = ecs_id(ecs_u64_t),
        .constants = {
            {"Red"}, {"Blue"}, {"Green", .value_unsigned = UINT64_MAX }
        }
    });

    test_assert(type != 0);

    const EcsComponent *c = ecs_get(world, type, EcsComponent);
    test_assert(c != NULL);
    test_int(c->size, ECS_SIZEOF(ecs_u64_t));
    test_int(c->alignment, ECS_ALIGNOF(ecs_u64_t));

    const EcsEnum *e = ecs_get(world, type, EcsEnum);
    test_assert(e != NULL);
    test_assert(e->underlying_type == ecs_id(ecs_u64_t));

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 0);
        test_assert(ec != NULL);
        test_str(ec->name, "Red");
        test_int(ec->value_unsigned, 0);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Red");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u64_t);
        test_assert(v != NULL);
        test_int(*v, 0);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, 1);
        test_assert(ec != NULL);
        test_str(ec->name, "Blue");
        test_int(ec->value_unsigned, 1);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Blue");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u64_t);
        test_assert(v != NULL);
        test_int(*v, 1);
    }

    {
        ecs_enum_constant_t *ec = ecs_map_get_deref(
            &e->constants, ecs_enum_constant_t, UINT64_MAX);
        test_assert(ec != NULL);
        test_str(ec->name, "Green");
        test_int(ec->value_unsigned, UINT64_MAX);

        ecs_entity_t constant = ecs_lookup_from(world, type, "Green");
        test_assert(constant != 0);
        test_uint(ec->constant, constant);

        const ecs_u64_t *v = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_u64_t);
        test_assert(v != NULL);
        test_int(*v, UINT64_MAX);
    }

    ecs_fini(world);
}
