#include <stdio.h>
#include <stdlib.h>

/* flex/bison interfaces */
extern FILE *yyin;
extern int yylineno;    

/* pull in scanner implementation so make doesn't need to link lex.yy.o */
#include "lex.yy.c"

/* prototypes provided by lex.yy.c now; duplicate declarations removed */

void scan_file(FILE *file) {
    yyin = file;
    yylineno = 1;        /* reset for each new stream */
    yyrestart(yyin);
    int token;
    while ((token = yylex()) != 0) {}
}

int main(int argc, char *argv[]){
    for (int i = 1; i < argc; i++) {
        FILE *file = fopen(argv[i], "r");
        if (file == NULL) {
            fprintf(stderr, "Error opening file: %s\n", argv[i]);
            continue;
        }
        scan_file(file);
        fclose(file);
    }
    return 0;
}