#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

static char *xstrdup(const char *s) {
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

/* flex/bison interfaces */
extern FILE *yyin;
extern int yylineno;
extern int yyparse(void);

#include "syntax.tab.h"

struct TreeNode {
    char *name;
    int line;
    int is_token;
    char *text;
    struct TreeNode *child;
    struct TreeNode *sibling;
};

#include "lex.yy.c"

void reset_parser_state(void);
void print_tree(TreeNode *root, int depth);

extern TreeNode *syntax_root;
extern int has_lexical_error;
extern int has_syntax_error;

static int has_semantic_error = 0;

typedef enum {
    TYPE_BASIC,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_FUNCTION,
    TYPE_ERROR
} TypeKind;

typedef enum {
    BASIC_INT,
    BASIC_FLOAT
} BasicKind;

typedef struct Type Type;

typedef struct {
    Type *return_type;
    Type **param_types;
    int param_count;
} FunctionSig;

struct Type {
    TypeKind kind;
    union {
        BasicKind basic;
        struct {
            Type *elem;
            int size;
        } array;
        char *struct_name;
        FunctionSig func;
    } u;
};

typedef struct {
    char *name;
    Type *type;
    int line;
} VarSymbol;

typedef struct {
    char *name;
    Type *type;
    int line;
} FuncSymbol;

typedef struct {
    char *name;
    Type *type;
} FieldSymbol;

typedef struct {
    char *name;
    FieldSymbol *fields;
    int field_count;
    int line;
} StructSymbol;

typedef struct {
    Type *type;
    int is_lvalue;
} ExprInfo;

typedef struct {
    int type;
    int line;
} ErrorRecord;

#define MAX_VARS 2048
#define MAX_FUNCS 512
#define MAX_STRUCTS 512
#define MAX_ERRORS 4096
#define MAX_SCOPES 512

static VarSymbol g_vars[MAX_VARS];
static int g_var_count = 0;
static FuncSymbol g_funcs[MAX_FUNCS];
static int g_func_count = 0;
static StructSymbol g_structs[MAX_STRUCTS];
static int g_struct_count = 0;
static ErrorRecord g_errors[MAX_ERRORS];
static int g_error_count = 0;
static int g_anon_struct_id = 0;
static int g_scope_var_base[MAX_SCOPES];
static int g_scope_top = 1;

static Type *g_error_type = NULL;

static void enter_scope(void) {
    if (g_scope_top < MAX_SCOPES) {
        g_scope_var_base[g_scope_top++] = g_var_count;
    }
}

static void leave_scope(void) {
    if (g_scope_top > 1) {
        g_var_count = g_scope_var_base[g_scope_top - 1];
        g_scope_top--;
    }
}

static TreeNode *child_at(TreeNode *node, int idx) {
    TreeNode *cur = node ? node->child : NULL;
    int i = 0;
    while (cur && i < idx) {
        cur = cur->sibling;
        i++;
    }
    return cur;
}

static int child_count(TreeNode *node) {
    int n = 0;
    TreeNode *cur = node ? node->child : NULL;
    while (cur) {
        n++;
        cur = cur->sibling;
    }
    return n;
}

static int is_token(TreeNode *node, const char *name) {
    return node && node->is_token && strcmp(node->name, name) == 0;
}

static int is_nonterminal(TreeNode *node, const char *name) {
    return node && !node->is_token && strcmp(node->name, name) == 0;
}

static int is_same_error_reported(int type, int line) {
    for (int i = 0; i < g_error_count; i++) {
        if (g_errors[i].type == type && g_errors[i].line == line) {
            return 1;
        }
    }
    return 0;
}

static void report_semantic_error(int type, int line, const char *fmt, ...) {
    va_list ap;
    if (line <= 0) {
        line = yylineno;
    }
    if (is_same_error_reported(type, line)) {
        return;
    }
    if (g_error_count < MAX_ERRORS) {
        g_errors[g_error_count].type = type;
        g_errors[g_error_count].line = line;
        g_error_count++;
    }
    has_semantic_error = 1;
    printf("Error type %d at Line %d: ", type, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(".\n");
}

static Type *new_type(TypeKind kind) {
    Type *t = (Type *)malloc(sizeof(Type));
    t->kind = kind;
    return t;
}

static Type *make_error_type(void) {
    if (g_error_type == NULL) {
        g_error_type = new_type(TYPE_ERROR);
    }
    return g_error_type;
}

static Type *make_basic_type(BasicKind b) {
    Type *t = new_type(TYPE_BASIC);
    t->u.basic = b;
    return t;
}

static Type *make_array_type(Type *elem, int size) {
    Type *t = new_type(TYPE_ARRAY);
    t->u.array.elem = elem;
    t->u.array.size = size;
    return t;
}

static Type *make_struct_type(const char *name) {
    Type *t = new_type(TYPE_STRUCT);
    t->u.struct_name = xstrdup(name);
    return t;
}

static Type *copy_type(Type *src);

static Type *make_function_type(Type *ret, Type **params, int param_count) {
    Type *t = new_type(TYPE_FUNCTION);
    t->u.func.return_type = copy_type(ret);
    t->u.func.param_count = param_count;
    t->u.func.param_types = NULL;
    if (param_count > 0) {
        t->u.func.param_types = (Type **)malloc(sizeof(Type *) * param_count);
        for (int i = 0; i < param_count; i++) {
            t->u.func.param_types[i] = copy_type(params[i]);
        }
    }
    return t;
}

static Type *copy_type(Type *src) {
    if (src == NULL) {
        return NULL;
    }
    if (src->kind == TYPE_ERROR) {
        return make_error_type();
    }
    if (src->kind == TYPE_BASIC) {
        return make_basic_type(src->u.basic);
    }
    if (src->kind == TYPE_ARRAY) {
        return make_array_type(copy_type(src->u.array.elem), src->u.array.size);
    }
    if (src->kind == TYPE_STRUCT) {
        return make_struct_type(src->u.struct_name);
    }
    if (src->kind == TYPE_FUNCTION) {
        return make_function_type(src->u.func.return_type, src->u.func.param_types, src->u.func.param_count);
    }
    return make_error_type();
}

static int same_type(Type *a, Type *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->kind == TYPE_ERROR || b->kind == TYPE_ERROR) {
        return 1;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
        case TYPE_BASIC:
            return a->u.basic == b->u.basic;
        case TYPE_ARRAY:
            /* Requirement says array length is ignored: base type + dimension only. */
            return same_type(a->u.array.elem, b->u.array.elem);
        case TYPE_STRUCT:
            return strcmp(a->u.struct_name, b->u.struct_name) == 0;
        case TYPE_FUNCTION:
            if (!same_type(a->u.func.return_type, b->u.func.return_type)) {
                return 0;
            }
            if (a->u.func.param_count != b->u.func.param_count) {
                return 0;
            }
            for (int i = 0; i < a->u.func.param_count; i++) {
                if (!same_type(a->u.func.param_types[i], b->u.func.param_types[i])) {
                    return 0;
                }
            }
            return 1;
        default:
            return 0;
    }
}

static int is_int(Type *t) {
    return t && t->kind == TYPE_BASIC && t->u.basic == BASIC_INT;
}

static int is_float(Type *t) {
    return t && t->kind == TYPE_BASIC && t->u.basic == BASIC_FLOAT;
}

static int is_numeric(Type *t) {
    return is_int(t) || is_float(t);
}

static int is_error(Type *t) {
    return t && t->kind == TYPE_ERROR;
}

static VarSymbol *find_var(const char *name) {
    for (int i = g_var_count - 1; i >= 0; i--) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return &g_vars[i];
        }
    }
    return NULL;
}

static FuncSymbol *find_func(const char *name) {
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) {
            return &g_funcs[i];
        }
    }
    return NULL;
}

static StructSymbol *find_struct(const char *name) {
    for (int i = 0; i < g_struct_count; i++) {
        if (strcmp(g_structs[i].name, name) == 0) {
            return &g_structs[i];
        }
    }
    return NULL;
}

static int add_var(const char *name, Type *type, int line) {
    int scope_base = g_scope_var_base[g_scope_top - 1];
    for (int i = g_var_count - 1; i >= scope_base; i--) {
        if (strcmp(g_vars[i].name, name) == 0) {
            report_semantic_error(3, line, "redefined variable '%s'", name);
            return 0;
        }
    }
    if (find_struct(name) != NULL) {
        report_semantic_error(3, line, "redefined variable '%s'", name);
        return 0;
    }
    if (g_var_count >= MAX_VARS) {
        return 0;
    }
    g_vars[g_var_count].name = xstrdup(name);
    g_vars[g_var_count].type = copy_type(type);
    g_vars[g_var_count].line = line;
    g_var_count++;
    return 1;
}

static int add_struct_symbol(StructSymbol *sym) {
    if (sym->name && sym->name[0] != '\0') {
        if (find_struct(sym->name) != NULL || find_var(sym->name) != NULL) {
            report_semantic_error(16, sym->line, "redefined struct '%s'", sym->name);
            return 0;
        }
    }
    if (g_struct_count >= MAX_STRUCTS) {
        return 0;
    }
    g_structs[g_struct_count++] = *sym;
    return 1;
}

static void define_function(const char *name, Type *ret_t, Type **params, int param_count, int line) {
    FuncSymbol *f = find_func(name);
    if (f != NULL) {
        report_semantic_error(4, line, "redefined function '%s'", name);
        return;
    }
    if (g_func_count >= MAX_FUNCS) {
        return;
    }
    g_funcs[g_func_count].name = xstrdup(name);
    g_funcs[g_func_count].type = make_function_type(ret_t, params, param_count);
    g_funcs[g_func_count].line = line;
    g_func_count++;
}

static Type *analyze_specifier(TreeNode *specifier, int line);
static ExprInfo analyze_exp(TreeNode *exp, Type *current_ret_type);

static Type *parse_vardec(TreeNode *vardec, Type *base, char **out_name, int line) {
    TreeNode *first = child_at(vardec, 0);
    if (is_token(first, "ID")) {
        if (out_name) {
            *out_name = xstrdup(first->text);
        }
        return copy_type(base);
    }
    if (is_nonterminal(first, "VarDec")) {
        Type *inner = parse_vardec(first, base, out_name, line);
        TreeNode *int_node = child_at(vardec, 2);
        int size = 0;
        if (is_token(int_node, "INT") && int_node->text) {
            size = atoi(int_node->text);
        }
        return make_array_type(inner, size);
    }
    return make_error_type();
}

static void collect_params_from_varlist(TreeNode *varlist, Type ***out_types, char ***out_names, int *out_count, int line) {
    int cap = 8;
    int n = 0;
    Type **types = (Type **)malloc(sizeof(Type *) * cap);
    char **names = (char **)malloc(sizeof(char *) * cap);

    TreeNode *cur = varlist;
    while (cur && is_nonterminal(cur, "VarList")) {
        TreeNode *param_dec = child_at(cur, 0);
        if (is_nonterminal(param_dec, "ParamDec")) {
            TreeNode *spec = child_at(param_dec, 0);
            TreeNode *vardec = child_at(param_dec, 1);
            Type *base = analyze_specifier(spec, line);
            char *name = NULL;
            Type *t = parse_vardec(vardec, base, &name, line);
            if (n >= cap) {
                cap *= 2;
                types = (Type **)realloc(types, sizeof(Type *) * cap);
                names = (char **)realloc(names, sizeof(char *) * cap);
            }
            types[n] = t;
            names[n] = name;
            n++;
        }
        if (child_count(cur) == 3) {
            cur = child_at(cur, 2);
        } else {
            break;
        }
    }

    *out_types = types;
    *out_names = names;
    *out_count = n;
}

static Type *analyze_struct_specifier(TreeNode *struct_spec, int line) {
    int cc = child_count(struct_spec);
    if (cc == 2) {
        TreeNode *tag = child_at(struct_spec, 1);
        TreeNode *id_node = tag ? child_at(tag, 0) : NULL;
        if (!is_token(id_node, "ID")) {
            return make_error_type();
        }
        StructSymbol *sym = find_struct(id_node->text);
        if (sym == NULL) {
            report_semantic_error(17, id_node->line, "undefined struct '%s'", id_node->text);
            return make_error_type();
        }
        return make_struct_type(sym->name);
    }

    TreeNode *opt_tag = child_at(struct_spec, 1);
    TreeNode *deflist = NULL;
    for (TreeNode *c = struct_spec->child; c != NULL; c = c->sibling) {
        if (is_nonterminal(c, "DefList")) {
            deflist = c;
            break;
        }
    }

    char anon_name[64];
    const char *struct_name = NULL;
    if (opt_tag && child_count(opt_tag) == 1 && is_token(child_at(opt_tag, 0), "ID")) {
        struct_name = child_at(opt_tag, 0)->text;
    } else {
        snprintf(anon_name, sizeof(anon_name), "$anon_%d", g_anon_struct_id++);
        struct_name = anon_name;
    }

    FieldSymbol *fields = NULL;
    int field_count = 0;
    int field_cap = 8;
    fields = (FieldSymbol *)malloc(sizeof(FieldSymbol) * field_cap);

    TreeNode *dl = deflist;
    while (dl && is_nonterminal(dl, "DefList")) {
        TreeNode *def = child_at(dl, 0);
        TreeNode *next_dl = child_at(dl, 1);
        if (is_nonterminal(def, "Def")) {
            TreeNode *spec = child_at(def, 0);
            TreeNode *declist = child_at(def, 1);
            Type *base = analyze_specifier(spec, def->line > 0 ? def->line : line);

            TreeNode *dc = declist;
            while (dc && is_nonterminal(dc, "DecList")) {
                TreeNode *dec = child_at(dc, 0);
                if (is_nonterminal(dec, "Dec")) {
                    TreeNode *vardec = child_at(dec, 0);
                    char *fname = NULL;
                    Type *ft = parse_vardec(vardec, base, &fname, dec->line > 0 ? dec->line : line);

                    if (child_count(dec) == 3) {
                        report_semantic_error(15, dec->line, "field '%s' initialized in struct definition", fname ? fname : "");
                    }

                    for (int i = 0; i < field_count; i++) {
                        if (fname && strcmp(fields[i].name, fname) == 0) {
                            report_semantic_error(15, dec->line, "duplicated field '%s' in struct", fname);
                            break;
                        }
                    }

                    if (field_count >= field_cap) {
                        field_cap *= 2;
                        fields = (FieldSymbol *)realloc(fields, sizeof(FieldSymbol) * field_cap);
                    }
                    fields[field_count].name = fname ? xstrdup(fname) : xstrdup("");
                    fields[field_count].type = copy_type(ft);
                    field_count++;
                }
                if (child_count(dc) == 3) {
                    dc = child_at(dc, 2);
                } else {
                    break;
                }
            }
        }
        if (next_dl && is_nonterminal(next_dl, "DefList")) {
            dl = next_dl;
        } else {
            break;
        }
    }

    StructSymbol sym;
    sym.name = xstrdup(struct_name);
    sym.fields = fields;
    sym.field_count = field_count;
    sym.line = line;
    add_struct_symbol(&sym);

    return make_struct_type(struct_name);
}

static Type *analyze_specifier(TreeNode *specifier, int line) {
    TreeNode *c0 = child_at(specifier, 0);
    if (is_token(c0, "TYPE")) {
        if (c0->text && strcmp(c0->text, "int") == 0) {
            return make_basic_type(BASIC_INT);
        }
        if (c0->text && strcmp(c0->text, "float") == 0) {
            return make_basic_type(BASIC_FLOAT);
        }
        return make_error_type();
    }
    if (is_nonterminal(c0, "StructSpecifier")) {
        return analyze_struct_specifier(c0, line);
    }
    return make_error_type();
}

static int find_field_index(StructSymbol *sym, const char *field) {
    if (!sym) {
        return -1;
    }
    for (int i = 0; i < sym->field_count; i++) {
        if (strcmp(sym->fields[i].name, field) == 0) {
            return i;
        }
    }
    return -1;
}

static int collect_arg_types(TreeNode *args, Type ***out_types) {
    int cap = 8;
    int n = 0;
    Type **types = (Type **)malloc(sizeof(Type *) * cap);
    TreeNode *cur = args;
    while (cur && is_nonterminal(cur, "Args")) {
        TreeNode *e = child_at(cur, 0);
        ExprInfo info = analyze_exp(e, NULL);
        if (n >= cap) {
            cap *= 2;
            types = (Type **)realloc(types, sizeof(Type *) * cap);
        }
        types[n++] = info.type;
        if (child_count(cur) == 3) {
            cur = child_at(cur, 2);
        } else {
            break;
        }
    }
    *out_types = types;
    return n;
}

static ExprInfo make_expr(Type *t, int lv) {
    ExprInfo e;
    e.type = t;
    e.is_lvalue = lv;
    return e;
}

static ExprInfo analyze_exp(TreeNode *exp, Type *current_ret_type) {
    (void)current_ret_type;
    if (!exp) {
        return make_expr(make_error_type(), 0);
    }

    int cc = child_count(exp);
    if (cc == 1) {
        TreeNode *c0 = child_at(exp, 0);
        if (is_token(c0, "ID")) {
            VarSymbol *v = find_var(c0->text);
            if (v) {
                return make_expr(copy_type(v->type), 1);
            }
            if (find_func(c0->text)) {
                report_semantic_error(11, c0->line, "function '%s' used as variable", c0->text);
                return make_expr(make_error_type(), 0);
            }
            report_semantic_error(1, c0->line, "undefined variable '%s'", c0->text);
            return make_expr(make_error_type(), 0);
        }
        if (is_token(c0, "INT")) {
            return make_expr(make_basic_type(BASIC_INT), 0);
        }
        if (is_token(c0, "FLOAT")) {
            return make_expr(make_basic_type(BASIC_FLOAT), 0);
        }
    }

    if (cc == 2) {
        TreeNode *c0 = child_at(exp, 0);
        TreeNode *c1 = child_at(exp, 1);
        if (is_token(c0, "MINUS")) {
            ExprInfo e1 = analyze_exp(c1, current_ret_type);
            if (!is_numeric(e1.type)) {
                report_semantic_error(7, exp->line, "unary '-' requires numeric operand");
                return make_expr(make_error_type(), 0);
            }
            return make_expr(copy_type(e1.type), 0);
        }
        if (is_token(c0, "NOT")) {
            ExprInfo e1 = analyze_exp(c1, current_ret_type);
            if (!is_int(e1.type)) {
                report_semantic_error(7, exp->line, "'!' requires int operand");
            }
            return make_expr(make_basic_type(BASIC_INT), 0);
        }
    }

    if (cc == 3) {
        TreeNode *c0 = child_at(exp, 0);
        TreeNode *c1 = child_at(exp, 1);
        TreeNode *c2 = child_at(exp, 2);

        if (is_token(c0, "LP") && is_nonterminal(c1, "Exp") && is_token(c2, "RP")) {
            return analyze_exp(c1, current_ret_type);
        }

        if (is_nonterminal(c0, "Exp") && is_token(c1, "ASSIGNOP") && is_nonterminal(c2, "Exp")) {
            ExprInfo left = analyze_exp(c0, current_ret_type);
            ExprInfo right = analyze_exp(c2, current_ret_type);
            if (!left.is_lvalue && !is_error(left.type)) {
                report_semantic_error(6, exp->line, "left side of assignment is not assignable");
            }
            if (!is_error(left.type) && !is_error(right.type) && !same_type(left.type, right.type)) {
                report_semantic_error(5, exp->line, "type mismatched for assignment");
            }
            return make_expr(copy_type(left.type), 0);
        }

        if (is_nonterminal(c0, "Exp") && (is_token(c1, "AND") || is_token(c1, "OR")) && is_nonterminal(c2, "Exp")) {
            ExprInfo a = analyze_exp(c0, current_ret_type);
            ExprInfo b = analyze_exp(c2, current_ret_type);
            if (!is_error(a.type) && !is_error(b.type) && (!is_int(a.type) || !is_int(b.type))) {
                report_semantic_error(7, exp->line, "logical operator requires int operands");
            }
            return make_expr(make_basic_type(BASIC_INT), 0);
        }

        if (is_nonterminal(c0, "Exp") && is_token(c1, "RELOP") && is_nonterminal(c2, "Exp")) {
            ExprInfo a = analyze_exp(c0, current_ret_type);
            ExprInfo b = analyze_exp(c2, current_ret_type);
            if (!is_error(a.type) && !is_error(b.type) && (!same_type(a.type, b.type) || !is_numeric(a.type))) {
                report_semantic_error(7, exp->line, "invalid operands for relational operation");
            }
            return make_expr(make_basic_type(BASIC_INT), 0);
        }

        if (is_nonterminal(c0, "Exp") && (is_token(c1, "PLUS") || is_token(c1, "MINUS") || is_token(c1, "STAR") || is_token(c1, "DIV")) && is_nonterminal(c2, "Exp")) {
            ExprInfo a = analyze_exp(c0, current_ret_type);
            ExprInfo b = analyze_exp(c2, current_ret_type);
            if (!is_error(a.type) && !is_error(b.type) && (!same_type(a.type, b.type) || !is_numeric(a.type))) {
                report_semantic_error(7, exp->line, "invalid operands for arithmetic operation");
                return make_expr(make_error_type(), 0);
            }
            return make_expr(copy_type(a.type), 0);
        }

        if (is_nonterminal(c0, "Exp") && is_token(c1, "DOT") && is_token(c2, "ID")) {
            ExprInfo base = analyze_exp(c0, current_ret_type);
            if (is_error(base.type)) {
                return make_expr(make_error_type(), 0);
            }
            if (base.type->kind != TYPE_STRUCT) {
                report_semantic_error(13, exp->line, "accessing field of non-struct variable");
                return make_expr(make_error_type(), 0);
            }
            StructSymbol *ss = find_struct(base.type->u.struct_name);
            int idx = find_field_index(ss, c2->text);
            if (idx < 0) {
                report_semantic_error(14, c2->line, "non-existent field '%s'", c2->text);
                return make_expr(make_error_type(), 0);
            }
            return make_expr(copy_type(ss->fields[idx].type), 1);
        }

        if (is_token(c0, "ID") && is_token(c1, "LP") && is_token(c2, "RP")) {
            VarSymbol *v = find_var(c0->text);
            FuncSymbol *f = find_func(c0->text);
            if (v != NULL) {
                report_semantic_error(11, c0->line, "'%s' is not a function", c0->text);
                return make_expr(make_error_type(), 0);
            }
            if (f == NULL) {
                report_semantic_error(2, c0->line, "undefined function '%s'", c0->text);
                return make_expr(make_error_type(), 0);
            }
            if (f->type->u.func.param_count != 0) {
                report_semantic_error(9, exp->line, "function '%s' arguments mismatched", c0->text);
            }
            return make_expr(copy_type(f->type->u.func.return_type), 0);
        }
    }

    if (cc == 4) {
        TreeNode *c0 = child_at(exp, 0);
        TreeNode *c1 = child_at(exp, 1);
        TreeNode *c2 = child_at(exp, 2);
        TreeNode *c3 = child_at(exp, 3);

        if (is_nonterminal(c0, "Exp") && is_token(c1, "LB") && is_nonterminal(c2, "Exp") && is_token(c3, "RB")) {
            ExprInfo arr = analyze_exp(c0, current_ret_type);
            ExprInfo idx = analyze_exp(c2, current_ret_type);
            if (is_error(arr.type)) {
                return make_expr(make_error_type(), 0);
            }
            if (arr.type->kind != TYPE_ARRAY) {
                report_semantic_error(10, exp->line, "indexing a non-array variable");
                return make_expr(make_error_type(), 0);
            }
            if (!is_error(idx.type) && !is_int(idx.type)) {
                report_semantic_error(12, c2->line, "array index is not an integer");
            }
            return make_expr(copy_type(arr.type->u.array.elem), 1);
        }

        if (is_token(c0, "ID") && is_token(c1, "LP") && is_nonterminal(c2, "Args") && is_token(c3, "RP")) {
            VarSymbol *v = find_var(c0->text);
            FuncSymbol *f = find_func(c0->text);
            if (v != NULL) {
                report_semantic_error(11, c0->line, "'%s' is not a function", c0->text);
                return make_expr(make_error_type(), 0);
            }
            if (f == NULL) {
                report_semantic_error(2, c0->line, "undefined function '%s'", c0->text);
                return make_expr(make_error_type(), 0);
            }

            Type **arg_types = NULL;
            int arg_count = collect_arg_types(c2, &arg_types);
            if (arg_count != f->type->u.func.param_count) {
                report_semantic_error(9, exp->line, "function '%s' arguments mismatched", c0->text);
            } else {
                for (int i = 0; i < arg_count; i++) {
                    if (!same_type(arg_types[i], f->type->u.func.param_types[i])) {
                        report_semantic_error(9, exp->line, "function '%s' arguments mismatched", c0->text);
                        break;
                    }
                }
            }
            free(arg_types);
            return make_expr(copy_type(f->type->u.func.return_type), 0);
        }
    }

    return make_expr(make_error_type(), 0);
}

static void analyze_stmt(TreeNode *stmt, Type *func_ret_type);

static void analyze_declist(TreeNode *declist, Type *base_type, int line) {
    TreeNode *cur = declist;
    while (cur && is_nonterminal(cur, "DecList")) {
        TreeNode *dec = child_at(cur, 0);
        if (is_nonterminal(dec, "Dec")) {
            TreeNode *vardec = child_at(dec, 0);
            char *vname = NULL;
            Type *vtype = parse_vardec(vardec, base_type, &vname, line);
            if (vname) {
                add_var(vname, vtype, dec->line > 0 ? dec->line : line);
            }
            if (child_count(dec) == 3) {
                ExprInfo rhs = analyze_exp(child_at(dec, 2), NULL);
                if (!is_error(vtype) && !is_error(rhs.type) && !same_type(vtype, rhs.type)) {
                    report_semantic_error(5, dec->line, "type mismatched for assignment");
                }
            }
        }
        TreeNode *next = (child_count(cur) == 3) ? child_at(cur, 2) : NULL;
        if (next && is_nonterminal(next, "DecList")) {
            cur = next;
        } else {
            break;
        }
    }
}

static void analyze_deflist(TreeNode *deflist) {
    TreeNode *dl = deflist;
    while (dl && is_nonterminal(dl, "DefList")) {
        TreeNode *def = child_at(dl, 0);
        if (is_nonterminal(def, "Def")) {
            TreeNode *spec = child_at(def, 0);
            TreeNode *declist = child_at(def, 1);
            Type *base = analyze_specifier(spec, def->line);
            analyze_declist(declist, base, def->line);
        }
        TreeNode *next = child_at(dl, 1);
        if (next && is_nonterminal(next, "DefList")) {
            dl = next;
        } else {
            break;
        }
    }
}

static void analyze_stmtlist(TreeNode *stmtlist, Type *func_ret_type) {
    TreeNode *sl = stmtlist;
    while (sl && is_nonterminal(sl, "StmtList")) {
        TreeNode *stmt = child_at(sl, 0);
        if (stmt && is_nonterminal(stmt, "Stmt")) {
            analyze_stmt(stmt, func_ret_type);
        }
        TreeNode *next = child_at(sl, 1);
        if (next && is_nonterminal(next, "StmtList")) {
            sl = next;
        } else {
            break;
        }
    }
}

static void analyze_compst_body(TreeNode *compst, Type *func_ret_type) {
    if (!compst || !is_nonterminal(compst, "CompSt")) {
        return;
    }
    TreeNode *deflist = NULL;
    TreeNode *stmtlist = NULL;
    for (TreeNode *c = compst->child; c != NULL; c = c->sibling) {
        if (is_nonterminal(c, "DefList")) {
            deflist = c;
        } else if (is_nonterminal(c, "StmtList")) {
            stmtlist = c;
        }
    }
    analyze_deflist(deflist);
    analyze_stmtlist(stmtlist, func_ret_type);
}

static void analyze_compst(TreeNode *compst, Type *func_ret_type) {
    enter_scope();
    analyze_compst_body(compst, func_ret_type);
    leave_scope();
}

static void analyze_stmt(TreeNode *stmt, Type *func_ret_type) {
    if (!stmt || !is_nonterminal(stmt, "Stmt")) {
        return;
    }
    int cc = child_count(stmt);
    TreeNode *c0 = child_at(stmt, 0);

    if (cc == 1 && is_nonterminal(c0, "CompSt")) {
        analyze_compst(c0, func_ret_type);
        return;
    }

    if (cc == 2 && is_nonterminal(c0, "Exp")) {
        analyze_exp(c0, func_ret_type);
        return;
    }

    if (cc == 3 && is_token(c0, "RETURN")) {
        ExprInfo e = analyze_exp(child_at(stmt, 1), func_ret_type);
        if (!is_error(e.type) && !same_type(e.type, func_ret_type)) {
            report_semantic_error(8, stmt->line, "return type mismatched");
        }
        return;
    }

    if (cc == 5 && is_token(c0, "IF")) {
        ExprInfo cond = analyze_exp(child_at(stmt, 2), func_ret_type);
        if (!is_error(cond.type) && !is_int(cond.type)) {
            report_semantic_error(7, child_at(stmt, 2)->line, "if condition must be int");
        }
        analyze_stmt(child_at(stmt, 4), func_ret_type);
        return;
    }

    if (cc == 7 && is_token(c0, "IF")) {
        ExprInfo cond = analyze_exp(child_at(stmt, 2), func_ret_type);
        if (!is_error(cond.type) && !is_int(cond.type)) {
            report_semantic_error(7, child_at(stmt, 2)->line, "if condition must be int");
        }
        analyze_stmt(child_at(stmt, 4), func_ret_type);
        analyze_stmt(child_at(stmt, 6), func_ret_type);
        return;
    }

    if (cc == 5 && is_token(c0, "WHILE")) {
        ExprInfo cond = analyze_exp(child_at(stmt, 2), func_ret_type);
        if (!is_error(cond.type) && !is_int(cond.type)) {
            report_semantic_error(7, child_at(stmt, 2)->line, "while condition must be int");
        }
        analyze_stmt(child_at(stmt, 4), func_ret_type);
        return;
    }
}

static void analyze_extdeclist(TreeNode *extdeclist, Type *base_type, int line) {
    TreeNode *cur = extdeclist;
    while (cur && is_nonterminal(cur, "ExtDecList")) {
        TreeNode *vardec = child_at(cur, 0);
        char *name = NULL;
        Type *vt = parse_vardec(vardec, base_type, &name, line);
        if (name) {
            add_var(name, vt, vardec->line > 0 ? vardec->line : line);
        }
        TreeNode *next = (child_count(cur) == 3) ? child_at(cur, 2) : NULL;
        if (next && is_nonterminal(next, "ExtDecList")) {
            cur = next;
        } else {
            break;
        }
    }
}

static void analyze_fundec_signature(TreeNode *fundec, Type ***out_param_types, char ***out_param_names, int *out_param_count, char **out_func_name, int line) {
    TreeNode *id_node = child_at(fundec, 0);
    *out_func_name = (id_node && id_node->text) ? xstrdup(id_node->text) : xstrdup("");
    *out_param_types = NULL;
    *out_param_names = NULL;
    *out_param_count = 0;

    if (child_count(fundec) == 4) {
        TreeNode *varlist = child_at(fundec, 2);
        collect_params_from_varlist(varlist, out_param_types, out_param_names, out_param_count, line);
    }
}

static void analyze_extdef(TreeNode *extdef) {
    if (!extdef || !is_nonterminal(extdef, "ExtDef")) {
        return;
    }

    int cc = child_count(extdef);
    if (cc == 1) {
        TreeNode *spec = child_at(extdef, 0);
        if (is_nonterminal(spec, "Specifier")) {
            (void)analyze_specifier(spec, extdef->line);
        }
        return;
    }

    TreeNode *spec = child_at(extdef, 0);
    TreeNode *c1 = child_at(extdef, 1);
    TreeNode *c2 = child_at(extdef, 2);

    if (!is_nonterminal(spec, "Specifier")) {
        return;
    }

    if (cc == 2 && is_token(c1, "SEMI")) {
        (void)analyze_specifier(spec, extdef->line);
        return;
    }

    if (cc == 3 && is_nonterminal(c1, "ExtDecList") && is_token(c2, "SEMI")) {
        Type *base = analyze_specifier(spec, extdef->line);
        analyze_extdeclist(c1, base, extdef->line);
        return;
    }

    if (cc == 3 && is_nonterminal(c1, "FunDec") && is_nonterminal(c2, "CompSt")) {
        Type *ret_type = analyze_specifier(spec, extdef->line);
        Type **param_types = NULL;
        char **param_names = NULL;
        int param_count = 0;
        char *func_name = NULL;

        analyze_fundec_signature(c1, &param_types, &param_names, &param_count, &func_name, extdef->line);
        define_function(func_name, ret_type, param_types, param_count, extdef->line);

        enter_scope();
        for (int i = 0; i < param_count; i++) {
            if (param_names[i]) {
                add_var(param_names[i], param_types[i], extdef->line);
            }
        }

        analyze_compst_body(c2, ret_type);
        leave_scope();

        free(param_types);
        free(param_names);
        free(func_name);
        return;
    }
}

static void analyze_extdeflist(TreeNode *extdeflist) {
    TreeNode *cur = extdeflist;
    while (cur && is_nonterminal(cur, "ExtDefList")) {
        analyze_extdef(child_at(cur, 0));
        TreeNode *next = child_at(cur, 1);
        if (next && is_nonterminal(next, "ExtDefList")) {
            cur = next;
        } else {
            break;
        }
    }
}

static void analyze_program(TreeNode *program) {
    if (!program || !is_nonterminal(program, "Program")) {
        return;
    }
    analyze_extdeflist(child_at(program, 0));
}

static void reset_semantic_state(void) {
    has_semantic_error = 0;
    g_var_count = 0;
    g_func_count = 0;
    g_struct_count = 0;
    g_error_count = 0;
    g_anon_struct_id = 0;
    g_scope_top = 1;
    g_scope_var_base[0] = 0;
}

static void parse_file(FILE *file) {
    reset_parser_state();
    reset_semantic_state();

    yyin = file;
    yylineno = 1;
    yyrestart(yyin);

    yyparse();
    if (!has_lexical_error && !has_syntax_error && syntax_root != NULL) {
        analyze_program(syntax_root);
    }
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        return 0;
    }

    for (int i = 1; i < argc; i++) {
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
