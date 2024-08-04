// This test is obsolete
#if 1 

#ifndef SETCHURNTEST_HPP
#define SETCHURNTEST_HPP

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "HTMvEBTree.hpp"

template <class T>
class vEBChurnTest : public ChurnTest{
public:
	HTMvEBTree<T>* s;

	vEBChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range, prefill){ HTMvEBTreeRange = range; }
	vEBChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range){ HTMvEBTreeRange = range; }

	inline T fromInt(uint64_t v);

	void allocRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		s = dynamic_cast<HTMvEBTree<T>*>(ptr);
		if (!s) {
			 errexit("vEBChurnTest must be run on HTMvEBTree<T> type object.");
		}
#ifdef VEB_TEST
		init_elems();
#endif /* VEB_TEST */
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
#ifdef VEB_TEST
			acquireLock(&globalLock);
			set_elems(key, true);
			++total_insert;
			releaseLock(&globalLock);
#endif /* VEB_TEST */
			s->insert(k,tid);
		}
		else{ // op<=prop_removes
#ifdef VEB_TEST
			acquireLock(&globalLock);
			set_elems(key, false);
			++total_delete;
			releaseLock(&globalLock);
#endif /* VEB_TEST */
			s->remove(k,tid);
		}
	}
	void cleanup(GlobalTestConfig* gtc){
#ifdef VEB_TEST
		ofstream out("out.txt", ios::app);
		// out << "total_insert: " << total_insert << ", arr:";
		out << "total_insert: " << total_insert << "\n" << "total_delete: " << total_delete << "\n";
		// for (i64 i = 0; i < HTMvEBTreeRange; ++i)
		// 	out << ", " << elems[i];
		// out << "\nmember?: ";
		// for (i64 i = 0; i < HTMvEBTreeRange; ++i)
		// 	out << ", " << s->member(i, 0) ? "T" : "F";
		// out << "\n";
		bool in_set, in_tree;
		for (i64 key = 0; key < HTMvEBTreeRange; ++key) {
			in_set = get_elems(key);
			in_tree = s->member(key, 0);
			if (in_set)
				if (!in_tree)
					out << "TF" << key << "\n"; // less
			else
				if (in_tree)
					out << "FT" << key << "\n"; // more
		}
		delete_elems();
#endif /* VEB_TEST */
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