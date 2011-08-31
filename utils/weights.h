#ifndef _WEIGHTS_H_
#define _WEIGHTS_H_

#include <string>
#include <map>
#include <vector>
#include "sparse_vector.h"

class Weights {
 public:
  Weights() {}
  void InitFromFile(const std::string& fname, std::vector<std::string>* feature_list = NULL);
  void WriteToFile(const std::string& fname, bool hide_zero_value_features = true, const std::string* extra = NULL) const;
  void InitVector(std::vector<double>* w) const;
  void InitSparseVector(SparseVector<double>* w) const;
  void InitFromVector(const std::vector<double>& w);
  void InitFromVector(const SparseVector<double>& w);
  static bool ReadSparseVectorString(const std::string& s, SparseVector<double>* v);

 private:
  std::vector<double> wv_;
};

#endif
