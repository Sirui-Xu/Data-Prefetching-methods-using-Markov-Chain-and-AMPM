//
//          Data Prefetching Championship 2
//                  Slim AMPM
//              %%%%Authors%%%%%%
//        Vinson Young and Ajit Krisshna
#include <stdio.h>
#include "../inc/prefetcher.h"
#include <stdlib.h>
#include <stdbool.h>
#define TEPOCH 256000
#define AMPM_PAGE_COUNT 512
#define DCPT_SIZE 200
#define DELTA_SIZE 9
#define DELTA_MAX 4
#define EVAL_PERIOD 256
int max_ampm_page=512;
int l2_mshr_thresh = 8;
int eval_counter;
unsigned long long int total_misses=1;
unsigned long long int total_hits=1;
int AMPM_PREFETCH_DEGREE = 2;
int PREFETCH_DEGREE;
static unsigned long long int total_cycles=0;
int GP;
int TP;
int CM; //cache miss
int CH; //cache hit
unsigned long long int Epoch;
int num_cand = 4; // Default set to 4, so AMPM does -4 to 4 strides
//AMPM structure initialize
typedef struct ampm_page
{
  // page address
  unsigned long long int page; //64 bit
  // The access map itself.
  // Each element is set when the corresponding cache line is accessed.
  // The whole structure is analyzed to make prefetching decisions.
  // While this is coded as an integer array, it is used conceptually as a single 64-bit vector.
  int access_map[64];// 64 bit vector
  // This map represents cache lines in this page that have already been prefetched.
  // We will only prefetch lines that haven't already been either demand accessed or prefetched.
  int pf_map[64]; //64 bit vector
  // used for page replacement
  unsigned long long int lru; //64 bit
} ampm_page_t;

ampm_page_t ampm_pages[AMPM_PAGE_COUNT];
// Delta-Correlation Pattern -- DCPT Prefetcher structure intitialize
typedef struct table
{
  unsigned long long int pc; // 64 bit
  unsigned long long int last_address; // 64 bit
  unsigned long long int last_prefetch; // 64 bit
  bool valid; // 1-bit
  unsigned long long int lru_stamp; // 64 bit
  int delta[DELTA_SIZE]; //32 bit x 9 elements
}DCPT;

typedef unsigned long long int address_long;
address_long prefetch_candidates[DELTA_SIZE];
address_long prefetches[DELTA_SIZE];
DCPT dcpt_table[DCPT_SIZE];
int num_prefetch_candidates=0; //DCPT finds prefetch candidates
int num_prefetches=0; //After filtering of prefetches, we keep a count of how many succedded
bool FOUND=0; //Flag variable  which is true if the PC is found in DCPT table

//%%%%%%%%%%%%%Initialize all deltas in each entry to 0%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void initialize_delta(int table_id)
{
  int i;
  for(i=0;i<DELTA_SIZE;i++) 
  {
    dcpt_table[table_id].delta[i]=0;
  }
  return;
}
//%%%%%%%%%%%%%Initialize all deltas in each entry to 0%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void initialize_dcpt()
{
  int i;
  for(i=0;i<DCPT_SIZE;i++)
  {
    dcpt_table[i].pc=0;
    dcpt_table[i].last_address=0;
    dcpt_table[i].last_prefetch=0;
    dcpt_table[i].lru_stamp=get_current_cycle(0); // why not zero
    dcpt_table[i].valid=0;
    initialize_delta(i);
  }
}
//%%%%%%%%%%%%%Initialize all prefetch candidates to a "value"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void init_candidates(int value) 
{
  int i;
  for(i=0;i<DELTA_SIZE;i++)
  {
    prefetch_candidates[i]=value;
    prefetches[i]=value;
  }
}
//%%%%%%%%%%%%%Find PC in DCPT table%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
int table_lookup(unsigned long long int ip)
{
  //Do LOOKUP
  int i;
  for(i=0;i<DCPT_SIZE;i++)
  {
    if(dcpt_table[i].pc==ip)
    {
      //FOUND. Now we can exit with the index
      FOUND=1;
      dcpt_table[i].lru_stamp=get_current_cycle(0);
      return i;      
    }
  }
  FOUND=0;
  //INSERT - Not found in table. Insert new entry by trying to find invalid entry. Update LRU stamp.
  for(i=0;i<DCPT_SIZE;i++)
  {
    if(dcpt_table[i].valid==0)
    {
      dcpt_table[i].lru_stamp=get_current_cycle(0);
      return i;
    }
  }
  //REPLACE - Found only valid entries. Do LRU replacement.
  unsigned long long int min=dcpt_table[0].lru_stamp;
  int lru_index=0;
  for(i=0;i<DCPT_SIZE;i++)
  {
    if(dcpt_table[i].lru_stamp<min)
    {
      lru_index=i;
      min=dcpt_table[i].lru_stamp;
    }
  }
  dcpt_table[lru_index].lru_stamp=get_current_cycle(0);
  return lru_index;
}
//%%%%%%%%%%%%%Insert new delta in the delta array which is implemented as a circular buffer0%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void insert_circ_buf(int entry_id, int single_delta)
{
  int temp[DELTA_SIZE-1];
  int i;
  for(i=0;i<DELTA_SIZE-1;i++)
  {
    temp[i]=dcpt_table[entry_id].delta[i];
  }
  dcpt_table[entry_id].delta[0]=single_delta;
  for(i=1;i<DELTA_SIZE;i++)
  {
    dcpt_table[entry_id].delta[i]=temp[i-1];
  }
  return;

}
//%%%%%%%%%%%%%Find the starting index corresponding to the pattern%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
int find_last_index(int entry_id, int d1, int d2)
{
  int i;
  for(i=2;i<DELTA_SIZE-1;i++)
  {
    if(dcpt_table[entry_id].delta[i]==d1 && dcpt_table[entry_id].delta[i+1]==d2) return i-1;
  }
  return -1;
}
//%%%%%%%%%%%%%Go through each delta after starting index for the pattern and store in prefetch candidates array%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void delta_correlation(int entry_id)
{
  init_candidates(0);
  num_prefetch_candidates=0;
  int d1=dcpt_table[entry_id].delta[0];
  int d2=dcpt_table[entry_id].delta[1];
  int last_delta=find_last_index(entry_id,d1,d2);
  if(last_delta==-1) return;
  address_long temp_address=dcpt_table[entry_id].last_address;
  int i;
  for(i=last_delta;i>=0;i--)
  {
    if(dcpt_table[entry_id].delta[i]==0) //Ignore if delta is zero
      continue;
    temp_address=temp_address+dcpt_table[entry_id].delta[i];//Finding prefetch address
    prefetch_candidates[last_delta-i]=temp_address;
    num_prefetch_candidates++;//Found a candidate, increment counter
  }

}
//%%%%%%%%%%%%%Filter the prefetch candidates%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void filter_and_issue_prefetches(int entry_id, int page_index, unsigned long long int addr)
{
  num_prefetches=0;
  int i;
  for(i=0;i<num_prefetch_candidates;i++)
  {
        //Number of prefetches exceeds max allowed prefetches for DCPT
     if(num_prefetches >= DELTA_MAX) break;
     int pf_index = (prefetch_candidates[i] >> 6) & 63; //pf cacheline address
     if((prefetch_candidates[i]>>12)!=(addr>>12))
      {
        //Not within page boundary
        continue;
      }
      if(ampm_pages[page_index].access_map[pf_index] == 1)
      {
        // don't prefetch something that's already been demand accessed
       continue;
      }

      if(ampm_pages[page_index].pf_map[pf_index] == 1)
      {
        // don't prefetch something that's alrady been prefetched
        continue;
      }      
      if(prefetch_candidates[i]==dcpt_table[entry_id].last_prefetch)
      {
        // don't prefetch something that's alrady been prefetched in this iteration
        continue;
      }
      prefetches[num_prefetches]=prefetch_candidates[i];
      dcpt_table[entry_id].last_prefetch=prefetch_candidates[i];
      unsigned long long int pf_address = prefetches[num_prefetches];
        // check the MSHR occupancy to decide if we're going to prefetch into L2 or LLC
      if(get_l2_mshr_occupancy(0) < l2_mshr_thresh)
      {
          l2_prefetch_line(0, addr, pf_address, FILL_L2);
      }
      else
      {
          l2_prefetch_line(0, addr, pf_address, FILL_LLC);        
      }
        // mark the prefetched line so we don't prefetch it again
      ampm_pages[page_index].pf_map[pf_index] = 1;
      num_prefetches++;
  }
  return;
}
void l2_prefetcher_initialize(int cpu_num)
{      
  printf("Slim AMPM Prefetcher\n");
  printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);
  if(knob_low_bandwidth) 
  {
    //Reduce prefetch degree for both AMPM and DCPT for low bandwidth requirements
    AMPM_PREFETCH_DEGREE=1;
    #undef DELTA_MAX
    #define DELTA_MAX 3
  }
  int i;
  for(i=0; i<AMPM_PAGE_COUNT; i++)
  {
      ampm_pages[i].page = 0;
      ampm_pages[i].lru = 0;
      int j;
      for(j=0; j<64; j++)
	    {
	       ampm_pages[i].access_map[j] = 0;
	       ampm_pages[i].pf_map[j] = 0;
	    }
  }
  GP = 0;
  TP = 0;
  CM = 0;
  CH = 0;
  Epoch = 0;
  eval_counter = 0;
  initialize_dcpt();
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
    P_cov=Pcov;
    if(Mbw < 1.0) Mbw = 1.0;
    l2_mshr_thresh = 0;
    //If BW is high and hit rate is low, increase chance of prefetching more into LLC
    if ((Mbw > 8.0) && (Phit < .2)) l2_mshr_thresh = 6;
    //If prefetch accuracy and coverage are low, increase number of AMPM candidate strides to 1,2,3,4,6,8
    if ((Pacc < .9)&&(Pcov < .5))
    {
      num_cand = 6; //AMPM stride now is 1,2,3,4,6,8
      if (Phit < .2) l2_mshr_thresh = 6;
      else l2_mshr_thresh = 9-(((int)((Phit-0.2)*10))%9); //Vary L2_MSHR Threshold with hit rate. High hit rate would mean more prefetch into LCC. Less hit rate -> more into L2
    }
    else num_cand = 4; //fix AMPM candidates to a small value if accuracy and coverage are high
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

  // If miss rate is very low, keep a high page count and recycle pages less frequently
  if(miss_rate<0.05) max_ampm_page=512;
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
  // check to see if we have a page hit
  for(i=0; i<max_ampm_page; i++)
  {
    if(ampm_pages[i].page == page)
    {
      total_hits++;  // is this refer to page hit??
      page_index = i;
      break;
    }
  }

 if(page_index == -1)
  {
     total_misses++;
     // the page was not found, so we must replace an old page with this new page
     // find the oldest page
     int lru_index = 0;
     unsigned long long int lru_cycle = ampm_pages[lru_index].lru;
    
     for(i=0; i<max_ampm_page; i++)
     {
    	  if(ampm_pages[i].lru < lru_cycle)
    	  {
          lru_index = i;
          lru_cycle = ampm_pages[lru_index].lru;
    	  }
     }
     // reset the oldest page
     page_index = lru_index;
     // refresh a random page 1% of misses
     if((rand() % 100) < 1) page_index = rand() % max_ampm_page;

     ampm_pages[page_index].page = page;
     for(i=0; i<64; i++)
     {
    	  ampm_pages[page_index].access_map[i] = 0;
    	  ampm_pages[page_index].pf_map[i] = 0;
     }
  }
  // Prefetch->access
  if((ampm_pages[page_index].pf_map[page_offset] == 1)&&(ampm_pages[page_index].access_map[page_offset] == 0)) GP++;   
  // access->access
  if(ampm_pages[page_index].access_map[page_offset] == 1) CH++;
  // init/pref -> access
  else CM++;
// --------------------------------------------------
  // update LRU
  ampm_pages[page_index].lru = get_current_cycle(0);
  // mark the access map
  ampm_pages[page_index].access_map[page_offset] = 1;
  int count_prefetches = 0;

  //DCPT ---------------------------------------------------------
  //Lookup the PC in the DCPT table
  int entry_id=table_lookup(ip);
  if(FOUND==0) // If not found insert the new entry with zeros and update latest PC information
  {
    dcpt_table[entry_id].pc=ip;
    dcpt_table[entry_id].last_address=addr;
    initialize_delta(entry_id);
    dcpt_table[entry_id].last_prefetch=0;
  }
  else if(((addr)-dcpt_table[entry_id].last_address)!=0) //If found
  {
    insert_circ_buf(entry_id, ((addr)-dcpt_table[entry_id].last_address)); //Calculate new delta and insert into delta buffer
    dcpt_table[entry_id].last_address=addr; //Store current address into entry structure for future delta calculation
    delta_correlation(entry_id);//Find prefetch candidates
    if(num_prefetch_candidates>0) //If not empty
      {
        filter_and_issue_prefetches(entry_id,page_index,addr); //Filter candidates and issue prefetches
        if(num_prefetches>0) return; //If DCPT sent prefetches do not use AMPM
      }
// If DCPT did not send any prefetch, use AMPM
  }

 // --------------------- AMPM PREFETCHING -------------------
  PREFETCH_DEGREE = AMPM_PREFETCH_DEGREE;
  // positive prefetching
  int k;
  int j[] = {1, 2, 3, 4, 6, 8};
  for(k=0; k<num_cand; k++) //number of AMPM candidates can be either 4 or 6 based on miss rate, accuracy, and coverage
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

    if(ampm_pages[page_index].access_map[pf_index] == 1)
  	{
  	  // don't prefetch something that's already been demand accessed
  	  continue;
  	}

    if(ampm_pages[page_index].pf_map[pf_index] == 1)
  	{
  	  // don't prefetch something that's alrady been prefetched
  	  continue;
  	}

    if((ampm_pages[page_index].access_map[check_index1]==1) && (ampm_pages[page_index].access_map[check_index2]==1))
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
	     ampm_pages[page_index].pf_map[pf_index] = 1;
       count_prefetches++;
       TP++; //why only add it here? why not count it on DCPT
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

      if(ampm_pages[page_index].access_map[pf_index] == 1)
    	{
    	  // don't prefetch something that's already been demand accessed
    	  continue;
    	}

      if(ampm_pages[page_index].pf_map[pf_index] == 1)
    	{
    	  // don't prefetch something that's alrady been prefetched
    	  continue;
    	}

      if((ampm_pages[page_index].access_map[check_index1]==1) && (ampm_pages[page_index].access_map[check_index2]==1))
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
  	       ampm_pages[page_index].pf_map[pf_index] = 1;
           count_prefetches++;
           TP++;
      }
	  }  
}
void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{
  // uncomment this line to see the information available to you when there is a cache fill event
  //printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
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

