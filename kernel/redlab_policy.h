#ifndef COXVBN_REDLAB_POLICY_H
#define COXVBN_REDLAB_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define COXVBN_REDLAB_MAX_RULES 32U
#define COXVBN_REDLAB_MAX_TARGETS 32U
#define COXVBN_REDLAB_MAX_JOBS 64U
#define COXVBN_REDLAB_AUDIT_VERSION 2U
enum coxvbn_ip_family { COXVBN_IP_V4 = 4, COXVBN_IP_V6 = 6 };
struct coxvbn_ip_addr { uint8_t family; uint8_t bytes[16]; };
struct coxvbn_cidr { uint8_t family; uint8_t prefix; uint8_t network[16]; };
struct coxvbn_redlab_rule {
 struct coxvbn_cidr target; uint16_t first_port, last_port;
 uint64_t not_before; uint64_t expires_at; bool exclude;
};
struct coxvbn_redlab_limits {
 uint32_t requests_per_second; uint16_t max_concurrent_jobs_per_target;
 uint32_t max_job_duration_seconds; uint64_t max_job_bytes;
};
enum coxvbn_redlab_decision {
 COXVBN_REDLAB_ALLOW=0, COXVBN_REDLAB_DENY_INVALID=1,
 COXVBN_REDLAB_DENY_SCOPE=2, COXVBN_REDLAB_DENY_EXCLUDED=3,
 COXVBN_REDLAB_DENY_TIME=4, COXVBN_REDLAB_DENY_ACTIVE_DISABLED=5,
 COXVBN_REDLAB_DENY_RATE=6, COXVBN_REDLAB_DENY_CONCURRENCY=7,
 COXVBN_REDLAB_DENY_JOB_LIMIT=8, COXVBN_REDLAB_DENY_STOPPED=9,
 COXVBN_REDLAB_DENY_CAPACITY=10, COXVBN_REDLAB_DENY_CAPABILITY=11
};
struct coxvbn_redlab_audit_event {
 uint32_t version; uint64_t module_id, module_generation;
 uint32_t operation; uint64_t job_id, monotonic_seconds;
 struct coxvbn_ip_addr target; uint16_t port;
 enum coxvbn_redlab_decision decision; bool active;
};
typedef void (*coxvbn_redlab_audit_fn)(const struct coxvbn_redlab_audit_event *, void *);
struct coxvbn_redlab_target_state {
 bool used; struct coxvbn_ip_addr address; uint64_t rate_window;
 uint32_t requests_in_window; uint16_t active_jobs;
};
struct coxvbn_redlab_job_state {
 bool used; uint64_t job_id, started_at, bytes_used; size_t target_slot;
};
struct coxvbn_redlab_policy {
 struct coxvbn_redlab_rule rules[COXVBN_REDLAB_MAX_RULES]; size_t rule_count;
 struct coxvbn_redlab_limits limits;
 struct coxvbn_redlab_target_state targets[COXVBN_REDLAB_MAX_TARGETS];
 struct coxvbn_redlab_job_state jobs[COXVBN_REDLAB_MAX_JOBS];
 coxvbn_redlab_audit_fn audit; void *audit_context;
 uint64_t last_observed_time; bool time_initialized, active_enabled, stopped;
};
enum coxvbn_redlab_operation {
 COXVBN_REDLAB_OP_DISCOVERY=0,
 COXVBN_REDLAB_OP_PASSIVE_CAPTURE=1,
 COXVBN_REDLAB_OP_ACTIVE_PROBE=2
};
struct coxvbn_module_registry;
struct coxvbn_module_ref;
struct coxvbn_redlab_audit_context {
 uint64_t module_id, module_generation;
 enum coxvbn_redlab_operation operation;
};
struct coxvbn_redlab_request {
 uint64_t job_id; struct coxvbn_ip_addr target; uint16_t port;
 uint64_t now, bytes; bool active;
};
bool coxvbn_redlab_policy_init(struct coxvbn_redlab_policy *,
 const struct coxvbn_redlab_rule *, size_t, const struct coxvbn_redlab_limits *,
 bool, coxvbn_redlab_audit_fn, void *);
 #ifdef COXVBN_REDLAB_TESTING
/* Host-test hook only; not exported by production kernel builds. */
enum coxvbn_redlab_decision coxvbn_redlab_policy_check(
 struct coxvbn_redlab_policy *, const struct coxvbn_redlab_request *);
#endif
enum coxvbn_redlab_decision coxvbn_redlab_policy_check_authorized(
 struct coxvbn_redlab_policy *, struct coxvbn_module_registry *,
 const struct coxvbn_module_ref *, enum coxvbn_redlab_operation,
 const struct coxvbn_redlab_request *);
bool coxvbn_redlab_job_finish(struct coxvbn_redlab_policy *, uint64_t);
void coxvbn_redlab_global_stop(struct coxvbn_redlab_policy *);
bool coxvbn_redlab_cidr_valid(const struct coxvbn_cidr *);
bool coxvbn_redlab_cidr_contains(const struct coxvbn_cidr *, const struct coxvbn_ip_addr *);
#endif
