#ifndef _JLM_FF_H_
#define _JLM_FF_H_

#include <vector>
#include <string>

using namespace std;

#include "ff_factory.h"
#include "ff.h"

struct JLanguageModelImpl;

class JLanguageModel : public FeatureFunction {
 public:
  // param = "filename.lm [-o n]"
  JLanguageModel(const std::string& param);
  ~JLanguageModel();
  virtual void FinalTraversalFeatures(const void* context,
                                      SparseVector<double>* features) const;
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
  int fid_; // conceptually const; mutable only to simplify constructor
  JLanguageModelImpl* pimpl_;
};

struct JLanguageModelFactory : public FactoryBase<FeatureFunction> {
  FP Create(std::string param) const;
  std::string usage(bool params,bool verbose) const;
};

#endif
