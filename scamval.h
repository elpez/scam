#pragma once
#include <stdio.h>

// Possible values for the type field of the scamval struct
// Note that some of these types are never exposed to the user
enum {SCAM_INT, SCAM_DEC, SCAM_BOOL, SCAM_LIST, SCAM_STR, SCAM_LAMBDA, 
      SCAM_PORT, SCAM_BUILTIN, SCAM_SEXPR, SCAM_SYM, SCAM_ERR, SCAM_NULL};

// Type values that are only used for typechecking
enum {SCAM_SEQ=1000, SCAM_NUM, SCAM_CMP, SCAM_FUNCTION, SCAM_ANY};

// Forward declaration of scamval and scamenv
struct scamval;
typedef struct scamval scamval;
struct scamenv;
typedef struct scamenv scamenv;

// Another convenient typedef
typedef scamval* (scambuiltin_t)(scamval*);

typedef struct {
    scamenv* env; // the environment the function was created in
    scamval* parameters;
    scamval* body;
} scamfun_t;

enum { SCAMPORT_OPEN, SCAMPORT_CLOSED };
typedef struct {
    int status;
    FILE* fp;
} scamport_t;

struct scamval {
    int type;
    int line, col;
    size_t count, mem_size; // used by SCAM_LIST, SCAM_SEXPR and SCAM_STR
    union {
        long long n; // used by SCAM_INT and SCAM_BOOL
        double d; // SCAM_DEC
        char* s; // SCAM_STR, SCAM_SYM and SCAM_ERR
        scamval** arr; // SCAM_LIST and SCAM_SEXPR
        scamfun_t* fun; // SCAM_LAMBDA
        scamport_t* port; // SCAM_PORT
        scambuiltin_t* bltin; // SCAM_BUILTIN
    } vals;
    // accounting info for the garbage collector
    int refs;
};


/*** SCAMVAL CONSTRUCTORS ***/
scamval* scamval_new(int type);
scamval* scamsym(const char*);
scamval* scamfunction(scamenv* env, scamval* parameters, scamval* body);
scamval* scambuiltin(scambuiltin_t*);
scamval* scamnull();


/*** NUMERIC API ***/
scamval* scamint(long long);
scamval* scamdec(double);
scamval* scambool(int);
long long scam_as_int(const scamval*);
long long scam_as_bool(const scamval*);
double scam_as_dec(const scamval*);


/*** SEQUENCE API ***/
scamval* scamlist();
scamval* scamsexpr();
scamval* scamsexpr_from_vals(size_t, ...);

// Return a reference to the i'th element of the sequence
// Make sure not to free this reference!
scamval* scamseq_get(const scamval*, size_t i);

// Remove and return the i'th element of the sequence
// The caller assumes responsibility for freeing the value
scamval* scamseq_pop(scamval*, size_t i);

// Set the i'th element of the sequence, obliterating the old element without
// freeing it (DO NOT USE unless you know the i'th element is already free)
void scamseq_set(scamval* seq, size_t i, scamval* v);

// Same as scamval_set, except the previous element is free'd before
void scamseq_replace(scamval*, size_t, scamval*);

// Return the actual number of elements in the sequence or string
size_t scamseq_len(const scamval*);

// Append/prepend a value to a sequence
// The sequence takes responsibility for freeing the value, so it's best not
// to use a value once you've appended or prepended it somewhere
void scamseq_append(scamval* seq, scamval* v);
void scamseq_prepend(scamval* seq, scamval* v);

// Concatenate the second argument to the first, freeing the second arg
void scamseq_concat(scamval* seq1, scamval* seq2);

// Free the internal sequence of a scamval, without freeing the actual value
void scamseq_free(scamval*);


/*** STRING API ***/
// Initialize a string from a character array (a copy is made)
scamval* scamstr(const char*);
// Read a line from a file
scamval* scamstr_read(FILE*);
// Initialize a string from a character array without making a copy
scamval* scamstr_no_copy(char*);
scamval* scamstr_empty();
scamval* scamstr_from_char(char);
const char* scam_as_str(const scamval*);
void scamstr_set(scamval*, size_t, char);
void scamstr_map(scamval*, int map_f(int));
// Return the i'th character without removing it
char scamstr_get(const scamval*, size_t i);
// Remove and return the i'th character
char scamstr_pop(scamval*, size_t i);
void scamstr_remove(scamval*, size_t beg, size_t end);
void scamstr_truncate(scamval*, size_t);
// Return a newly-allocated substring
scamval* scamstr_substr(scamval*, size_t, size_t);
void scamstr_concat(scamval* s1, scamval* s2);
size_t scamstr_len(const scamval*);


/*** ERROR API ***/
scamval* scamerr(const char*, ...);
scamval* scamerr_arity(const char* name, size_t got, size_t expected);
scamval* scamerr_min_arity(const char* name, size_t got, size_t expected);
scamval* scamerr_eof();


/*** PORT API ***/
scamval* scamport(FILE*);
FILE* scam_as_file(scamval*);
int scamport_status(const scamval*);
void scamport_set_status(scamval*, int);


/*** SCAMVAL MEMORY MANAGEMENT ***/
// Return a copy of the given value
scamval* scamval_copy(scamval*);
// Free all resources used by a scamval, including the pointer itself
void scamval_free(scamval*);


/*** SCAMVAL PRINTING ***/
void scamval_print(const scamval*);
void scamval_println(const scamval*);
void scamval_print_debug(const scamval*);
void scamval_print_ast(const scamval*, int indent);


/*** SCAMVAL COMPARISONS ***/
int scamval_eq(const scamval*, const scamval*);
int scamval_gt(const scamval*, const scamval*);


/*** SCAMENV ***/
struct scamenv {
    scamenv* enclosing;
    // symbols and values are stored as scamval lists
    scamval* syms;
    scamval* vals;
};

// Initialize and free environments
scamenv* scamenv_init(scamenv* enclosing);
void scamenv_free(scamenv*);

// Create a new binding in the environment, or update an existing one
// sym should be of type SCAM_STR
// Both sym and val are appropriated by the environment, so don't use them
// after calling this function
void scamenv_bind(scamenv*, scamval* sym, scamval* val);

// Lookup the symbol in the environment, returning a copy of the value if it
// exists and an error if it doesn't
scamval* scamenv_lookup(scamenv*, scamval* sym);


/*** TYPECHECKING ***/
// Return the names of types as strings
const char* scamtype_name(int type);
const char* scamtype_debug_name(int type);

// Check if the value belongs to the given type
int scamval_typecheck(const scamval*, int type);

// Return the narrowest type applicable to both types
int narrowest_type(int, int);

// Return the narrowest type applicable to all elements of the sequence
int scamseq_narrowest_type(scamval*);

// Construct a type error message
scamval* scamerr_type(const char* name, size_t pos, int got, int expected);
