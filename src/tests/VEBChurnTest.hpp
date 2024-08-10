#ifndef VEBCHURNTEST_HPP
#define VEBCHURNTEST_HPP

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "MontageVEBTree.hpp"
#include <bitset>

#define VEB_DEBUG

#ifdef VEB_DEBUG
std::bitset<INT_MAX> occur;
#endif

template <class T>
class VEBChurnTest : public ChurnTest{
public:
	RSet<T>* s;

	VEBChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range) { veb_u = range; }

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
#ifdef VEB_DEBUG
			acquireLock(&globalLock);
			occur[key] = true;
			releaseLock(&globalLock);
#endif
			s->insert(k,tid);
		}
		else{ // op<=prop_removes
#ifdef VEB_DEBUG
			acquireLock(&globalLock);
			occur[key] = false;
			releaseLock(&globalLock);
#endif
			s->remove(k,tid);
		}
	}
	void cleanup(GlobalTestConfig* gtc){
#ifdef VEB_DEBUG
		bool in_arr, in_tree;
		for (int i = 0; i < veb_u; i++) {
			in_arr = occur[i];
			in_tree = s->get(i, 0);
			if (in_arr != in_tree)
				printf("%d: in_arr=%d, in_tree=%d\n", i, (int)in_arr, (int)in_tree);
		}
#endif
		ChurnTest::cleanup(gtc);
		delete s;
	}
	inline T fromInt(uint64_t v) { return (T)v; }

	void allocRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		s = dynamic_cast<RSet<T>*>(ptr);
		if (!s) {
			 errexit("VEBChurnTest must be run on RSet<T> type object.");
		}
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
		// 	}7
		// 	if(gtc->verbose){
		// 		printf("Prefilled %d\n",i);
		// 	}
		// }
	}
};

#endif // VEBCHURNTEST_HPP