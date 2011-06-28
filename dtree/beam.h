#ifndef BEAM_H_
#define BEAM_H_

#include <vector>
using namespace std;

// an inefficient beam implementation
template <typename T>
class Beam {
 public:
 Beam(size_t sz)
   : sz_(sz),
     list_(sz+1) {
    list_.resize(0); // TODO: ???
    assert(list_.size() == 0);
  }

  bool WillAccept(double score) const {
    if(list_.size() >= sz_) {
      return score > list_.back().GetScore();
    } else {
      return true;
    }
  }

  void Add(const T& t) {
    if(WillAccept(t.GetScore())) {
      typename vector<T>::iterator it = list_.begin();
      while(it != list_.end() && it->GetScore() >= t.GetScore()) {
	++it;
      }
      list_.insert(it, t);
      if(list_.size() > sz_) {
	list_.erase(list_.end()-1);
      }
    }
  }

  // returns a pointer to the position in the beam
  T* Add(const double score) {
    if(WillAccept(score)) {
      typename vector<T>::iterator it = list_.begin();
      while(it != list_.end() && it->GetScore() >= score) {
	++it;
      }
      list_.insert(it, T());
      if(list_.size() > sz_) {
	list_.erase(list_.end()-1);
      }
      // be careful not to invalidate this iterator...
      return &*it;
    } else {
      return NULL;
    }
  }

  T& At(size_t i) {
    return list_.at(i);
  }

  T& Best() {
    return list_.front();
  }

  T& Worst() {
    return list_.back();
  }

  size_t Size() const {
    return list_.size();
  }

  size_t Capacity() const {
    return sz_;
  }
 private:
  const size_t sz_;
  vector<T> list_;
};

#endif
