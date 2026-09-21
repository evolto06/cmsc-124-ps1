/*
 * dt.h: CMSC 124 Problem Set 1 data types.
 *
 * Do not edit this header. The driver uses these declarations.
 * A declaration change breaks the build.
 *
 * Implement the ten dt_*.c files in src/. Each file implements one category
 * from Unit 5.
 *
 * The complete interface uses two rules.
 *
 * 1. Each operation that can fail returns dt_status. A special data value
 *    cannot report failure because that value can also be a valid result.
 *
 * 2. Each module owns its representation storage. Containers borrow the
 *    objects in their dt_value elements. The driver releases these objects.
 *    Only dt_ref manages object ownership.
 */

#ifndef DT_H
#define DT_H

#ifdef __cplusplus
#error "CMSC 124 Problem Set 1 is a C17 assignment. Do not compile it as C++."
#endif

#include <stdbool.h>
#include <stddef.h>

/* Status codes. */

/*
 * Each failure has one code. The driver writes its message to stderr and exits
 * with code 70. The data-type modules do not print.
 */
typedef enum {
    DT_OK = 0,
    DT_ERR_OVERFLOW,  /* a checked integer operation left the usable range */
    DT_ERR_RANGE,     /* an index or an ordinal fell outside its bounds */
    DT_ERR_KEY,       /* an associative array has no such key */
    DT_ERR_FIELD,     /* a record has no field with that name */
    DT_ERR_TAG,       /* a reader received the wrong value category */
    DT_ERR_EMPTY,     /* car or cdr received the empty list */
    DT_ERR_CAPACITY,  /* a requested allocation or representation is unavailable */
    DT_ERR_RELEASED,  /* an operation received a released reference */
    DT_ERR_LEAK       /* a reference was still holding memory when the program ended */
} dt_status;

/* The text the driver prints for a status. */
const char *dt_status_message(dt_status status);

/* Tagged union. */

typedef enum {
    DT_NIL = 0,
    DT_INT,
    DT_STR,
    DT_ENUM,
    DT_ARRAY,
    DT_MAP,
    DT_RECORD,
    DT_TUPLE,
    DT_LIST,
    DT_REF
} dt_tag;

typedef struct dt_str    dt_str;
typedef struct dt_array  dt_array;
typedef struct dt_map    dt_map;
typedef struct dt_record dt_record;
typedef struct dt_tuple  dt_tuple;
typedef struct dt_list   dt_list;
typedef struct dt_ref    dt_ref;

/*
 * The value type has ten alternatives. Its tag identifies the active
 * alternative. Containers store dt_value objects.
 */
typedef struct {
    dt_tag tag;
    union {
        long long  integer; /* DT_INT  */
        int        ordinal; /* DT_ENUM */
        dt_str    *string;  /* DT_STR  */
        dt_array  *array;   /* DT_ARRAY */
        dt_map    *map;     /* DT_MAP  */
        dt_record *record;  /* DT_RECORD */
        dt_tuple  *tuple;   /* DT_TUPLE */
        dt_list   *list;    /* DT_LIST */
        dt_ref    *ref;     /* DT_REF  */
    } as;
} dt_value;

/* These constructors cannot fail and return a value directly. */
dt_value dt_value_nil(void);
dt_value dt_value_int(long long n);
dt_value dt_value_enum(int ordinal);
dt_value dt_value_str(dt_str *s);
dt_value dt_value_array(dt_array *a);
dt_value dt_value_map(dt_map *m);
dt_value dt_value_record(dt_record *r);
dt_value dt_value_tuple(dt_tuple *t);
dt_value dt_value_list(dt_list *l);
dt_value dt_value_ref(dt_ref *p);

/* Return the tag text for messages and the tag command. */
const char *dt_tag_name(dt_tag tag);

/*
 * Each reader checks the tag before it reads the payload. It returns
 * DT_ERR_TAG when another alternative is active.
 */
dt_status dt_value_as_int(dt_value v, long long *out);
dt_status dt_value_as_enum(dt_value v, int *out);
dt_status dt_value_as_str(dt_value v, dt_str **out);

/* Checked integers. */

/*
 * C gives signed overflow undefined behavior. These functions detect overflow
 * before the arithmetic operation.
 *
 * The functions do not write *out when they return DT_ERR_OVERFLOW.
 */
dt_status dt_int_add(long long a, long long b, long long *out);
dt_status dt_int_sub(long long a, long long b, long long *out);
dt_status dt_int_mul(long long a, long long b, long long *out);

/* Strings. */

/*
 * A string stores its length with its bytes. The data can include a zero byte.
 * dt_str_len reads the stored length.
 */
dt_str   *dt_str_new(const char *bytes, size_t length);
void      dt_str_free(dt_str *s);
size_t    dt_str_len(const dt_str *s);
const char *dt_str_bytes(const dt_str *s); /* Use dt_str_len for the data size. */
dt_status dt_str_append(dt_str *s, const char *bytes, size_t length);
dt_status dt_str_substr(const dt_str *s, size_t start, size_t length, dt_str **out);
bool      dt_str_eq(const dt_str *a, const dt_str *b);

/* Enumerations. */

/*
 * The enumeration contains a fixed set of named values. Reject ordinals that
 * are outside this set.
 */
typedef enum {
    DT_COLOR_RED = 0,
    DT_COLOR_GREEN,
    DT_COLOR_BLUE,
    DT_COLOR_COUNT
} dt_color;

bool      dt_enum_is_valid(int ordinal);
dt_status dt_enum_name(int ordinal, const char **out);
dt_status dt_enum_from_name(const char *name, int *out);

/* Arrays. */

/*
 * An array descriptor stores its elements, length, and lower bound. Each index
 * must fit in long long. Access computes the offset without signed overflow.
 */
dt_array *dt_array_new(size_t length, long long lower_bound);
void      dt_array_free(dt_array *a);
size_t    dt_array_len(const dt_array *a);
long long dt_array_lower_bound(const dt_array *a);
dt_status dt_array_get(const dt_array *a, long long index, dt_value *out);
dt_status dt_array_set(dt_array *a, long long index, dt_value v);

/* Associative arrays. */

/*
 * Keys select values. The map stores each key. Traversal uses insertion order
 * to give stable output. A removed and reinserted key moves to the end.
 */
dt_map   *dt_map_new(void);
void      dt_map_free(dt_map *m);
size_t    dt_map_len(const dt_map *m);
dt_status dt_map_put(dt_map *m, const char *key, dt_value v);
dt_status dt_map_get(const dt_map *m, const char *key, dt_value *out);
dt_status dt_map_remove(dt_map *m, const char *key);
/* Return the key at this insertion position or DT_ERR_RANGE. */
dt_status dt_map_key_at(const dt_map *m, size_t index, const char **out);

/* Records. */

#define DT_RECORD_MAX_FIELDS 8

/*
 * This record searches its field-name table at run time. An undeclared field
 * returns DT_ERR_FIELD. A record cannot add fields after construction.
 */
dt_record *dt_record_new(const char **field_names, size_t field_count);
void       dt_record_free(dt_record *r);
size_t     dt_record_field_count(const dt_record *r);
dt_status  dt_record_field_name(const dt_record *r, size_t index, const char **out);
dt_status  dt_record_get(const dt_record *r, const char *field, dt_value *out);
dt_status  dt_record_set(dt_record *r, const char *field, dt_value v);

/* Tuples. */

#define DT_TUPLE_MAX_ARITY 8

/*
 * A tuple selects its parts by position. Construction sets all parts. Later
 * operations can only read them by index.
 */
dt_tuple *dt_tuple_new(const dt_value *values, size_t count);
void      dt_tuple_free(dt_tuple *t);
size_t    dt_tuple_arity(const dt_tuple *t);
dt_status dt_tuple_at(const dt_tuple *t, size_t index, dt_value *out);

/* Lists. */

/*
 * A list contains cons cells. dt_list_nil returns a null pointer. An empty list
 * uses no memory. dt_list_cdr returns the shared tail.
 */
dt_list  *dt_list_nil(void);
dt_list  *dt_list_cons(dt_value head, dt_list *tail);
void      dt_list_free(dt_list *l); /* Free one cell and preserve its tail. */
size_t    dt_list_len(const dt_list *l);
dt_status dt_list_car(const dt_list *l, dt_value *out);
dt_status dt_list_cdr(const dt_list *l, dt_list **out);

/* Owned references. */

/*
 * dt_ref_new allocates a cell that contains a value copy. The reference owns
 * this cell. The environment owns objects referenced by the copied value.
 *
 * Detect access after release, repeated release, and an unreleased cell.
 */
dt_ref   *dt_ref_new(dt_value v);
dt_status dt_ref_borrow(const dt_ref *p, dt_value *out);
dt_status dt_ref_release(dt_ref *p);
bool      dt_ref_is_released(const dt_ref *p);
void      dt_ref_destroy(dt_ref *p); /* Free the handle. The driver calls this. */

#endif /* DT_H */
