#ifndef COXVBN_EXECUTION_CONTEXT_H
#define COXVBN_EXECUTION_CONTEXT_H
#include <stdbool.h>
#include "module_registry.h"

/*
 * Kernel-internal scheduler hook. The provider must resolve the currently
 * executing task from trusted per-CPU scheduler state; it must never accept
 * a module reference supplied by user space.
 */
typedef bool (*coxvbn_current_module_provider)(
    struct coxvbn_module_ref *module_out, void *context);

/* Install once during boot, before concurrent task execution begins. */
bool coxvbn_execution_context_install_provider(
    coxvbn_current_module_provider provider, void *context);

/* Fails closed until a trusted provider has been installed. */
bool coxvbn_execution_context_current_module(
    struct coxvbn_module_ref *module_out);

/*
 * Resolve identity from the trusted provider and enforce the registry grant.
 * This is a kernel-side authorization boundary; callers must invoke it at
 * each privileged operation. It does not create a scheduler or syscall ABI.
 */
bool coxvbn_module_authorize_current(
    struct coxvbn_module_registry *registry,
    coxvbn_capability_t required);

#endif
