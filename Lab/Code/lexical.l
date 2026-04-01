%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.tab.h"   /* token defs, YYSTYPE */

extern YYLTYPE yylloc;
void report_lexical_error(int line, const char *msg, const char *lexeme);

static char *dup_lexeme(const char *s) {
   size_t n;
   char *p;
   if (s == NULL) {
      return NULL;
   }
   n = strlen(s) + 1;
   p = (char *)malloc(n);
   if (p == NULL) {
      return NULL;
   }
   memcpy(p, s, n);
   return p;
}

#define YY_USER_ACTION yylloc.first_line = yylloc.last_line = yylineno;
%}

%option yylineno noyywrap
%x COMMENT

DIGIT    [0-9]
ID_START [_A-Za-z]
ID_BODY  [_A-Za-z0-9]
ID       {ID_START}{ID_BODY}{0,30}

DEC_INT  0|[1-9]{DIGIT}*
OCT_INT  0[0-7]+
HEX_INT  0[xX][0-9a-fA-F]+

FLOAT_DEC ({DIGIT}+\.{DIGIT}*|\.{DIGIT}+)
FLOAT_EXP ({DIGIT}+\.{DIGIT}*|\.{DIGIT}+)[eE][+-]?{DIGIT}+

BAD_OCT  0[0-7]*[89]{DIGIT}*
BAD_HEX  0[xX][0-9a-fA-F]*[g-zG-Z_][A-Za-z0-9_]*
BAD_EXP  ({DIGIT}+\.{DIGIT}*|\.{DIGIT}+)[eE][+-]?

WS       [ \t\r]+

%%


"//"[^\n]*         { /* skip line comment */ }
"/*"               { BEGIN(COMMENT); }
<COMMENT>"*/"      { BEGIN(INITIAL); }
<COMMENT>\n        { /* yylineno auto increment */ }
<COMMENT>.         { /* skip comment body */ }
<COMMENT><<EOF>>   {
                      report_lexical_error(yylineno, "Unterminated comment", "");
                      BEGIN(INITIAL);
                      return 0;
                   }


\n                { /* ignore newline, yylineno updates automatically */ }
{WS}              { /* ignore whitespace */ }


"int"             { yylval.sval = dup_lexeme(yytext); return TYPE; }
"float"           { yylval.sval = dup_lexeme(yytext); return TYPE; }
"struct"          { return STRUCT; }
"return"          { return RETURN; }
"if"              { return IF; }
"else"            { return ELSE; }
"while"           { return WHILE; }

"&&"              { return AND; }
"||"              { return OR; }
"!"               { return NOT; }

"<="              { yylval.sval = dup_lexeme(yytext); return RELOP; }
">="              { yylval.sval = dup_lexeme(yytext); return RELOP; }
"=="              { yylval.sval = dup_lexeme(yytext); return RELOP; }
"!="              { yylval.sval = dup_lexeme(yytext); return RELOP; }
"<"               { yylval.sval = dup_lexeme(yytext); return RELOP; }
">"               { yylval.sval = dup_lexeme(yytext); return RELOP; }

";"               { return SEMI; }
","               { return COMMA; }
"="               { return ASSIGNOP; }
"+"               { return PLUS; }
"-"               { return MINUS; }
"*"               { return STAR; }
"/"               { return DIV; }
"("               { return LP; }
")"               { return RP; }
"["               { return LB; }
"]"               { return RB; }
"{"               { return LC; }
"}"               { return RC; }
"."               { return DOT; }


{BAD_HEX}         {
                     report_lexical_error(yylineno, "Illegal hexadecimal number", yytext);
                     /* Return a placeholder integer token so parser can continue. */
                     yylval.ival = 0;
                     return INT;
                  }
{BAD_OCT}         {
                     report_lexical_error(yylineno, "Illegal octal number", yytext);
                     /* Return a placeholder integer token so parser can continue. */
                     yylval.ival = 0;
                     return INT;
                  }
{BAD_EXP}         {
                     report_lexical_error(yylineno, "Illegal floating point number", yytext);
                     /* Return a placeholder float token so parser can continue. */
                     yylval.fval = 0.0f;
                     return FLOAT;
                  }

{FLOAT_EXP}       { yylval.fval = strtof(yytext, NULL); return FLOAT; }
{FLOAT_DEC}       { yylval.fval = strtof(yytext, NULL); return FLOAT; }

{HEX_INT}         { yylval.ival = (int)strtol(yytext, NULL, 16); return INT; }
{OCT_INT}         { yylval.ival = (int)strtol(yytext, NULL, 8); return INT; }
{DEC_INT}         { yylval.ival = (int)strtol(yytext, NULL, 10); return INT; }

{ID}              { yylval.sval = dup_lexeme(yytext); return ID; }

.                 { report_lexical_error(yylineno, "Mysterious character", yytext); }

%%
