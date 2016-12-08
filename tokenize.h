#pragma once
#include <stdio.h>
#include "stream.h"

enum { TKN_LPAREN, TKN_RPAREN, TKN_LBRACKET, TKN_RBRACKET, TKN_LBRACE, 
       TKN_RBRACE, TKN_INT, TKN_DEC, TKN_SYM, TKN_STR, TKN_EOF, TKN_UNKNOWN };
typedef struct {
    int type;
    char* val;
    int line, col;
} Token;

typedef struct {
    Stream strm;
    Token tkn;
} Tokenizer;

// Initialize tokenizers from various sources
void tokenizer_from_str(Tokenizer*, char*);
void tokenizer_from_file(Tokenizer*, char*);

// Move to the next token
void tokenizer_advance(Tokenizer*);
// Free all tokenizer resources, including the pointer itself
void tokenizer_close(Tokenizer*);

// Print all tokens and their type to stdout (useful for debugging)
void print_all_tokens(Tokenizer*);
const char* token_type_name(int type);