/*
 * gml_api_string - bindings for the string built-ins (gm_string.h; Function_String.js).
 * Indices go through yyGetInt32 as in the runner.
 */
#include "gml_rt.h"

#include "gm_string.h"

double gml_fn_ord(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return gm_string_ord(a0);
}

const char *gml_fn_string_char_at(gm_instance_t *self, gm_instance_t *other, const char *a0, double a1)
{
    (void)self;
    (void)other;
    return gm_string_char_at(a0, gm_to_int32(a1));
}

const char *gml_fn_string_delete(gm_instance_t *self, gm_instance_t *other, const char *a0, double a1, double a2)
{
    (void)self;
    (void)other;
    return gm_string_delete(a0, gm_to_int32(a1), gm_to_int32(a2));
}

const char *gml_fn_string_insert(gm_instance_t *self, gm_instance_t *other, const char *a0, const char *a1, double a2)
{
    (void)self;
    (void)other;
    return gm_string_insert(a0, a1, gm_to_int32(a2));
}

double gml_fn_string_length(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return (double)gm_string_length(a0);
}

const char *gml_fn_string_lower(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return gm_string_lower(a0);
}

const char *gml_fn_string_upper(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return gm_string_upper(a0);
}

double gml_fn_string_pos(gm_instance_t *self, gm_instance_t *other, const char *a0, const char *a1)
{
    (void)self;
    (void)other;
    return (double)gm_string_pos(a0, a1);
}

const char *gml_fn_string_hash_to_newline(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return gm_string_hash_to_newline(a0);
}
