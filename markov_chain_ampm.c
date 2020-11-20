// Prefetcher based on markov chain and ampm_prefetcher
// Authors: xusirui(xusirui@pku.edu.cn)

#include <stdio.h>
#include "../inc/prefetcher.h"
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#define NOFFSETS 46
int OFFSET[NOFFSETS] = {1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,7,-7,8,-8,9,-9,10,-10,11,-11,12,-12,13,-13,14,-14,15,-15,16,-16,18,-18,20,-20,24,-24,30,-30,32,-32,36,-36,40,-40};
#define DEFAULT_OFFSET 1
#define STATES_MAX 20
#define PAGE_COUNT 512
#define TEPOCH 256000
#define DELTA_SIZE 9
#define DELTA_MAX 4
#define EVAL_PERIOD 256
int max_ampm_page=512;
int l2_mshr_thresh = 8;
int eval_counter;
unsigned long long int total_misses=1; // misses num for page_cache
unsigned long long int total_hits=1; // hits num for page_cache
int AMPM_PREFETCH_DEGREE = 2;
int PREFETCH_DEGREE;
static unsigned long long int total_cycles=0;
int GP; //Good prefetches
int TP; //Total Prefetches
int CM; //cache miss
int CH; //cache hit
unsigned long long int Epoch;
int num_cand = 4; // Default set to 4, so AMPM does -4 to 4 strides
int num_prefetches=0;
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
    int access_map[64];
    int pf_map[64];
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
        int j;
        for(j = 0; j < 64; j++)
	    {
	        page_cache[i].access_map[j] = 0;
	        page_cache[i].pf_map[j] = 0;
	    }
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
  	printf("markov ampm Prefetcher\n");
  	// you can inspect these knob values from your code to see which configuration you're runnig in
  	printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);
	initialize_delta_cache();
	initialize_page_cache();
	if(knob_low_bandwidth) 
	{
		//Reduce prefetch degree for both AMPM and DCPT for low bandwidth requirements
		AMPM_PREFETCH_DEGREE=1;
		#undef DELTA_MAX
		#define DELTA_MAX 3
	}
	GP = 0;
	TP = 0;
	CM = 0;
	CH = 0;
	Epoch = 0;
	eval_counter = 0;
	srand((int)time(0));
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
	float miss_rate=0.0;
	double P_cov=0;
	total_cycles++;
	miss_rate=(float)total_misses/(float)(total_hits+total_misses);
	unsigned long long int cl_address = addr>>6;
	unsigned long long int page = cl_address>>6;
	unsigned long long int page_offset = cl_address&63;  
	eval_counter++;
	//Begin Evaluation Period
	if (eval_counter % EVAL_PERIOD == 0)
	{
		double Pcov = (double)GP/(double)CM; //Good prefetches / cache miss
		double Pacc = (double)GP/(double)TP; //Good prefetches / Total Prefetches
		double Phit = (double)CH/(double)(CM + CH);
		// 13.5ns row hit, 40.5ns row miss. 4, 10, 20 latency l1 l2 l3. assume 100 cycle latency
		double Mbw = (double)(CM - GP + TP) / (double)(get_current_cycle(0)-Epoch) * 88.0;
		P_cov = Pcov;
		if(Mbw < 1.0) Mbw = 1.0;
		l2_mshr_thresh = 0;
		//If BW is high and hit rate is low, increase chance of prefetching more into LLC
		if ((Mbw > 8.0) && (Phit < .2)) l2_mshr_thresh = 6;
		//If prefetch accuracy and coverage are low, increase number of AMPM candidate strides to 1,2,3,4,6,8
		if ((Pacc < .9)&&(Pcov < .5)){
			num_cand = 6; //AMPM stride now is 1,2,3,4,6,8
			if (Phit < .2) l2_mshr_thresh = 6;
			else l2_mshr_thresh = 9-(((int)((Phit-0.2)*10))%9); //Vary L2_MSHR Threshold with hit rate. High hit rate would mean more prefetch into LCC. Less hit rate -> more into L2
		}else num_cand = 4; //fix AMPM candidates to a small value if accuracy and coverage are high
		if((TP+1)/(GP+1) > 10.0) //If ratio of Total Prefetches to Good prefetches is very high, further reduce MSHR threshold, and prefetch more into LLC
		{
			if (Phit < .2) l2_mshr_thresh = 2;
			else l2_mshr_thresh = 9-(((int)((Phit-0.1)*10))%9); //Modulate MSHR threshold based on hit rate. Increase threshold for low hit rate -> Prefetch more into L2.
		}
		GP = 0;
		TP = 0;
		CM = 0;
		CH = 0;
		Epoch = get_current_cycle(0);
	}// End of evaluation period	
	if (miss_rate<0.05) max_ampm_page=512;
	// For high miss rate, recycle old pages with a higher probability
	else
	{
		if(P_cov<=0.5) max_ampm_page=384; //For low coverage keep a slightly higher page count
		else max_ampm_page=200;
	}
	if(knob_small_llc)//Small LLC -> reduce Pages even further to 512/4=128 pages
	{
		if(miss_rate<0.05) max_ampm_page=128;
		// For high miss rate, recycle old pages with a higher probability
		else
		{
			if(P_cov<=0.5) max_ampm_page=64; //For low coverage keep a slightly higher page count
			else max_ampm_page=32;
		}    
	}
	int page_index = -1;
	int i;
	for(i = 0; i < max_ampm_page; i++){
		if(page_cache[i].page == page)
		{
			total_hits++;
			page_index = i;
			break;
		}
	}

	if(page_index == -1){
		// the page was not found, so we must replace an old page with this new page

		// find the oldest page
		total_misses++;
		int lru_index = 0;
		unsigned long long int lru_cycle = page_cache[lru_index].lru;
		int i;
		for(i = 1; i < max_ampm_page; i++){
			if(page_cache[i].lru < lru_cycle){
				lru_index = i;
				lru_cycle = page_cache[lru_index].lru;
			}
		}
		page_index = lru_index;
		if((rand() % 100) < 1) page_index = rand() % max_ampm_page;

		// reset the oldest page
		page_cache[page_index].page = page;
		page_cache[page_index].delta = 0;
		page_cache[page_index].offset = page_offset;
		page_cache[page_index].lru = get_current_cycle(0);
		for(i = 0; i < 64; i++)
     	{
    	  	page_cache[page_index].access_map[i] = 0;
    	  	page_cache[page_index].pf_map[i] = 0;
     	}
		CM++;
		page_cache[page_index].access_map[page_offset] = 1;
		return;
	}
	// Prefetch->access
	if((page_cache[page_index].pf_map[page_offset] == 1)&&(page_cache[page_index].access_map[page_offset] == 0)) GP++;   
	// access->access
  	if(page_cache[page_index].access_map[page_offset] == 1) CH++;
  	// init/pref -> access
  	else CM++;
    page_cache[page_index].access_map[page_offset] = 1;
	// update LRU
	page_cache[page_index].lru = get_current_cycle(0);
	int count_prefetches = 0;

	int new_delta = page_offset - page_cache[page_index].offset;
	int delta_index_old = hashing(page_cache[page_index].delta);
	int delta_index_new = hashing(new_delta);

	if(delta_index_old != -1 && delta_index_new != -1){
		update_delta_cache(delta_index_old, new_delta);
	}

	if(delta_index_new != -1){
		num_prefetches = 0;
		int delta;
		delta = delta_cache[delta_index_new].delta_next_best;
		int pf_index = delta + page_offset;
		if(pf_index <= 63 && pf_index >= 0 && delta != 0 && page_cache[page_index].access_map[pf_index] == 0 && page_cache[page_index].pf_map[pf_index] == 0){
			unsigned long long int pf_address = (page<<12)+(pf_index<<6);
			if(get_l2_mshr_occupancy(0) < l2_mshr_thresh){
				l2_prefetch_line(0, addr, pf_address, FILL_L2);
			}
			else{
				l2_prefetch_line(0, addr, pf_address, FILL_LLC);	      
			}
			page_cache[page_index].pf_map[pf_index] = 1;
			num_prefetches++;
		}
		
		page_cache[page_index].offset = page_offset;
		page_cache[page_index].delta = new_delta;
	}else{
		page_cache[page_index].offset = page_offset;
		page_cache[page_index].delta = 0;
	}
	//else
	if(delta_cache[page_index].max_lfu > (1 << 4) || delta_index_new == -1 || num_prefetches == 0){
		PREFETCH_DEGREE = AMPM_PREFETCH_DEGREE;
		// positive prefetching
		int k;
		int j[] = {1, 2, 3, 4, 6, 8};
		for(k = 0; k < num_cand; k++) //number of AMPM candidates can be either 4 or 6 based on miss rate, accuracy, and coverage
		{
			i = j[k];
			int check_index1 = page_offset - i;
			int check_index2 = page_offset - 2*i;
			int pf_index = page_offset + i;
			if(check_index2 < 0)
			{
				break;
			}

			if(pf_index > 63)
			{
				break;
			}

			if(count_prefetches >= PREFETCH_DEGREE)
			{
				break;
			}

			if(page_cache[page_index].access_map[pf_index] == 1)
			{
				// don't prefetch something that's already been demand accessed
				continue;
			}

			if(page_cache[page_index].pf_map[pf_index] == 1)
			{
				// don't prefetch something that's alrady been prefetched
				continue;
			}

			if((page_cache[page_index].access_map[check_index1]==1) && (page_cache[page_index].access_map[check_index2]==1))
			{
				// we found the stride repeated twice, so issue a prefetch
				unsigned long long int pf_address = (page<<12)+(pf_index<<6);
				// check the MSHR occupancy to decide if we're going to prefetch to the L2 or LLC
				if(get_l2_mshr_occupancy(0) < l2_mshr_thresh)
				{
					l2_prefetch_line(0, addr, pf_address, FILL_L2);
				}
				else
				{
					l2_prefetch_line(0, addr, pf_address, FILL_LLC);	      
				}
				// mark the prefetched line so we don't prefetch it again
				page_cache[page_index].pf_map[pf_index] = 1;
				count_prefetches++;
				TP++; 
			}
		}

		// negative prefetching
		count_prefetches = 0;
		for(k=0; k<num_cand; k++)
		{
			i = j[k];
			int check_index1 = page_offset + i;
			int check_index2 = page_offset + 2*i;
			int pf_index = page_offset - i;

			if(check_index2 > 63)
			{
				break;
			}

			if(pf_index < 0)
			{
				break;
			}

			if(count_prefetches >= PREFETCH_DEGREE)
			{
				break;
			}

			if(page_cache[page_index].access_map[pf_index] == 1)
			{
				// don't prefetch something that's already been demand accessed
				continue;
			}

			if(page_cache[page_index].pf_map[pf_index] == 1)
			{
				// don't prefetch something that's alrady been prefetched
				continue;
			}

			if((page_cache[page_index].access_map[check_index1]==1) && (page_cache[page_index].access_map[check_index2]==1))
			{
				// we found the stride repeated twice, so issue a prefetch
				unsigned long long int pf_address = (page<<12)+(pf_index<<6);
				// check the MSHR occupancy to decide if we're going to prefetch to the L2 or LLC
				if(get_l2_mshr_occupancy(0) < l2_mshr_thresh)
				{
					l2_prefetch_line(0, addr, pf_address, FILL_L2);
				}
				else
				{
					l2_prefetch_line(0, addr, pf_address, FILL_LLC);	      
				}
				// mark the prefetched line so we don't prefetch it again
				page_cache[page_index].pf_map[pf_index] = 1;
				count_prefetches++;

			}
		}
	}
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
