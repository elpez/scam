#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "collector.h"
#include "scamval.h"

/*** SCAMVAL CONSTRUCTORS ***/
scamval* scamint(long long n) {
    scamval* ret = gc_new_scamval(SCAM_INT);
    ret->vals.n = n;
    return ret;
}

scamval* scamdec(double d) {
    scamval* ret = gc_new_scamval(SCAM_DEC);
    ret->vals.d = d;
    return ret;
}

scamval* scambool(int b) {
    scamval* ret = gc_new_scamval(SCAM_BOOL);
    ret->vals.n = b;
    return ret;
}

// Construct a value that is internally a sequence (lists and S-expressions)
static scamval* scam_internal_seq(int type) {
    scamval* ret = gc_new_scamval(type);
    ret->count = 0;
    ret->mem_size = 0;
    ret->vals.arr = NULL;
    return ret;
}

scamval* scamlist(void) {
    return scam_internal_seq(SCAM_LIST);
}

scamval* scamsexpr(void) {
    return scam_internal_seq(SCAM_SEXPR);
}

scamval* scamsexpr_from_vals(size_t n, ...) {
    va_list vlist;
    va_start(vlist, n);
    scamval* ret = gc_new_scamval(SCAM_SEXPR);
    ret->vals.arr = gc_malloc(n * sizeof *ret->vals.arr);
    for (int i = 0; i < n; i++) {
        ret->vals.arr[i] = va_arg(vlist, scamval*);
        gc_unset_root(ret->vals.arr[i]);
    }
    ret->count = n;
    ret->mem_size = n;
    va_end(vlist);
    return ret;
}

// Construct a value that is internally a string (strings, symbols and errors)
static scamval* scam_internal_str(int type, const char* s) {
    scamval* ret = gc_new_scamval(type);
    ret->count = strlen(s);
    ret->mem_size = ret->count + 1;
    ret->vals.s = strdup(s);
    return ret;
}

scamval* scamstr(const char* s) {
    return scam_internal_str(SCAM_STR, s);
}

scamval* scamstr_read(FILE* fp) {
    scamval* ret = gc_new_scamval(SCAM_STR);
    ret->vals.s = NULL;
    ret->count = getline(&ret->vals.s, &ret->mem_size, fp);
    if (ret->count != -1) {
        return ret;
    } else {
        gc_unset_root(ret);
        return scamerr_eof();
    }
}

scamval* scamstr_empty(void) {
    scamval* ret = gc_new_scamval(SCAM_STR);
    ret->count = 0;
    ret->mem_size = 0;
    ret->vals.s = NULL;
    return ret;
}

scamval* scamstr_no_copy(char* s) {
    scamval* ret = gc_new_scamval(SCAM_STR);
    ret->vals.s = s;
    ret->count = ret->mem_size = strlen(s);
    return ret;
}

scamval* scamstr_from_char(char c) {
    scamval* ret = gc_new_scamval(SCAM_STR);
    ret->vals.s = gc_malloc(2);
    ret->vals.s[0] = c;
    ret->vals.s[1] = '\0';
    ret->count = 1;
    ret->mem_size = 2;
    return ret;
}

scamval* scamsym(const char* s) {
    return scam_internal_str(SCAM_SYM, s);
}

enum { MAX_ERROR_SIZE = 100 };
scamval* scamerr(const char* format, ...) {
    scamval* ret = gc_new_scamval(SCAM_ERR);
    va_list vlist;
    va_start(vlist, format);
    ret->vals.s = gc_malloc(MAX_ERROR_SIZE);
    vsnprintf(ret->vals.s, MAX_ERROR_SIZE, format, vlist);
    va_end(vlist);
    return ret;
}

scamval* scamerr_arity(const char* name, size_t got, size_t expected) {
    return scamerr("'%s' got %d arg(s), expected %d", name, got, expected);
}

scamval* scamerr_min_arity(const char* name, size_t got, size_t expected) {
    return scamerr("'%s' got %d arg(s), expected at least %d", name, got, expected);
}

scamval* scamerr_eof(void) {
    return scamerr("reached EOF while reading from a port");
}

scamval* scamlambda(scamval* env, scamval* parameters, scamval* body) {
    scamval* ret = gc_new_scamval(SCAM_LAMBDA);
    ret->vals.fun = gc_malloc(sizeof *ret->vals.fun);
    ret->vals.fun->env = env;
    ret->vals.fun->parameters = parameters;
    ret->vals.fun->body = body;
    return ret;
}

scamval* scambuiltin(scambuiltin_fun bltin) {
    scamval* ret = gc_new_scamval(SCAM_BUILTIN);
    ret->vals.bltin = gc_malloc(sizeof *ret->vals.bltin);
    ret->vals.bltin->fun = bltin;
    ret->vals.bltin->constant = 0;
    return ret;
}

scamval* scambuiltin_const(scambuiltin_fun bltin) {
    scamval* ret = gc_new_scamval(SCAM_BUILTIN);
    ret->vals.bltin = gc_malloc(sizeof *ret->vals.bltin);
    ret->vals.bltin->fun = bltin;
    ret->vals.bltin->constant = 1;
    return ret;
}

scamval* scamport(FILE* fp) {
    scamval* ret = gc_new_scamval(SCAM_PORT);
    ret->vals.port = gc_malloc(sizeof *ret->vals.port);
    ret->vals.port->status = (fp == NULL ? SCAMPORT_CLOSED : SCAMPORT_OPEN);
    ret->vals.port->fp = fp;
    return ret;
}

scamval* scamnull(void) {
    scamval* ret = gc_new_scamval(SCAM_NULL);
    ret->is_root = 0;
    return ret;
}


/*** SCAMVAL COMPARISONS ***/
static int scamval_numeric_eq(const scamval* v1, const scamval* v2) {
    if (v1->type == SCAM_INT) {
        if (v2->type == SCAM_INT) {
            return scam_as_int(v1) == scam_as_int(v2);
        } else {
            return scam_as_int(v1) == scam_as_dec(v2);
        }
    } else {
        if (v2->type == SCAM_INT) {
            return scam_as_dec(v1) == scam_as_int(v2);
        } else {
            return scam_as_dec(v1) == scam_as_dec(v2);
        }
    }
}

static int scamval_list_eq(const scamval* v1, const scamval* v2) {
    size_t n1 = scamseq_len(v1);
    size_t n2 = scamseq_len(v2);
    if (n1 == n2) {
        for (int i = 0; i < n1; i++) {
            if (!scamval_eq(scamseq_get(v1, i), scamseq_get(v2, i))) {
                return 0;
            }
        }
        return 1;
    } else {
        return 0;
    }
}

static int scamval_dict_eq(const scamval* v1, const scamval* v2) {
    for (size_t i = 0; i < scamdict_len(v1); i++) {
        scamval* key = scamdict_key(v1, i);
        scamval* val1 = scamdict_val(v1, i);
        scamval* val2 = scamdict_lookup(v2, key);
        if (!scamval_eq(val1, val2)) {
            return 0;
        }
    }
    return 1;
}

int scamval_eq(const scamval* v1, const scamval* v2) {
    if (scamval_typecheck(v1, SCAM_NUM) && scamval_typecheck(v2, SCAM_NUM)) {
        return scamval_numeric_eq(v1, v2);
    } else if (v1->type == v2->type) {
        switch (v1->type) {
            case SCAM_BOOL:
                return scam_as_bool(v1) == scam_as_bool(v2);
            case SCAM_SEXPR:
            case SCAM_LIST:
                return scamval_list_eq(v1, v2);
            case SCAM_SYM:
            case SCAM_STR:
                return (strcmp(scam_as_str(v1), scam_as_str(v2)) == 0);
            case SCAM_DICT:
                return scamval_dict_eq(v1, v2);
            case SCAM_NULL:
                return 1;
            default:
                return 0;
        }
    } else {
        return 0;
    }
}

static int scamval_numeric_gt(const scamval* v1, const scamval* v2) {
    if (v1->type == SCAM_INT) {
        if (v2->type == SCAM_INT) {
            return scam_as_int(v1) > scam_as_int(v2);
        } else {
            return scam_as_int(v1) > scam_as_dec(v2);
        }
    } else {
        if (v2->type == SCAM_INT) {
            return scam_as_dec(v1) > scam_as_int(v2);
        } else {
            return scam_as_dec(v1) > scam_as_dec(v2);
        }
    }
}

int scamval_gt(const scamval* v1, const scamval* v2) {
    if (scamval_typecheck(v1, SCAM_NUM) && scamval_typecheck(v2, SCAM_NUM)) {
        return scamval_numeric_gt(v1, v2);
    } else if (scamval_typecheck(v1, SCAM_STR) && 
               scamval_typecheck(v2, SCAM_STR)) {
        return strcmp(v1->vals.s, v2->vals.s) > 0;
    } else {
        return 0;
    }
}


/*** NUMERIC API ***/
long long scam_as_int(const scamval* v) { return v->vals.n; }
long long scam_as_bool(const scamval* v) { return v->vals.n; }
double scam_as_dec(const scamval* v) { 
    if (v->type == SCAM_DEC) {
        return v->vals.d; 
    } else {
        return v->vals.n;
    }
}


/*** SEQUENCE API ***/
// constants for the scamseq_grow function
enum { SEQ_SIZE_INITIAL = 5, SEQ_SIZE_GROW = 2};

// Grow the size of the sequence in memory so that it is at least the given minimum size, and 
// possibly larger
static void scamseq_grow(scamval* seq, size_t min_new_sz) {
    if (seq->vals.arr == NULL) {
        seq->mem_size = SEQ_SIZE_INITIAL;
        if (seq->mem_size < min_new_sz)
            seq->mem_size = min_new_sz;
        seq->vals.arr = gc_malloc(seq->mem_size * sizeof *seq->vals.arr);
    } else {
        seq->mem_size *= SEQ_SIZE_GROW;
        if (seq->mem_size < min_new_sz)
            seq->mem_size = min_new_sz;
        seq->vals.arr = gc_realloc(seq->vals.arr, seq->mem_size * sizeof *seq->vals.arr);
    }
}

// Unlike scamseq_grow, the new sequence is guaranteed to be exactly the new size provided
static void scamseq_resize(scamval* seq, size_t new_sz) {
    seq->mem_size = new_sz;
    if (seq->vals.arr == NULL) {
        seq->vals.arr = gc_malloc(seq->mem_size * sizeof *seq->vals.arr);
    } else {
        seq->vals.arr = gc_realloc(seq->vals.arr, seq->mem_size * sizeof *seq->vals.arr);
    }
}

scamval* scamseq_pop(scamval* seq, size_t i) {
    if (i >= 0 && i < seq->count) {
        scamval* ret = seq->vals.arr[i];
        memmove(seq->vals.arr + i, seq->vals.arr + i + 1,
                (seq->count - i - 1) * sizeof *seq->vals.arr);
        seq->count--;
        gc_set_root(ret);
        return ret;
    } else {
        return scamerr("attempted sequence access out of range");
    }
}

void scamseq_delete(scamval* seq, size_t i) {
    gc_unset_root(scamseq_pop(seq, i));
}

scamval* scamseq_get(const scamval* seq, size_t i) {
    return seq->vals.arr[i];
}

size_t scamseq_len(const scamval* seq) {
    return seq->count;
}

void scamseq_set(scamval* seq, size_t i, scamval* v) {
    if (i >= 0 && i < seq->count) {
        seq->vals.arr[i] = v;
    }
}

void scamseq_prepend(scamval* seq, scamval* v) {
    scamseq_insert(seq, 0, v);
}

void scamseq_append(scamval* seq, scamval* v) {
    scamseq_insert(seq, scamseq_len(seq), v);
}

void scamseq_insert(scamval* seq, size_t i, scamval* v) {
    gc_unset_root(v);
    if (++seq->count > seq->mem_size) {
        scamseq_grow(seq, seq->count);
    }
    memmove(seq->vals.arr + i + 1, seq->vals.arr + i, (seq->count - i - 1) * sizeof *seq->vals.arr);
    seq->vals.arr[i] = v;
}

void scamseq_concat(scamval* seq1, scamval* seq2) {
    if (scamseq_len(seq2) > 0) {
        scamseq_resize(seq1, scamseq_len(seq1) + scamseq_len(seq2));
        while (scamseq_len(seq2) > 0) {
            scamseq_append(seq1, scamseq_pop(seq2, 0));
        }
    }
}

scamval* scamseq_subseq(const scamval* seq, size_t start, size_t end) {
    size_t n = scamseq_len(seq);
    if (start >= 0 && end <= n && start <= end) {
        scamval* ret = scam_internal_seq(seq->type);
        for (size_t i = start; i < end; i++) {
            scamseq_append(ret, gc_copy_scamval(scamseq_get(seq, i)));
        }
        return ret;
    } else {
        return scamerr("attempted sequence access out of bounds");
    }
}


/*** FUNCTION API ***/
size_t scamlambda_nparams(const scamval* v) {
    return scamseq_len(v->vals.fun->parameters);
}

scamval* scamlambda_param(const scamval* v, size_t i) {
    return gc_copy_scamval(scamseq_get(v->vals.fun->parameters, i));
}

scamval* scamlambda_body(const scamval* v) {
    return gc_copy_scamval(v->vals.fun->body);
}

scamval* scamlambda_env(const scamval* v) {
    return scamdict(v->vals.fun->env);
}

const scamval* scamlambda_env_ref(const scamval* v) {
    return v->vals.fun->env;
}

scambuiltin_fun scambuiltin_function(const scamval* v) {
    return v->vals.bltin->fun;
}

int scambuiltin_is_const(const scamval* v) {
    return v->vals.bltin->constant;
}


/*** STRING API ***/
static void scamstr_resize(scamval* s, size_t new_sz) {
    s->mem_size = new_sz;
    if (s->vals.s == NULL) {
        s->vals.s = gc_malloc(new_sz);
    } else {
        s->vals.s = gc_realloc(s->vals.s, new_sz);
    }
}

const char* scam_as_str(const scamval* v) { return v->vals.s; }

void scamstr_set(scamval* v, size_t i, char c) {
    if (i >= 0 && i < v->count) {
        v->vals.s[i] = c;
    }
}

void scamstr_map(scamval* v, int map_f(int)) {
    for (char* p = v->vals.s; *p != '\0'; p++) {
        *p = map_f(*p);
    }
}

char scamstr_get(const scamval* v, size_t i) {
    if (i >= 0 && i < scamstr_len(v)) {
        return v->vals.s[i];
    } else {
        return EOF;
    }
}

char scamstr_pop(scamval* v, size_t i) {
    if (i >= 0 && i < scamstr_len(v)) {
        char ret = v->vals.s[i];
        scamstr_remove(v, i, i + 1);
        return ret;
    } else {
        return EOF;
    }
}

void scamstr_remove(scamval* v, size_t start, size_t end) {
    if (start >= 0 && end <= scamstr_len(v) && start < end) {
        memmove(v->vals.s + start, v->vals.s + end, v->count - end);
        v->count -= (end - start);
        v->vals.s[v->count] = '\0';
    }
}

void scamstr_truncate(scamval* v, size_t i) {
    if (i >= 0 && i < scamstr_len(v)) {
        v->vals.s[i] = '\0';
    }
}

scamval* scamstr_substr(const scamval* v, size_t start, size_t end) {
    if (start >= 0 && end <= scamstr_len(v) && start <= end) {
        char* s = gc_malloc(end - start + 1);
        strncpy(s, v->vals.s + start, end - start);
        s[end - start] = '\0';
        return scamstr_no_copy(s);
    } else {
        return scamerr("string access out of bounds");
    }
}

size_t scamstr_len(const scamval* s) {
    return s->count;
}

void scamstr_concat(scamval* s1, scamval* s2) {
    size_t n1 = scamstr_len(s1);
    size_t n2 = scamstr_len(s2);
    scamstr_resize(s1, n1 + n2 + 1);
    for (int i = 0; i <= n2; i++) {
        s1->vals.s[n1 + i] = s2->vals.s[i];
    }
    s1->count = n1 + n2;
    gc_unset_root(s2);
}


/*** PORT API ***/
FILE* scam_as_file(scamval* v) { return v->vals.port->fp; }
int scamport_status(const scamval* v) { return v->vals.port->status; }

void scamport_set_status(scamval* v, int new_status) {
    v->vals.port->status = new_status;
}


/*** DICTIONARY API ***/
scamval* scamdict(scamval* enclosing) {
    scamval* ret = gc_new_scamval(SCAM_ANY);
    ret->vals.dct = gc_malloc(sizeof *ret->vals.dct);
    ret->vals.dct->enclosing = enclosing;
    // The order is very important here: if ret was constructed as a SCAM_DICT right away, then the 
    // garbage collector might try to access syms or vals before they were allocated. The two calls 
    // to scamlist are safe because if the first call invokes the collector, the second call cannot 
    // as the collector will allocate space for at least one additional object.
    ret->vals.dct->syms = scamlist();
    ret->vals.dct->vals = scamlist();
    ret->type = SCAM_DICT;
    gc_unset_root(ret->vals.dct->syms);
    gc_unset_root(ret->vals.dct->vals);
    return ret;
}

scamval* scamdict_keys(const scamval* dct) { return dct->vals.dct->syms; }
scamval* scamdict_vals(const scamval* dct) { return dct->vals.dct->vals; }
scamval* scamdict_enclosing(const scamval* dct) { 
    return dct->vals.dct->enclosing; 
}

void scamdict_set_keys(scamval* dct, scamval* new_keys) {
    gc_unset_root(scamdict_keys(dct));
    dct->vals.dct->syms = new_keys;
    gc_unset_root(new_keys);
}

void scamdict_set_vals(scamval* dct, scamval* new_vals) {
    gc_unset_root(scamdict_vals(dct));
    dct->vals.dct->vals = new_vals;
    gc_unset_root(new_vals);
}

size_t scamdict_len(const scamval* dct) {
    return scamseq_len(dct->vals.dct->syms);
}

scamval* scamdict_key(const scamval* dct, size_t i) {
    return scamseq_get(scamdict_keys(dct), i);
}

scamval* scamdict_val(const scamval* dct, size_t i) {
    return scamseq_get(scamdict_vals(dct), i);
}

void scamdict_bind(scamval* dct, scamval* sym, scamval* val) {
    gc_unset_root(sym);
    gc_unset_root(val);
    if (sym->type == SCAM_PORT || sym->type == SCAM_LAMBDA || sym->type == SCAM_BUILTIN || 
        sym->type == SCAM_NULL) {
        // unbindable types
        return;
        //return scamerr("cannot bind type '%s'", scamtype_name(sym->type));
    }
    for (int i = 0; i < scamdict_len(dct); i++) {
        if (scamval_eq(scamdict_key(dct, i), sym)) {
            gc_unset_root(scamdict_val(dct, i));
            scamseq_set(scamdict_vals(dct), i, val);
            return;
        }
    }
    scamseq_append(scamdict_keys(dct), sym);
    scamseq_append(scamdict_vals(dct), val);
}

scamval* scamdict_lookup(const scamval* dct, const scamval* key) {
    for (int i = 0; i < scamdict_len(dct); i++) {
        scamval* this_key = scamdict_key(dct, i);
        if (scamval_eq(key, this_key)) {
            return scamdict_val(dct, i);
        }
    }
    if (scamdict_enclosing(dct) != NULL) {
        return scamdict_lookup(scamdict_enclosing(dct), key);
    } else {
        if (key->type == SCAM_STR) {
            return scamerr("unbound variable '%s'", scam_as_str(key));
        } else {
            return scamerr("unbound variable");
        }
    }
}

static void scamseq_print(const scamval* seq, char* open, char* close) {
    printf("%s", open);
    for (size_t i = 0; i < scamseq_len(seq); i++) {
        scamval_print(scamseq_get(seq, i));
        if (i != seq->count - 1)
            printf(" ");
    }
    printf("%s", close);
}

static void scamdict_print(const scamval* dct) {
    printf("{");
    for (size_t i = 0; i < scamdict_len(dct); i++) {
        scamval_print(scamdict_key(dct, i));
        printf(":");
        scamval_print(scamdict_val(dct, i));
        if (i != scamdict_len(dct) - 1)
            printf(" ");
    }
    printf("}");
}

void scamval_print(const scamval* v) {
    if (!v) return;
    switch (v->type) {
        case SCAM_INT: printf("%lli", scam_as_int(v)); break;
        case SCAM_DEC: printf("%f", scam_as_dec(v)); break;
        case SCAM_BOOL: printf("%s", scam_as_bool(v) ? "true":"false"); break;
        case SCAM_LIST: scamseq_print(v, "[", "]"); break;
        case SCAM_SEXPR: scamseq_print(v, "(", ")"); break;
        case SCAM_LAMBDA: printf("<Scam function>"); break;
        case SCAM_BUILTIN: printf("<Scam builtin>"); break;
        case SCAM_PORT: printf("<Scam port>"); break;
        case SCAM_STR: printf("\"%s\"", scam_as_str(v)); break;
        case SCAM_SYM: printf("%s", scam_as_str(v)); break;
        case SCAM_ERR: printf("Error: %s", scam_as_str(v)); break;
        case SCAM_DICT: scamdict_print(v); break;
    }
}

void scamval_println(const scamval* v) {
    if (!v || v->type == SCAM_NULL) return;
    scamval_print(v);
    printf("\n");
}

void scamval_print_debug(const scamval* v) {
    scamval_print(v);
    printf(" (%s)", scamtype_debug_name(v->type));
}

void scamval_print_ast(const scamval* ast, int indent) {
    for (int i = 0; i < indent; i++)
        printf("  ");
    if (ast->type == SCAM_SEXPR) {
        size_t n = scamseq_len(ast);
        if (n == 0) {
            printf("EMPTY EXPR%s\n", ast->is_root ? " (root)" : "");
        } else {
            printf("EXPR%s\n", ast->is_root ? " (root)" : "");
            for (int i = 0; i < scamseq_len(ast); i++) {
                scamval_print_ast(scamseq_get(ast, i), indent + 1);
            }
        }
    } else {
        scamval_print(ast);
        printf("%s\n", ast->is_root ? " (root)" : "");
    }
}


/*** TYPECHECKING ***/
int scamval_typecheck(const scamval* v, int type) {
    switch (type) {
        case SCAM_ANY: 
            return 1;
        case SCAM_SEQ: 
            return v->type == SCAM_LIST || v->type == SCAM_STR;
        case SCAM_CONTAINER:
            return v->type == SCAM_LIST || v->type == SCAM_STR || v->type == SCAM_DICT;
        case SCAM_NUM: 
            return v->type == SCAM_INT || v->type == SCAM_DEC;
        case SCAM_CMP: 
            return v->type == SCAM_STR || v->type == SCAM_INT || v->type == SCAM_DEC;
        case SCAM_FUNCTION: 
            return v->type == SCAM_LAMBDA || v->type == SCAM_BUILTIN;
        default: 
            return v->type == type;
    }
}

int is_numeric_type(int type) { return type == SCAM_INT || type == SCAM_DEC; }
int is_seq_type(int type) { return type == SCAM_LIST || type == SCAM_STR; }
int is_container_type(int type) {
    return type == SCAM_LIST || type == SCAM_STR || type == SCAM_DICT;
}


int narrowest_type(int type1, int type2) {
    if (type1 == type2) {
        return type1;
    } else if (is_numeric_type(type1) && is_numeric_type(type2)) {
        return SCAM_NUM;
    } else if (is_seq_type(type1) && is_seq_type(type2)) {
        return SCAM_SEQ;
    } else if (is_container_type(type1) && is_container_type(type2)) {
        return SCAM_CONTAINER;
    } else {
        return SCAM_ANY;
    }
}

int scamseq_narrowest_type(scamval* args) {
    size_t n = scamseq_len(args);
    if (n == 0) return SCAM_ANY;
    int type_so_far = scamseq_get(args, 0)->type;
    for (int i = 1; i < n; i++) {
        type_so_far = narrowest_type(scamseq_get(args, i)->type, type_so_far);
    }
    return type_so_far;
}

scamval* scamerr_type(const char* name, size_t pos, int got, int expected) {
    return scamerr("'%s' got %s as arg %d, expected %s", name, scamtype_name(got), pos + 1, 
                   scamtype_name(expected));
}

const char* scamtype_name(int type) {
    switch (type) {
        case SCAM_INT: return "integer";
        case SCAM_DEC: return "decimal";
        case SCAM_BOOL: return "boolean";
        case SCAM_LIST: return "list";
        case SCAM_STR: return "string";
        case SCAM_LAMBDA: return "function";
        case SCAM_PORT: return "port";
        case SCAM_BUILTIN: return "builtin";
        case SCAM_SEXPR: return "S-expression";
        case SCAM_SYM: return "symbol";
        case SCAM_ERR: return "error";
        case SCAM_NULL: return "null";
        case SCAM_DICT: return "dictionary";
        // abstract types
        case SCAM_SEQ: return "list or string";
        case SCAM_CONTAINER: return "list, string or dictionary";
        case SCAM_NUM: return "integer or decimal";
        case SCAM_CMP: return "integer, decimal or string";
        case SCAM_ANY: return "any value";
        default: return "bad scamval type";
    }
}

const char* scamtype_debug_name(int type) {
    switch (type) {
        case SCAM_INT: return "SCAM_INT";
        case SCAM_DEC: return "SCAM_DEC";
        case SCAM_BOOL: return "SCAM_BOOL";
        case SCAM_LIST: return "SCAM_LIST";
        case SCAM_STR: return "SCAM_STR";
        case SCAM_LAMBDA: return "SCAM_LAMBDA";
        case SCAM_PORT: return "SCAM_PORT";
        case SCAM_BUILTIN: return "SCAM_BUILTIN";
        case SCAM_SEXPR: return "SCAM_SEXPR";
        case SCAM_SYM: return "SCAM_SYM";
        case SCAM_ERR: return "SCAM_ERR";
        case SCAM_NULL: return "SCAM_NULL";
        case SCAM_DICT: return "SCAM_DICT";
        // abstract types
        case SCAM_SEQ: return "SCAM_SEQ";
        case SCAM_CONTAINER: return "SCAM_CONTAINER";
        case SCAM_NUM: return "SCAM_NUM";
        case SCAM_CMP: return "SCAM_CMP";
        case SCAM_ANY: return "SCAM_ANY";
        default: return "bad scamval type";
    }
}
