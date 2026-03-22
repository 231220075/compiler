%code requires {
#ifndef TREE_NODE_TYPEDEF
#define TREE_NODE_TYPEDEF
typedef struct TreeNode TreeNode;
#endif
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef TREE_NODE_TYPEDEF
#define TREE_NODE_TYPEDEF
typedef struct TreeNode TreeNode;
#endif

int yylex(void);
int yylineno;
void yyerror(const char *msg);

struct TreeNode {
        char *name;
        int line;
        int is_token;
        char *text;
        struct TreeNode *child;
        struct TreeNode *sibling;
};

TreeNode *syntax_root = NULL;
int has_lexical_error = 0;
int has_syntax_error = 0;

static int last_syntax_error_line = -1;

static TreeNode *new_terminal(const char *name, int line, const char *text) {
        TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
        node->name = strdup(name);
        node->line = line;
        node->is_token = 1;
        node->text = text ? strdup(text) : NULL;
        node->child = NULL;
        node->sibling = NULL;
        return node;
}

static TreeNode *new_nonterminal(const char *name, int count, ...) {
        TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
        va_list ap;
        int i;
        TreeNode *first = NULL;
        TreeNode *tail = NULL;

        node->name = strdup(name);
        node->line = 0;
        node->is_token = 0;
        node->text = NULL;
        node->child = NULL;
        node->sibling = NULL;

        va_start(ap, count);
        for (i = 0; i < count; i++) {
                TreeNode *c = va_arg(ap, TreeNode *);
                if (c == NULL) {
                        continue;
                }
                if (node->line == 0) {
                        node->line = c->line;
                }
                if (first == NULL) {
                        first = c;
                        tail = c;
                } else {
                        tail->sibling = c;
                        tail = c;
                }
        }
        va_end(ap);
        node->child = first;
        return node;
}

void print_tree(TreeNode *root, int depth) {
        int i;
        TreeNode *c;
        if (root == NULL) {
                return;
        }

        for (i = 0; i < depth; i++) {
                printf("  ");
        }

        if (root->is_token) {
                if (root->text != NULL) {
                        printf("%s: %s\n", root->name, root->text);
                } else {
                        printf("%s\n", root->name);
                }
        } else {
                printf("%s (%d)\n", root->name, root->line);
        }

        c = root->child;
        while (c != NULL) {
                print_tree(c, depth + 1);
                c = c->sibling;
        }
}

void free_tree(TreeNode *root) {
        TreeNode *c;
        TreeNode *n;
        if (root == NULL) {
                return;
        }

        c = root->child;
        while (c != NULL) {
                n = c->sibling;
                free_tree(c);
                c = n;
        }

        free(root->name);
        if (root->text != NULL) {
                free(root->text);
        }
        free(root);
}

void reset_parser_state(void) {
        if (syntax_root != NULL) {
                free_tree(syntax_root);
        }
        syntax_root = NULL;
        has_lexical_error = 0;
        has_syntax_error = 0;
        last_syntax_error_line = -1;
}

void report_lexical_error(int line, const char *msg, const char *lexeme) {
        has_lexical_error = 1;
        if (lexeme != NULL && lexeme[0] != '\0') {
                printf("Error type A at Line %d: %s '%s'.\n", line, msg, lexeme);
        } else {
                printf("Error type A at Line %d: %s.\n", line, msg);
        }
}

void report_syntax_error(int line, const char *msg) {
        if (line == last_syntax_error_line) {
                return;
        }
        has_syntax_error = 1;
        last_syntax_error_line = line;
        printf("Error type B at Line %d: %s.\n", line, msg);
}

static TreeNode *int_terminal(int line, int value) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", value);
        return new_terminal("INT", line, buf);
}

static TreeNode *float_terminal(int line, float value) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%f", value);
        return new_terminal("FLOAT", line, buf);
}

#define NODE(name, count, ...) new_nonterminal(name, count, __VA_ARGS__)
#define TOKEN(name, line) new_terminal(name, line, NULL)
%}

%code provides {
extern TreeNode *syntax_root;
extern int has_lexical_error;
extern int has_syntax_error;

void print_tree(TreeNode *root, int depth);
void free_tree(TreeNode *root);
void reset_parser_state(void);
void report_lexical_error(int line, const char *msg, const char *lexeme);
void report_syntax_error(int line, const char *msg);
}

%define parse.error verbose
%locations

%union {
        int ival;
        float fval;
        char *sval;
        TreeNode *node;
}

%token <sval> TYPE ID RELOP
%token <ival> INT
%token <fval> FLOAT
%token STRUCT RETURN IF ELSE WHILE AND OR NOT
%token SEMI COMMA ASSIGNOP PLUS MINUS STAR DIV LP RP LB RB LC RC DOT

%type <node> Program ExtDefList ExtDef ExtDecList Specifier StructSpecifier OptTag Tag
%type <node> VarDec FunDec VarList ParamDec CompSt StmtList Stmt DefList Def DecList Dec
%type <node> Exp Args

%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left PLUS MINUS
%left STAR DIV
%right NOT UMINUS
%left LP RP LB RB DOT
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

Program
        : ExtDefList
            {
                    $$ = NODE("Program", 1, $1);
                    syntax_root = $$;
            }
        ;

ExtDefList
        : ExtDef ExtDefList { $$ = NODE("ExtDefList", 2, $1, $2); }
        | /* empty */       { $$ = NULL; }
        ;

ExtDef
        : Specifier ExtDecList SEMI
            { $$ = NODE("ExtDef", 3, $1, $2, TOKEN("SEMI", @3.first_line)); }
        | Specifier SEMI
            { $$ = NODE("ExtDef", 2, $1, TOKEN("SEMI", @2.first_line)); }
        | Specifier FunDec CompSt
            { $$ = NODE("ExtDef", 3, $1, $2, $3); }
        | Specifier ExtDecList error
            {
                    report_syntax_error(@3.first_line, "Missing \";\"");
                    $$ = NULL;
                    yyerrok;
            }
        ;

ExtDecList
        : VarDec
            { $$ = NODE("ExtDecList", 1, $1); }
        | VarDec COMMA ExtDecList
            { $$ = NODE("ExtDecList", 3, $1, TOKEN("COMMA", @2.first_line), $3); }
        ;

Specifier
        : TYPE
            {
                    $$ = NODE("Specifier", 1, new_terminal("TYPE", @1.first_line, $1));
                    free($1);
            }
        | StructSpecifier
            { $$ = NODE("Specifier", 1, $1); }
        ;

StructSpecifier
        : STRUCT OptTag LC DefList RC
            {
                    $$ = NODE("StructSpecifier", 5,
                                        TOKEN("STRUCT", @1.first_line), $2,
                                        TOKEN("LC", @3.first_line), $4,
                                        TOKEN("RC", @5.first_line));
            }
        | STRUCT Tag
            { $$ = NODE("StructSpecifier", 2, TOKEN("STRUCT", @1.first_line), $2); }
        ;

OptTag
        : ID
            {
                    $$ = NODE("OptTag", 1, new_terminal("ID", @1.first_line, $1));
                    free($1);
            }
        | /* empty */
            { $$ = NULL; }
        ;

Tag
        : ID
            {
                    $$ = NODE("Tag", 1, new_terminal("ID", @1.first_line, $1));
                    free($1);
            }
        ;

VarDec
        : ID
            {
                    $$ = NODE("VarDec", 1, new_terminal("ID", @1.first_line, $1));
                    free($1);
            }
        | VarDec LB INT RB
            {
                    $$ = NODE("VarDec", 4,
                                        $1,
                                        TOKEN("LB", @2.first_line),
                                        int_terminal(@3.first_line, $3),
                                        TOKEN("RB", @4.first_line));
            }
        | VarDec LB INT error
            {
                    report_syntax_error(@4.first_line, "Missing \"]\"");
                    $$ = NULL;
                    yyerrok;
            }
        ;

FunDec
        : ID LP VarList RP
            {
                    $$ = NODE("FunDec", 4,
                                        new_terminal("ID", @1.first_line, $1),
                                        TOKEN("LP", @2.first_line),
                                        $3,
                                        TOKEN("RP", @4.first_line));
                    free($1);
            }
        | ID LP RP
            {
                    $$ = NODE("FunDec", 3,
                                        new_terminal("ID", @1.first_line, $1),
                                        TOKEN("LP", @2.first_line),
                                        TOKEN("RP", @3.first_line));
                    free($1);
            }
        | ID LP VarList error
            {
                    report_syntax_error(@4.first_line, "Missing \")\"");
                    free($1);
                    $$ = NULL;
                    yyerrok;
            }
        | ID LP error
            {
                    report_syntax_error(@3.first_line, "Missing \")\"");
                    free($1);
                    $$ = NULL;
                    yyerrok;
            }
        ;

VarList
        : ParamDec COMMA VarList
            { $$ = NODE("VarList", 3, $1, TOKEN("COMMA", @2.first_line), $3); }
        | ParamDec
            { $$ = NODE("VarList", 1, $1); }
        ;

ParamDec
        : Specifier VarDec
            { $$ = NODE("ParamDec", 2, $1, $2); }
        ;

CompSt
        : LC DefList StmtList RC
            {
                    $$ = NODE("CompSt", 4,
                                        TOKEN("LC", @1.first_line),
                                        $2,
                                        $3,
                                        TOKEN("RC", @4.first_line));
            }
        | LC DefList StmtList error
            {
                    report_syntax_error(@4.first_line, "Missing \"}\"");
                    $$ = NULL;
                    yyerrok;
            }
        ;

StmtList
        : Stmt StmtList
            { $$ = NODE("StmtList", 2, $1, $2); }
        | /* empty */
            { $$ = NULL; }
        ;

Stmt
        : Exp SEMI
            { $$ = NODE("Stmt", 2, $1, TOKEN("SEMI", @2.first_line)); }
        | CompSt
            { $$ = NODE("Stmt", 1, $1); }
        | RETURN Exp SEMI
            { $$ = NODE("Stmt", 3, TOKEN("RETURN", @1.first_line), $2, TOKEN("SEMI", @3.first_line)); }
        | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
            {
                    $$ = NODE("Stmt", 5,
                                        TOKEN("IF", @1.first_line), TOKEN("LP", @2.first_line), $3,
                                        TOKEN("RP", @4.first_line), $5);
            }
        | IF LP Exp RP Stmt ELSE Stmt
            {
                    $$ = NODE("Stmt", 7,
                                        TOKEN("IF", @1.first_line), TOKEN("LP", @2.first_line), $3,
                                        TOKEN("RP", @4.first_line), $5,
                                        TOKEN("ELSE", @6.first_line), $7);
            }
                | IF LP Exp RP Exp ELSE Stmt
                        {
                                        report_syntax_error(@6.first_line, "Missing \";\"");
                                        $$ = NULL;
                                        yyerrok;
                        }
        | WHILE LP Exp RP Stmt
            {
                    $$ = NODE("Stmt", 5,
                                        TOKEN("WHILE", @1.first_line), TOKEN("LP", @2.first_line),
                                        $3, TOKEN("RP", @4.first_line), $5);
            }
        | Exp error
            {
                    report_syntax_error(@2.first_line, "Missing \";\"");
                    $$ = NULL;
                    yyerrok;
            }
        ;

DefList
        : Def DefList
            { $$ = NODE("DefList", 2, $1, $2); }
        | /* empty */
            { $$ = NULL; }
        ;

Def
        : Specifier DecList SEMI
            { $$ = NODE("Def", 3, $1, $2, TOKEN("SEMI", @3.first_line)); }
        | Specifier DecList error
            {
                    report_syntax_error(@3.first_line, "Missing \";\"");
                    $$ = NULL;
                    yyerrok;
            }
        ;

DecList
        : Dec
            { $$ = NODE("DecList", 1, $1); }
        | Dec COMMA DecList
            { $$ = NODE("DecList", 3, $1, TOKEN("COMMA", @2.first_line), $3); }
        ;

Dec
        : VarDec
            { $$ = NODE("Dec", 1, $1); }
        | VarDec ASSIGNOP Exp
            { $$ = NODE("Dec", 3, $1, TOKEN("ASSIGNOP", @2.first_line), $3); }
        ;

Exp
        : Exp ASSIGNOP Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("ASSIGNOP", @2.first_line), $3); }
        | Exp AND Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("AND", @2.first_line), $3); }
        | Exp OR Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("OR", @2.first_line), $3); }
        | Exp RELOP Exp
            {
                    $$ = NODE("Exp", 3, $1, new_terminal("RELOP", @2.first_line, $2), $3);
                    free($2);
            }
        | Exp PLUS Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("PLUS", @2.first_line), $3); }
        | Exp MINUS Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("MINUS", @2.first_line), $3); }
        | Exp STAR Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("STAR", @2.first_line), $3); }
        | Exp DIV Exp
            { $$ = NODE("Exp", 3, $1, TOKEN("DIV", @2.first_line), $3); }
        | LP Exp RP
            { $$ = NODE("Exp", 3, TOKEN("LP", @1.first_line), $2, TOKEN("RP", @3.first_line)); }
        | MINUS Exp %prec UMINUS
            { $$ = NODE("Exp", 2, TOKEN("MINUS", @1.first_line), $2); }
        | NOT Exp
            { $$ = NODE("Exp", 2, TOKEN("NOT", @1.first_line), $2); }
        | ID LP Args RP
            {
                    $$ = NODE("Exp", 4,
                                        new_terminal("ID", @1.first_line, $1),
                                        TOKEN("LP", @2.first_line), $3,
                                        TOKEN("RP", @4.first_line));
                    free($1);
            }
        | ID LP RP
            {
                    $$ = NODE("Exp", 3,
                                        new_terminal("ID", @1.first_line, $1),
                                        TOKEN("LP", @2.first_line),
                                        TOKEN("RP", @3.first_line));
                    free($1);
            }
        | Exp LB Exp RB
            {
                    $$ = NODE("Exp", 4,
                                        $1,
                                        TOKEN("LB", @2.first_line),
                                        $3,
                                        TOKEN("RB", @4.first_line));
            }
                | Exp LB Exp COMMA Exp RB
                        {
                                        report_syntax_error(@4.first_line, "Missing \"]\"");
                                        $$ = NULL;
                        }
        | Exp LB Exp error
            {
                    report_syntax_error(@4.first_line, "Missing \"]\"");
                    $$ = NULL;
                    yyerrok;
            }
        | Exp DOT ID
            {
                    $$ = NODE("Exp", 3,
                                        $1,
                                        TOKEN("DOT", @2.first_line),
                                        new_terminal("ID", @3.first_line, $3));
                    free($3);
            }
        | ID
            {
                    $$ = NODE("Exp", 1, new_terminal("ID", @1.first_line, $1));
                    free($1);
            }
        | INT
            { $$ = NODE("Exp", 1, int_terminal(@1.first_line, $1)); }
        | FLOAT
            { $$ = NODE("Exp", 1, float_terminal(@1.first_line, $1)); }
        ;

Args
        : Exp COMMA Args
            { $$ = NODE("Args", 3, $1, TOKEN("COMMA", @2.first_line), $3); }
        | Exp
            { $$ = NODE("Args", 1, $1); }
        ;

%%

void yyerror(const char *msg) {
        (void)msg;
        report_syntax_error(yylineno, "Syntax error");
}