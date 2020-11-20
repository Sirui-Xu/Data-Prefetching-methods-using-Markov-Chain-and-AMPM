// Prefetcher based on markov chain
// Authors: xusirui(xusirui@pku.edu.cn)


#include <stdio.h>
#include "../inc/prefetcher.h"

#define NOFFSETS 46
int OFFSET[NOFFSETS] = {1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,7,-7,8,-8,9,-9,10,-10,11,-11,12,-12,13,-13,14,-14,15,-15,16,-16,18,-18,20,-20,24,-24,30,-30,32,-32,36,-36,40,-40};
#define DEFAULT_OFFSET 1
#define STATES_MAX 20
#define PAGE_COUNT 64

struct delta_cache {
	int delta_next[STATES_MAX]; 		// 7 * 20 bits
	unsigned int lfu[STATES_MAX]; 		// 8 * 20 bits
	int delta_next_best;				// 7 bits
	unsigned int max_lfu;				// 8 bits
} delta_cache[NOFFSETS];


struct page_cache {
	unsigned long long int page; 		// 52 bit
	int delta;							// 7 bits
	unsigned int offset;				// 6 bits
	unsigned long long int lru; 		// 64 bit
} page_cache[PAGE_COUNT];


void initialize_delta_cache()
{
	int i;
	for(i = 0; i < NOFFSETS; i++){
		int j;
		for(j = 0; j < STATES_MAX; j++){
			delta_cache[i].delta_next[j] = 0;
			delta_cache[i].lfu[j] = 0;
		}
		delta_cache[i].delta_next_best = 0;
		delta_cache[i].max_lfu = 0;
	}
	return;
}

void initialize_page_cache(){
	int i;
	for(i = 0; i < PAGE_COUNT; i++){
		page_cache[i].page = 0;
		page_cache[i].delta = 0;
		page_cache[i].offset = 0;
		page_cache[i].lru = 0;
	}
}

int hashing(int delta){
	if(delta == 0){
		return -1;
	}
	if(delta <= 16 && delta >= -16){
		if(delta > 0){
			return 2*(delta-1);
		}else{
			return 2*(-delta-1)+1;
		}
	}
	int i;
	for(i = 32; i < NOFFSETS; i++){
		if(delta == OFFSET[i]){
			return i;
		}
	}
	return -1;
}

void deal_with_overflow(int index){
	int i;
	for(i = 0; i < STATES_MAX; i++){
		delta_cache[index].lfu[i] >>= 1;
	}
	delta_cache[index].max_lfu >>= 1;
	return;
}

void update_delta_cache(int delta_index_old, int new_delta){
	int delta_index = -1;
	int i;
	for(i = 0; i < STATES_MAX; i++){
		if(delta_cache[delta_index_old].delta_next[i] == new_delta)
		{
			delta_index = i;
			break;
		}
	}

	if(delta_index == -1){
		int lfu_min_index = 0;
		unsigned int lfu_min = delta_cache[delta_index_old].lfu[0];
		for(i = 1; i < STATES_MAX; i++){
			if(delta_cache[delta_index_old].lfu[i] < lfu_min){
				lfu_min = delta_cache[delta_index_old].lfu[i];
				lfu_min_index = i;
			}
		}
		delta_cache[delta_index_old].delta_next[lfu_min_index] = new_delta;
		delta_cache[delta_index_old].lfu[lfu_min_index] = 1;
		delta_index = lfu_min_index;
	}else{
		delta_cache[delta_index_old].lfu[delta_index] ++;
	}

	if(delta_cache[delta_index_old].lfu[delta_index] >= delta_cache[delta_index_old].max_lfu){
		delta_cache[delta_index_old].max_lfu = delta_cache[delta_index_old].lfu[delta_index];
		delta_cache[delta_index_old].delta_next_best = delta_cache[delta_index_old].delta_next[delta_index];
	}
	if(delta_cache[delta_index_old].max_lfu == ((1 << 8) - 1)){
		deal_with_overflow(delta_index_old);
	}
}

void l2_prefetcher_initialize(int cpu_num)
{
  	printf("Markov Prefetcher\n");
  	// you can inspect these knob values from your code to see which configuration you're runnig in
  	printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);
	initialize_delta_cache();
	initialize_page_cache();
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
	unsigned long long int cl_address = addr>>6;
  	unsigned long long int page = cl_address>>6;
  	unsigned long long int page_offset = cl_address&63;
	int page_index = -1;
	int i;
	for(i = 0; i < PAGE_COUNT; i++){
		if(page_cache[i].page == page)
		{
			page_index = i;
			break;
		}
	}

	if(page_index == -1){
		// the page was not found, so we must replace an old page with this new page

		// find the oldest page
		int lru_index = 0;
		unsigned long long int lru_cycle = page_cache[lru_index].lru;
		int i;
		for(i = 1; i < PAGE_COUNT; i++){
			if(page_cache[i].lru < lru_cycle){
				lru_index = i;
				lru_cycle = page_cache[lru_index].lru;
			}
		}
		page_index = lru_index;

		// reset the oldest page
		page_cache[page_index].page = page;
		page_cache[page_index].delta = 0;
		page_cache[page_index].offset = page_offset;
		page_cache[page_index].lru = get_current_cycle(0);
		return;
	}

	// update LRU
	page_cache[page_index].lru = get_current_cycle(0);
	int new_delta = page_offset - page_cache[page_index].offset;
	int delta_index_old = hashing(page_cache[page_index].delta);
	int delta_index_new = hashing(new_delta);
	if(delta_index_new == -1){
		page_cache[page_index].offset = page_offset;
		page_cache[page_index].delta = 0;
		// or DEFAULT_OFFSET
		return;
	}

	if(delta_index_old != -1){
		update_delta_cache(delta_index_old, new_delta);
	}

	int delta;
	delta = delta_cache[delta_index_new].delta_next_best;
	int pf_index = delta + page_offset;
	if(pf_index <= 63 && pf_index >= 0){
		unsigned long long int pf_address = (page<<12)+(pf_index<<6);
		if(get_l2_mshr_occupancy(0) < 8){
	      	l2_prefetch_line(0, addr, pf_address, FILL_L2);
	    }
	  	else{
	      	l2_prefetch_line(0, addr, pf_address, FILL_LLC);	      
	    }
	}
	page_cache[page_index].offset = page_offset;
	page_cache[page_index].delta = new_delta;
	// mark the access map
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{
  	// uncomment this line to see the information available to you when there is a cache fill event
  	// printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);

}

void l2_prefetcher_heartbeat_stats(int cpu_num)
{
  	printf("Prefetcher heartbeat stats\n");
}

void l2_prefetcher_warmup_stats(int cpu_num)
{
  	printf("Prefetcher warmup complete stats\n\n");
}

void l2_prefetcher_final_stats(int cpu_num)
{
  	printf("Prefetcher final stats\n");
}
