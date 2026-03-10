%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.tab.h"   /* token defs, YYSTYPE */

/* codes for relational operators stored in yylval.op */
#define LE 1
#define GE 2
#define EQ 3
#define NE 4
#define LT 5
#define GT 6

void yyerror(const char *s);
%}

%option yylineno 

DIGIT    [0-9]
LETTER   [A-Za-z]
ID       {LETTER}({LETTER}|{DIGIT}|_){0,51}
INT_LIT  {DIGIT}+
FLOAT_LIT {DIGIT}+"."{DIGIT}+

WS       [ \t\r\n]+    

%%


{WS}              { /* no actions*/ printf("WS "); }


"int"              { printf("int TYPE ");return TYPE; }
"float"            { printf("float TYPE ");return TYPE; }

"struct"           { printf("STRUCT ");return STRUCT; }
"return"           { printf("RETURN ");return RETURN; }
"if"               { printf("IF ");return IF; }
"else"             { printf("ELSE ");return ELSE; }
"while"            { printf("WHILE ");return WHILE; }

"&&"               { printf("AND ");return AND; }
"\|\|"             { printf("OR ");return OR; }
"!"                { printf("NOT ");return NOT; }

"<="               { printf("LE ");yylval.op = LE; return RELOP; }
">="               { printf("GE ");yylval.op = GE; return RELOP; }
"=="               { printf("EQ ");yylval.op = EQ; return RELOP; }
"!="               { printf("NE ");yylval.op = NE; return RELOP; }
"<"                { printf("LT ");yylval.op = LT; return RELOP; }
">"                { printf("GT ");yylval.op = GT; return RELOP; }

";"                { printf("SEMI ");return SEMI; }
","                { printf("COMMA ");return COMMA; }
"="                { printf("ASSIGNOP ");return ASSIGNOP; }
"+"                { printf("PLUS ");return PLUS; }
"-"                { printf("MINUS ");return MINUS; }
"*"                { printf("STAR ");return STAR; }
"/"                { printf("DIV ");return DIV; }
"("                { printf("LP ");return LP; }
")"                { printf("RP ");return RP; }
"["                { printf("LB ");return LB; }
"]"                { printf("RB ");return RB; }
"{"                { printf("LC ");return LC; }
"}"                { printf("RC ");return RC; }
"."                { printf("DOT ");return DOT; }

{FLOAT_LIT}        { printf("FLOAT_LIT ");yylval.fval = atof(yytext); return FLOAT; }
{INT_LIT}          { printf("INT_LIT ");yylval.ival = atoi(yytext); return INT; }

{ID}               {
                     if (strcmp(yytext,"int")==0 || strcmp(yytext,"float")==0 ||
                         strcmp(yytext,"struct")==0 || strcmp(yytext,"return")==0 ||
                         strcmp(yytext,"if")==0 || strcmp(yytext,"else")==0 ||
                         strcmp(yytext,"while")==0) {
                     }
                     yylval.sval = strdup(yytext);
                     printf("ID ");
                     return ID;
                   }

.                  { yyerror("unknown string"); }

%%


void yyerror(const char *s) {
    fprintf(stderr, "line %d: %s at '%s'\n", yylineno, s, yytext);
}

int yywrap(void) {
    return 1;
}
