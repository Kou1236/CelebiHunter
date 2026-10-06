#ifndef CONTROLLED_SOLVER_SOL61_H
#define CONTROLLED_SOLVER_SOL61_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
#define SOL61_API __declspec(dllexport)
#else
#define SOL61_API
#endif

/* Conditional canonical-start/final-normalization model only. No device IO.
 * seed values are the actually sampled pre-call2-origin RNG, never written.
 * min_model_n=2 allows zero new waiting calls; 3 requires nonzero waiting.
 * A false/unclosed source/normalization contract must be rejected by caller.
 */
#define SOL61_NODE_CAP 32768u
#define SOL61_HASH_CAP 8192u
#define SOL61_CACHE_CAP 16384u
#define SOL61_ITE_OPERATION_LIMIT 20000000u
typedef struct { uint16_t low, high, next; uint8_t variable, pad; } Sol61Node;
typedef struct { uint16_t condition, yes, no, result; } Sol61Cache;
typedef struct {
    Sol61Node nodes[SOL61_NODE_CAP];
    uint16_t unique[SOL61_HASH_CAP];
    Sol61Cache cache[SOL61_CACHE_CAP];
    uint16_t counts[SOL61_NODE_CAP];
    uint32_t node_count, max_nodes, ite_calls, total_ite_calls, errors;
} Sol61Workspace;
typedef struct {
    uint32_t status;              /* 0=OK; 1=arena/operation bound; 2=input */
    uint32_t target_pair_count;
    uint32_t branch_counts[3];    /* none, first_item, second_item */
    uint32_t found;
    uint32_t model_n, div_x, branch, predicted_dv; /* predicted_dv=DV1<<8|AA */
    uint32_t expected_wait_add, expected_wait_sub;
    uint32_t additional_released_calls;
    uint32_t max_nodes, ite_calls, algebraic_x_cases;
    uint32_t source_certificate_id[8];
} Sol61Result;

/* Caller-owned fixed arena; no malloc, global mutable state, or runtime libs.
 * Never execute inside a per-frame hook; solve once on an isolated job.
 */
SOL61_API int sol61_solve(uint32_t rng_add, uint32_t rng_sub, uint32_t min_model_n,
                Sol61Workspace *workspace, Sol61Result *result);
SOL61_API uint32_t sol61_workspace_bytes(void);
SOL61_API uint32_t sol61_result_bytes(void);
SOL61_API const uint32_t *sol61_source_certificate_id(void);

/* A sealed per-seed solution table from the same inverse predicates. Building
 * expands satisfying BDD assignments only, never unsolved N/x encounters.
 * Selecting a later lower bound reads this table and does not call the BDD.
 * This is still conditional on the exact profile/source certificates. */
#define SOL61_FRONTIER_ABI 1u
#define SOL61_FRONTIER_SLOTS 508u
typedef struct {
    uint16_t branch_pairs[3], div_x, predicted_dv;
    uint8_t branch, expected_wait_add, expected_wait_sub, found;
} Sol61FrontierEntry;
typedef struct {
    uint32_t abi, complete, rng_add, rng_sub, checksum;
    uint32_t max_nodes, ite_calls, algebraic_x_cases;
    uint32_t source_certificate_id[8];
    Sol61FrontierEntry entries[SOL61_FRONTIER_SLOTS];
} Sol61Frontier;
SOL61_API uint32_t sol61_frontier_bytes(void);
SOL61_API int sol61_build_frontier(uint32_t rng_add, uint32_t rng_sub,
    Sol61Workspace *workspace, Sol61Frontier *frontier);
SOL61_API int sol61_frontier_select(uint32_t rng_add, uint32_t rng_sub,
    uint32_t min_model_n, const Sol61Frontier *frontier, Sol61Result *result);
#endif
