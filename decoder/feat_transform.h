// == grammars ==
// 1) discretize all feature values from 0.25, etc. to 1,2,3... so that they there's no binary search in decoding

// == decoder ==
// 0) how do we configure the feature transformation when the decoder loads? statically?
// 1) discretize on-demand in decoder via transformation
// 2) conjoin on-demand in decoder via transformation
// 3) mark some features as "non-initial"
// 4) dump k-best lists with only initial features and with local feature boundaries

// == optimizer ==
// 1) read in k-best lists with grouped initial features and generate samples with grouped initial feature differences
// 2) read in samples with grouped initial feature differences and generate transformed features

#include <unordered_set>

// hash code for int pair
template <>
struct std::hash<std::pair<int, int> > {
 public:
  size_t operator()(std::pair<int, int> x) const throw() {
    size_t hash = x.first * 3931 + x.second;
    return hash;
  }
};

class FeatureTransformer {
 public:
  static void Configure(const std::string config) {
    if (conjoin_) {
      if (!discretize_) {
        std::cerr << "ERROR: Conjunctions require discretization in current implementation" << std::endl;
        abort();
      }
    }
  }

  // TODO: Also pass in sentence-level info?
  static void Transform(const SparseVector<double>& rule_scores, SparseVector<double>* transformed_scores) {
    assert(transformed_scores != nullptr);

    std::vector<int> discretized_feats;
    std::vector<int> conjoined_feats;

    // first, discretize
    if (discretize_) {
      discretized_feats.reserve(rule_scores.size());
      for (std::pair<int, double>& rule_score : rule_scores) {
        int orig_fid = rule_score.first;
        double feat_value = rule_score.second;
        
        // assume that when discretization is active, 
        int whole_feat_value = static_cast<int>(feat_value);
        assert(feat_value == whole_feat_value);
    
        auto found = discretized_map_.find(std::pair<int,int>(orig_fid, whole_feat_value));
        if (found == discretized_map_.end()) {
          std::cerr << "WARNING: Feature not found in discretization map: " << orig_fid << " = " << whole_feat_value << ". Ignoring." << std::endl;
        } else {
          int discretized_feat = *found;
          discretized_feats.push_back(discretized_feat);
        }
      }
     
      // then, layer local conjunctions on top of that
      conjoined_feats.reserve(disretized_feats.size() * discretized_feats.size());
      for (int fid1 : discretized_feats) {
        for (int fid2 : discretized_feats) {
          if (fid2 >= fid1)
            continue;

          auto found = conjoined_map_.find(std::pair<int,int>(fid1, fid2));
          int conjoined_fid;
          if (found == conjoined_map_.end()) {
            std::string conjoined_feat_name = boost::format("%s__%s") % FD::Convert(fid1) % FD::Convert(fid2);
            conjoined_fid = FD::Convert(conjoined_feat_name);
            conjoined_fids_.insert(std::pair(fid1, fid2), conjoined_fid);
          } else {
            conjoined_fid = *found;
          }
          conjoined_feats.push_back(conjoined_fid);
        }
      }
    }

    // Note: Initial features and conjoined features will both still exist in the rules
    // (so that we can pull tricks like dumping only initial features to k-best lists)
    // However, only features with non-zero weights will matter
    transformed_scores->reserve(rule_scores.size() + discretized_feats.size() + conjoined_feats_size());
    *transformed_scores = rule_scores;
    for (int fid : discretized_feats)
      transformed_scores->set_value(fid, 1.0);
    for (int fid : conjoined_feats)
      transformed_scores->set_value(fid, 1.0);
    
    for (std::pair<int, double>& pair : *transformed_scores) {
      transformed_feats_.insert(pair.first);
    }
  }

  static IsInitialFeature(int fid) {
    return transformed_feats_.find(fid) == transformed_feats_.end();
  }

 private:
  bool discretize_;
  bool conjoin_;
  
  // populated from a configuration file on load
  // key is pair of (orig_fid, quantized_value)
  std::unordered_map<std::pair<int,int>, int> discretized_map_;

  // lazily populated as we encounter new conjunctions
  // key is pair of (fid, fid) where fid's are sorted
  std::unordered_map<std::pair<int,int>, int> conjoined_map_;

  // populated lazily
  std::unordered_set<int> transformed_feats_;
};
