/* NOVA OS user-mode test interface (Phase 4b). */
#ifndef NOVA_USER_H
#define NOVA_USER_H

#include <stdint.h>

int user_selftest(void);
int user_sched_test(void);
void user_ret_handler(void);

#endif /* NOVA_USER_H */
