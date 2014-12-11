#include "ff_conjunctions.h"

#include "fdict.h"
#include <sstream>
#include <iostream>

using namespace std;

// TODO: Add some tests for simple examples:
// "then , he said , '" -> "he said -- '"
// "..." -> ". . ."
namespace {
  // stolen from ff_ngram.cc...

  inline std::string Escape(const std::string& x) {
    std::string y = x;
    for (int i = 0; i < y.size(); ++i) {
      if (y[i] == '=') y[i]='_';
      if (y[i] == ';') y[i]='_';
    }
    return y;
  }

  inline int GetFeatureID(const std::string& prefix, WordID a, WordID b) {
    std::cerr << a << " " << b << " " << TD::Convert(a) << " " << TD::Convert(b) << std::endl;
    std::ostringstream os;
    const std::string& tokA = TD::Convert(a);
    const std::string& tokB = TD::Convert(b);
    os << prefix << ":";
    os << Escape(tokA);
    os << "__";
    os << Escape(tokB);
    std::string feat_name = os.str();
    int fid = FD::Convert(feat_name);
    return fid;
  }
}

void ConjoinedWordSet::TraversalFeaturesImpl(const SentenceMetadata& /*smeta*/ ,
				    const Hypergraph::Edge& edge,
				    const vector<const void*>& /* ant_contexts */,
				    SparseVector<double>* features,
				    SparseVector<double>* /* estimated_features */,
				    void* /* context */) const {

  // AFTER debugging...
  if (alignedOnly_) {
    int alignedCount = 0;
    for(std::vector<AlignmentPoint>::const_iterator it = edge.rule_->a_.begin(); it != edge.rule_->a_.end(); ++it) {
      const AlignmentPoint& a = *it;
      if (a.s_ >= edge.rule_->f_.size() || a.t_ >= edge.rule_->e_.size()) {
        std::cerr << "Skipping borken rule " << edge.rule_ << " with borken alignment point " << a << std::endl;
        continue;
      }
      WordID f = edge.rule_->f_.at(a.s_);
      WordID e = edge.rule_->e_.at(a.t_);

      bool inSrcVocab = (srcVocab_.find(f) != srcVocab_.end());
      if (invertSrc_ && f > 0)
	inSrcVocab = !inSrcVocab;

      bool inTgtVocab = (tgtVocab_.find(e) != tgtVocab_.end());
      if (invertTgt_ && e > 0)
	inTgtVocab = !inTgtVocab;

      if (inSrcVocab && inTgtVocab) {
        ++alignedCount;
        if (lexicalized_) {
          // TODO: Cache these
          int lexical_fid = GetFeatureID(alignedName_, f, e);
          features->add_value(lexical_fid, 1);
        }
      }
    }

    features->set_value(fid_, alignedCount);
  } else {
    int srcCount = 0;
    for(std::vector<WordID>::const_iterator it = edge.rule_->f_.begin(); it != edge.rule_->f_.end(); ++it) {    
      bool inSrcVocab = (srcVocab_.find(*it) != srcVocab_.end());
      if (invertSrc_ && *it > 0)
	inSrcVocab = !inSrcVocab;

      if (inSrcVocab)
        ++srcCount;
    }

    int tgtCount = 0;
    for(std::vector<WordID>::const_iterator it = edge.rule_->e_.begin(); it != edge.rule_->e_.end(); ++it) {    
      bool inTgtVocab = (tgtVocab_.find(*it) != tgtVocab_.end());
      if (invertTgt_ && *it > 0)
	inTgtVocab = !inTgtVocab;
      if (inTgtVocab)
        ++tgtCount;
    }

    if (lexicalized_) {
      for(std::vector<WordID>::const_iterator it1 = edge.rule_->f_.begin(); it1 != edge.rule_->f_.end(); ++it1) {    
        int f = *it1;
        bool inSrcVocab = (srcVocab_.find(f) != srcVocab_.end());
        if (invertSrc_ && f > 0)
          inSrcVocab = !inSrcVocab;
        
        for(std::vector<WordID>::const_iterator it2 = edge.rule_->e_.begin(); it2 != edge.rule_->e_.end(); ++it2) {    
          int e = *it2;
          bool inTgtVocab = (tgtVocab_.find(e) != tgtVocab_.end());
          if (invertTgt_ && e > 0)
            inTgtVocab = !inTgtVocab;

          // TODO: Cache these
          if (inSrcVocab && inTgtVocab) {
            int lexical_fid = GetFeatureID(acrossName_, f, e);
            features->add_value(lexical_fid, 1);
          }
        }
      }
    }

    int diff = tgtCount - srcCount;
    if (absValue_)
      diff = abs(diff);
    else if (positive_ && diff < 0)
      diff = 0;
    else if (negative_ && diff > 0)
      diff = 0;
    
    features->set_value(fid_, diff);
  }
}

