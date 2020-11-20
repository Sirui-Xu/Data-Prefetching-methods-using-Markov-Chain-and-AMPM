# Data Prefetching methods using Markov Chain and AMPM

An implement in Data Prefetching Championship. (Project in "Computer Architecture", a course from pkueecs)

Almost beat every model in 2nd Data Prefetching Championship.

### Algorithm

`Markov_chain_prefetcher.c`

I define the state of Markov chain as the difference between the prefetched offset and the previous offset (the same page is between - 63 and + 63). Referring to the idea that dpc2 is the first place, I limit the state quantity to 46, that is, to satisfy $|offset| = 2^i*3^j*5^k$. In order to reduce the storage capacity, I refer to the paper "Pangloss: Novel Markov chain prefetcher" 

`Markov_chain_ampm.c`
Based on AMPM algorithm, An improvement was achieved.

you can check the `exmaple_prefetchers/` for implements'details

### Results

|traces/IPCA |best-offset prefetcher(champion)|  Markov chain prefetcher|
|gcc_trace2|0.343890|**0.344170**|
|lbm_trace2|**1.998883**|1.976280|
|libquantum_trace2|3.254704|**3.281847**|
|milc_trace2|**1.270079**|1.186075|
|GemsFDTD_trace2|**3.452212**|3.440013|
|leslie3d_trace2|**1.631569**|1.356836|
|mcf_trace2.dpc|**0.371862**|0.368020|
|omnetpp_trace2|2.155531|**2.2042942**|

|traces/IPC|ampm|slim_ampm(runner-up)|  best offset(champion)|  markov chain | markov chain + ampm(ours)|
|gcc_trace2|0.339157|<font color='blue'>0.345508</font>|0.343890|   0.344170|<font color='red'> 0.350100 </font>|
|lbm_trace2|2.035000|<font color='blue'>2.035816</font>|1.998883|   1.976280|<font color='red'>2.067567</font>|
|libquantum_trace2|   3.274124|  3.276598|3.254704|<font color='red'>3.281847</font>|<font color='blue'>3.280319</font>|
|milc_trace2|1.179511|<font color='red'>1.311450</font>|1.270079|   1.186075|<font color='blue'>1.302887</font>|
|GemsFDTD_trace2|  3.451324|  3.450598|<font color='blue'>3.452212</font>|3.440013|<font color='red'>3.452546</font>|
|leslie3d_trace2|1.623532|<font color='blue'>1.696512</font>|1.631569|   1.356836|<font color='red'>1.697695</font>|
|mcf_trace2.dpc|0.372714|<font color='red'>0.395652</font>|0.371862|   0.368020|<font color='blue'>0.380612</font>|
|omnetpp_trace2|<font color='red'>2.269448</font>|<font color='blue'>2.262831</font>|2.155531|   2.204294|2.210579|


