#ifndef IMPORT_H__
#define IMPORT_H__

#include "nodes.h"

ERROR lexical_import(struct module *mod, struct node *original_import,
                     struct node *import);

#endif
