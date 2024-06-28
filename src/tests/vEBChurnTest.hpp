// This test is obsolete
#if 1 

#ifndef SETCHURNTEST_HPP
#define SETCHURNTEST_HPP

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "HTMvEBTree.hpp"
// #include <cstdio>

template <class T>
class vEBChurnTest : public ChurnTest{
public:
	VebKNaive<T>* s;

	vEBChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range, prefill){}
	vEBChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range){}

	inline T fromInt(uint64_t v);

	void allocRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		s = dynamic_cast<VebKNaive<T>*>(ptr);
		if (!s) {
			 errexit("vEBChurnTest must be run on VebKNaive<T> type object.");
		}

		// printf("kMap:\n");
		// for (auto it = kMap.begin(); it != kMap.end(); it++){
		// 	printf("VebKNaive(u): u=%ld, ui=(clusterSize=%ld, nClusters=%ld, lowBits=%ld, highBits=%ld, lowMask=%ld)\n", it->first, it->second.clusterSize, it->second.nClusters, it->second.lowBits, it->second.highBits, it->second.lowMask);
		// }
	}

	void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc) override {
		s->initThread(ltc->tid);
	}

	Rideable* getRideable(){
		return s;
	}
	void doPrefill(GlobalTestConfig* gtc){
		// // prefill deterministically:
		// if (this->prefill > 0){
		// 	/* Wentao: 
		// 	 *	to avoid repeated k during prefilling, we 
		// 	 *	insert [0,min(prefill-1,range)] 
		// 	 */
		// 	// int stride = this->range/this->prefill;
		// 	int i = 0;
		// 	while(i<this->prefill){
		// 		K k = this->fromInt(i%range);
		// 		m->insert(k,0);
		// 		i++;
		// 	}
		// 	if(gtc->verbose){
		// 		printf("Prefilled %d\n",i);
		// 	}
		// }
	}
	void operation(uint64_t key, int op, int tid){
		T k = this->fromInt(key);
		// printf("%d.\n", r);
		
		if(op<this->prop_gets){
			s->get(k,tid);
		}
		else if(op<this->prop_puts){
			s->put(k,tid);
		}
		else if(op<this->prop_inserts){
			s->insert(k,tid);
		}
		else{ // op<=prop_removes
			s->remove(k,tid);
		}
	}
	void cleanup(GlobalTestConfig* gtc){
		ChurnTest::cleanup(gtc);
		delete s;
	}

};

template <class T>
inline T vEBChurnTest<T>::fromInt(uint64_t v){
	return (T)v;
}

template<>
inline std::string vEBChurnTest<std::string>::fromInt(uint64_t v){
	return std::to_string(v);
}

#endif // SETCHURNTEST_HPP
#endif // 0