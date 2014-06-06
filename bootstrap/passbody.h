#ifndef PASSBODY_H__
#define PASSBODY_H__

#include "passes.h"

a_pass passbody[PASSBODY_COUNT];

extern const uint64_t step_type_inference_filter;
error step_type_inference(struct module *mod, struct node *node, void *user, bool *stop);
extern const uint64_t step_type_destruct_mark_filter;
error step_type_destruct_mark(struct module *mod, struct node *node, void *user, bool *stop);

extern const uint64_t step_record_current_statement_filter;
error step_record_current_statement(struct module *mod, struct node *node,
                                    void *user, bool *stop);

extern const uint64_t step_remove_typeconstraints_filter;
error step_remove_typeconstraints(struct module *mod, struct node *node,
                                  void *user, bool *stop);

error passbody0(struct module *mod, struct node *root,
                void *user, ssize_t shallow_last_up);

struct phi_tracker_state *get_phi_tracker(struct node *def);

#endif
