#include <stdint.h>
#include <stdio.h>

#include "measure.h"
#include "cycle.h"
#include "parameters.h"

#ifdef MEASURE_CYCLE
	THREAD_LOCAL_MEASURE_VARIABLE(cycle, NPROCESSORS)
#endif

#ifdef MEASURE_SLEEP
 THREAD_LOCAL_MEASURE_VARIABLE(sleep, NPROCESSORS)
#endif

#ifdef MEASURE_PUSH_FIND
	COMMON_MEASURE_VARIABLE(push_find)
	SUCCESS_COUNTER(push_find)
	FAIL_COUNTER(push_find)
	ALL_COUNTER(push_find)
#endif

#ifdef MEASURE_PULL_FIND
	COMMON_MEASURE_VARIABLE(pull_find)
	SUCCESS_COUNTER(pull_find)
	FAIL_COUNTER(pull_find)
	ALL_COUNTER(pull_find)
#endif

#ifdef MEASURE_PUSH_PREEMPT
	COMMON_MEASURE_VARIABLE(push_preempt)
	ALL_COUNTER(push_preempt)
#endif

#ifdef MEASURE_PULL_PREEMPT
	COMMON_MEASURE_VARIABLE(pull_preempt)
	ALL_COUNTER(pull_preempt)
#endif

#ifdef MEASURE_FIND_LOCK
	COMMON_MEASURE_VARIABLE(find_lock)
	ALL_COUNTER(find_lock)
#endif

#ifdef MEASURE_FIND_UNLOCK
	COMMON_MEASURE_VARIABLE(find_unlock)
	ALL_COUNTER(find_unlock)
#endif

/* tsc_cost global variable */
uint64_t tsc_cost;

/*
 * set_tsc_cost - calculate how many CPU 
 * cycles are needed to read TSC and set
 * the corresponding tsc_cost static variable
 */
void set_tsc_cost()
{
	ticks t1, t2;
	uint64_t elapsed, min_tsc_cost;
	int i;

	min_tsc_cost = ~0ULL;
	for(i = 0; i < CALIBRATION_CYCLES; i++){
		t1 = get_ticks();
		t2 = get_ticks();
		elapsed = t2 - t1;
		if(elapsed < min_tsc_cost)
			min_tsc_cost = elapsed;
	}

	tsc_cost = min_tsc_cost;
}

/*
 * get_tsc_cost - return the number of cycles
 * needed to read TSC twice
	*/
uint64_t get_tsc_cost()
{
	if(!tsc_cost)
		set_tsc_cost();

	return tsc_cost;
}

/*
 * ticks_to_milliseconds - converts ticks in milliseconds
 * using the defined constant CPU_FREQ
 */
uint64_t ticks_to_milliseconds(const uint64_t ticks)
{
	return ticks / (CPU_FREQ / 1000ULL);
}

/*
 * ticks_to_microseconds - converts ticks in microseconds
 * using the defined constant CPU_FREQ
 */
uint64_t ticks_to_microseconds(const uint64_t ticks)
{
	return ticks / (CPU_FREQ / 1000000ULL);
}

/*
 * get_ticks - get the value of TSC
 */
uint64_t get_ticks(){
	return (uint64_t)getticks();
}

/*
 * get_elapsed_ticks - calculate elapsed CPU clock 
 * cycles from start to end
 * @start:	TSC value at the beginning of measurements
 * @end:		TSC value at the end of measurements
 */
uint64_t get_elapsed_ticks(const uint64_t start, const uint64_t end)
{
	uint64_t elapsed;

	elapsed = end - start;
	if(elapsed < tsc_cost){
		fprintf(stderr, "WARNING: elapsed time (%llu ticks) < tsc cost (%llu ticks)\n", elapsed, tsc_cost);
		return 0;
	}
	else
		return elapsed - tsc_cost;
}

/*
 * get_current_thread_time - get current
 * thread specific time
 * @t:		timespec variabile pointer where we
 * save current time
 */
void get_current_thread_time(struct timespec *t)
{
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, t);
}

/*
 * get_current_thread_time - get current
 * process specific time
 * @t:		timespec variabile pointer where we
 * save current time
 */
void get_current_process_time(struct timespec *t)
{
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, t);
}

/*
 * get_elapsed_time - calculate elapsed time
 * from start to end
 * @start:	measurement start time
 * @end:		measurement end time
 */
struct timespec get_elapsed_time(const struct timespec start, const struct timespec end)
{
	struct timespec temp;
	
	if (end.tv_nsec - start.tv_nsec < 0){
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = NANO_SECONDS_IN_SEC + end.tv_nsec - start.tv_nsec;
	}else{
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}

	return temp;
}

/*
 * common_measure_print - print common measure 
 * result helper function
 * @variable:	a string that identifies which
 * variable we want to print
 * @avg:			average measure result
 * @max:			maximum measure result
 * @number:		measures number
 */
void common_measure_print(char *variable, uint64_t avg, uint64_t max, uint64_t number)
{
	printf("%s takes\n", variable);
	printf("avg)\tticks: %15llu\n", avg);
	printf("max)\tticks: %15llu\n", max);
	printf("%s total number: %llu\n", variable, number);
}

/*
 * common_measure_outcome_print - print measure 
 * outcome (success/fail rate) result helper function
 * @variable:		a string that identifies which
 * variable we want to print
 * @success:		number of successful operation
 * @fail:				number of failed operation
 */
void common_measure_outcome_print(char *variable, uint64_t success, uint64_t fail)
{
	printf("%s successful: %llu failed: %llu\n", variable, success, fail);
}

/*
 * thread_local_measure_print - print thread-local 
 * measure result helper function
 * @sum_elapsed:	array of elapsed time (for each thread)
 * @max_elapsed:	array of max elapsed (for each thread)
 */
void thread_local_measure_print(uint64_t *sum_elapsed, uint64_t *max_elapsed)
{
	uint64_t res;
	int i;

	for(i = 0; i < NPROCESSORS; i++){
		res = sum_elapsed[i] / NCYCLES;
		printf("[%d] cycle takes\n", i);
		printf("avg)\tticks: %15llu\tmilliseconds: %5llu\tmicroseconds: %8llu\n", res, ticks_to_milliseconds(res), ticks_to_microseconds(res));
		printf("max)\tticks: %15llu\tmilliseconds: %5llu\tmicroseconds: %8llu\n", max_elapsed[i], ticks_to_milliseconds(max_elapsed[i]), ticks_to_microseconds(max_elapsed[i]));
	}
}
