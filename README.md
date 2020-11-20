# Data Prefetching methods using Markov Chain and AMPM

An implement in Data Prefetching Championship. (Project in "Computer Architecture", a course from pkueecs)

Almost beat every model in 2nd Data Prefetching Championship.

### Algorithm

`Markov_chain_prefetcher.c`

The state of Markov chain is defined as the difference between the prefetched offset and the previous offset (the same page is between - 63 and + 63). The state quantity is limited to 46, also, to satisfy that offset is a multiple of 2,3,5. In order to reduce the storage capacity, some ideas refer to "Pangloss: Novel Markov chain prefetcher".

`Markov_chain_ampm.c`

Based on AMPM algorithm, An improvement was achieved.

you can check the [exmaple_prefetchers/](https://github.com/Sirui-Xu/Data-Prefetching-methods-using-Markov-Chain-and-AMPM/tree/main/example_prefetchers) for implements'details

### Results

|traces/IPCA |best-offset prefetcher(champion)|  Markov chain prefetcher|
|----|----|----|
|gcc_trace2|0.343890|**0.344170**|
|lbm_trace2|**1.998883**|1.976280|
|libquantum_trace2|3.254704|**3.281847**|
|milc_trace2|**1.270079**|1.186075|
|GemsFDTD_trace2|**3.452212**|3.440013|
|leslie3d_trace2|**1.631569**|1.356836|
|mcf_trace2.dpc|**0.371862**|0.368020|
|omnetpp_trace2|2.155531|**2.2042942**|

|traces/IPC|ampm|slim_ampm(runner-up)|  best offset(champion)|  markov chain | markov chain + ampm(ours)|
|----|----|----|----|----|----|
|gcc_trace2|0.339157|**0.345508**|0.343890|   0.344170|**`0.350100`**|
|lbm_trace2|2.035000|**2.035816**|1.998883|   1.976280|**`2.067567`**|
|libquantum_trace2|   3.274124|  3.276598|3.254704|**`3.281847`**|**3.280319**|
|milc_trace2|1.179511|**`1.311450`**|1.270079|   1.186075|**1.302887**|
|GemsFDTD_trace2|  3.451324|  3.450598|**3.452212**|3.440013|**`3.452546`**|
|leslie3d_trace2|1.623532|**1.696512**|1.631569|   1.356836|**`1.697695`**|
|mcf_trace2.dpc|0.372714|**`0.395652`**|0.371862|   0.368020|**0.380612**|
|omnetpp_trace2|**`2.269448`**|**2.262831**|2.155531|   2.204294|2.210579|


