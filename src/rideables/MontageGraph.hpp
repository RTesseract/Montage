/**
 * Author:      Benjamin Valpey & Louis Jenkins
 * Date:        29 July 2020
 * Filename:    MontageGraph.hpp
 * Description: Adaptation of fine-grained concurrent graph to Montage.
 */

#ifndef MONTAGEGRAPH_HPP
#define MONTAGEGRAPH_HPP

#include "TestConfig.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RGraph.hpp"
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include "Recoverable.hpp"
#include <omp.h>
#include <cassert>

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad=50>
class MontageGraph : public RGraph, public Recoverable{

    public:

        class tVertex;
        class BasePayload : public pds::PBlk {
        public:
            GENERATE_FIELD(int, tag, BasePayload);
            BasePayload(){}
            BasePayload(const BasePayload& oth): pds::PBlk(oth){}
            void persist();

        };
        class Vertex : public BasePayload {
        public:
            GENERATE_FIELD(int, id, Vertex);
            GENERATE_FIELD(int, lbl, Vertex);
            Vertex(){this->m_tag = 0;}
            Vertex(int id, int lbl): m_id(id), m_lbl(lbl){this->m_tag = 0;}
            Vertex(const Vertex& oth): BasePayload(oth), m_id(oth.m_id), m_lbl(oth.m_lbl) {this->m_tag = 0;}
            bool operator==(const Vertex& oth) const { return m_id==oth.m_id;}
        };

        class Relation : public BasePayload {
        public:
            GENERATE_FIELD(int, weight, Relation);
            GENERATE_FIELD(int, src, Relation);
            GENERATE_FIELD(int, dest, Relation);
            Relation(){this->m_tag = 1;}
            Relation(int src, int dest, int weight): m_weight(weight), m_src(src), m_dest(dest){this->m_tag = 1;}
            Relation(const Relation& oth): BasePayload(oth), m_weight(oth.m_weight), m_src(oth.m_src), m_dest(oth.m_dest){this->m_tag = 1;}
            int src() const {
                return m_src;
            }
            int dest() const {
                return m_dest;
            }
	};

        struct RelationHash {
            std::size_t operator()(const Relation *r) const {
                return std::hash<int>()(r->src()) ^ std::hash<int>()(r->dest());
            }
        };

        struct RelationEqual {
            bool operator()(const Relation *r1, const Relation *r2) const {
                return r1->src() == r2->src() && r1->dest() == r2->dest();
            }
        };

        using Set = std::unordered_set<Relation*,RelationHash,RelationEqual>;

        class tVertex {
            public:
                MontageGraph* ds;
                Vertex *payload = nullptr;
                int id; // cached id
                Set adjacency_list;//only relations in this list is reclaimed
                Set dest_list;// relations in this list is a duplication of those in some adjacency list

                tVertex(MontageGraph* ds_, int id, int lbl): ds(ds_) {
                    payload = ds->pnew<Vertex>(id, lbl);
                    this->id = id;
                }
                tVertex(MontageGraph* ds_, Vertex* p): ds(ds_) {
                    // Use this method for recovery to avoid having to call PNEW when the block already exists.
                    payload = p;
                    this->id = p->get_unsafe_id(ds);
                }

                ~tVertex() {
                    if (payload){
                        ds->pdelete(payload);
                    }
                }

                void set_lbl(int l) {
                    payload = payload->set_lbl(ds, l);
                }
                int get_lbl() {
                    return payload->get_lbl(ds);
                }

                // Immutable
                int get_id() {
                    return id;
                }

                Vertex *payload_ptr() {
                    return payload;
                }
        };

        struct alignas(64) VertexMeta {
            tVertex* idxToVertex;// Transient set of transient vertices to index map
            std::mutex vertexLocks;// Transient locks for transient vertices
            uint32_t vertexSeqs;// Transient sequence numbers for transactional operations on vertices
        };

        MontageGraph(GlobalTestConfig* gtc) : Recoverable(gtc) {
            size_t sz = numVertices;
            this->vMeta = new VertexMeta[numVertices];
            std::mt19937_64 gen(time(NULL));
            std::uniform_int_distribution<> verticesRNG(0, numVertices - 1);
            std::uniform_int_distribution<> coinflipRNG(0, 100);
            if(gtc->verbose) std::cout << "Allocated core..." << std::endl;
            // Fill to vertexLoad
            for (int i = 0; i < numVertices; i++) {
                if (coinflipRNG(gen) <= vertexLoad) {
                    vMeta[i].idxToVertex = new tVertex(this, i,i);
                } else {
                    vMeta[i].idxToVertex = nullptr;
                }
                vMeta[i].vertexSeqs = 0;
            }
            if(gtc->verbose) std::cout << "Filled vertexLoad" << std::endl;

            // Fill to mean edges per vertex
            for (int i = 0; i < numVertices; i++) {
                if (vMeta[i].idxToVertex == nullptr) continue;
                for (int j = 0; j < meanEdgesPerVertex * 100 / vertexLoad; j++) {
                    int k = verticesRNG(gen);
                    if (k == i) {
                        continue;
                    }
                    if (vMeta[k].idxToVertex != nullptr) {
                        Relation *r = pnew<Relation>(i, k, -1);
                        auto ret1 = source(i).insert(r);
                        auto ret2 = destination(k).insert(r);
                        assert(ret1.second==ret2.second);
                        if(ret1.second==false){
                            // relation exists, reclaiming
                            pdelete(r);
                        }
                    }
                }
            }
            if(gtc->verbose) std::cout << "Filled mean edges per vertex" << std::endl;
        }

        ~MontageGraph() {}
        
	// Obtain statistics of graph (|V|, |E|, average degree, vertex degrees)
        // Not concurrent safe...
        std::tuple<int, int, double, int *, int> grab_stats() {
            int numV = 0;
            int numE = 0;
            int *degrees = new int[numVertices];
            double averageEdgeDegree = 0;
            for (auto i = 0; i < numVertices; i++) {
                if (vMeta[i].idxToVertex != nullptr) {
                    numV++;
                    numE += source(i).size();
                    degrees[i] = source(i).size() + destination(i).size();
                } else {
                    degrees[i] = 0;
                }
            }
            averageEdgeDegree = numE / ((double) numV);
            return std::make_tuple(numV, numE, averageEdgeDegree, degrees, numVertices);
        }

        void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
            Recoverable::init_thread(gtc, ltc);
        }

        VertexMeta* vMeta;
        
        // Thread-safe and does not leak edges
        void clear() {
            assert(0&&"clear() not implemented!");
            // for (auto i = 0; i < numVertices; i++) {
            //     lock(i);
            // }
            // for (auto i = 0; i < numVertices; i++) {
            //     if (vertex(i) == nullptr) continue;
            //     std::vector<Relation*> toDelete(source(i).size() + destination(i).size());
            //     for (auto r : source(i)) toDelete.push_back(r);
            //     for (auto r : destination(i)) toDelete.push_back(r);
            //     source(i).clear();
            //     destination(i).clear();
            //     for (auto r : toDelete) delete r;
            // }
            // for (int i = numVertices - 1; i >= 0; i--) {
            //     destroy(i);
            //     inc_seq(i);
            //     unlock(i);
            // }
        }

        /**
         * Adds an edge to the graph, given two node IDs
         * @param src A pointer to the source node
         * @param dest A pointer to the destination node
         * @return Whether or not adding the edge was successful
         */
        bool add_edge(int src, int dest, int weight) {
            bool retval = false;
            Relation *r = pnew<Relation>(src,dest,weight);
            if (src == dest) return false; // Loops not allowed
            if (src > dest) {
                lock(dest);
                lock(src);
            } else {
                lock(src);
                lock(dest);
            }

            auto& srcSet = source(src);
            auto& destSet = destination(dest);
            if (vertex(src) == nullptr || vertex(dest) == nullptr) {
                goto exitEarly;
            }

            {
                MontageOpHolder _holder(this);
                auto ret1 = srcSet.insert(r);
                auto ret2 = destSet.insert(r);
                assert(ret1.second == ret2.second);
                if(ret1.second){
                    inc_seq(src);
                    inc_seq(dest);
                    retval = true;
                }else{
                    retval = false;
                }
            }
            
            exitEarly:
                if (!retval){
                    pdelete(r);
                }
                if (src > dest) {
                    unlock(src);
                    unlock(dest);
                } else {
                    unlock(dest);
                    unlock(src);
                }
                return retval;
        }

        bool has_edge(int src, int dest) {
            bool retval = false;            
            // We utilize `get_unsafe` API because the Relation destination and vertex id will not change at all.
            lock(src);
            if (vertex(src) == nullptr) {
                unlock(src);
                return false;
            }

            {
                MontageOpHolder _holder(this);
                Relation r(src, dest, -1);
                retval = has_relation(source(src), &r);
            }
            unlock(src);
            return retval;
        }

        /**
         * Removes an edge from the graph. Acquires the unique_lock.
         * @param src The integer id of the source node of the edge.
         * @param dest The integer id of the destination node of the edge
         * @return True if the edge exists
         */
        bool remove_edge(int src, int dest) {
            if (src == dest) return false;
            if (src > dest) {
                lock(dest);
                lock(src);
            } else {
                lock(src);
                lock(dest);
            }
            bool ret = false;
            if (vertex(src) != nullptr && vertex(dest) != nullptr) {
                MontageOpHolder _holder(this);
                Relation r(src, dest, -1);
                auto ret1 = remove_relation(source(src), &r);
                auto ret2 = remove_relation(destination(dest), &r);
                assert(ret1==ret2);
                ret = (ret1!=nullptr);
                if(ret){
                    pdelete(ret1);
                    inc_seq(src);
                    inc_seq(dest);
                }
            }
            
            if (src > dest) {
                unlock(src);
                unlock(dest);
            } else {
                unlock(dest);
                unlock(src);
            }
            return ret;
        }
        
        int recover(bool simulated) {
            assert(0&&"recover() not implemented!");
            // if (simulated) {
            //     recover_mode();
            //     delete idxToVertex;
            //     idxToVertex = new tVertex*[numVertices];
            //     #pragma omp parallel for
            //     for (size_t i = 0; i < numVertices; i++) {
            //         idxToVertex[i] = nullptr;
            //     }
            //     online_mode();
            // }

            // int block_cnt = 0;
            // auto begin = chrono::high_resolution_clock::now();
            // std::unordered_map<uint64_t, pds::PBlk*>* recovered = recover_pblks();
            // auto end = chrono::high_resolution_clock::now();
            // auto dur = end - begin;
            // auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            // std::cout << "Spent " << dur_ms << "ms getting PBlk(" << recovered->size() << ")" << std::endl;
            
            // begin = chrono::high_resolution_clock::now();
            // std::vector<Relation*> relationVector;
            // std::vector<Vertex*> vertexVector;
            // {
            //     MontageOpHolder _holder(this);
            //     for (auto itr = recovered->begin(); itr != recovered->end(); ++itr) {
            //         // iterate through all recovered blocks.  Sort the blocks into vectors containing the different
            //         // payloads to be iterated over later.
            //         block_cnt ++;


            //         // Should these be parallel?  I'm not sure..
            //         BasePayload* b = reinterpret_cast<BasePayload*>(itr->second);

            //         switch (b->get_unsafe_tag(this)) {
            //             case 0:
            //                 {
            //                     Vertex* v = reinterpret_cast<Vertex*>(itr->second);
            //                     vertexVector.push_back(v);
            //                     break;
            //                 }
            //             case 1:
            //                 {
            //                     Relation* r = reinterpret_cast<Relation*>(itr->second);
            //                     relationVector.push_back(r);
            //                     break;
            //                 }
            //             default:
            //                 {
            //                     std::cerr << "Found bad tag " << b->get_unsafe_tag(this) << std::endl;
            //                 }
            //         }
            //     }
            // }
            // end = chrono::high_resolution_clock::now();
            // dur = end - begin;
            // dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            // std::cout << "Spent " << dur_ms << "ms gathering vertices(" << vertexVector.size() << ") and edges(" << relationVector.size() << ")..." << std::endl;
            // begin = chrono::high_resolution_clock::now();
            // #pragma omp parallel
            // {
            //     Recoverable::init_thread(omp_get_thread_num());
            //     #pragma omp for
            //     for (size_t i = 0; i < vertexVector.size(); ++i) {
            //         int id = vertexVector[i]->get_unsafe_id(this);
            //         if (idxToVertex[id] != nullptr) {
            //             std::cerr << "Somehow recovered vertex " << id << " twice!" << std::endl;
            //             continue;
            //         }
            //         tVertex* new_node = new tVertex(this, vertexVector[i]);
            //         idxToVertex[id] = new_node;
            //     }
            // }
            // end = chrono::high_resolution_clock::now();
            // dur = end - begin;
            // dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            // std::cout << "Spent " << dur_ms << "ms creating vertices..." << std::endl;
            // begin = chrono::high_resolution_clock::now();
            
            // int num_threads;
            // #pragma omp parallel
            // num_threads = omp_get_num_threads(); 
            
            // // Vertices are cyclically distributed across all OpenMP threads, such that a thread t_i
            // // owns vertices i, i + T, i + 2T, etc., where T is the number of OpenMP threads.
            // // The vector of edges is iterated over in parallel and given an edge e=(v,v'), if
            // // v belongs to thread t_i, t_i will add it to the source set of v; if v' belongs to
            // // thread t_i, t_i will add v' to the destination set of v'; if t_i does not own it, nothing happens
            // std::vector<RelationWrapper> *buffers = new std::vector<RelationWrapper>[num_threads * num_threads];
            // #pragma omp parallel
            // {
            //     int tid = omp_get_thread_num();
            //     Recoverable::init_thread(tid);
            //     #pragma omp for
            //     for (size_t i = 0; i < relationVector.size(); ++i) {
            //         Relation *e = relationVector[i];
            //         int id1 = e->m_src(this);
            //         int id2 = e->m_dest(this);
            //         RelationWrapper item = { id1, id2, e };
            //         if (id1 < 0 || (size_t) id1 >= numVertices || id2 < 0 ||  (size_t) id2 >= numVertices) {
            //             std::cerr << "Found a relation with a bad edge: (" << id1 << "," << id2 << ")" << std::endl;
            //             continue; 
            //         }

            //         tVertex *v1 = idxToVertex[id1];
            //         tVertex *v2 = idxToVertex[id2];   
            //         if (v1 == nullptr || v2 == nullptr) {
            //             std::cerr << "Edge (" << id1 << ", " << id2 << ") has nullptr(v1=" << (v1 == nullptr) << ", v2=" << (v2 == nullptr) << ")" << std::endl;
            //             continue; 
            //         }

            //         buffers[tid * num_threads + (id1 % num_threads)].push_back(item);
            //         if ((id2 % num_threads) != (id1 % num_threads)) {
            //             buffers[tid * num_threads + (id2 % num_threads)].push_back(item);
            //         }
            //     }
                
            //     std::vector<RelationWrapper> tpls;
            //     size_t size = 0;
            //     for (int _tid = 0; _tid < num_threads; _tid++) {
            //         size += buffers[_tid * num_threads + tid].size();
            //     }
            //     tpls.resize(size);
                
            //     size_t offset = 0;
            //     for (int _tid = 0; _tid < num_threads; _tid++) {
            //         auto *buffer = &buffers[_tid * num_threads + tid];
            //         std::copy(buffer->begin(), buffer->end(), tpls.begin() + offset);
            //         offset += buffer->size();
            //     }
                
            //     #pragma omp barrier
            //     #pragma omp master
            //     delete[] buffers; 

            //     std::sort(tpls.begin(), tpls.end(), [](RelationWrapper r1, RelationWrapper r2) { return r1.v1 < r2.v1; });
            //     for (auto r : tpls) {
            //         int v1 = r.v1;
            //         Relation *e = r.e;
            //         if (v1 % num_threads == tid) {
            //             idxToVertex[v1]->adjacency_list.insert(e);
            //         }
            //     }
            //     std::sort(tpls.begin(), tpls.end(), [](RelationWrapper r1, RelationWrapper r2) { return r1.v2 < r2.v2; });
            //     for (auto r : tpls) {
            //         int v2 = r.v2;
            //         Relation *e = r.e;
            //         if (v2 % num_threads == tid) {
            //             idxToVertex[v2]->dest_list.insert(e);
            //         }
            //     }
            // }

            // end = chrono::high_resolution_clock::now();
            // dur = end - begin;
            // dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            // std::cout << "Spent " << dur_ms << "ms forming edges..." << std::endl;
            // begin = chrono::high_resolution_clock::now();

            // delete recovered;
            // return block_cnt;
            return false;
	}

        bool add_vertex(int vid) {
            std::mt19937_64 vertexGen(time(NULL));
            std::uniform_int_distribution<> uniformVertex(0,numVertices);
            bool retval = true;
            // Randomly sample vertices...
            std::vector<int> vec;
            for (size_t i = 0; i < meanEdgesPerVertex * 100 / vertexLoad; i++) {
                int u = uniformVertex(vertexGen);
                if (u == i) {
                    continue;
                }
                vec.push_back(u);
            }
            vec.push_back(vid);
            std::sort(vec.begin(), vec.end()); 
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());

            auto new_v = new tVertex(this, vid, vid);
            for (int u : vec) {
                lock(u);
            }

            if (vertex(vid) == nullptr) {
                MontageOpHolder _holder(this);
                vertex(vid) = new_v;
                for (int u : vec) {
                    if (vertex(u) == nullptr) continue;
                    if (u == vid) continue;
                    Relation *r = pnew<Relation>(vid, u, -1);
                    source(vid).insert(r);
                    destination(u).insert(r);
                }
            } else {
                retval = false;
            }

            for (auto u = vec.rbegin(); u != vec.rend(); u++) {
                if (vertex(vid) != nullptr && vertex(*u) != nullptr) inc_seq(*u);
                unlock(*u);
            }
            if(retval==false){
                delete(new_v);
            }
            return retval;
        }


        bool remove_vertex(int vid) {
startOver:
            {
                // Step 1: Acquire vertex and collect neighbors...
                std::vector<int> vertices;
                lock(vid);
                if (vertex(vid) == nullptr) {
                    unlock(vid);
                    return false;
                }
                uint32_t seq = get_seq(vid);
                for (auto r : source(vid)) {
                    vertices.push_back(r->dest());
                }
                for (auto r : destination(vid)) {
                    vertices.push_back(r->src());
                }
                
                unlock(vid);
                vertices.push_back(vid);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());


                // Step 2: Acquire lock-order...
                for (int _vid : vertices) {
                    lock(_vid);
                    if (vertex(_vid) == nullptr && get_seq(vid) == seq) {
                        for (auto r : source(vid)) {
                            if (r->dest() == _vid)
                            std::cout << "(" << r->src() << "," << r->dest() << ")" << std::endl;
                        }
                        for (auto r : destination(vid)) {
                            if (r->src() == _vid)
                            std::cout << "(" << r->src() << "," << r->dest() << ")" << std::endl;
                        }
                        std::abort();
                    }
                }

                // Has vertex been changed? Start over
                if (get_seq(vid) != seq) {
                    for (auto _vid = vertices.rbegin(); _vid != vertices.rend(); _vid++) {
                        unlock(*_vid);
                    }
                    goto startOver;
                }

                // Has not changed, continue...
                // Step 3: Remove edges from all other
                // vertices that relate to this vertex
                {
                std::vector<Relation*> toDelete;
                toDelete.reserve(source(vid).size()+destination(vid).size());
                MontageOpHolder _holder(this);
                for (int other : vertices) {
                    if (other == vid) continue;

                    Relation src(other, vid, -1);
                    Relation dest(vid, other, -1);
                    if (!has_relation(source(other), &src) && !has_relation(destination(other), &dest)) {
                        std::cout << "Observed pair (" << vid << "," << other << ") that was originally there but no longer is..." << std::endl;
                        for (auto r : source(vid)) {
                            if (r->dest() == other)
                            std::cout << "Us: (" << r->src() << "," << r->dest() << ")" << std::endl;
                        }
                        for (auto r : destination(other)) {
                            if (r->src() == vid) {
                                std::cout << "Them: (" << r->src() << "," << r->dest() << ")" << std::endl;
                            }
                        }
                        for (auto r : destination(vid)) {
                            if (r->src() == other) {
                                std::cout << "Us: (" << r->src() << "," << r->dest() << ")" << std::endl;
                            }
                        }
                        for (auto r : source(other)) {
                            if (r->dest() == vid) {
                                std::cout << "Them: (" << r->src() << "," << r->dest() << ")" << std::endl;
                            }
                        }
                        std::abort();
                    }
                    
                    auto ret1 = remove_relation(source(other), &src); // this may fail
                    auto ret2 = remove_relation(destination(other), &dest);// this may fail
                    if(ret1!=nullptr){
                        toDelete.push_back(ret1);// only deallocate relation removed from source
                    }
                    assert(!has_relation(source(other), &src) && !has_relation(destination(other), &dest));
                }
                
                for (auto r : source(vid)) toDelete.push_back(r);
                source(vid).clear();
                destination(vid).clear();
                destroy(vid);
                for (auto r : toDelete) {
                    assert (r != nullptr);
                    pdelete(r);
                }
                }
                
                // Step 4: Release in reverse order
                for (auto _vid = vertices.rbegin(); _vid != vertices.rend(); _vid++) {
                    inc_seq(*_vid);
                    unlock(*_vid);
                }
            }
            return true;
        }
        
        private:
            tVertex *& vertex(size_t idx) {
                return vMeta[idx].idxToVertex;
            }

            void lock(size_t idx) {
                vMeta[idx].vertexLocks.lock();
            }

            void unlock(size_t idx) {
                vMeta[idx].vertexLocks.unlock();
            }

            // Lock must be owned for next operations...
            void inc_seq(size_t idx) {
                vMeta[idx].vertexSeqs++;
            }
                
            uint64_t get_seq(size_t idx) {
                return vMeta[idx].vertexSeqs;
            }

            void destroy(size_t idx) {
                assert(vertex(idx)!=nullptr);
                delete vertex(idx);
                vertex(idx) = nullptr;
            }

            // Incoming edges
            Set& source(int idx) {
                return vertex(idx)->adjacency_list;

            }

            // Outgoing edges
            Set& destination(int idx) {
                return vertex(idx)->dest_list;
            }

            bool has_relation(Set& set, Relation *r) {
                auto search = set.find(r);
                return search != set.end();
            }

            Relation* remove_relation(Set& set, Relation *r) {
                // remove relation from set but NOT deallocate it
                // return Relation* in the set
                auto search = set.find(r);
                if (search != set.end()) {
                    Relation *tmp = *search;
                    set.erase(search);
                    return tmp;
                }
                return nullptr;
            }

};

template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad = 50>
class MontageGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new MontageGraph<numVertices, meanEdgesPerVertex, vertexLoad>(gtc);
    }
};


#endif
