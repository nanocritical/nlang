#ifndef PRINTER_H__
#define PRINTER_H__

#include "common.h"
#include "parser.h"

error printer_pretty(int fd, const struct module *mod);
error printer_tree(int fd, const struct module *mod, const struct node *root);

const char *tokens_string[TOKEN__NUM];

#endif