// This test is obsolete
#if 1 

#ifndef SETCHURNTEST_HPP
#define SETCHURNTEST_HPP

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "HTMvEBTree.hpp"
#ifdef VEB_TEST
#include <cstdio>
#endif /* VEB_TEST */

template <class T>
class vEBChurnTest : public ChurnTest{
public:
#ifdef VEB_TEST
	unsigned char *bitmap;
	FILE *fp;

	inline void init() {
		bitmap = new unsigned char[HTMvEBTreeRange / 8 + 1]();
		fp = fopen("veb.log", "w");
		fclose(fp);
	}

	inline void deinit() {
		delete[] bitmap;
	}

	inline void set(i64 x) {
		acquireLock(&globalLock);
		bitmap[x / 8] |= 1 << (x % 8);
		releaseLock(&globalLock);
	}

	inline void unset(i64 x) {
		acquireLock(&globalLock);
		bitmap[x / 8] &= ~(1 << (x % 8));
		releaseLock(&globalLock);
	}

	inline bool get(i64 x) {
		return bitmap[x / 8] & (1 << (x % 8));
	}

	inline void print(const char *format, i64 x) {
		acquireLock(&globalLock);
		fp = fopen("veb.log", "a");
		fprintf(fp, format, x);
		for (int i = 0; i < HTMvEBTreeRange; ++i)
			fprintf(fp, !i + ",%c", get(i) ? 'T' : 'F');
		fputc('\n', fp);
		fclose(fp);
		releaseLock(&globalLock);
	}
#endif /* VEB_TEST */

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
		init();
		print("start: ", 0);
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
			set(key);
			print("insert(%ld): ", key);
#endif /* VEB_TEST */
			s->insert(k,tid);
		}
		else{ // op<=prop_removes
#ifdef VEB_TEST
			unset(key);
			print("remove(%ld): ", key);
#endif /* VEB_TEST */
			s->remove(k,tid);
		}
	}
#ifdef VEB_TEST
	void test() {
		bool in_set, in_tree;
		for (i64 k = 0; k < HTMvEBTreeRange; ++k) {
			in_set = get(k);
			in_tree = s->member(k, 0);
			if (in_set && !in_tree)
				printf("%7ld:TF", k);
			else if (!in_set && in_tree)
				printf("%7ld:FT", k);
		}
		putchar('\n');
	}
#endif /* VEB_TEST */
	void cleanup(GlobalTestConfig* gtc){
#ifdef VEB_TEST
		test();
		deinit();
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