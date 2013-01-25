#include "parser.h"
#include "printer.h"
#include "firstpass.h"
#include "table.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define CFLAGS "-Wall -std=c99 -pedantic -Ilib"

static error zero(struct globalctx *gctx, struct module *mod,
                  const char *prefix, const char *fn) {
  error e = module_open(gctx, mod, prefix, fn);
  EXCEPT(e);

  step zeropass_down[] = {
    step_detect_deftype_kind,
    step_add_builtin_members,
    step_add_builtin_functions,
    step_add_builtin_methods,
    step_add_builtin_self,
    step_add_codegen_variables,
    NULL,
  };
  step zeropass_up[] = {
    step_add_scopes,
    NULL,
  };

  e = pass(mod, NULL, zeropass_down, zeropass_up, NULL);
  EXCEPT(e);

  return 0;
}

static error first(struct node *node) {
  assert(node->which == MODULE);
  struct module *mod = node->as.MODULE.mod;

  step firstpass_down[] = {
    step_lexical_scoping,
    step_type_definitions,
    step_type_destruct_mark,
    step_type_gather_returns,
    step_type_gather_excepts,
    NULL,
  };
  step firstpass_up[] = {
    step_type_inference,
    step_operator_call_inference,
    step_unary_call_inference,
    step_ctor_call_inference,
    step_call_arguments_prepare,
    step_temporary_inference,
    NULL,
  };

  error e = pass(mod, NULL, firstpass_down, firstpass_up, NULL);
  EXCEPT(e);

  return 0;
}

static char *o_filename(const char *filename) {
  char *o_fn = malloc(strlen(filename) + sizeof(".o"));
  sprintf(o_fn, "%s.o", filename);
  return o_fn;
}

static error cc(const struct module *mod, const char *o_fn,
                const char *c_fn, const char *h_fn) {
  static const char *fmt = "gcc " CFLAGS " -xc %s -c -o %s";
  char *cmd = calloc(strlen(fmt) + strlen(c_fn) + 1, sizeof(char));
  sprintf(cmd, fmt, c_fn, o_fn);

  int status = system(cmd);
  if (status == -1) {
    EXCEPTF(errno, "system(3) failed");
  }
  if (WIFSIGNALED(status)) {
    EXCEPTF(ECHILD, "command terminated by signal %d: %s", WTERMSIG(status), cmd);
  } else if (WEXITSTATUS(status) != 0) {
    EXCEPTF(ECHILD, "command exited with %d: %s", WEXITSTATUS(status), cmd);
  }

  return 0;
}

error generate(struct node *node) {
  assert(node->which == MODULE);
  struct module *mod = node->as.MODULE.mod;

  const char *fn = mod->filename;
  error e;

  char *out_fn = malloc(strlen(fn) + sizeof(".tree.out"));
  sprintf(out_fn, "%s.tree.out", fn);

  int fd = creat(out_fn, 00600);
  if (fd < 0) {
    EXCEPTF(errno, "Cannot open output file '%s'", out_fn);
  }
  free(out_fn);

  e = printer_tree(fd, mod, NULL);
  EXCEPT(e);
  close(fd);

  out_fn = malloc(strlen(fn) + sizeof(".pretty.out"));
  sprintf(out_fn, "%s.pretty.out", fn);

  fd = creat(out_fn, 00600);
  if (fd < 0) {
    EXCEPTF(errno, "Cannot open output file '%s'", out_fn);
  }
  free(out_fn);

  e = printer_pretty(fd, mod);
  EXCEPT(e);
  close(fd);

  char *c_fn = malloc(strlen(fn) + sizeof(".c.out"));
  sprintf(c_fn, "%s.c.out", fn);

  fd = creat(c_fn, 00600);
  if (fd < 0) {
    EXCEPTF(errno, "Cannot open output file '%s'", c_fn);
  }

  e = printer_c(fd, mod);
  EXCEPT(e);
  close(fd);

  char *h_fn = malloc(strlen(fn) + sizeof(".h.out"));
  sprintf(h_fn, "%s.h.out", fn);

  fd = creat(h_fn, 00600);
  if (fd < 0) {
    EXCEPTF(errno, "Cannot open output file '%s'", h_fn);
  }

  e = printer_h(fd, mod);
  EXCEPT(e);
  close(fd);

  char *o_fn = o_filename(mod->filename);
  e = cc(mod, o_fn, c_fn, h_fn);
  EXCEPT(e);

  free(o_fn);
  free(c_fn);
  free(h_fn);

  return 0;
}

struct node_op {
  enum node_which filter;
  error (*fun)(struct node *node, void *user, bool *stop);
  void *user;
};

static error step_for_all_nodes(struct module *mod, struct node *node,
                                void *user, bool *stop) {
  struct node_op *op = user;

  if (op->filter == 0 || node->which == op->filter) {
    error e = op->fun(node, op->user, stop);
    EXCEPT(e);
  }

  return 0;
}

static error for_all_nodes(struct node *root,
                           enum node_which filter, // 0 to get all nodes
                           error (*node_fun)(struct node *node, void *user, bool *stop_descent),
                           void *user) {
  step downsteps[] = {
    step_for_all_nodes,
    NULL,
  };
  step upsteps[] = {
    NULL,
  };

  struct node_op op = {
    .filter = filter,
    .fun = node_fun,
    .user = user,
  };

  error e = pass(NULL, root, downsteps, upsteps, &op);
  EXCEPT(e);

  return 0;
}

static error load_module(struct globalctx *gctx, const char *prefix, const char *fn) {

  //FIXME check if already loaded.

  struct module *mod = calloc(1, sizeof(struct module));

  error e = zero(gctx, mod, prefix, fn);
  EXCEPT(e);

  return 0;
}

static void import_filename(char **fn, size_t *len,
                            struct module *mod, struct node *import) {
  struct node *to_append = NULL;

  switch (import->which) {
  case BIN:
    import_filename(fn, len, mod, import->subs[0]);
    to_append = import->subs[1];
    break;
  case IDENT:
    to_append = import;
    break;
  default:
    assert(FALSE);
  }

  assert(to_append->which == IDENT);
  const char *app = idents_value(mod->gctx, to_append->as.IDENT.name);
  const size_t applen = strlen(app);

  if (*len == 0) {
    *fn = realloc(*fn, applen + 1);
    strcpy(*fn, app);
    *len += applen;
  } else {
    *fn = realloc(*fn, *len + 1 + applen + 1);
    (*fn)[*len] = '/';
    strcpy(*fn + *len + 1, app);
    *len += applen + 1;
  }
}

static error lookup_import(char **fn, struct module *mod, struct node *import,
                           const char *prefix) {
  assert(import->which == IMPORT);
  struct node *import_path = import->subs[0];

  *fn = NULL;
  size_t len = 0;

  import_filename(fn, &len, mod, import_path);

  *fn = realloc(*fn, len + 2 + 1);
  strcpy(*fn + len, ".n");

  return 0;
}

static error load_import(struct node *node, void *user, bool *stop) {
  assert(node->which == IMPORT);
  struct module *mod = user;

  char *fn = NULL;
  error e = lookup_import(&fn, mod, node, "lib");
  EXCEPT(e);

  e = load_module(mod->gctx, "lib", fn);
  free(fn);
  EXCEPT(e);

  *stop = TRUE;

  return 0;
}

static error load_imports(struct node *node, void *user, bool *stop) {
  assert(node->which == MODULE);

  if (node->as.MODULE.is_placeholder) {
    return 0;
  }

  assert(node->as.MODULE.mod != NULL);
  error e = for_all_nodes(node->as.MODULE.mod->root, IMPORT, load_import,
                          node->as.MODULE.mod);
  EXCEPT(e);

  return 0;
}

struct dependencies {
  struct module **tmp;
  size_t tmp_count;
  struct module **modules;
  size_t modules_count;
  struct globalctx *gctx;
};

static error gather_dependencies_in_module(struct node *node, void *user, bool *stop) {
  assert(node->which == IMPORT);
  struct dependencies *deps = user;

  struct node *nmod = NULL;
  error e = scope_lookup(&nmod, node_module_owner(node)->as.MODULE.mod,
                         deps->gctx->modules_root.scope, node->subs[0]);
  EXCEPT(e);

  deps->tmp_count += 1;
  deps->tmp = realloc(deps->tmp, deps->tmp_count * sizeof(*deps->tmp));
  deps->tmp[deps->tmp_count - 1] = nmod->as.MODULE.mod;

  return 0;
}

static error gather_dependencies(struct node *node, void *user, bool *stop) {
  assert(node->which == MODULE);
  struct dependencies *deps = user;

  if (node->as.MODULE.is_placeholder) {
    return 0;
  }

  error e = for_all_nodes(node, IMPORT, gather_dependencies_in_module, deps);
  EXCEPT(e);

  deps->tmp_count += 1;
  deps->tmp = realloc(deps->tmp, deps->tmp_count * sizeof(*deps->tmp));
  deps->tmp[deps->tmp_count - 1] = node_module_owner(node)->as.MODULE.mod;

  return 0;
}

HTABLE_SPARSE(dependencies_map, int, struct module *);
implement_htable_sparse(__attribute__((unused)) static, dependencies_map, int, struct module *);

uint32_t module_pointer_hash(const struct module **mod) {
  return hash32_hsieh(mod, sizeof(*mod));
}

int module_pointer_cmp(const struct module **a, const struct module **b) {
  return memcmp(a, b, sizeof(*a));
}

static error calculate_dependencies(struct dependencies *deps) {
  struct dependencies_map map;
  dependencies_map_init(&map, 0);
  dependencies_map_set_delete_val(&map, 0);
  dependencies_map_set_custom_hashf(&map, module_pointer_hash);
  dependencies_map_set_custom_cmpf(&map, module_pointer_cmp);

  for (ssize_t n = deps->tmp_count - 1; n >= 0; --n) {
    struct module *m = deps->tmp[n];
    const int yes = 1;
    int already = dependencies_map_set(&map, m, yes);
    if (already) {
      continue;
    }

    deps->modules_count += 1;
    deps->modules = realloc(deps->modules, deps->modules_count * sizeof(*deps->modules));
    deps->modules[deps->modules_count - 1] = m;
  }

  dependencies_map_destroy(&map);

  free(deps->tmp);
  deps->tmp = NULL;
  deps->tmp_count = 0;

  return 0;
}

static error clink(const struct dependencies *deps) {
  static const char *fmt = "gcc " CFLAGS;
  size_t len = strlen(fmt);
  char *cmd = calloc(len + 1, sizeof(char));
  strcpy(cmd, fmt);

  for (size_t n = 0; n < deps->modules_count; ++n) {
    struct module *mod = deps->modules[n];
    const size_t old_len = len;
    char *o_fn = o_filename(mod->filename);
    len += 1 + strlen(o_fn);
    cmd = realloc(cmd, (len + 1) * sizeof(char));
    strcpy(cmd + old_len, " ");
    strcpy(cmd + old_len + 1, o_fn);
    free(o_fn);
  }

  int status = system(cmd);
  if (status == -1) {
    EXCEPTF(errno, "system(3) failed");
  }
  if (WIFSIGNALED(status)) {
    EXCEPTF(ECHILD, "command terminated by signal %d: %s", WTERMSIG(status), cmd);
  } else if (WEXITSTATUS(status) != 0) {
    EXCEPTF(ECHILD, "command exited with %d: %s", WEXITSTATUS(status), cmd);
  }

  return 0;
}

int main(int argc, char **argv) {
  struct globalctx gctx;
  globalctx_init(&gctx);

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <input.n>\n", argv[0]);
    exit(1);
  }

  error e = load_module(&gctx, NULL, argv[1]);
  EXCEPT(e);

  e = for_all_nodes(&gctx.modules_root, MODULE, load_imports, NULL);
  EXCEPT(e);

  struct dependencies deps;
  memset(&deps, 0, sizeof(deps));
  deps.gctx = &gctx;

  e = for_all_nodes(&gctx.modules_root, MODULE, gather_dependencies, &deps);
  EXCEPT(e);
  e = calculate_dependencies(&deps);
  EXCEPT(e);

  for (size_t n = 0; n < deps.modules_count; ++n) {
    e = first(deps.modules[n]->root);
    EXCEPT(e);
  }

  for (size_t n = 0; n < deps.modules_count; ++n) {
    e = generate(deps.modules[n]->root);
    EXCEPT(e);
  }

  e = clink(&deps);
  EXCEPT(e);

  return 0;
}
