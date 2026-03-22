#include <stdio.h>
#include <stdlib.h>

/* flex/bison interfaces */
extern FILE *yyin;
extern int yylineno;    
extern int yyparse(void);

#include "lex.yy.c"
#include "syntax.tab.h"

void reset_parser_state(void);
void print_tree(TreeNode *root, int depth);

extern TreeNode *syntax_root;
extern int has_lexical_error;
extern int has_syntax_error;

static void parse_file(FILE *file) {
    reset_parser_state();
    yyin = file;
    yylineno = 1;
    yyrestart(yyin);

    yyparse();
    if (!has_lexical_error && !has_syntax_error && syntax_root != NULL) {
        print_tree(syntax_root, 0);
    }
}

int main(int argc, char *argv[]){
    int i;
    if (argc <= 1) {
        return 0;
    }

    for (i = 1; i < argc; i++) {
        FILE *file = fopen(argv[i], "r");
        if (file == NULL) {
            printf("Cannot open file: %s\n", argv[i]);
            continue;
        }
        parse_file(file);
        fclose(file);
    }

    return 0;
}