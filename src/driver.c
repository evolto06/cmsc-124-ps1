/*
 * driver.c: Complete case-file front end.
 *
 * The driver reads, validates, and executes a case file. It uses the three
 * stages from Unit 3:
 *
 *   scan     Convert one line into tokens.
 *   parse    Check each command and each referenced name.
 *   execute  Run commands until one reports an error.
 *
 * A scan or parse failure exits with code 65 before execution. An execution
 * failure exits with code 70 after the preceding commands run.
 *
 * Do not change this file.
 */

#include "driver.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 10

/* Token data. */

typedef struct {
    char  *text;   /* Storage ends with NUL. The len field gives the data size. */
    size_t len;
    bool   quoted;
} token;

/* Command verbs. */

typedef enum {
    V_INT_ADD, V_INT_SUB, V_INT_MUL,
    V_STR_NEW, V_STR_APPEND, V_STR_SUBSTR, V_STR_LEN, V_STR_EQ,
    V_ENUM_OF, V_ENUM_NAME,
    V_ARR_NEW, V_ARR_SET, V_ARR_GET, V_ARR_LEN,
    V_MAP_NEW, V_MAP_PUT, V_MAP_GET, V_MAP_DEL, V_MAP_LEN,
    V_REC_NEW, V_REC_SET, V_REC_GET,
    V_TUP_NEW, V_TUP_AT, V_TUP_ARITY,
    V_LIST_NIL, V_LIST_CONS, V_LIST_CAR, V_LIST_CDR, V_LIST_LEN,
    V_REF_NEW, V_REF_BORROW, V_REF_RELEASE,
    V_AS_INT, V_AS_STR, V_AS_ENUM,
    V_TAG, V_PRINT
} verb_id;

/*
 * Each argument kind uses one letter:
 *   d  Define a name.
 *   n  Use a previously defined name.
 *   i  Read a signed integer.
 *   u  Read a nonnegative integer.
 *   v  Read a value literal.
 *   f  Read a field name or enumeration name.
 *   k  Read a quoted key.
 *   s  Read a quoted string.
 *   *  Repeat the preceding kind.
 */
typedef struct {
    const char *word1;
    const char *word2; /* NULL identifies a one-word verb. */
    verb_id     id;
    const char *kinds;
    int         min_args;
    int         max_args;
} verb_spec;

static const verb_spec VERBS[] = {
    { "int",  "add",     V_INT_ADD,     "ii",    2, 2 },
    { "int",  "sub",     V_INT_SUB,     "ii",    2, 2 },
    { "int",  "mul",     V_INT_MUL,     "ii",    2, 2 },

    { "str",  "new",     V_STR_NEW,     "ds",    2, 2 },
    { "str",  "append",  V_STR_APPEND,  "ns",    2, 2 },
    { "str",  "substr",  V_STR_SUBSTR,  "nuud",  4, 4 },
    { "str",  "len",     V_STR_LEN,     "n",     1, 1 },
    { "str",  "eq",      V_STR_EQ,      "nn",    2, 2 },

    { "enum", "of",      V_ENUM_OF,     "f",     1, 1 },
    { "enum", "name",    V_ENUM_NAME,   "i",     1, 1 },

    { "arr",  "new",     V_ARR_NEW,     "dui",   3, 3 },
    { "arr",  "set",     V_ARR_SET,     "niv",   3, 3 },
    { "arr",  "get",     V_ARR_GET,     "ni",    2, 2 },
    { "arr",  "len",     V_ARR_LEN,     "n",     1, 1 },

    { "map",  "new",     V_MAP_NEW,     "d",     1, 1 },
    { "map",  "put",     V_MAP_PUT,     "nkv",   3, 3 },
    { "map",  "get",     V_MAP_GET,     "nk",    2, 2 },
    { "map",  "del",     V_MAP_DEL,     "nk",    2, 2 },
    { "map",  "len",     V_MAP_LEN,     "n",     1, 1 },

    { "rec",  "new",     V_REC_NEW,     "df*",   2, 10 },
    { "rec",  "set",     V_REC_SET,     "nfv",   3, 3 },
    { "rec",  "get",     V_REC_GET,     "nf",    2, 2 },

    { "tup",  "new",     V_TUP_NEW,     "dv*",   1, 10 },
    { "tup",  "at",      V_TUP_AT,      "nu",    2, 2 },
    { "tup",  "arity",   V_TUP_ARITY,   "n",     1, 1 },

    { "list", "nil",     V_LIST_NIL,    "d",     1, 1 },
    { "list", "cons",    V_LIST_CONS,   "dvn",   3, 3 },
    { "list", "car",     V_LIST_CAR,    "n",     1, 1 },
    { "list", "cdr",     V_LIST_CDR,    "nd",    2, 2 },
    { "list", "len",     V_LIST_LEN,    "n",     1, 1 },

    { "ref",  "new",     V_REF_NEW,     "dv",    2, 2 },
    { "ref",  "borrow",  V_REF_BORROW,  "n",     1, 1 },
    { "ref",  "release", V_REF_RELEASE, "n",     1, 1 },

    { "as",   "int",     V_AS_INT,      "v",     1, 1 },
    { "as",   "str",     V_AS_STR,      "v",     1, 1 },
    { "as",   "enum",    V_AS_ENUM,     "v",     1, 1 },

    { "tag",  NULL,      V_TAG,         "v",     1, 1 },
    { "print", NULL,     V_PRINT,       "v",     1, 1 },
};

static const size_t VERB_COUNT = sizeof VERBS / sizeof VERBS[0];

/* Parsed commands. */

typedef struct {
    int     line;
    verb_id id;
    token   args[MAX_ARGS];
    int     argc;
} command;

/* Program state. */

typedef enum {
    REG_STR, REG_ARRAY, REG_MAP, REG_RECORD, REG_TUPLE, REG_LIST, REG_REF
} reg_kind;

typedef struct {
    reg_kind kind;
    void    *ptr;
} reg_entry;

typedef struct {
    char    *name;
    dt_value value;
} binding;

typedef struct {
    command  *commands;
    size_t    command_count;
    size_t    command_capacity;

    binding  *bindings;
    size_t    binding_count;
    size_t    binding_capacity;

    reg_entry *registry;
    size_t     registry_count;
    size_t     registry_capacity;
} program;

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (q == NULL) {
        fputs("Out of memory.\n", stderr);
        exit(70);
    }
    return q;
}

static char *xstrndup(const char *s, size_t n)
{
    char *copy = xrealloc(NULL, n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

static void registry_add(program *prog, reg_kind kind, void *ptr)
{
    if (prog->registry_count == prog->registry_capacity) {
        prog->registry_capacity = prog->registry_capacity ? prog->registry_capacity * 2 : 16;
        prog->registry = xrealloc(prog->registry,
                                  prog->registry_capacity * sizeof *prog->registry);
    }
    prog->registry[prog->registry_count].kind = kind;
    prog->registry[prog->registry_count].ptr = ptr;
    prog->registry_count++;
}

static binding *binding_find(program *prog, const char *name)
{
    for (size_t i = 0; i < prog->binding_count; i++) {
        if (strcmp(prog->bindings[i].name, name) == 0) {
            return &prog->bindings[i];
        }
    }
    return NULL;
}

static void binding_define(program *prog, const char *name, dt_value v)
{
    binding *existing = binding_find(prog, name);
    if (existing != NULL) {
        existing->value = v;
        return;
    }
    if (prog->binding_count == prog->binding_capacity) {
        prog->binding_capacity = prog->binding_capacity ? prog->binding_capacity * 2 : 16;
        prog->bindings = xrealloc(prog->bindings,
                                  prog->binding_capacity * sizeof *prog->bindings);
    }
    prog->bindings[prog->binding_count].name = xstrndup(name, strlen(name));
    prog->bindings[prog->binding_count].value = v;
    prog->binding_count++;
}

/* Scanner. */

static bool is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_identifier(const token *t)
{
    if (t->quoted || t->len == 0 || !is_ident_start(t->text[0])) {
        return false;
    }
    for (size_t i = 1; i < t->len; i++) {
        if (!is_ident_char(t->text[i])) {
            return false;
        }
    }
    return true;
}

static bool parse_integer(const char *text, long long *out)
{
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE) {
        return false;
    }
    *out = value;
    return true;
}

static bool is_integer_token(const token *t)
{
    long long ignored;
    return !t->quoted && t->len > 0 && parse_integer(t->text, &ignored);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void free_tokens(token *tokens, int count);

/*
 * Split one line into tokens. Return the token count. Return -1 and set *why
 * when a quoted string has invalid syntax. A token that starts with '#' is a comment.
 *
 * Release created tokens after a failure. This prevents a malformed line from
 * leaking memory. A sanitizer detects this leak.
 */
static int scan_line(const char *line, token *out, int max_tokens, const char **why)
{
    int count = 0;
    const char *p = line;

    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            return count;
        }
        if (count == max_tokens) {
            *why = "too many arguments";
            free_tokens(out, count);
            return -1;
        }

        char  *buffer = NULL;
        size_t len = 0;
        size_t capacity = 0;
        bool   quoted = false;

        if (*p == '"') {
            quoted = true;
            p++;
            for (;;) {
                if (*p == '\0') {
                    free(buffer);
                    *why = "unterminated string";
                    free_tokens(out, count);
                    return -1;
                }
                if (*p == '"') {
                    p++;
                    break;
                }

                char c = *p;
                if (c == '\\') {
                    p++;
                    switch (*p) {
                    case 'n':  c = '\n'; p++; break;
                    case 't':  c = '\t'; p++; break;
                    case '0':  c = '\0'; p++; break;
                    case '"':  c = '"';  p++; break;
                    case '\\': c = '\\'; p++; break;
                    case 'x': {
                        int hi = hex_digit(p[1]);
                        int lo = hi < 0 ? -1 : hex_digit(p[2]);
                        if (lo < 0) {
                            free(buffer);
                            *why = "bad \\x escape in string";
                            free_tokens(out, count);
                            return -1;
                        }
                        c = (char)(hi * 16 + lo);
                        p += 3;
                        break;
                    }
                    default:
                        free(buffer);
                        *why = "unknown escape in string";
                        free_tokens(out, count);
                        return -1;
                    }
                } else {
                    p++;
                }

                if (len + 1 >= capacity) {
                    capacity = capacity ? capacity * 2 : 16;
                    buffer = xrealloc(buffer, capacity);
                }
                buffer[len++] = c;
            }
        } else {
            const char *start = p;
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
                p++;
            }
            len = (size_t)(p - start);
            capacity = len + 1;
            buffer = xrealloc(NULL, capacity);
            memcpy(buffer, start, len);
        }

        if (buffer == NULL) {
            buffer = xrealloc(NULL, 1);
        }
        buffer[len] = '\0';

        out[count].text = buffer;
        out[count].len = len;
        out[count].quoted = quoted;
        count++;
    }
}

/* Parser. */

/* Store names that the parser has defined. */
typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} name_set;

static bool name_set_has(const name_set *set, const char *name)
{
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static void name_set_add(name_set *set, const char *name)
{
    if (set->count == set->capacity) {
        set->capacity = set->capacity ? set->capacity * 2 : 16;
        set->names = xrealloc(set->names, set->capacity * sizeof *set->names);
    }
    set->names[set->count++] = xstrndup(name, strlen(name));
}

static void name_set_free(name_set *set)
{
    for (size_t i = 0; i < set->count; i++) {
        free(set->names[i]);
    }
    free(set->names);
}

static const verb_spec *find_verb(const token *tokens, int count, int *words)
{
    if (count >= 2 && !tokens[0].quoted && !tokens[1].quoted) {
        for (size_t i = 0; i < VERB_COUNT; i++) {
            if (VERBS[i].word2 != NULL &&
                strcmp(VERBS[i].word1, tokens[0].text) == 0 &&
                strcmp(VERBS[i].word2, tokens[1].text) == 0) {
                *words = 2;
                return &VERBS[i];
            }
        }
    }
    if (count >= 1 && !tokens[0].quoted) {
        for (size_t i = 0; i < VERB_COUNT; i++) {
            if (VERBS[i].word2 == NULL &&
                strcmp(VERBS[i].word1, tokens[0].text) == 0) {
                *words = 1;
                return &VERBS[i];
            }
        }
    }
    return NULL;
}

/* Return the argument kind at `index`. Permit a trailing '*'. */
static char kind_at(const char *kinds, int index)
{
    int i = 0;
    char last = '\0';
    while (kinds[i] != '\0') {
        if (kinds[i] == '*') {
            return last;
        }
        if (i == index) {
            return kinds[i];
        }
        last = kinds[i];
        i++;
    }
    return (index >= i && i > 0 && strchr(kinds, '*') != NULL) ? last : '\0';
}

/* Validate the form. Execution validates enumeration names and keys. */
static bool value_literal_ok(const token *t, const name_set *defined, const char **why)
{
    if (t->quoted) {
        return true;
    }
    if (strcmp(t->text, "nil") == 0) {
        return true;
    }
    if (is_integer_token(t)) {
        return true;
    }
    if (strncmp(t->text, "enum:", 5) == 0) {
        if (t->text[5] == '\0') {
            *why = "enum literal needs a name";
            return false;
        }
        return true;
    }
    if (t->text[0] == '@') {
        if (t->text[1] == '\0') {
            *why = "'@' needs a name after it";
            return false;
        }
        if (!name_set_has(defined, t->text + 1)) {
            *why = "name is not defined yet";
            return false;
        }
        return true;
    }
    *why = "not a value literal";
    return false;
}

static void report_parse_error(int line, const char *why, const char *detail)
{
    if (detail != NULL) {
        fprintf(stderr, "[line %d] Parse error: %s: %s\n", line, why, detail);
    } else {
        fprintf(stderr, "[line %d] Parse error: %s\n", line, why);
    }
}

static void free_tokens(token *tokens, int count)
{
    for (int i = 0; i < count; i++) {
        free(tokens[i].text);
    }
}

static bool parse_file(program *prog, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Parse error: cannot open case file: %s\n", path);
        return false;
    }

    name_set defined = { 0 };
    char    *line = NULL;
    size_t   line_capacity = 0;
    int      line_number = 0;
    bool     ok = true;

    for (;;) {
        int c;
        size_t length = 0;

        for (;;) {
            c = fgetc(file);
            if (c == EOF || c == '\n') {
                break;
            }
            if (length + 1 >= line_capacity) {
                line_capacity = line_capacity ? line_capacity * 2 : 128;
                line = xrealloc(line, line_capacity);
            }
            line[length++] = (char)c;
        }
        if (c == EOF && length == 0) {
            break;
        }
        if (line_capacity == 0) {
            line_capacity = 1;
            line = xrealloc(line, line_capacity);
        }
        line[length] = '\0';
        line_number++;

        token tokens[MAX_ARGS + 2];
        const char *why = NULL;
        int token_count = scan_line(line, tokens, MAX_ARGS + 2, &why);

        if (token_count < 0) {
            report_parse_error(line_number, why, NULL);
            ok = false;
            break;
        }
        if (token_count == 0) {
            if (c == EOF) break;
            continue;
        }

        int words = 0;
        const verb_spec *spec = find_verb(tokens, token_count, &words);
        if (spec == NULL) {
            report_parse_error(line_number, "unknown command", tokens[0].text);
            free_tokens(tokens, token_count);
            ok = false;
            break;
        }

        int argc = token_count - words;
        if (argc < spec->min_args || argc > spec->max_args) {
            char verb[64];
            char detail[160];

            if (spec->word2 != NULL) {
                snprintf(verb, sizeof verb, "%s %s", spec->word1, spec->word2);
            } else {
                snprintf(verb, sizeof verb, "%s", spec->word1);
            }
            if (spec->min_args == spec->max_args) {
                snprintf(detail, sizeof detail, "'%s' takes exactly %d, got %d",
                         verb, spec->min_args, argc);
            } else {
                snprintf(detail, sizeof detail, "'%s' takes %d to %d, got %d",
                         verb, spec->min_args, spec->max_args, argc);
            }
            report_parse_error(line_number, "wrong number of arguments", detail);
            free_tokens(tokens, token_count);
            ok = false;
            break;
        }

        bool line_ok = true;
        for (int i = 0; i < argc && line_ok; i++) {
            const token *arg = &tokens[words + i];
            char kind = kind_at(spec->kinds, i);
            long long number = 0;
            const char *detail = NULL;

            switch (kind) {
            case 'd':
                if (!is_identifier(arg)) {
                    report_parse_error(line_number, "not a valid name", arg->text);
                    line_ok = false;
                } else if (name_set_has(&defined, arg->text)) {
                    report_parse_error(line_number, "name is already defined", arg->text);
                    line_ok = false;
                } else {
                    name_set_add(&defined, arg->text);
                }
                break;

            case 'n':
                if (!is_identifier(arg)) {
                    report_parse_error(line_number, "not a valid name", arg->text);
                    line_ok = false;
                } else if (!name_set_has(&defined, arg->text)) {
                    report_parse_error(line_number, "name is not defined", arg->text);
                    line_ok = false;
                }
                break;

            case 'i':
                if (!is_integer_token(arg)) {
                    report_parse_error(line_number, "not an integer", arg->text);
                    line_ok = false;
                }
                break;

            case 'u':
                if (!is_integer_token(arg) ||
                    !parse_integer(arg->text, &number) || number < 0) {
                    report_parse_error(line_number, "not a non-negative integer", arg->text);
                    line_ok = false;
                }
                break;

            case 'f':
                if (!is_identifier(arg)) {
                    report_parse_error(line_number, "not a valid name", arg->text);
                    line_ok = false;
                }
                break;

            case 'k':
            case 's':
                if (!arg->quoted) {
                    report_parse_error(line_number, "expected a quoted string", arg->text);
                    line_ok = false;
                }
                break;

            case 'v':
                if (!value_literal_ok(arg, &defined, &detail)) {
                    report_parse_error(line_number, detail, arg->text);
                    line_ok = false;
                }
                break;

            default:
                report_parse_error(line_number, "too many arguments", arg->text);
                line_ok = false;
                break;
            }
        }

        if (!line_ok) {
            free_tokens(tokens, token_count);
            ok = false;
            break;
        }

        if (prog->command_count == prog->command_capacity) {
            prog->command_capacity = prog->command_capacity ? prog->command_capacity * 2 : 32;
            prog->commands = xrealloc(prog->commands,
                                      prog->command_capacity * sizeof *prog->commands);
        }

        command *cmd = &prog->commands[prog->command_count++];
        cmd->line = line_number;
        cmd->id = spec->id;
        cmd->argc = argc;
        for (int i = 0; i < argc; i++) {
            cmd->args[i] = tokens[words + i];
        }
        free_tokens(tokens, words); /* Release the verb words. */

        if (c == EOF) {
            break;
        }
    }

    free(line);
    name_set_free(&defined);
    fclose(file);
    return ok;
}

/* Executor. */

static dt_status resolve_value(program *prog, const token *t, dt_value *out)
{
    if (t->quoted) {
        dt_str *s = dt_str_new(t->text, t->len);
        if (s == NULL) {
            return DT_ERR_CAPACITY;
        }
        registry_add(prog, REG_STR, s);
        *out = dt_value_str(s);
        return DT_OK;
    }
    if (strcmp(t->text, "nil") == 0) {
        *out = dt_value_nil();
        return DT_OK;
    }
    if (strncmp(t->text, "enum:", 5) == 0) {
        int ordinal = 0;
        dt_status status = dt_enum_from_name(t->text + 5, &ordinal);
        if (status != DT_OK) {
            return status;
        }
        *out = dt_value_enum(ordinal);
        return DT_OK;
    }
    if (t->text[0] == '@') {
        binding *b = binding_find(prog, t->text + 1);
        if (b == NULL) {
            return DT_ERR_KEY;
        }
        *out = b->value;
        return DT_OK;
    }

    long long number = 0;
    if (!parse_integer(t->text, &number)) {
        return DT_ERR_TAG;
    }
    *out = dt_value_int(number);
    return DT_OK;
}

static dt_status bound_value(program *prog, const token *t, dt_value *out)
{
    binding *b = binding_find(prog, t->text);
    if (b == NULL) {
        return DT_ERR_KEY;
    }
    *out = b->value;
    return DT_OK;
}

static dt_status bound_of_tag(program *prog, const token *t, dt_tag tag, dt_value *out)
{
    dt_status status = bound_value(prog, t, out);
    if (status != DT_OK) {
        return status;
    }
    if (out->tag != tag) {
        return DT_ERR_TAG;
    }
    return DT_OK;
}

static void print_size(size_t n)
{
    printf("%zu\n", n);
}

static dt_status execute(program *prog, const command *cmd)
{
    dt_status status = DT_OK;
    dt_value  a;
    dt_value  b;
    dt_value  result;
    long long lhs = 0;
    long long rhs = 0;

    switch (cmd->id) {
    case V_INT_ADD:
    case V_INT_SUB:
    case V_INT_MUL: {
        long long out = 0;
        parse_integer(cmd->args[0].text, &lhs);
        parse_integer(cmd->args[1].text, &rhs);
        if (cmd->id == V_INT_ADD) status = dt_int_add(lhs, rhs, &out);
        else if (cmd->id == V_INT_SUB) status = dt_int_sub(lhs, rhs, &out);
        else status = dt_int_mul(lhs, rhs, &out);
        if (status != DT_OK) return status;
        printf("%lld\n", out);
        return DT_OK;
    }

    case V_STR_NEW: {
        dt_str *s = dt_str_new(cmd->args[1].text, cmd->args[1].len);
        if (s == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_STR, s);
        binding_define(prog, cmd->args[0].text, dt_value_str(s));
        return DT_OK;
    }

    case V_STR_APPEND:
        status = bound_of_tag(prog, &cmd->args[0], DT_STR, &a);
        if (status != DT_OK) return status;
        return dt_str_append(a.as.string, cmd->args[1].text, cmd->args[1].len);

    case V_STR_SUBSTR: {
        long long start = 0;
        long long length = 0;
        dt_str *piece = NULL;

        status = bound_of_tag(prog, &cmd->args[0], DT_STR, &a);
        if (status != DT_OK) return status;
        parse_integer(cmd->args[1].text, &start);
        parse_integer(cmd->args[2].text, &length);

        status = dt_str_substr(a.as.string, (size_t)start, (size_t)length, &piece);
        if (status != DT_OK) return status;
        registry_add(prog, REG_STR, piece);
        binding_define(prog, cmd->args[3].text, dt_value_str(piece));
        return DT_OK;
    }

    case V_STR_LEN:
        status = bound_of_tag(prog, &cmd->args[0], DT_STR, &a);
        if (status != DT_OK) return status;
        print_size(dt_str_len(a.as.string));
        return DT_OK;

    case V_STR_EQ:
        status = bound_of_tag(prog, &cmd->args[0], DT_STR, &a);
        if (status != DT_OK) return status;
        status = bound_of_tag(prog, &cmd->args[1], DT_STR, &b);
        if (status != DT_OK) return status;
        puts(dt_str_eq(a.as.string, b.as.string) ? "true" : "false");
        return DT_OK;

    case V_ENUM_OF: {
        int ordinal = 0;
        status = dt_enum_from_name(cmd->args[0].text, &ordinal);
        if (status != DT_OK) return status;
        printf("%d\n", ordinal);
        return DT_OK;
    }

    case V_ENUM_NAME: {
        const char *name = NULL;
        parse_integer(cmd->args[0].text, &lhs);
        if (lhs < INT_MIN || lhs > INT_MAX) return DT_ERR_RANGE;
        status = dt_enum_name((int)lhs, &name);
        if (status != DT_OK) return status;
        puts(name);
        return DT_OK;
    }

    case V_ARR_NEW: {
        long long length = 0;
        long long lower = 0;
        parse_integer(cmd->args[1].text, &length);
        parse_integer(cmd->args[2].text, &lower);

        dt_array *array = dt_array_new((size_t)length, lower);
        if (array == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_ARRAY, array);
        binding_define(prog, cmd->args[0].text, dt_value_array(array));
        return DT_OK;
    }

    case V_ARR_SET:
        status = bound_of_tag(prog, &cmd->args[0], DT_ARRAY, &a);
        if (status != DT_OK) return status;
        parse_integer(cmd->args[1].text, &lhs);
        status = resolve_value(prog, &cmd->args[2], &b);
        if (status != DT_OK) return status;
        return dt_array_set(a.as.array, lhs, b);

    case V_ARR_GET:
        status = bound_of_tag(prog, &cmd->args[0], DT_ARRAY, &a);
        if (status != DT_OK) return status;
        parse_integer(cmd->args[1].text, &lhs);
        status = dt_array_get(a.as.array, lhs, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_ARR_LEN:
        status = bound_of_tag(prog, &cmd->args[0], DT_ARRAY, &a);
        if (status != DT_OK) return status;
        print_size(dt_array_len(a.as.array));
        return DT_OK;

    case V_MAP_NEW: {
        dt_map *map = dt_map_new();
        if (map == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_MAP, map);
        binding_define(prog, cmd->args[0].text, dt_value_map(map));
        return DT_OK;
    }

    case V_MAP_PUT:
        status = bound_of_tag(prog, &cmd->args[0], DT_MAP, &a);
        if (status != DT_OK) return status;
        status = resolve_value(prog, &cmd->args[2], &b);
        if (status != DT_OK) return status;
        return dt_map_put(a.as.map, cmd->args[1].text, b);

    case V_MAP_GET:
        status = bound_of_tag(prog, &cmd->args[0], DT_MAP, &a);
        if (status != DT_OK) return status;
        status = dt_map_get(a.as.map, cmd->args[1].text, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_MAP_DEL:
        status = bound_of_tag(prog, &cmd->args[0], DT_MAP, &a);
        if (status != DT_OK) return status;
        return dt_map_remove(a.as.map, cmd->args[1].text);

    case V_MAP_LEN:
        status = bound_of_tag(prog, &cmd->args[0], DT_MAP, &a);
        if (status != DT_OK) return status;
        print_size(dt_map_len(a.as.map));
        return DT_OK;

    case V_REC_NEW: {
        const char *fields[DT_RECORD_MAX_FIELDS + 2];
        size_t count = (size_t)cmd->argc - 1;

        if (count > DT_RECORD_MAX_FIELDS) return DT_ERR_CAPACITY;
        for (size_t i = 0; i < count; i++) {
            fields[i] = cmd->args[i + 1].text;
        }

        dt_record *record = dt_record_new(fields, count);
        if (record == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_RECORD, record);
        binding_define(prog, cmd->args[0].text, dt_value_record(record));
        return DT_OK;
    }

    case V_REC_SET:
        status = bound_of_tag(prog, &cmd->args[0], DT_RECORD, &a);
        if (status != DT_OK) return status;
        status = resolve_value(prog, &cmd->args[2], &b);
        if (status != DT_OK) return status;
        return dt_record_set(a.as.record, cmd->args[1].text, b);

    case V_REC_GET:
        status = bound_of_tag(prog, &cmd->args[0], DT_RECORD, &a);
        if (status != DT_OK) return status;
        status = dt_record_get(a.as.record, cmd->args[1].text, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_TUP_NEW: {
        dt_value values[DT_TUPLE_MAX_ARITY + 2];
        size_t count = (size_t)cmd->argc - 1;

        if (count > DT_TUPLE_MAX_ARITY) return DT_ERR_CAPACITY;
        for (size_t i = 0; i < count; i++) {
            status = resolve_value(prog, &cmd->args[i + 1], &values[i]);
            if (status != DT_OK) return status;
        }

        dt_tuple *tuple = dt_tuple_new(values, count);
        if (tuple == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_TUPLE, tuple);
        binding_define(prog, cmd->args[0].text, dt_value_tuple(tuple));
        return DT_OK;
    }

    case V_TUP_AT:
        status = bound_of_tag(prog, &cmd->args[0], DT_TUPLE, &a);
        if (status != DT_OK) return status;
        parse_integer(cmd->args[1].text, &lhs);
        status = dt_tuple_at(a.as.tuple, (size_t)lhs, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_TUP_ARITY:
        status = bound_of_tag(prog, &cmd->args[0], DT_TUPLE, &a);
        if (status != DT_OK) return status;
        print_size(dt_tuple_arity(a.as.tuple));
        return DT_OK;

    case V_LIST_NIL:
        binding_define(prog, cmd->args[0].text, dt_value_list(dt_list_nil()));
        return DT_OK;

    case V_LIST_CONS: {
        status = resolve_value(prog, &cmd->args[1], &a);
        if (status != DT_OK) return status;
        status = bound_of_tag(prog, &cmd->args[2], DT_LIST, &b);
        if (status != DT_OK) return status;

        dt_list *cell = dt_list_cons(a, b.as.list);
        if (cell == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_LIST, cell);
        binding_define(prog, cmd->args[0].text, dt_value_list(cell));
        return DT_OK;
    }

    case V_LIST_CAR:
        status = bound_of_tag(prog, &cmd->args[0], DT_LIST, &a);
        if (status != DT_OK) return status;
        status = dt_list_car(a.as.list, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_LIST_CDR: {
        dt_list *tail = NULL;
        status = bound_of_tag(prog, &cmd->args[0], DT_LIST, &a);
        if (status != DT_OK) return status;
        status = dt_list_cdr(a.as.list, &tail);
        if (status != DT_OK) return status;
        binding_define(prog, cmd->args[1].text, dt_value_list(tail));
        return DT_OK;
    }

    case V_LIST_LEN:
        status = bound_of_tag(prog, &cmd->args[0], DT_LIST, &a);
        if (status != DT_OK) return status;
        print_size(dt_list_len(a.as.list));
        return DT_OK;

    case V_REF_NEW: {
        status = resolve_value(prog, &cmd->args[1], &a);
        if (status != DT_OK) return status;

        dt_ref *ref = dt_ref_new(a);
        if (ref == NULL) return DT_ERR_CAPACITY;
        registry_add(prog, REG_REF, ref);
        binding_define(prog, cmd->args[0].text, dt_value_ref(ref));
        return DT_OK;
    }

    case V_REF_BORROW:
        status = bound_of_tag(prog, &cmd->args[0], DT_REF, &a);
        if (status != DT_OK) return status;
        status = dt_ref_borrow(a.as.ref, &result);
        if (status != DT_OK) return status;
        dt_print_line(result);
        return DT_OK;

    case V_REF_RELEASE:
        status = bound_of_tag(prog, &cmd->args[0], DT_REF, &a);
        if (status != DT_OK) return status;
        return dt_ref_release(a.as.ref);

    case V_AS_INT: {
        long long number = 0;
        status = resolve_value(prog, &cmd->args[0], &a);
        if (status != DT_OK) return status;
        status = dt_value_as_int(a, &number);
        if (status != DT_OK) return status;
        printf("%lld\n", number);
        return DT_OK;
    }

    case V_AS_ENUM: {
        int ordinal = 0;
        status = resolve_value(prog, &cmd->args[0], &a);
        if (status != DT_OK) return status;
        status = dt_value_as_enum(a, &ordinal);
        if (status != DT_OK) return status;
        printf("%d\n", ordinal);
        return DT_OK;
    }

    case V_AS_STR: {
        dt_str *s = NULL;
        status = resolve_value(prog, &cmd->args[0], &a);
        if (status != DT_OK) return status;
        status = dt_value_as_str(a, &s);
        if (status != DT_OK) return status;
        dt_print_line(dt_value_str(s));
        return DT_OK;
    }

    case V_TAG:
        status = resolve_value(prog, &cmd->args[0], &a);
        if (status != DT_OK) return status;
        puts(dt_tag_name(a.tag));
        return DT_OK;

    case V_PRINT:
        status = resolve_value(prog, &cmd->args[0], &a);
        if (status != DT_OK) return status;
        dt_print_line(a);
        return DT_OK;
    }

    return DT_OK;
}

/* Cleanup. */

static void program_free(program *prog)
{
    /* Use reverse creation order to preserve referenced cells during cleanup. */
    for (size_t i = prog->registry_count; i-- > 0;) {
        reg_entry *entry = &prog->registry[i];
        switch (entry->kind) {
        case REG_STR:    dt_str_free(entry->ptr);     break;
        case REG_ARRAY:  dt_array_free(entry->ptr);   break;
        case REG_MAP:    dt_map_free(entry->ptr);     break;
        case REG_RECORD: dt_record_free(entry->ptr);  break;
        case REG_TUPLE:  dt_tuple_free(entry->ptr);   break;
        case REG_LIST:   dt_list_free(entry->ptr);    break;
        case REG_REF:    dt_ref_destroy(entry->ptr);  break;
        }
    }
    free(prog->registry);

    for (size_t i = 0; i < prog->binding_count; i++) {
        free(prog->bindings[i].name);
    }
    free(prog->bindings);

    for (size_t i = 0; i < prog->command_count; i++) {
        for (int j = 0; j < prog->commands[i].argc; j++) {
            free(prog->commands[i].args[j].text);
        }
    }
    free(prog->commands);
}

/*
 * Report a reference that still owns its cell when the program ends.
 */
static bool report_leaks(program *prog)
{
    bool leaked = false;
    for (size_t i = 0; i < prog->binding_count; i++) {
        dt_value v = prog->bindings[i].value;
        if (v.tag == DT_REF && !dt_ref_is_released(v.as.ref)) {
            fprintf(stderr, "Runtime error: %s (%s)\n",
                    dt_status_message(DT_ERR_LEAK), prog->bindings[i].name);
            leaked = true;
        }
    }
    return leaked;
}

int dt_driver_run(const char *path)
{
    program prog = { 0 };
    int exit_code = 0;

    if (!parse_file(&prog, path)) {
        program_free(&prog);
        return 65;
    }

    for (size_t i = 0; i < prog.command_count; i++) {
        dt_status status = execute(&prog, &prog.commands[i]);
        if (status != DT_OK) {
            fflush(stdout);
            fprintf(stderr, "[line %d] Runtime error: %s\n",
                    prog.commands[i].line, dt_status_message(status));
            exit_code = 70;
            break;
        }
    }

    if (exit_code == 0) {
        fflush(stdout);
        if (report_leaks(&prog)) {
            exit_code = 70;
        }
    }

    program_free(&prog);
    return exit_code;
}
