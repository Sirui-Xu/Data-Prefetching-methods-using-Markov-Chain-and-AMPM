// Paper #8
// Title: Prefetching on time and when it works
// Authors:
// Ibrahim Burak Karsli - bkarsli@ele.uri.edu - Department of Electrical, Computer and Biomedical Engineering - University of Rhode Island
// Mustafa Cavus - mcavus@my.uri.edu - Department of Electrical, Computer and Biomedical Engineering - University of Rhode Island
// Resit Sendag - sendag@ele.uri.edu - Department of Electrical, Computer and Biomedical Engineering - University of Rhode Island


#include <stdio.h>
#include <stdlib.h>
#include "inc/prefetcher.h"


// *********************************************************************************************
// ************************************* BEGINNING OF SEQ **************************************
//**********************************************************************************************

#define MAXDISTANCE 6 // max distance pointer
#define INTERVAL 512
#define TQSIZE 128
#define PFQSIZE 128
#define IP_MASK_8b (0x0ff)
#define IP_MASK_16b (0x0ffff)
#define IP_MASK_32b (0x0ffffffff)

//prefetch usefulness
/* every 2048 accesses, 4 evaluation periods, if prefetching is on, turn-off prefetches on blocks with addr, addr%4 == 2. then at the end of the current period, check the L2 demand access miss rate on blocks that prefetching was turned off, and miss rate for all other blocks. If hit rate for other blocks is lower, turn off prefetching for the next region.*/

#define TESTPERIOD 4*INTERVAL
// testing related
int testblks = 0;
int testblks_hits = 0;
int otherblks = 0;
int otherblks_hits = 0;
int testing = 0;
int testperiod = TESTPERIOD;
int nexttest = TESTPERIOD;
int cntoff = 0;
int cnton = 0;
int DIFFLIMIT = INTERVAL*0.6;
//test queue for testing timeliness and relative success
typedef struct q{
  int tail;
  struct{
    unsigned long long int addr;
  }ent[TQSIZE];
}q_t;

//prefetch queue for prefetch filtering
typedef struct pq{
  int tail;
  struct{
    unsigned long long int addr;
  }ent[PFQSIZE];
}pq_t;
pq_t *pfq;

q_t *tq;
int tqhits = 0;
int avgtqhits = 0;
int tqmhits = 0;
unsigned long long int sumdiff = 0;
int l2acc = 0;
int l2miss = 0;
int high = 0;
int low = 0;
int pfcut = 0;
int pfcut2 = 0;
int low2 = 0;
int l2_mshr_limit = 8;
int distance = 1;
int pf_turn_on = 0;
int pf_turn_off = 0;


int dist[] = {0,1,2,3,4,5,6,7,8,9,10,11,12};
int dist_ptr = 1; // start with offset 1


void tq_ini(){

  int i;
  for(i=0;i<TQSIZE;i++){
    tq->ent[i].addr = 0;
  }
  tq->tail = -1;

}

int intq(unsigned long long int addr){

  int i;
  int found = 0;
  for(i=0;i<TQSIZE;i++){
    if(addr == tq->ent[i].addr){
      found = 1;
      break;
    }
  }
  return found;
}

void pfq_ini(){

  int i;
  for(i=0;i<PFQSIZE;i++){
    pfq->ent[i].addr = 0;
  }
  pfq->tail = -1;

}

int inpfq(unsigned long long int addr){
  int i;
  int found = 0;
  for(i=0;i<PFQSIZE;i++){
    if(addr == pfq->ent[i].addr){
      found = 1;
      break;
    }
  }
  return found;
}


void SEQT_ini(int low_bw){


  tq = (q_t *)malloc(sizeof(q_t));
  pfq = (pq_t *)malloc(sizeof(pq_t));
	if(low_bw) DIFFLIMIT = INTERVAL*0.5;
  tq_ini();
  pfq_ini();

}


void SEQT(unsigned long long int addr, unsigned long long int ip, int cache_hit){
  
  static int suml2occ = 0;
  static int numl2occhigh = 0;
  
  suml2occ +=  get_l2_mshr_occupancy(0);
  
  if(get_l2_mshr_occupancy(0)>=15)
    numl2occhigh++;
  
  unsigned long long int cycle = get_current_cycle(0); 
  l2acc++;
  if(!cache_hit)
    l2miss++;
  
  //search for l2acc in tq
  int m;
  for(m=0; m<TQSIZE; m++){
    if(((addr&~0x3f) >> 6) == tq->ent[m].addr){
      tqhits++;

      if(!cache_hit)
	tqmhits++;
      
      break;
      
    }
  }
  
  
  if((l2acc%INTERVAL)==0){
    //decide on distance

    
    if(tqhits < 16){
      if(pfcut > 1){
	dist_ptr = 0;
	pfcut2 = 0;
      }
      else{
	if(dist_ptr > 1)
	  dist_ptr--;
      }

      if((l2miss-tqmhits)>DIFFLIMIT)
	pfcut2++;
      else
	pfcut2 = 0;

      high = 0;
      low = 0;
      low2 = 0;
      pfcut++;
    }
    else{
      pfcut = 0;

      if(l2miss < 10){
	pfcut2 = 0;
      }
      else if((l2miss-tqmhits)>DIFFLIMIT){

	low2 = 0;

	if(pfcut2 > 1){
	  dist_ptr = 0;
	  pfcut2 = 0;
	}
	else{
	  pfcut2++;

	  if(dist_ptr == 0){
	    if(pf_turn_on > 1){
	      pf_turn_on = 0;
	      dist_ptr = 1;
	    }
	    else
	      pf_turn_on++;
	  }

	}
	high = 0;
	low = 0;
      }
      else if((l2miss!=0) && (tqmhits !=0)){
	if((l2miss/tqmhits < 2)){
	  
	  if(low2>=2){
	    if(dist_ptr<MAXDISTANCE){
	      dist_ptr++;
	      low2 = 0;
	    }
	  }
	  else{
	    low2++;
	  }
	}
	else
	  low2 = 0;

	high = 0;
	low = 0;
	pfcut2 = 0;
	
	
	if(dist_ptr == 0){
	  if(pf_turn_on > 1){
	    pf_turn_on = 0;
	    dist_ptr = 1;
	  }
	  else
	    pf_turn_on++;
	}
      }
      else{
	pfcut2 = 0;
	high = 0;
	low = 0;
	low2 = 0;

      }
    }

    distance = dist[dist_ptr];

	//reset table
	tq_ini();

    avgtqhits = 0;
    tqmhits = 0;
    sumdiff = 0;
    tqhits = 0;
    l2miss = 0;


    suml2occ = 0;
    numl2occhigh = 0;

    //testing related
    if(testing == 1){

      double tblks_hitrate = (double)testblks_hits/(double)testblks;
      double oblks_hitrate = (double)otherblks_hits/(double)otherblks;

      int pf_off = 0;

      if(knob_low_bandwidth){
	//turn off prefetching if really not worth it
	pf_off = ((double)oblks_hitrate < (1.2*(double)tblks_hitrate));

	if(pf_off == 0)
	  pf_off = (otherblks_hits < 16);
      }
      else{
	// give advantage to prefetching
	pf_off = (((double)tblks_hitrate/(double)oblks_hitrate) > 1.2);

	if(pf_off == 0)
	  pf_off = (otherblks_hits < 8);

      }

      int failedtest = ((testblks < 32) || (otherblks < 32));	


      if(!failedtest){
	if(pf_off){

	  distance = 0;
	  if(testperiod > INTERVAL)
	    testperiod = testperiod>>1;
	  
	  //update turning off counter
	  if(cntoff < 3)
	    cntoff++;
	  
	  cnton = 0;
	  
	}
	else{
	  if(testperiod < (32*INTERVAL))
	    testperiod = testperiod*2;
	  
	  //update turning off counter
	  cntoff = 0;
	  
	  if(cnton < 3)
	    cnton++;

	}
      }
      else{
	//failed test, try again next interval
	testperiod = INTERVAL;
      }
      nexttest += testperiod;
      
    }
    
    testing = 0;
    testblks_hits = 0;
    testblks = 0;
    otherblks_hits = 0;
    otherblks = 0;

  }

  //for now, do this only for low bandwidth
  if(l2acc==nexttest && knob_low_bandwidth){
    if(distance!=0)
      testing = 1;
    else
      nexttest += INTERVAL;
  }
  
  int Istblk = 0;

  if(testing == 1){

    
    //if keeps turning off prefetcher, increase the number of tblks
    /**/
    if(cntoff>=2)
      Istblk = (addr>>6)%3 != 1;
    else
      Istblk = (addr>>6)%4 == 2;



    if(Istblk){
      testblks++;

      if(cache_hit)
	testblks_hits++;
    }
    else{
      otherblks++;

      if(cache_hit)
	otherblks_hits++;
    }
    
  }


  unsigned long long int pf_address;

  pf_address = ((addr>>6)+distance)<<6;
  int samepage = (pf_address>>12) == (addr>>12);  

  //if distance is 0 (nopref), put in the queue as if distance = 1
  if(distance == 0)
    pf_address = ((addr>>6)+1)<<6;

  if(testing == 1){
    //if keeps turning off prefetcher, increase the number of tblks
    /**/
    if(cntoff>=2)
      Istblk = (pf_address>>6)%3 != 1;
    else
      Istblk = (pf_address>>6)%4 == 2;

   }


  int nopf = ((testing ==1) && (Istblk));
  
  
  if(!nopf){

    if(samepage && !inpfq(pf_address >> 6)){
      
      if(distance != 0){
	//todo: make occupancy limit check dynamic
	
	if(get_l2_mshr_occupancy(0) < l2_mshr_limit)
	  {
	    l2_prefetch_line(0, addr, pf_address, FILL_L2);
	  }
	else
	  {
	    l2_prefetch_line(0, addr, pf_address, FILL_LLC);	      
	  }
        
	//add prefetched addr to the prefetch queue
	pfq->tail = (pfq->tail+1) % PFQSIZE;
	pfq->ent[pfq->tail].addr = (pf_address >> 6);
	
      }
      
    }
    
    
  }
  
  
    //add potential prefetch addr to the test queue
  if(samepage && !intq(pf_address>> 6)){
    tq->tail = (tq->tail+1) % TQSIZE;
    tq->ent[tq->tail].addr = (pf_address >> 6);
  }
  
  
  
} // end SEQT()


// *********************************************************************************************
// ************************************* ENDING OF SEQ **************************************
//**********************************************************************************************


// *********************************************************************************************
// ******************************* BEGINNING OF IP STRIDE **************************************
//**********************************************************************************************

#define IP_TRACKER_COUNT 1024
#define IP_PREFETCH_DEGREE 1
#define IP_DISTANCE 2

typedef struct ip_tracker
{
  // the IP we're tracking
  unsigned int ip;//32 bits

  // the last address accessed by this IP
  unsigned short last_addr;//16 bits
  // the stride between the last two addresses accessed by this IP
  short last_stride;//8 bits

  // use LRU to evict old IP trackers
  unsigned long long int lru_cycle;
} ip_tracker_t;

ip_tracker_t trackers[IP_TRACKER_COUNT];


void IP_STRIDE_ini() {
  int i;
  for(i=0; i<IP_TRACKER_COUNT; i++)
    {
      trackers[i].ip = 0;
      trackers[i].last_addr = 0;
      trackers[i].last_stride = 0;
      trackers[i].lru_cycle = 0;
    }
}

void IP_STRIDE(unsigned long long int addr, unsigned long long int ip, int cache_hit) {
  // check for a tracker hit
  int tracker_index = -1;
  unsigned long long int addr_blk = addr >> 6;

  int i;
  for(i=0; i<IP_TRACKER_COUNT; i++)
    {
      if(trackers[i].ip == (ip & IP_MASK_32b))
	{
	  trackers[i].lru_cycle = get_current_cycle(0);
	  tracker_index = i;
	  break;
	}
    }

  if(tracker_index == -1)
    {
      // this is a new IP that doesn't have a tracker yet, so allocate one
      int lru_index=0;
      unsigned long long int lru_cycle = trackers[lru_index].lru_cycle;
      int i;
      for(i=0; i<IP_TRACKER_COUNT; i++)
	{
	  if(trackers[i].lru_cycle < lru_cycle)
	    {
	      lru_index = i;
	      lru_cycle = trackers[lru_index].lru_cycle;
	    }
	}

      tracker_index = lru_index;

      // reset the old tracker
      trackers[tracker_index].ip = ip & IP_MASK_32b;
      trackers[tracker_index].last_addr = addr_blk & IP_MASK_16b;
      trackers[tracker_index].last_stride = 0;
      trackers[tracker_index].lru_cycle = get_current_cycle(0);

      return;
    }

  // calculate the stride between the current address and the last address
  // this bit appears overly complicated because we're calculating
  // differences between unsigned address variables
  short stride = 0;
  if((addr_blk & IP_MASK_16b) > trackers[tracker_index].last_addr)
    {
      stride = ((addr_blk & IP_MASK_16b) - trackers[tracker_index].last_addr) & IP_MASK_8b;
    }
  else
    {
      stride = (trackers[tracker_index].last_addr - (addr_blk & IP_MASK_16b)) & IP_MASK_8b;
      stride *= -1;
    }

  // don't do anything if we somehow saw the same address twice in a row
  if(stride == 0)
    {
      return;
    }

  // only do any prefetching if there's a pattern of seeing the same
  // stride more than once
  if(stride == trackers[tracker_index].last_stride)
    {
      // do some prefetching
      int i;
      int tempdist = distance;
      tempdist = tempdist / 2;
      if (tempdist == 0) tempdist = 1;
      for(i=tempdist; i<IP_PREFETCH_DEGREE+tempdist; i++)
      //for(i=IP_DISTANCE; i<IP_PREFETCH_DEGREE+IP_DISTANCE; i++)
	{
	  unsigned long long int pf_address = (addr_blk + (stride*(i+1))) << 6;

	  // only issue a prefetch if the prefetch address is in the same 4 KB page 
	  // as the current demand access address
	  if((pf_address>>12) != (addr>>12))
	    {
	      break;
	    }

	  // check the MSHR occupancy to decide if we're going to prefetch to the L2 or LLC
	  if (!inpfq((pf_address >> 6))) {
	  if(get_l2_mshr_occupancy(0) < l2_mshr_limit)
	    {
	      l2_prefetch_line(0, addr, pf_address, FILL_L2);
	    }
	  else
	    {
	      l2_prefetch_line(0, addr, pf_address, FILL_LLC);
	    }
	 	pfq->tail = (pfq->tail+1) % PFQSIZE;
		pfq->ent[pfq->tail].addr = (pf_address >> 6);
	  }
	  
	}
    }

  trackers[tracker_index].last_addr = addr_blk & IP_MASK_16b;
  trackers[tracker_index].last_stride = stride;
}

// *********************************************************************************************
// ************************************* END OF IP STRIDE **************************************
//**********************************************************************************************


void l2_prefetcher_initialize(int cpu_num)
{
  IP_STRIDE_ini();
  SEQT_ini(knob_low_bandwidth);
}


void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
  IP_STRIDE(addr, ip, cache_hit);
  SEQT(addr, ip, cache_hit);  
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{

}

void l2_prefetcher_heartbeat_stats(int cpu_num)
{

}
void l2_prefetcher_warmup_stats(int cpu_num)
{

}

void l2_prefetcher_final_stats(int cpu_num)
{

}


