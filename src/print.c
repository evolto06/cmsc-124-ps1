/*
 * print.c: Complete value printer.
 *
 * The grading corpus compares stdout byte for byte. This file defines each
 * value format. Do not change this file.
 */

#include "driver.h"

#include <stdio.h>

const char *dt_status_message(dt_status status)
{
    switch (status) {
    case DT_OK:            return "ok";
    case DT_ERR_OVERFLOW:  return "Integer operation overflowed.";
    case DT_ERR_RANGE:     return "Out of range.";
    case DT_ERR_KEY:       return "No such key.";
    case DT_ERR_FIELD:     return "No such field.";
    case DT_ERR_TAG:       return "Value holds a different alternative.";
    case DT_ERR_EMPTY:     return "Empty list has no car or cdr.";
    case DT_ERR_CAPACITY:  return "Requested capacity is not available.";
    case DT_ERR_RELEASED:  return "Reference was already released.";
    case DT_ERR_LEAK:      return "Reference was never released.";
    }
    return "Unknown status.";
}

const char *dt_tag_name(dt_tag tag)
{
    switch (tag) {
    case DT_NIL:    return "nil";
    case DT_INT:    return "int";
    case DT_STR:    return "str";
    case DT_ENUM:   return "enum";
    case DT_ARRAY:  return "array";
    case DT_MAP:    return "map";
    case DT_RECORD: return "record";
    case DT_TUPLE:  return "tuple";
    case DT_LIST:   return "list";
    case DT_REF:    return "ref";
    }
    return "?";
}

static void print_string(const dt_str *s)
{
    size_t length = dt_str_len(s);
    const char *bytes = dt_str_bytes(s);

    putchar('"');
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (c < 0x20 || c == 0x7f) {
                printf("\\x%02x", c);
            } else {
                putchar((int)c);
            }
        }
    }
    putchar('"');
}

void dt_print_value(dt_value v)
{
    switch (v.tag) {
    case DT_NIL:
        fputs("nil", stdout);
        break;

    case DT_INT:
        printf("%lld", v.as.integer);
        break;

    case DT_STR:
        print_string(v.as.string);
        break;

    case DT_ENUM: {
        const char *name = NULL;
        if (dt_enum_name(v.as.ordinal, &name) == DT_OK) {
            fputs(name, stdout);
        } else {
            printf("enum(%d)", v.as.ordinal);
        }
        break;
    }

    case DT_ARRAY: {
        const dt_array *a = v.as.array;
        size_t length = dt_array_len(a);
        long long lower = dt_array_lower_bound(a);

        putchar('[');
        for (size_t i = 0; i < length; i++) {
            dt_value element;
            if (i > 0) {
                fputs(", ", stdout);
            }
            if (dt_array_get(a, lower + (long long)i, &element) == DT_OK) {
                dt_print_value(element);
            } else {
                fputs("?", stdout);
            }
        }
        putchar(']');
        break;
    }

    case DT_MAP: {
        const dt_map *m = v.as.map;
        size_t length = dt_map_len(m);

        putchar('{');
        for (size_t i = 0; i < length; i++) {
            const char *key = NULL;
            dt_value bound;

            if (i > 0) {
                fputs(", ", stdout);
            }
            if (dt_map_key_at(m, i, &key) != DT_OK) {
                fputs("?", stdout);
                continue;
            }
            printf("\"%s\" -> ", key);
            if (dt_map_get(m, key, &bound) == DT_OK) {
                dt_print_value(bound);
            } else {
                fputs("?", stdout);
            }
        }
        putchar('}');
        break;
    }

    case DT_RECORD: {
        const dt_record *r = v.as.record;
        size_t count = dt_record_field_count(r);

        putchar('{');
        for (size_t i = 0; i < count; i++) {
            const char *field = NULL;
            dt_value bound;

            if (i > 0) {
                fputs(", ", stdout);
            }
            if (dt_record_field_name(r, i, &field) != DT_OK) {
                fputs("?", stdout);
                continue;
            }
            printf("%s = ", field);
            if (dt_record_get(r, field, &bound) == DT_OK) {
                dt_print_value(bound);
            } else {
                fputs("?", stdout);
            }
        }
        putchar('}');
        break;
    }

    case DT_TUPLE: {
        const dt_tuple *t = v.as.tuple;
        size_t arity = dt_tuple_arity(t);

        putchar('(');
        for (size_t i = 0; i < arity; i++) {
            dt_value element;
            if (i > 0) {
                fputs(", ", stdout);
            }
            if (dt_tuple_at(t, i, &element) == DT_OK) {
                dt_print_value(element);
            } else {
                fputs("?", stdout);
            }
        }
        putchar(')');
        break;
    }

    case DT_LIST: {
        const dt_list *cell = v.as.list;
        bool first = true;

        putchar('(');
        while (cell != NULL) {
            dt_value head;
            dt_list *tail = NULL;

            if (!first) {
                putchar(' ');
            }
            first = false;

            if (dt_list_car(cell, &head) != DT_OK) {
                break;
            }
            dt_print_value(head);
            if (dt_list_cdr(cell, &tail) != DT_OK) {
                break;
            }
            cell = tail;
        }
        putchar(')');
        break;
    }

    case DT_REF: {
        const dt_ref *p = v.as.ref;
        dt_value pointee;

        if (dt_ref_borrow(p, &pointee) == DT_OK) {
            fputs("ref(", stdout);
            dt_print_value(pointee);
            putchar(')');
        } else {
            fputs("ref(released)", stdout);
        }
        break;
    }
    }
}

void dt_print_line(dt_value v)
{
    dt_print_value(v);
    putchar('\n');
}
