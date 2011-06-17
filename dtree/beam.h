#ifndef BEAM_H_
#define BEAM_H_

#include <vector>
using namespace std;

// an inefficient beam implementation
template <typename T>
class Beam {
 public:
 Beam(size_t sz) : sz_(sz) {}

  void Add(const T& t) {
    if(list_.size() == sz_ && t < list_.back()) {
      return;
    }

    // TODO: XXX: log(n) insertion
    // instead of this gross n*log(n) business
    list_.push_back(t);
    sort(list_.begin(), list_.end());
    if(list_.size() > sz_) {
      list_.erase(list_.end()-1);
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
