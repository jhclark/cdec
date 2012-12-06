#ifndef _LENRATIO_FF_H_
#define _LENRATIO_FF_H_

#include <vector>
#include <string>

using namespace std;

#include "ff_factory.h"
#include "ff.h"

// if the hypothesis isn't the desired length, hammer it
// (requires each input sentence's SGML to have desired_len="N"
class LengthHammer : public FeatureFunction {
 public:
  LengthHammer(const std::string& param);
  ~LengthHammer();
  virtual void FinalTraversalFeatures(const void* context,
                                      SparseVector<double>* features) const;
  virtual void PrepareForInput(const SentenceMetadata& smeta);
  static std::string usage(bool param,bool verbose);
  Features features() const;

 protected:
  virtual void TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                     const Hypergraph::Edge& edge,
                                     const std::vector<const void*>& ant_contexts,
                                     SparseVector<double>* features,
                                     SparseVector<double>* estimated_features,
                                     void* out_context) const;
 private:
  float CalcLengthHammerPenalty(short int start, short int end, short int tgt_words) const;

  int fid_; // conceptually const; mutable only to simplify constructor
  uint desired_len_;
  uint src_len_;
};

#endif
