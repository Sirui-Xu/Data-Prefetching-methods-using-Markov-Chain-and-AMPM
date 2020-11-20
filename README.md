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
|gcc_trace2|0.339157|$\color{blue}{0.345508}$|0.343890|   0.344170|$\color{red}{0.350100}$|
|lbm_trace2|2.035000|$\color{blue}{2.035816}$|1.998883|   1.976280|$\color{red}{2.067567}$|
|libquantum_trace2|   3.274124|  3.276598|3.254704|$\color{red}{3.281847}$|$\color{blue}{3.280319}$|
|milc_trace2|1.179511|$\color{red}{1.311450}$|1.270079|   1.186075|$\color{blue}{1.302887}$|
|GemsFDTD_trace2|  3.451324|  3.450598|$\color{blue}{3.452212}$|3.440013|$\color{red}{3.452546}$|
|leslie3d_trace2|1.623532|$\color{blue}{1.696512}$|1.631569|   1.356836|$\color{red}{1.697695}$|
|mcf_trace2.dpc|0.372714|$\color{red}{0.395652}$|0.371862|   0.368020|$\color{blue}{0.380612}$|
|omnetpp_trace2|$\color{red}{2.269448}$|$\color{blue}{2.262831}$|2.155531|   2.204294|2.210579|


