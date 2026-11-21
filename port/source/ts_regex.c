/* ts_regex.c -- POSIX regcomp / regexec / regfree for the game, as bionic
 * lays them out.
 *
 * Why: libTestSuite.so's shader loader expands the "#include <...>" and
 * '#include "..."' lines of its GLSL sources with
 *   regcomp("(#include ((<[^>]+>)|(\"[^\"]+\")))", REG_EXTENDED)
 * and regexec's submatches. newlib (devkitARM) has no regex, and with
 * regcomp failing the includes stayed in the sources: every shader from the
 * OBB failed to compile ("0:1(1): error: syntax error"), nothing linked, and
 * the game ran at 60 fps drawing black after the title (hardware 2026-11-18,
 * 4th run).
 *
 * What: a small POSIX extended regular expression engine. The pattern is
 * parsed (recursive descent) into a program for a backtracking machine, in
 * the style of Russ Cox's re1: CHAR / ANY / CLASS / BOL / EOL, SPLIT and JMP
 * for alternation and repetition, SAVE for the groups' bounds, MATCH.
 * Supported: literals, ., [...] / [^...] with ranges and [:class:], ^ $,
 * ( ) groups, |, * + ? and {m} {m,} {m,n} (the bounds expanded, at most 32),
 * \ escapes; REG_ICASE, REG_NOSUB, REG_NEWLINE (. and [^..] do not match
 * '\n', ^ $ at line ends); REG_NOTBOL / REG_NOTEOL. The match is the
 * leftmost one, alternatives and repeats tried in order (greedy) -- not
 * POSIX's leftmost-LONGEST, which agrees with it on patterns like the one
 * above. Backtracking recursion is capped, so a pathological pattern fails
 * (REG_NOMATCH) instead of running out of stack.
 *
 * The layouts are bionic's (NetBSD's regex.h) on armeabi-v7a:
 *   regex_t    { int re_magic; size_t re_nsub; const char *re_endp;
 *                struct re_guts *re_g; }          16 bytes
 *   regmatch_t { regoff_t rm_so, rm_eo; }         regoff_t = off_t, 32-bit
 * The compiled program hangs from re_g. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TS_REGEX_TEST
#include <stdio.h>
#define debugPrintf printf
#else
#include "util.h"
#endif

/* ---- bionic's ABI ---- */
typedef struct {
  int re_magic;
  uint32_t re_nsub;
  const char *re_endp;
  void *re_g;
} ts_regex_t;
typedef struct {
  int32_t rm_so, rm_eo;
} ts_regmatch_t;

enum { REG_EXTENDED = 1, REG_ICASE = 2, REG_NOSUB = 4, REG_NEWLINE = 8 };
enum { REG_NOTBOL = 1, REG_NOTEOL = 2 };
enum { REG_NOMATCH = 1, REG_BADPAT = 2, REG_EBRACK = 7, REG_EPAREN = 8, REG_BADRPT = 13, REG_ESPACE = 12 };
#define TS_RE_MAGIC 0x54535245 /* 'TSRE' */
#define MAX_GROUPS 32
#define MAX_DEPTH 20000

/* ---- the program ---- */
enum { I_CHAR, I_ANY, I_CLASS, I_BOL, I_EOL, I_SPLIT, I_JMP, I_SAVE, I_MATCH };

typedef struct {
  uint8_t op;
  uint8_t c;          /* I_CHAR */
  uint16_t cls;       /* I_CLASS: index into classes */
  int x, y;           /* I_SPLIT (x preferred), I_JMP (x), I_SAVE (x = slot) */
} Inst;

typedef struct {
  uint8_t bits[32]; /* 256 bits */
} Class;

typedef struct {
  Inst *code;
  int n, cap;
  Class *classes;
  int ncls, capcls;
  int ngroups; /* including group 0 */
  int cflags;
} Prog;

/* ---- AST ---- */
enum { N_CHAR, N_ANY, N_CLASS, N_BOL, N_EOL, N_CAT, N_ALT, N_GROUP, N_REP, N_EMPTY };
typedef struct Node {
  int type;
  int c, cls, group, min, max;
  struct Node *a, *b;
} Node;

typedef struct {
  const char *p;
  int err;
  int ngroups;
  Prog *prog;
  int cflags;
} Parser;

static Node *node(Parser *ps, int type) {
  Node *n = calloc(1, sizeof *n);
  if (!n)
    ps->err = REG_ESPACE;
  else
    n->type = type;
  return n;
}

static void free_ast(Node *n) {
  if (!n)
    return;
  free_ast(n->a);
  free_ast(n->b);
  free(n);
}

static int add_class(Prog *pg, const Class *c) {
  if (pg->ncls == pg->capcls) {
    int cap = pg->capcls ? pg->capcls * 2 : 8;
    Class *nc = realloc(pg->classes, (size_t)cap * sizeof *nc);
    if (!nc)
      return -1;
    pg->classes = nc;
    pg->capcls = cap;
  }
  pg->classes[pg->ncls] = *c;
  return pg->ncls++;
}

static void set_bit(Class *c, int ch) { c->bits[(ch & 0xff) >> 3] |= (uint8_t)(1u << (ch & 7)); }
static int has_bit(const Class *c, int ch) { return c->bits[(ch & 0xff) >> 3] >> (ch & 7) & 1; }

static int is_lower(int c) { return c >= 'a' && c <= 'z'; }
static int is_upper(int c) { return c >= 'A' && c <= 'Z'; }
static int to_lower(int c) { return is_upper(c) ? c + 32 : c; }
static int to_upper(int c) { return is_lower(c) ? c - 32 : c; }

static int named_class(const char *name, size_t len, int ch) {
#define IS(s) (len == sizeof(s) - 1 && !memcmp(name, s, len))
  if (IS("alpha")) return is_lower(ch) || is_upper(ch);
  if (IS("digit")) return ch >= '0' && ch <= '9';
  if (IS("alnum")) return is_lower(ch) || is_upper(ch) || (ch >= '0' && ch <= '9');
  if (IS("upper")) return is_upper(ch);
  if (IS("lower")) return is_lower(ch);
  if (IS("space")) return ch == ' ' || (ch >= '\t' && ch <= '\r');
  if (IS("blank")) return ch == ' ' || ch == '\t';
  if (IS("punct")) return ch > 32 && ch < 127 && !(is_lower(ch) || is_upper(ch) || (ch >= '0' && ch <= '9'));
  if (IS("xdigit")) return (ch >= '0' && ch <= '9') || (to_lower(ch) >= 'a' && to_lower(ch) <= 'f');
  if (IS("cntrl")) return ch < 32 || ch == 127;
  if (IS("print")) return ch >= 32 && ch < 127;
  if (IS("graph")) return ch > 32 && ch < 127;
#undef IS
  return -1;
}

static Node *parse_alt(Parser *ps);

static Node *parse_bracket(Parser *ps) {
  Class c;
  memset(&c, 0, sizeof c);
  int neg = 0;
  if (*ps->p == '^') {
    neg = 1;
    ps->p++;
  }
  int first = 1;
  while (*ps->p && (first || *ps->p != ']')) {
    first = 0;
    int lo;
    if (ps->p[0] == '[' && ps->p[1] == ':') {
      const char *name = ps->p + 2, *end = strstr(name, ":]");
      if (!end) {
        ps->err = REG_EBRACK;
        return NULL;
      }
      for (int ch = 0; ch < 256; ch++) {
        int r = named_class(name, (size_t)(end - name), ch);
        if (r < 0) {
          ps->err = REG_BADPAT;
          return NULL;
        }
        if (r)
          set_bit(&c, ch);
      }
      ps->p = end + 2;
      continue;
    }
    lo = (unsigned char)*ps->p++;
    if (ps->p[0] == '-' && ps->p[1] && ps->p[1] != ']') {
      int hi = (unsigned char)ps->p[1];
      ps->p += 2;
      for (int ch = lo; ch <= hi; ch++)
        set_bit(&c, ch);
    } else {
      set_bit(&c, lo);
    }
  }
  if (*ps->p != ']') {
    ps->err = REG_EBRACK;
    return NULL;
  }
  ps->p++;
  if (ps->cflags & REG_ICASE)
    for (int ch = 0; ch < 256; ch++)
      if (has_bit(&c, ch)) {
        set_bit(&c, to_lower(ch));
        set_bit(&c, to_upper(ch));
      }
  if (neg) {
    for (int i = 0; i < 32; i++)
      c.bits[i] = (uint8_t)~c.bits[i];
    if (ps->cflags & REG_NEWLINE)
      c.bits['\n' >> 3] &= (uint8_t)~(1u << ('\n' & 7));
  }
  Node *n = node(ps, N_CLASS);
  if (n) {
    n->cls = add_class(ps->prog, &c);
    if (n->cls < 0)
      ps->err = REG_ESPACE;
  }
  return n;
}

static Node *parse_atom(Parser *ps) {
  char ch = *ps->p;
  if (ch == '(') {
    ps->p++;
    int g = ps->ngroups++;
    Node *inner = *ps->p == ')' ? node(ps, N_EMPTY) : parse_alt(ps);
    if (ps->err)
      return inner;
    if (*ps->p != ')') {
      ps->err = REG_EPAREN;
      return inner;
    }
    ps->p++;
    Node *n = node(ps, N_GROUP);
    if (n) {
      n->group = g;
      n->a = inner;
    }
    return n;
  }
  if (ch == '[') {
    ps->p++;
    return parse_bracket(ps);
  }
  ps->p++;
  if (ch == '.')
    return node(ps, N_ANY);
  if (ch == '^')
    return node(ps, N_BOL);
  if (ch == '$')
    return node(ps, N_EOL);
  if (ch == '\\') {
    if (!*ps->p) {
      ps->err = REG_BADPAT;
      return NULL;
    }
    ch = *ps->p++;
  }
  Node *n = node(ps, N_CHAR);
  if (n)
    n->c = (unsigned char)ch;
  return n;
}

static int parse_int(Parser *ps) {
  int v = -1;
  while (*ps->p >= '0' && *ps->p <= '9')
    v = (v < 0 ? 0 : v * 10) + (*ps->p++ - '0');
  return v;
}

static Node *parse_repeat(Parser *ps) {
  Node *a = parse_atom(ps);
  while (!ps->err && a && (*ps->p == '*' || *ps->p == '+' || *ps->p == '?' || *ps->p == '{')) {
    int min, max;
    char q = *ps->p++;
    if (q == '*')
      min = 0, max = -1;
    else if (q == '+')
      min = 1, max = -1;
    else if (q == '?')
      min = 0, max = 1;
    else {
      min = parse_int(ps);
      max = min;
      if (*ps->p == ',') {
        ps->p++;
        max = parse_int(ps); /* -1: no bound */
      }
      if (min < 0 || *ps->p != '}' || (max >= 0 && max < min) || min > 32 || max > 32) {
        ps->err = REG_BADRPT;
        return a;
      }
      ps->p++;
    }
    Node *r = node(ps, N_REP);
    if (!r)
      return a;
    r->a = a;
    r->min = min;
    r->max = max;
    a = r;
  }
  return a;
}

static Node *parse_cat(Parser *ps) {
  Node *left = NULL;
  while (!ps->err && *ps->p && *ps->p != '|' && *ps->p != ')') {
    Node *r = parse_repeat(ps);
    if (!left) {
      left = r;
    } else {
      Node *c = node(ps, N_CAT);
      if (!c)
        return left;
      c->a = left;
      c->b = r;
      left = c;
    }
  }
  return left ? left : node(ps, N_EMPTY);
}

static Node *parse_alt(Parser *ps) {
  Node *left = parse_cat(ps);
  while (!ps->err && *ps->p == '|') {
    ps->p++;
    Node *right = parse_cat(ps);
    Node *n = node(ps, N_ALT);
    if (!n)
      return left;
    n->a = left;
    n->b = right;
    left = n;
  }
  return left;
}

/* ---- code generation ---- */
static int emit(Prog *pg, int op) {
  if (pg->n == pg->cap) {
    int cap = pg->cap ? pg->cap * 2 : 64;
    Inst *nc = realloc(pg->code, (size_t)cap * sizeof *nc);
    if (!nc)
      return -1;
    pg->code = nc;
    pg->cap = cap;
  }
  memset(&pg->code[pg->n], 0, sizeof(Inst));
  pg->code[pg->n].op = (uint8_t)op;
  return pg->n++;
}

static int gen(Prog *pg, const Node *n) {
  int i, j;
  switch (n->type) {
  case N_EMPTY:
    return 0;
  case N_CHAR:
    if ((i = emit(pg, I_CHAR)) < 0)
      return -1;
    pg->code[i].c = (uint8_t)n->c;
    return 0;
  case N_ANY:
    return emit(pg, I_ANY) < 0 ? -1 : 0;
  case N_CLASS:
    if ((i = emit(pg, I_CLASS)) < 0)
      return -1;
    pg->code[i].cls = (uint16_t)n->cls;
    return 0;
  case N_BOL:
    return emit(pg, I_BOL) < 0 ? -1 : 0;
  case N_EOL:
    return emit(pg, I_EOL) < 0 ? -1 : 0;
  case N_CAT:
    return gen(pg, n->a) || gen(pg, n->b) ? -1 : 0;
  case N_GROUP:
    if ((i = emit(pg, I_SAVE)) < 0)
      return -1;
    pg->code[i].x = 2 * n->group;
    if (gen(pg, n->a) || (j = emit(pg, I_SAVE)) < 0)
      return -1;
    pg->code[j].x = 2 * n->group + 1;
    return 0;
  case N_ALT: /* SPLIT L1, L2; L1: a; JMP L3; L2: b; L3: */
    if ((i = emit(pg, I_SPLIT)) < 0)
      return -1;
    pg->code[i].x = pg->n;
    if (gen(pg, n->a) || (j = emit(pg, I_JMP)) < 0)
      return -1;
    pg->code[i].y = pg->n;
    if (gen(pg, n->b))
      return -1;
    pg->code[j].x = pg->n;
    return 0;
  case N_REP: {
    for (int k = 0; k < n->min; k++)
      if (gen(pg, n->a))
        return -1;
    if (n->max < 0) { /* L1: SPLIT L2, L3; L2: a; JMP L1; L3: */
      int l1 = emit(pg, I_SPLIT);
      if (l1 < 0)
        return -1;
      pg->code[l1].x = pg->n;
      if (gen(pg, n->a) || (j = emit(pg, I_JMP)) < 0)
        return -1;
      pg->code[j].x = l1;
      pg->code[l1].y = pg->n;
      return 0;
    }
    /* up to max - min optional copies: SPLIT L2, END; L2: a; ... */
    int opt = n->max - n->min, fix[32], nfix = 0;
    for (int k = 0; k < opt; k++) {
      if ((i = emit(pg, I_SPLIT)) < 0)
        return -1;
      pg->code[i].x = pg->n;
      fix[nfix++] = i;
      if (gen(pg, n->a))
        return -1;
    }
    for (int k = 0; k < nfix; k++)
      pg->code[fix[k]].y = pg->n;
    return 0;
  }
  }
  return -1;
}

/* ---- the backtracking machine ---- */
typedef struct {
  const Prog *pg;
  const char *str, *end, *bol_base;
  int eflags;
  int depth;
} Run;

static int run(Run *r, int pc, const char *sp, const char **cap) {
  if (++r->depth > MAX_DEPTH)
    return 0;
  const Prog *pg = r->pg;
  int icase = pg->cflags & REG_ICASE, nl = pg->cflags & REG_NEWLINE;
  for (;;) {
    const Inst *in = &pg->code[pc];
    switch (in->op) {
    case I_CHAR:
      if (sp >= r->end)
        goto fail;
      if (icase ? to_lower((unsigned char)*sp) != to_lower(in->c) : (unsigned char)*sp != in->c)
        goto fail;
      sp++, pc++;
      continue;
    case I_ANY:
      if (sp >= r->end || (nl && *sp == '\n'))
        goto fail;
      sp++, pc++;
      continue;
    case I_CLASS:
      if (sp >= r->end || !has_bit(&pg->classes[in->cls], (unsigned char)*sp))
        goto fail;
      sp++, pc++;
      continue;
    case I_BOL:
      if (sp == r->bol_base ? (r->eflags & REG_NOTBOL) : !(nl && sp[-1] == '\n'))
        goto fail;
      pc++;
      continue;
    case I_EOL:
      if (sp == r->end ? (r->eflags & REG_NOTEOL) : !(nl && *sp == '\n'))
        goto fail;
      pc++;
      continue;
    case I_JMP:
      pc = in->x;
      continue;
    case I_SPLIT:
      if (run(r, in->x, sp, cap)) {
        r->depth--;
        return 1;
      }
      pc = in->y;
      continue;
    case I_SAVE: {
      const char *old = cap[in->x];
      cap[in->x] = sp;
      if (run(r, pc + 1, sp, cap)) {
        r->depth--;
        return 1;
      }
      cap[in->x] = old;
      goto fail;
    }
    case I_MATCH:
      cap[1] = sp; /* group 0's end (its start is set by the caller) */
      r->depth--;
      return 1;
    }
  }
fail:
  r->depth--;
  return 0;
}

/* ---- the API ---- */
int b_regcomp(void *preg_, const char *pattern, int cflags) {
  ts_regex_t *preg = preg_;
  if (!preg || !pattern)
    return REG_BADPAT;
  Prog *pg = calloc(1, sizeof *pg);
  if (!pg)
    return REG_ESPACE;
  pg->cflags = cflags;
  Parser ps = {.p = pattern, .ngroups = 1, .prog = pg, .cflags = cflags};
  Node *ast = parse_alt(&ps);
  if (!ps.err && *ps.p)
    ps.err = *ps.p == ')' ? REG_EPAREN : REG_BADPAT;
  if (!ps.err && ps.ngroups > MAX_GROUPS)
    ps.err = REG_ESPACE;
  if (!ps.err && (gen(pg, ast) || emit(pg, I_MATCH) < 0))
    ps.err = REG_ESPACE;
  free_ast(ast);
  if (ps.err) {
    debugPrintf("[regex] regcomp(\"%s\", 0x%x): error %d\n", pattern, cflags, ps.err);
    free(pg->code);
    free(pg->classes);
    free(pg);
    return ps.err;
  }
  pg->ngroups = ps.ngroups;
  preg->re_magic = TS_RE_MAGIC;
  preg->re_nsub = (uint32_t)(ps.ngroups - 1);
  preg->re_endp = pattern + strlen(pattern);
  preg->re_g = pg;
  return 0;
}

int b_regexec(const void *preg_, const char *string, unsigned nmatch, void *pmatch_, int eflags) {
  const ts_regex_t *preg = preg_;
  ts_regmatch_t *pmatch = pmatch_;
  if (!preg || preg->re_magic != TS_RE_MAGIC || !string)
    return REG_BADPAT;
  const Prog *pg = preg->re_g;
  if (pg->cflags & REG_NOSUB)
    nmatch = 0;
  const char *cap[2 * MAX_GROUPS];
  Run r = {.pg = pg, .str = string, .end = string + strlen(string), .bol_base = string,
           .eflags = eflags};
  for (const char *start = string;; start++) {
    for (int i = 0; i < 2 * pg->ngroups; i++)
      cap[i] = NULL;
    cap[0] = start;
    r.depth = 0;
    if (run(&r, 0, start, cap)) {
      for (unsigned i = 0; i < nmatch; i++) {
        if ((int)i < pg->ngroups && cap[2 * i] && cap[2 * i + 1]) {
          pmatch[i].rm_so = (int32_t)(cap[2 * i] - string);
          pmatch[i].rm_eo = (int32_t)(cap[2 * i + 1] - string);
        } else {
          pmatch[i].rm_so = pmatch[i].rm_eo = -1;
        }
      }
      return 0;
    }
    if (start >= r.end)
      break;
  }
  return REG_NOMATCH;
}

void b_regfree(void *preg_) {
  ts_regex_t *preg = preg_;
  if (!preg || preg->re_magic != TS_RE_MAGIC)
    return;
  Prog *pg = preg->re_g;
  if (pg) {
    free(pg->code);
    free(pg->classes);
    free(pg);
  }
  preg->re_g = NULL;
  preg->re_magic = 0;
}

#ifdef TS_REGEX_TEST
/* gcc -DTS_REGEX_TEST ts_regex.c && ./a.out */
static int fails;
static void expect(const char *pat, int cflags, const char *s, int ok, const char *g1, const char *g3,
                   const char *g4) {
  ts_regex_t re;
  ts_regmatch_t m[5];
  int rc = b_regcomp(&re, pat, cflags);
  if (rc) {
    printf("FAIL compile %s: %d\n", pat, rc);
    fails++;
    return;
  }
  rc = b_regexec(&re, s, 5, m, 0);
  int good = (rc == 0) == ok;
  char buf[3][128] = {{0}};
  const char *want[3] = {g1, g3, g4};
  int idx[3] = {1, 3, 4};
  for (int k = 0; good && ok && k < 3; k++) {
    if (!want[k])
      continue;
    if (m[idx[k]].rm_so < 0) {
      good = !want[k][0];
      continue;
    }
    snprintf(buf[k], sizeof buf[k], "%.*s", (int)(m[idx[k]].rm_eo - m[idx[k]].rm_so), s + m[idx[k]].rm_so);
    good = !strcmp(buf[k], want[k]);
  }
  printf("%s  /%s/ on \"%s\": rc=%d", good ? "ok  " : "FAIL", pat, s, rc);
  if (!rc)
    printf(" g0=[%d,%d] g1=\"%s\" g3=\"%s\" g4=\"%s\"", (int)m[0].rm_so, (int)m[0].rm_eo, buf[0], buf[1], buf[2]);
  printf(" nsub=%u\n", (unsigned)re.re_nsub);
  if (!good)
    fails++;
  b_regfree(&re);
}

int main(void) {
  const char *inc = "(#include ((<[^>]+>)|(\"[^\"]+\")))";
  expect(inc, REG_EXTENDED, "precision mediump float;\n#include <common.glsl>\nvoid main(){}", 1,
         "#include <common.glsl>", "<common.glsl>", "");
  expect(inc, REG_EXTENDED, "#include \"lighting.fsh\"\n", 1, "#include \"lighting.fsh\"", "",
         "\"lighting.fsh\"");
  expect(inc, REG_EXTENDED, "void main() { gl_FragColor = vec4(1.0); }", 0, NULL, NULL, NULL);
  expect(inc, REG_EXTENDED, "#include <>", 0, NULL, NULL, NULL);
  expect("^a[0-9]+b$", REG_EXTENDED, "a123b", 1, NULL, NULL, NULL);
  expect("^a[0-9]+b$", REG_EXTENDED, "xa123b", 0, NULL, NULL, NULL);
  expect("(ab|cd){2}x", REG_EXTENDED, "zzabcdx", 1, NULL, NULL, NULL);
  expect("[[:digit:]]{2,3}", REG_EXTENDED, "a1b22c", 1, NULL, NULL, NULL);
  expect("HELLO", REG_EXTENDED | REG_ICASE, "say hello", 1, NULL, NULL, NULL);
  /* a long input: no deep recursion trouble */
  static char big[200000];
  memset(big, 'a', sizeof big - 1);
  expect(inc, REG_EXTENDED, big, 0, NULL, NULL, NULL);
  printf("%s (%d failures)\n", fails ? "SOME FAILED" : "ALL OK", fails);
  return fails != 0;
}
#endif
