%union { int ival; float fval; char *sval; int op; }
%token TYPE STRUCT RETURN IF ELSE WHILE AND OR NOT
%token RELOP SEMI COMMA ASSIGNOP PLUS MINUS STAR DIV LP RP LB RB LC RC DOT
%token INT FLOAT ID

%%
/* start symbol and simple empty production so that bison has a grammar */
start:
    /* nothing */
    ;

%%