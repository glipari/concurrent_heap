#ifndef __MEASURE_H
#define __MEASURE_H

#include <time.h>
#include <stdint.h>
#include "cycle.h"
#include "parameters.h"

/*
 * As recommended by Intel we have to
 * repeat the rdtsc cost measurement 
 * at least 3 times.
 * See http://www.ccsl.carleton.ca/~jamuir/rdtscpm1.pdf
 * for more details
 */
#define CALIBRATION_CYCLES		3
#define NANO_SECONDS_IN_SEC		1000000000

/*
 * since we use a constant_tsc processor
 * we need only to set here the maximum
 * clock frequency available, no matter
 * if cpu dynamic frequency scaling is
 * enabled (it only affects the benchmark
 * result, not the time measurement)
 */
#define CPU_FREQ			2400000000ULL

#ifdef MEASURE_ALL
	#define MEASURE_SLEEP
	#define MEASURE_CYCLE
	#define MEASURE_PUSH_FIND
	#define MEASURE_PULL_FIND
	#define MEASURE_PUSH_PREEMPT
	#define MEASURE_PULL_PREEMPT
	#define MEASURE_FIND_LOCK
	#define	MEASURE_FIND_UNLOCK
#endif

#if defined(MEASURE_SLEEP) || defined(MEASURE_CYCLE) || \
	defined(MEASURE_PUSH_FIND) || defined(MEASURE_PULL_FIND) || \
	defined(MEASURE_PUSH_PREEMPT) || defined(MEASURE_PULL_PREEMPT) || \
	defined(MEASURE_FIND_LOCK) || defined(MEASURE_FIND_UNLOCK)

	#define MEASURE
#endif

#define IDENTIFIER(prefix, name) prefix##name

#define EXTERN_DECL(decl) extern decl

#define _N_ALL(prefix) uint64_t prefix##_n_all
#define _N_SUCCESS(prefix) uint64_t prefix##_n_success
#define _N_FAIL(prefix) uint64_t prefix##_n_fail

#define _START_TICKS(prefix) uint64_t IDENTIFIER(prefix, _start_ticks)
#define _END_TICKS(prefix) uint64_t IDENTIFIER(prefix, _end_ticks)
#define _CURRENT_ELAPSED(prefix) uint64_t IDENTIFIER(prefix, _current_elapsed)
#define _MAX_ELAPSED(prefix) uint64_t IDENTIFIER(prefix, _max_elapsed)
#define _MIN_ELAPSED(prefix) uint64_t IDENTIFIER(prefix, _min_elapsed)
#define _SUM_ELAPSED(prefix) uint64_t IDENTIFIER(prefix, _sum_elapsed)

#define __START_TICKS(prefix, number) _START_TICKS(prefix)[number]
#define __END_TICKS(prefix, number) _END_TICKS(prefix)[number]
#define __CURRENT_ELAPSED(prefix, number) _CURRENT_ELAPSED(prefix)[number]
#define __MAX_ELAPSED(prefix, number) _MAX_ELAPSED(prefix)[number]
#define __SUM_ELAPSED(prefix, number) _SUM_ELAPSED(prefix)[number]

#define THREAD_LOCAL_MEASURE_VARIABLE(prefix, number) \
	__START_TICKS(prefix, number); \
	__END_TICKS(prefix, number); \
	__CURRENT_ELAPSED(prefix, number); \
	__MAX_ELAPSED(prefix, number); \
	__SUM_ELAPSED(prefix, number);

#define EXTERN_THREAD_LOCAL_MEASURE_VARIABLE(prefix, number) \
	EXTERN_DECL(__START_TICKS(prefix, number)); \
	EXTERN_DECL(__END_TICKS(prefix, number)); \
	EXTERN_DECL(__CURRENT_ELAPSED(prefix, number)); \
	EXTERN_DECL(__MAX_ELAPSED(prefix, number)); \
	EXTERN_DECL(__SUM_ELAPSED(prefix, number));

#define THREAD_LOCAL_MEASURE_START(variable, thread_index)\
	IDENTIFIER(variable, _start_ticks[thread_index]) = get_ticks();

#define THREAD_LOCAL_MEASURE_END(variable, thread_index)\
	IDENTIFIER(cycle, _end_ticks[thread_index]) = get_ticks();\
	IDENTIFIER(variable, _current_elapsed[thread_index]) = get_elapsed_ticks(IDENTIFIER(variable, _start_ticks[thread_index]), IDENTIFIER(variable, _end_ticks[thread_index]));\
	if(IDENTIFIER(variable, _current_elapsed[thread_index]) > IDENTIFIER(variable, _max_elapsed[thread_index]))\
		IDENTIFIER(variable, _max_elapsed[thread_index]) = IDENTIFIER(variable, _current_elapsed[thread_index]);\
	IDENTIFIER(variable, _sum_elapsed[thread_index]) += IDENTIFIER(variable, _current_elapsed[thread_index]);

#define THREAD_LOCAL_MEASURE_PRINT(variable) thread_local_measure_print(IDENTIFIER(variable, _sum_elapsed), IDENTIFIER(variable, _max_elapsed))

#define SUCCESS_COUNTER(prefix)	_N_SUCCESS(prefix);

#define FAIL_COUNTER(prefix) _N_FAIL(prefix);

#define ALL_COUNTER(prefix) _N_ALL(prefix);

#define COMMON_MEASURE_VARIABLE(prefix) \
	_MIN_ELAPSED(prefix) = ~0ULL; \
	_MAX_ELAPSED(prefix); \
	_SUM_ELAPSED(prefix);

#define EXTERN_COMMON_MEASURE_VARIABLE(prefix) \
	EXTERN_DECL(_MIN_ELAPSED(prefix)); \
	EXTERN_DECL(_MAX_ELAPSED(prefix)); \
	EXTERN_DECL(_SUM_ELAPSED(prefix));

#define COMMON_MEASURE_START(variable)\
	_START_TICKS(variable);\
	_END_TICKS(variable);\
	_CURRENT_ELAPSED(variable);\
	IDENTIFIER(variable, _start_ticks) = get_ticks();

#define COMMON_MEASURE_END(variable)\
	IDENTIFIER(variable, _end_ticks) = get_ticks(); \
	IDENTIFIER(variable, _current_elapsed) = get_elapsed_ticks(IDENTIFIER(variable, _start_ticks), IDENTIFIER(variable, _end_ticks));\
	while(!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _sum_elapsed), IDENTIFIER(variable, _sum_elapsed), IDENTIFIER(variable, _sum_elapsed) + IDENTIFIER(variable, _current_elapsed)))\
		;\
	while(!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _n_all), IDENTIFIER(variable, _n_all), IDENTIFIER(variable, _n_all) + 1))\
		;\
	/* 
	 * if CAS fails we need to check 
	 * max_elapsed again (it may be higher) 
	 */\
	while(1){\
		if(IDENTIFIER(variable, _current_elapsed) > IDENTIFIER(variable, _max_elapsed) && \
			!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _max_elapsed), IDENTIFIER(variable, _max_elapsed), IDENTIFIER(variable, _current_elapsed)))\
				continue;\
		else\
			break;\
	}\
	/* 
	 * if CAS fails we need to check 
	 * min_elapsed again (it may be higher) 
	 */\
	while(1){\
		if(IDENTIFIER(variable, _current_elapsed) < IDENTIFIER(variable, _min_elapsed) && \
			!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _min_elapsed), IDENTIFIER(variable, _min_elapsed), IDENTIFIER(variable, _current_elapsed)))\
				continue;\
		else\
			break;\
	}	

#define COMMON_MEASURE_REGISTER_OUTCOME(variable, result, bad_value) \
	if(result != bad_value)\
		while(!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _n_success), IDENTIFIER(variable, _n_success), IDENTIFIER(variable, _n_success) + 1))\
		;\
	else{\
		while(!__sync_bool_compare_and_swap(&IDENTIFIER(variable, _n_fail), IDENTIFIER(variable, _n_fail), IDENTIFIER(variable, _n_fail) + 1))\
		;\
	}

#define COMMON_MEASURE_PRINT(variable) common_measure_print(#variable, IDENTIFIER(variable, _sum_elapsed) / IDENTIFIER(variable, _n_all), IDENTIFIER(variable, _max_elapsed), IDENTIFIER(variable, _min_elapsed), IDENTIFIER(variable, _n_all))

#define COMMON_MEASURE_OUTCOME_PRINT(variable) common_measure_outcome_print(#variable, IDENTIFIER(variable, _n_success), IDENTIFIER(variable, _n_fail))

#ifdef MEASURE_CYCLE
	EXTERN_THREAD_LOCAL_MEASURE_VARIABLE(cycle, NPROCESSORS)
#endif

#ifdef MEASURE_SLEEP
 EXTERN_THREAD_LOCAL_MEASURE_VARIABLE(sleep, NPROCESSORS)
#endif

#ifdef MEASURE_PUSH_FIND
	EXTERN_COMMON_MEASURE_VARIABLE(push_find)
	EXTERN_DECL(SUCCESS_COUNTER(push_find))
	EXTERN_DECL(FAIL_COUNTER(push_find))
	EXTERN_DECL(ALL_COUNTER(push_find))
#endif

#ifdef MEASURE_PULL_FIND
	EXTERN_COMMON_MEASURE_VARIABLE(pull_find)
	EXTERN_DECL(SUCCESS_COUNTER(pull_find))
	EXTERN_DECL(FAIL_COUNTER(pull_find))
	EXTERN_DECL(ALL_COUNTER(pull_find))
#endif

#ifdef MEASURE_PUSH_PREEMPT
	EXTERN_COMMON_MEASURE_VARIABLE(push_preempt)
	EXTERN_DECL(ALL_COUNTER(push_preempt))
#endif

#ifdef MEASURE_PULL_PREEMPT
	EXTERN_COMMON_MEASURE_VARIABLE(pull_preempt)
	EXTERN_DECL(ALL_COUNTER(pull_preempt))
#endif

#ifdef MEASURE_FIND_LOCK
	EXTERN_COMMON_MEASURE_VARIABLE(find_lock)
	EXTERN_DECL(ALL_COUNTER(find_lock))
#endif

#ifdef MEASURE_FIND_UNLOCK
	EXTERN_COMMON_MEASURE_VARIABLE(find_unlock)
	EXTERN_DECL(ALL_COUNTER(find_unlock))
#endif

/* TSC measurement interface */

void set_tsc_cost();
uint64_t get_tsc_cost();
uint64_t get_ticks();
uint64_t get_elapsed_ticks(const uint64_t start, const uint64_t end);
uint64_t ticks_to_seconds(const uint64_t ticks);
uint64_t ticks_to_milliseconds(const uint64_t ticks);
uint64_t ticks_to_microseconds(const uint64_t ticks);

/* clock_gettime() measurement interface */

void get_current_thread_time(struct timespec *t);
void get_current_process_time();
struct timespec get_elapsed_time(const struct timespec start, const struct timespec end);

/* measurement print interface */

void common_measure_print(char *variable, uint64_t avg, uint64_t max, uint64_t min, uint64_t number);
void common_measure_outcome_print(char *variable, uint64_t success, uint64_t fail);
void thread_local_measure_print(uint64_t *sum_elapsed, uint64_t *max_elapsed);

#endif
