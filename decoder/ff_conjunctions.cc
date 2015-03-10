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
    //std::cerr << a << " " << b << " " << TD::Convert(a) << " " << TD::Convert(b) << std::endl;
    assert(a >= 0);
    assert(b >= 0);
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

  inline bool InVocab(const std::tr1::unordered_set<WordID>& vocab, WordID w) {
    if (vocab.empty())
      return w >= 0;
    else
      return vocab.find(w) != vocab.end();
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
    //std::cerr << "Aligned" << std::endl;

    int alignedCount = 0;
    for(std::vector<AlignmentPoint>::const_iterator it = edge.rule_->a_.begin(); it != edge.rule_->a_.end(); ++it) {
      const AlignmentPoint& a = *it;
      if (a.s_ >= edge.rule_->f_.size() || a.t_ >= edge.rule_->e_.size()) {
        std::cerr << "Skipping borken rule " << edge.rule_ << " with borken alignment point " << a << std::endl;
        continue;
      }
      WordID f = edge.rule_->f_.at(a.s_);
      WordID e = edge.rule_->e_.at(a.t_);
      
      bool inSrcVocab = InVocab(srcVocab_, f);
      if (invertSrc_ && f > 0)
	inSrcVocab = !inSrcVocab;

      bool inTgtVocab = InVocab(tgtVocab_, e);
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

      if (emit_classes_) {
        WordID f_class = class_map_f_.ClassifyWord(f);
	WordID e_class = class_map_e_.ClassifyWord(e);

	if (f_class >= 0 && e_class >= 0 && f >=0 && e >= 0) {
	  int class_fid = GetFeatureID(alignedClassName_, f_class, e_class);
	  features->add_value(class_fid, 1);

	  if (emit_mixed_) {
	    int mixed_fid1 = GetFeatureID(alignedMixedName_, f, e_class);
	    features->add_value(mixed_fid1, 1);

	    int mixed_fid2 = GetFeatureID(alignedMixedName_, f_class, e);
	    features->add_value(mixed_fid2, 1);
	  }
	}
      }
    }

    features->set_value(fid_, alignedCount);
  } else {
    //std::cerr << "Pairs" << std::endl;
    int srcCount = 0;
    for(std::vector<WordID>::const_iterator it = edge.rule_->f_.begin(); it != edge.rule_->f_.end(); ++it) {    
      bool inSrcVocab = InVocab(srcVocab_, *it);
      if (invertSrc_ && *it > 0)
	inSrcVocab = !inSrcVocab;

      if (inSrcVocab)
        ++srcCount;
    }

    int tgtCount = 0;
    for(std::vector<WordID>::const_iterator it = edge.rule_->e_.begin(); it != edge.rule_->e_.end(); ++it) {    
      bool inTgtVocab = InVocab(tgtVocab_, *it);
      if (invertTgt_ && *it > 0)
	inTgtVocab = !inTgtVocab;
      if (inTgtVocab)
        ++tgtCount;
    }

    if (lexicalized_) {


      for(std::vector<WordID>::const_iterator it1 = edge.rule_->f_.begin(); it1 != edge.rule_->f_.end(); ++it1) {    
        int f = *it1;
        bool inSrcVocab = InVocab(srcVocab_, f);
        if (invertSrc_ && f > 0)
          inSrcVocab = !inSrcVocab;
        
        for(std::vector<WordID>::const_iterator it2 = edge.rule_->e_.begin(); it2 != edge.rule_->e_.end(); ++it2) {    
          int e = *it2;
          bool inTgtVocab = InVocab(tgtVocab_, e);
          if (invertTgt_ && e > 0)
            inTgtVocab = !inTgtVocab;

	  //std::cerr << "PairsLexicalized " << f << " " << e << std::endl;

          // TODO: Cache these
          if (inSrcVocab && inTgtVocab) {
            int lexical_fid = GetFeatureID(acrossName_, f, e);
            features->add_value(lexical_fid, 1);
          }
	  if (emit_classes_) {
	    WordID f_class = class_map_f_.ClassifyWord(f);
	    WordID e_class = class_map_e_.ClassifyWord(e);

	    //std::cerr << "PairsLexicalizedClasses " << f_class << " " << e_class << std::endl;

	    if (f_class >= 0 && e_class >= 0 && f >= 0 && e >= 0) {
	      int class_fid = GetFeatureID(acrossClassName_, f_class, e_class);
	      features->add_value(class_fid, 1);

	      if (emit_mixed_) {
		int mixed_fid1 = GetFeatureID(acrossMixedName_, f, e_class);
		features->add_value(mixed_fid1, 1);

		int mixed_fid2 = GetFeatureID(acrossMixedName_, f_class, e);
		features->add_value(mixed_fid2, 1);
	      }
	    }
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

