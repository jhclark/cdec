#include "ff_jlm.h"
#include "ff_jlm_hash.h"

#include <cstring>
#include <iostream>
#include <vector>
#include <set>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "lattice.h"
#include "sentence_metadata.h"
#include "filelib.h"
#include "stringlib.h"
#include "hg.h"
#include "tdict.h"
#include "lm/model.hh"
#include "lm/enumerate_vocab.hh"
#include "vector_util.h"

// we no longer support multiple features per n-gram
// we'll have to modify the mmapped format to have a
// max vector size if we want to go back to this
//#define JLM_ONE_FEAT 1

using namespace std;

static const unsigned char HAS_FULL_CONTEXT = 1;
static const unsigned char HAS_EOS_ON_RIGHT = 2;
static const unsigned char MASK = 7;

static const WordID LEFT_MARKER = 0x80000000;
static const WordID REMOVE_LEFT_MARKER = 0x7FFFFFFF;

// see definitions in ff_jlm_hash.h
typedef unsigned short conj_fid;
typedef jlm::Table FeatMap;

typedef boost::unordered_map<pair<WordID,WordID>, pair<float,int> > LexProbMap;

void load_file_into_vec(const string& filename,
		vector<string>* conjoin_with_feats) {
	ReadFile in_read(filename);
	istream & in = *in_read.stream();
	string line;
	while (getline(in, line)) {
		if (!line.empty())
			conjoin_with_feats->push_back(line);
	}
}
// takes some set of features and maps it onto
// a finer/more complex/more conjoined set of features
// for now, we assume that we will do this individually (rather than in series)
// so that we end up with less model sparsity
class FeatureMapper {
public:
  virtual ~FeatureMapper() {}
  virtual void SetSrc(const vector<WordID>& src) = 0;
  virtual void InitFeats(const set<int>& all_feats, int order) = 0;
  virtual int MapFeat(const int coarse_feat,
		      const vector<WordID>& ngram,
		      const int words_since_phrase_boundary) = 0;
};

// maps coarser features to finer (conjoined) feature set
// that includes phrase boundary information
class PhraseBoundaryFeatureMapper : public FeatureMapper {

  virtual void SetSrc(const vector<WordID>& src) {}

  virtual void InitFeats(const set<int>& all_feats, int order) {
    int prev_feat_count = FD::NumFeats();
    feats_by_boundary_.resize(order+1);
    for(set<int>::const_iterator it = all_feats.begin(); it != all_feats.end(); ++it) {
      int fid = *it;
      const string& feat_name = FD::Convert(fid);
      for(int i=0; i<=order; ++i) {
	const string& with_bound = feat_name + "_Boundary" + boost::lexical_cast<std::string>(i);
	int with_bound_fid = FD::Convert(with_bound);
	feats_by_boundary_.at(i)[fid] = with_bound_fid;
      }
    }
    int boundary_feat_count = FD::NumFeats() - prev_feat_count;
    cerr << "JLM: Added " << boundary_feat_count << " boundary features" << endl;
  }

  virtual int MapFeat(const int coarse_feat,
		      const vector<WordID>& ngram /*unused*/,
		      const int words_since_phrase_boundary) {
    // NOTE: Phrase cound be longer than order of LM
    // but this might provide some interesting information
    return feats_by_boundary_.at(words_since_phrase_boundary)[coarse_feat];
  }

private:
  // feats augmented by distance since phrase boundary markers
  vector<map<int, int> > feats_by_boundary_;
};

// maps coarser features to finer (conjoined) feature set
// that includes anchor information (binary: is "anchor" or model is "guessing")
// **for every word in the n-gram (including context words)**
//
// anchor-or-not is determined by a threshold based on lexical probability and count,
// which is calculated over the entire source sentence globally
class AnchorFeatureMapper : public FeatureMapper {
public:
	AnchorFeatureMapper(const string& f2eLexFile, const string& stopwordsFile,
			    float min_prob, int min_link_count)
	  : min_prob_(min_prob),
	    min_link_count_(min_link_count) {
		load_lex_probs(f2eLexFile);
		load_stopwords(stopwordsFile);
	}

private:
	void load_lex_probs(const string& f2eLexFile) {
		ReadFile in_read(f2eLexFile);
		istream & in = *in_read.stream();
		boost::char_separator<char> sep(" ");
		string line;
		while (getline(in, line)) {
			if (!line.empty()) {
				// note to eclipse CDT users: there is a direct correlation between
				// using boost and the number of false positives on errors in CDT
				boost::tokenizer<boost::char_separator<char> > tok(line, sep);
				boost::tokenizer<boost::char_separator<char> >::iterator it = tok.begin();
				WordID e = TD::Convert(*it);
				++it;
				WordID f = TD::Convert(*it);
				++it;
				// P(e|f)
				float pegf = boost::lexical_cast<float>(*it);
				++it;
				// C(f,e)
				int countFE = boost::lexical_cast<int>(*it);

//				cerr << "Got lex line: " << line << " :: " << e << " " << f << " " << pegf << " " << countFE << endl;

				lex_probs_[pair<WordID,WordID>(e,f)] = pair<float,int>(pegf, countFE);
			}
		}
	}

	void load_stopwords(const string& stopwordsFile) {
		ReadFile in_read(stopwordsFile);
		istream & in = *in_read.stream();
		string line;
		while (getline(in, line)) {
			if (!line.empty()) {
				stopwords_.insert(TD::Convert(line));
			}
		}
	}

	void LexProb(const WordID tgt_word, const vector<WordID>& src, float* prob, int* link_count) const {
		*prob = 0.0;
		*link_count = 0;
		for(int i=0; i<src.size(); ++i) {
			LexProbMap::const_iterator match = lex_probs_.find(pair<WordID,WordID>(tgt_word, src.at(i)));
			if(match != lex_probs_.end()) {
				const pair<float, int>& value = match->second;
				*prob += value.first;
				*link_count += value.second;
//				cerr << "LexProb: Match" << endl;
			} else {
//				cerr << "LexProb: No match" << endl;
			}
		}
//		cerr << "LexProb: " << TD::Convert(tgt_word) << " " << *prob << " links=" << *link_count << endl;
	}

	bool IsAnchor(WordID tgt_word, const vector<WordID>& src) const {
		bool result = false;
		// 1) Exclude stopwords
		if(stopwords_.find(tgt_word) == stopwords_.end()) {
			// 2) Calculate lexical probability for this sentence
			float prob;
			int link_count;
			LexProb(tgt_word, src, &prob, &link_count);

			// 3) Threshold based on prob and count
			if(prob >= min_prob_ && link_count >= min_link_count_) {
				result = true;
			}
		}

		return result;
	}

	// must call SetSrc first!
	bool IsAnchor(WordID tgt) {
		if(tgt > TD::NumWords()) {
			cerr << "ERROR: tgt > TD::NumWords() :: " << tgt << " > " << TD::NumWords() << endl;
			abort();
		}
		if(cached_.size() <= TD::NumWords()) {
//			cerr << "Increased cache size to " << TD::NumWords() << endl;
			cached_.resize(TD::NumWords()+1, false);
			cached_anchors_.resize(TD::NumWords()+1, false);
		}
		if(!cached_.at(tgt)) {
		  cached_anchors_[tgt] = IsAnchor(tgt, src_);
//		  cerr << "Caching new word " << TD::Convert(tgt) << " wid=" << tgt << " confident? " << cached_anchors_[tgt] << endl;
		  cached_[tgt] = true;
		}
		return cached_anchors_.at(tgt);
	}

  // pre-compute the feature names so that we aren't doing string mangling during LM lookups
  void BuildFeats(const string& prev_feat_name, const int prev_fid, const int iOrder) {
	  // TODO: Recurse less times for features of lower orders...
	  // Right now, we cache too many features, even though they'll never get used at runtime
    if(iOrder >= 0) {
	const string& with_anchor = prev_feat_name + "_Anchor" + boost::lexical_cast<std::string>(iOrder);
	const string& with_confusion = prev_feat_name + "_Guess" + boost::lexical_cast<std::string>(iOrder);
	int with_anchor_fid = FD::Convert(with_anchor);
	int with_confusion_fid = FD::Convert(with_confusion);
	cerr << "Add feature " << with_anchor << " " << with_anchor_fid << " from " << prev_fid << endl;
	cerr << "Add feature " << with_confusion << " " << with_confusion_fid << " from " << prev_fid << endl;
	feat_map_[pair<int,bool>(prev_fid, true)] = with_anchor_fid;
	feat_map_[pair<int,bool>(prev_fid, false)] = with_confusion_fid;
	// recursively add...
	BuildFeats(with_anchor, with_anchor_fid, iOrder-1);
	BuildFeats(with_confusion, with_confusion_fid, iOrder-1);
    }
  }

  // very inefficiently gets the length of an ngram feature
  // to make this more efficient, we would need to use more memory
  // in the form of separate hash maps or encoding order info into each feat
  int GetLength(const string& feat_name) {

	  if(feat_name == "LM_UNK") {
		  return 0;
	  }

	  size_t where = feat_name.find("_Len");
	  if(where == string::npos) {
		  cerr << "ERROR: Expected to find Len in LM feature name: " << feat_name << endl;
		  abort();
	  }
	  // Assume order is <= 0 (i.e. is one digit)
	  const string& str_len = feat_name.substr(where+4, 1);
	  const int len = boost::lexical_cast<int>(str_len);
	  return len;
  }

public:
  virtual void SetSrc(const vector<WordID>& src) {
    src_ = src;
    cached_.clear();
    cached_.assign(TD::NumWords(), false);
    cached_anchors_.clear();
    cached_anchors_.assign(TD::NumWords(), false);
  }

  virtual void InitFeats(const set<int>& all_feats, int order) {
    int prev_feat_count = FD::NumFeats();
    for(set<int>::const_iterator it = all_feats.begin(); it != all_feats.end(); ++it) {
      int fid = *it;
      const string& feat_name = FD::Convert(fid);
      // recursively build features for all orders
      const int ngram_len = GetLength(feat_name);
      BuildFeats(feat_name, fid, ngram_len-1);
    }
    int new_feat_count = FD::NumFeats() - prev_feat_count;
    cerr << "JLM: Added " << new_feat_count << " anchor features" << endl;
  }

  virtual int MapFeat(const int coarse_feat,
		      const vector<WordID>& ngram,
		      const int words_since_phrase_boundary /*unused*/) {

    // Score each word in n-gram with confidence metric
    // (could memoize this, but oh well)
    // start with right-most word so that _Anchor0 indicates
    // that we are confident of the word being predicted
    // and _Anchor4 indicates that we are not confident
    // of the oldest word in the history
    int fid = coarse_feat;
    for(int i=0; i<ngram.size(); ++i) {
      bool is_confident = IsAnchor(ngram.at(i));
      map<pair<int, bool>, int>::const_iterator it = feat_map_.find(pair<int, bool>(fid, is_confident));
//      cerr << "Query feature anchor=" << is_confident << " from " << fid;
      if(it == feat_map_.end()) {
		cerr << "ERROR: For ngram "<<ngram<<" -- Could not map anchor feature " << FD::Convert(fid) << "; (fid=" << fid << ") for confidence=" << is_confident << "(wid=" << ngram.at(i) << "@"<<i<<") (pre-cached features are invalid)" << endl;
		cerr << "WARNING: Assuming this is an (INFREQUENT!) hash collision and moving on";
		break;
//		abort();
      }
      fid = it->second;
    }
    return fid;
  }

private:
	boost::unordered_set<int> stopwords_; // wids

	// (e_wid, f_wid) => (lexProb, linkCount)
	LexProbMap lex_probs_;

	const float min_prob_;
	const int min_link_count_;

	vector<WordID> src_;
	vector<bool> cached_;
	vector<bool> cached_anchors_;
  
  // conjoined feature IDs
  // to get the feature of a trigram, use (roughly):
  //   int f1 = feat_map_.get( (lm_fid, right_is_anchor) )
  //   int f2 = feat_map_.get( (f1, middle_is_anchor) )
  //   int mapped_fid = feat_map_.get( (f2, left_is_anchor) )
  map<pair<int, bool>, int> feat_map_;
};

static const conj_fid END_OF_CONJ_VEC = 0;

// -x : rules include <s> and </s>
// -n NAME : feature id is NAME
// -C conjoin_with_name : conjoined features must be *rule* features
// -C@file : specify file with list of names to conjoin with
// -P : Use phrase boundary features
// -c stopwords_file lex_f2e_file min_prob min_link_count: Use confidence features
// NOTE: stopwords_file: stopword file for lexical confidence features
// NOTE: lexicon_f2e_file: Custom formatted lexicon file with space-separated lines like:
//         e f P(e|f) countFE countF
bool ParseLMArgs(string const& in, string* filename, string* mapfile,
		bool* explicit_markers, string* featname,
                 vector<int>* conjoin_with_fids, vector<int>* conjoined_fids,
		 bool* use_phrase_bounds, bool* use_confidence,
		 string* lex_f2e_file, string* stopwords_file,
		 float* min_prob, int* min_link_count) {

	vector<string> const& argv = SplitOnWhitespace(in);
	*explicit_markers = false;
	*featname = "JLanguageModel";
	*mapfile = "";
	*use_phrase_bounds = false;
	*use_confidence = false;
	*lex_f2e_file = "";
	*stopwords_file = "";
	*min_prob = 0.0;
	*min_link_count = 0;
#define LMSPEC_NEXTARG if (i==argv.end()) {            \
    cerr << "Missing argument for "<<*last<<". "; goto usage; \
    } else { ++i; }

	vector<string> conjoin_with_feats;
	for (vector<string>::const_iterator last, i = argv.begin(), e = argv.end();
			i != e; ++i) {
		string const& s = *i;
		if (s[0] == '-') {
			if (s.size() > 2)
				goto fail;
			switch (s[1]) {
			case 'x':
				*explicit_markers = true;
				break;
			case 'm':
				LMSPEC_NEXTARG
				;
				*mapfile = *i;
				break;
			case 'n':
				LMSPEC_NEXTARG
				;
				*featname = *i;
				break;
			case 'P':
				*use_phrase_bounds = true;
				break;
			case 'C':
				LMSPEC_NEXTARG
				;
				if ((*i)[0] == '@') { // read feature names from file?
					const string filename = i->substr(1);
					load_file_into_vec(filename, &conjoin_with_feats);
				} else {
					conjoin_with_feats.push_back(*i);
				}
				break;
			case 'c':
			  *use_confidence = true;
				LMSPEC_NEXTARG
				;
				*stopwords_file = *i;
				LMSPEC_NEXTARG
				;
				*lex_f2e_file = *i;
				LMSPEC_NEXTARG
				;
				*min_prob = boost::lexical_cast<float>(*i);
				LMSPEC_NEXTARG
				;
				*min_link_count = boost::lexical_cast<int>(*i);
				break;
			case 'F':
			  cerr << "Target frequency file not yet implemented" << endl;
			  abort();
			  break;
#undef LMSPEC_NEXTARG
			default:
				fail: cerr << "Unknown JLanguageModel option " << s << " ; ";
				goto usage;
			}
		} else {
			if (filename->empty())
				*filename = s;
			else {
				cerr << "More than one filename provided. ";
				goto usage;
			}
		}
	}

	if (conjoin_with_feats.size() > 0) {
		cerr << "Using base feature in addition to conjoined" << endl;

		for (unsigned i = 0; i < conjoin_with_feats.size(); ++i) {
			const string& conjoin_with_name = conjoin_with_feats.at(i);
			int conjoin_with_fid = FD::Convert(conjoin_with_name);
			conjoin_with_fids->push_back(conjoin_with_fid);

			const string conjoined_name = conjoin_with_name + "__X__"
					+ *featname;
			int conjoined_fid = FD::Convert(conjoined_name);
			cerr << "Using Conjoined Feature Pattern: " << conjoined_name
					<< " (conjoining with fid " << conjoin_with_fid
					<< " to make fid " << conjoined_fid << ")" << endl;
			conjoined_fids->push_back(conjoined_fid);
		}
	} else {
		cerr << "Not using conjoined LM for any local features" << endl;
	}

	if (!filename->empty())
		return true;
	usage: cerr << "JLanguageModel is incorrect!\n";
	return false;
}

string JLanguageModel::usage(bool /*param*/, bool /*verbose*/) {
	return "JLanguageModel";
}

//struct VMapper: public lm::EnumerateVocab {
//	VMapper(vector<lm::WordIndex>* out) :
//			out_(out), kLM_UNKNOWN_TOKEN(0) {
//		out_->clear();
//	}
//	void Add(lm::WordIndex index, const StringPiece &str) {
//		const WordID cdec_id = TD::Convert(str.as_string());
//		if (cdec_id >= out_->size())
//			out_->resize(cdec_id + 1, kLM_UNKNOWN_TOKEN);
//		(*out_)[cdec_id] = index;
//	}
//	vector<lm::WordIndex>* out_;
//	const lm::WordIndex kLM_UNKNOWN_TOKEN;
//};

class JLanguageModelImpl {
public:
	void PrepareForInput(const SentenceMetadata& smeta) {
		vector<WordID> src;
		for(std::vector<std::vector<LatticeArc> >::const_iterator it1 = smeta.src_lattice_.begin();
		  it1 != smeta.src_lattice_.end();
		  ++it1)
		{
		  for(std::vector<LatticeArc>::const_iterator it2 = it1->begin(); it2 != it1->end(); ++it2) {
			src.push_back(it2->label);
		  }
		}
		for(int j=0; j<feat_mappers_.size(); ++j) {
		  feat_mappers_.at(j)->SetSrc(src);
		}
	}
private:

	// returns the number of unscored words at the left edge of a span
	inline int UnscoredSize(const void* state) const {
		return *(static_cast<const char*>(state) + unscored_size_offset_);
	}

	inline void SetUnscoredSize(int size, void* state) const {
		*(static_cast<char*>(state) + unscored_size_offset_) = size;
	}

	// the right-most terminals in a constituent, which are still usable (within the markov window)
	// as context for scoring future terminals
	inline void RemnantLMState(const void* state, vector<WordID>* toks) {
		toks->clear();
		// TODO: Clever Memcpy?
		const WordID* arr = reinterpret_cast<const WordID*>(state);
		for(int i=0; i<order_-1; ++i) {
			if(arr[i]) {
				toks->push_back(arr[i]);
			}
		}
	}

	inline void SetRemnantLMState(const vector<WordID>& toks, void* state) const {
		if(toks.size() >= order_) {
			cerr << "ERROR: JLM: Remnant state too big" << endl;
			abort();
		}
		const char* begin = reinterpret_cast<const char*>(&*toks.begin());
		int bytes_to_copy = static_cast<int>(reinterpret_cast<const char*>(&*toks.end()) - begin);
		memcpy(state, begin, bytes_to_copy);
		std::fill(static_cast<WordID*>(state) +  toks.size(),
				  static_cast<WordID*>(state) + (order_ - 1), 0);
	}

	// get one of the left-most terminals of a constituent, which has not yet
	// seen its full context
	inline WordID IthUnscoredWord(int i, const void* state) const {
		const WordID* const mem =
				reinterpret_cast<const WordID*>(static_cast<const char*>(state)
						+ unscored_words_offset_);
		return mem[i];
	}

	inline void SetIthUnscoredWord(int i, WordID index, void *state) const {
		WordID* mem =
				reinterpret_cast<WordID*>(reinterpret_cast<char*>(state)
						+ unscored_words_offset_);
		mem[i] = index;
	}

	// returns a pointer to the beginning of the vector
	// the last element in the vector is followed by 0 (END_OF_CONJ_VEC) unless it is of size conjoined_fids_.size()
	// this also means it's quite important that no actual fid stored in it be zero
	// (this should be guaranteed since we must query a base feature before we can conjoin with it)
	const conj_fid* IthConjVec(int i, const void* state) const {
		const conj_fid* begin =
				reinterpret_cast<const conj_fid*>(static_cast<const char*>(state)
						+ conjoined_feats_offset_ + i * conj_vec_size_);
		return begin;
	}

	inline void SetIthConjVec(int i, const conj_fid* vec, void* state) const {
		void* dest = reinterpret_cast<void*>(static_cast<char*>(state)
				+ conjoined_feats_offset_ + i * conj_vec_size_);
		memcpy(dest, vec, conj_vec_size_);
	}

	inline void FireConjFeats(SparseVector<double>* features, const conj_fid* feats_to_fire, bool est) const {
		int i = 0;
		if (features != NULL) {
			for (const conj_fid* it = feats_to_fire;
					i != conjoined_fids_.size() && *it != END_OF_CONJ_VEC;
					++it, ++i) {
				int fid = *it;
				///cerr << "Firing " << (est?"est":"feature") << " fid " << fid << ": " << value << endl;
				features->set_value(fid, 1);
			}
		} else {
			//cerr << "NOTE: NULL features" << endl;
		}
		//cerr << "Fired " << i << " conjoined feats" << endl;
	}

	inline bool GetFlag(const void *state, unsigned char flag) const {
		return (*(static_cast<const char*>(state) + is_complete_offset_) & flag);
	}

	inline void SetFlag(bool on, unsigned char flag, void *state) const {
		if (on) {
			*(static_cast<char*>(state) + is_complete_offset_) |= flag;
		} else {
			*(static_cast<char*>(state) + is_complete_offset_) &= (MASK ^ flag);
		}
	}

	inline bool HasFullContext(const void *state) const {
		return GetFlag(state, HAS_FULL_CONTEXT);
	}

	inline void SetHasFullContext(bool flag, void *state) const {
		SetFlag(flag, HAS_FULL_CONTEXT, state);
	}

public:
	// we now directly modify features and est_features here
	// so that we can modify multiple features (e.g. conjoined features)
	inline void LookupWords(const TRule& rule, const vector<const void*>& ant_states,
			SparseVector<double>* features, SparseVector<double>* est_features, void* remnant) {

		//    cerr << "Lookup words for rule: " << TD::Convert(rule.lhs_) << " ||| " << TD::GetString(rule.f_) << " ||| " << TD::GetString(rule.e_) << " ||| " << rule.scores_ << endl;
		//cerr << "Lookup words for rule: " << TD::Convert(rule.lhs_) << " ||| " << rule.f_ << " ||| " << rule.e_ << " ||| " << rule.scores_ << endl;

		int num_scored = 0;
		int num_estimated = 0;
		bool saw_eos = false;
		bool has_some_history = false;
		vector<WordID> ngram; // functions as state
		ngram.reserve(order_);
		//cerr << "Ngram (init):" << ngram << endl;
                int words_since_phrase_boundary = 0;

		const vector<WordID>& e = rule.e();
		bool context_complete = false;
		for (int j = 0; j < e.size(); ++j) {

			///cerr << "Target terminal number " << j << endl;
			if (e[j] < 1) { // handle non-terminal substitution
                          words_since_phrase_boundary = 0;
				const void* astate = (ant_states[-e[j]]);

				// score unscored items (always on the left, since they
				// might not have seen their full context yet)
				int unscored_ant_len = UnscoredSize(astate);
				for (int k = 0; k < unscored_ant_len; ++k) {
					const int cur_word_with_marker = IthUnscoredWord(k, astate);
                                        const bool is_leftmost_in_phrase = cur_word_with_marker & LEFT_MARKER;
                                        const int cur_word = cur_word_with_marker & REMOVE_LEFT_MARKER;
                                        if(is_leftmost_in_phrase) {
                                          words_since_phrase_boundary = 0;
                                        }
					//cerr << "Antecedent " << k << "(E nonterm index is " << -e[j] << "): Scoring LM Word ID " << cur_word << endl;
					const conj_fid* conj_fid_vec = IthConjVec(k, astate);

					const bool is_oov = (cur_word == 0);
					if(is_oov) {
						ngram.clear();
					} else if (cur_word == jSOS_) {
						//cerr << "NT SOS!" << endl;
						ngram.clear(); // null state
						if (has_some_history) { // this is immediately fully scored, and bad
							cerr << "ERROR: JLM: Saw EOS in the middle of the sentence. Embedded in rule?" << endl;
							abort();
							context_complete = true;
						} else { // this might be a real <s>
							num_scored = max(0, order_ - 2);
						}
					} else {
						///cerr << "LM SCORE: " << (int)ngram_len << " " << p << "; first history word is " << state.history_[0] << "; astate is " << "" /*astate->history_[0]*/ << "; cur_word is " << cur_word << endl;
						if (saw_eos) {
							cerr << "ERROR: JLM: Saw EOS in the middle of the sentence. Embedded in rule?" << endl;
							abort();
						}
						saw_eos = (cur_word == jEOS_);
						//if(saw_eos) 	    cerr << "NT EOS!" << endl;
					}
					ngram.push_back(cur_word);
					//cerr << "Ngram (ant):" << ngram << endl;
					has_some_history = true;
					++num_scored;
					if (!context_complete) {
						if (num_scored >= order_)
							context_complete = true;
					}
					if (context_complete) {
                                          //cerr << "Complete (in nonterm)" << endl;
                                          FireLMFeats(features, ngram, words_since_phrase_boundary, rule);
                                          FireConjFeats(features, conj_fid_vec, false);
					} else {
                                          FireLMFeats(est_features, ngram, words_since_phrase_boundary, rule);
						if (remnant) {
						  //cerr << "Storing (nonterm) Word "<<cur_word<<"in remnant at " << num_estimated << endl;
                                                  if(words_since_phrase_boundary == 0) {
							SetIthUnscoredWord(num_estimated, cur_word | LEFT_MARKER, remnant);
                                                  } else {
							SetIthUnscoredWord(num_estimated, cur_word, remnant);
                                                  }
							//cerr << "Storing (nonterm) IthConjVec in remnant at " << num_estimated << endl;
							SetIthConjVec(num_estimated, conj_fid_vec, remnant);
						}
						++num_estimated;
						//cerr << "Est (in nonterm)" << endl;
						FireConjFeats(est_features, conj_fid_vec, true);
					}
					if(ngram.size() >= order_) {
						ngram.erase(ngram.begin(), ngram.begin()+1);
						//cerr << "Ngram (ant bump):" << ngram << endl;
					}
                                        ++words_since_phrase_boundary;
				} // end iteration over antecedent terminals
				saw_eos = GetFlag(astate, HAS_EOS_ON_RIGHT);
				// if we should use the right-most terminals within this antecedent, copy them
				if (HasFullContext(astate)) { // this is equivalent to the "star" in Chiang 2007
					RemnantLMState(astate, &ngram);
					//cerr << "Ngram (grab remnant):" << ngram << endl;
					context_complete = true;
				}
			} else { // handle terminal
				const WordID cur_word = ClassifyWordIfNecessary(e[j]);

                                // TODO: Fire LM feature that
                                ++words_since_phrase_boundary;

				// determine which conjoined features these lexical items will contribute to
				conj_fid conj_fid_vec[conjoined_fids_.size()];
				SetConjoinedFeats(rule, conj_fid_vec);

				const bool is_oov = (cur_word == 0);
				if(is_oov) {
					ngram.clear();
				} else if (cur_word == jSOS_) {
					ngram.clear(); // null state
					if (has_some_history) { // this is immediately fully scored, and bad
						context_complete = true;
						cerr << "ERROR: JLM: Saw BOS in the middle of the sentence. Embedded in rule?" << endl;
						abort();
					} else { // this might be a real <s>
						num_scored = max(0, order_ - 2);
						//cerr << "T SOS!" << endl;
					}
				} else {
					if (saw_eos) {
						cerr << "ERROR: JLM: Saw EOS in the middle of the sentence. Embedded in rule?" << endl; // TODO: FIXME
						abort();
					}
					saw_eos = (cur_word == jEOS_);
					//if(saw_eos) 	    cerr << "T EOS!" << endl;
				}
				has_some_history = true;
				++num_scored;
				if (!context_complete) {
					if (num_scored >= order_)
						context_complete = true;
				}
				ngram.push_back(cur_word);
				//cerr << "Ngram (term):" << ngram << endl;
				if (context_complete) {
					//cerr << "Complete (in term)" << endl;
                                  FireLMFeats(features, ngram, words_since_phrase_boundary, rule);
                                  FireConjFeats(features, conj_fid_vec, false);
				} else {
					if (remnant) {
                                          if(words_since_phrase_boundary == 0) {
                                            // use high bit as "left of phrase" indicator
                                            SetIthUnscoredWord(num_estimated, cur_word | LEFT_MARKER, remnant);
                                          } else {
                                            SetIthUnscoredWord(num_estimated, cur_word, remnant);
                                          }
					  //cerr << "Storing (term) IthConjVec in remnant at " << num_estimated << endl;
                                          SetIthConjVec(num_estimated, conj_fid_vec, remnant);
					}
					++num_estimated;
					//cerr << "Est (in nonterm)" << endl;
					FireLMFeats(est_features, ngram, words_since_phrase_boundary, rule);
					FireConjFeats(est_features, conj_fid_vec, true);
				}
				if(ngram.size() >= order_) {
					ngram.erase(ngram.begin(), ngram.begin()+1);
					//cerr << "Ngram (term bump):" << ngram << endl;
				}
			}
		}
		if (remnant) {
			SetFlag(saw_eos, HAS_EOS_ON_RIGHT, remnant);
			if(ngram.size() >= order_ - 1 && ngram.size() > 0) {
				// we'll only need N-1 tokens of history as context in the future
				ngram.erase(ngram.begin(), ngram.begin()+1);
			}
			SetRemnantLMState(ngram, remnant);
			SetUnscoredSize(num_estimated, remnant);
			SetHasFullContext(context_complete || (num_scored >= order_),
					remnant);
		}
	}

  int GetID(const int orig, std::map<int,int>& m, bool must_have=true) {
    std::map<int,int>::const_iterator match = m.find(orig);
    if(match == m.end()) {
      if(must_have) {
        cerr << "ERROR: Could not map ID for feature or word " << orig << endl;
        abort();
      } else {
        return -1;
      }
    }
    return match->second;
  }

  // rule is only used by child feature mappers
  // usually, we already have enough information to compute the LM feats at this point
  void FireLMFeats(SparseVector<double>* feats,
                   const vector<WordID>& ngram,
                   const int words_since_phrase_boundary1,
                   const TRule& rule) {
    
    const int words_since_phrase_boundary = min(words_since_phrase_boundary1, order_);

	  //cerr << "Scoring ngram: " << ngram << endl;
	  // TODO: We *really* need a test harness for this...

		assert(ngram.size() <= order_);
		bool found_match = false;
		  // for storing the n-gram being looked up:
		  // XXX: this is less efficient than storing offsets, but is less error-prone
		  // TODO: We could also store the n-grams in reverse order to make dropping more efficient
		vector<WordID> ngram_buf;
		ngram_buf.reserve(ngram.size());
                for(int i=0; i<ngram.size(); ++i) {
                  ngram_buf.push_back(GetID(ngram.at(i), wid2jlm_, false));
                }
		vector<WordID> ngram_context_buf;
		ngram_context_buf.reserve(ngram.size());
		ngram_context_buf = ngram;
		// drop right-most word (word being predicted)
		ngram_context_buf.erase(ngram_context_buf.end()-1);

		for(int n=ngram.size()-1; n>=0; --n) {
		  
		  uint64_t hash = jlm::Hash(ngram_buf);
		  const jlm::Entry *feat_vec_match = NULL;
		  disc_feats_->Find(hash, feat_vec_match);
		  if(feat_vec_match != NULL) {
		    found_match = true;

		    const int feat = feat_vec_match->match_feat;
                    const int cdec_feat = GetID(feat, jlm2fid_);
		    const float feat_value
#ifdef JLM_REAL_VALUES		      
		      = feat_vec_match->match_value;
#else
   		      = 1;
#endif
		    
		    // cerr << "Firing feat " << FD::Convert(feat) << " for ngram " << ngram_buf << endl;
		    
		    // TODO: Disabling firing original feat?
		    feats->set_value(cdec_feat, feat_value);
		    for(int j=0; j<feat_mappers_.size(); ++j) {
		      const int fine_feat = feat_mappers_.at(j)->MapFeat(feat, ngram_buf, words_since_phrase_boundary);
                      const int cdec_fine_feat = GetID(fine_feat, jlm2fid_);
		      feats->set_value(cdec_fine_feat, feat_value);
		    }
		  } else if(!ngram_context_buf.empty()) {
		    // we can't backoff farther than a unigram
		    uint64_t backoff_hash = jlm::Hash(ngram_context_buf);
		    const jlm::Entry *backoff_feat_vec_match = NULL;
		    disc_feats_->Find(backoff_hash, backoff_feat_vec_match);
		    if(backoff_feat_vec_match != NULL) {
		      const int backoff_feat = backoff_feat_vec_match->miss_feat;
		      const float backoff_value
#ifdef JLM_REAL_VALUES		      
		        = feat_vec_match->miss_value;
#else
   		        = 1;
#endif

		      if(backoff_feat != -1) {
                        const int cdec_backoff_feat = GetID(backoff_feat, jlm2fid_);
			feats->set_value(cdec_backoff_feat, backoff_value);
			for(int j=0; j<feat_mappers_.size(); ++j) {
			  const int fine_backoff_feat = feat_mappers_.at(j)->MapFeat(backoff_feat, ngram_context_buf, words_since_phrase_boundary);
                          const int cdec_fine_backoff_feat = GetID(fine_backoff_feat, jlm2fid_);
			  feats->set_value(cdec_fine_backoff_feat, backoff_value);
			}
		      }
		    }
		  }
		  // remove the first word from the left of each n-gram buffer
		  if(!ngram_buf.empty()) {
		    ngram_buf.erase(ngram_buf.begin());
		  }
		  if(!ngram_context_buf.empty()) {
		    ngram_context_buf.erase(ngram_context_buf.begin());
		  }
		} // for each n in order
		if(!found_match) {
			// TODO: Precache this?
			vector<WordID> unk_ngram;
			unk_ngram.push_back(GetID(CDEC_UNK_, wid2jlm_, false));
			uint64_t hash = jlm::Hash(unk_ngram);
			const jlm::Entry *feat_vec_match = NULL;
			disc_feats_->Find(hash, feat_vec_match);
			if(feat_vec_match != NULL) {
                          const int feat = feat_vec_match->match_feat;
                          const int cdec_feat = GetID(feat, jlm2fid_);
                          feats->set_value(cdec_feat, 1);
			}
		}
	}

	inline void SetConjoinedFeats(const TRule& rule, conj_fid* conj_fid_vec) {
		const SparseVector<double>& rule_feats = rule.GetFeatureValues();
		std::fill(conj_fid_vec, conj_fid_vec + conjoined_fids_.size(), 0);
		int vec_i = 0;
		for (unsigned i = 0; i < conjoin_with_fids_.size(); ++i) {
			int conjoin_with_fid = conjoin_with_fids_.at(i);
			double conjoin_with_value = rule_feats.value(conjoin_with_fid);
			if (conjoin_with_value != 0.0) {
				//cerr << "Recognized conjoined fid " << conjoin_with_fid << endl;
				int conjoined_fid = conjoined_fids_.at(i);
				conj_fid_vec[vec_i] = conjoined_fid;
				++vec_i;
			}
		}
	}

	// this assumes no target words on final unary -> goal rule.  is that ok?
	// for <s> (n-1 left words) and (n-1 right words) </s>
	void FinalTraversalCost(const void* state, SparseVector<double>* features) {

		// Since this function no longer returns the lm cost,
		// we must directly add it to the affected conjoined features
		// TODO: Even if we have enable_main_feat_ == false, we still add final
		// transition cost there, since we don't know where else to add it.

		if (add_sos_eos_) { // rules do not produce <s> </s>, so do it here

			// dummy_state_ introduces <s> as a LM state
			vector<WordID> sos;
			sos.push_back(jSOS_);
			SetRemnantLMState(sos, dummy_state_);
			SetHasFullContext(1, dummy_state_);
			SetUnscoredSize(0, dummy_state_);

			// actual sentence replaces middle element of dummy rule via LM state
			dummy_ants_[1] = state; // dummy ants represents the rule "<s> state </s>"

			// dummy_rule_ adds </s> as a real terminal
			///cerr << "Looking up words for final transition" << endl;
			///cerr << "History is " << RemnantLMState(state).history_[0] << endl;


			//cerr << "FINAL TRAVERSAL" << endl;
			const vector<WordID>& e = dummy_rule_->e();
			for (int j = 0; j < e.size(); ++j) {
			  //cerr << "TARGET POS " << j << endl;
				if (e[j] < 1) { // handle non-terminal substitution
					const void* astate = (dummy_ants_[-e[j]]);
					int unscored_ant_len = UnscoredSize(astate);
					//cerr << "HAZ " << unscored_ant_len << " UNSCORED" << endl;
					for (int k = 0; k < unscored_ant_len; ++k) {
						const WordID cur_word = IthUnscoredWord(k,
								astate);
						//cerr << "CUR_WORD " << cur_word << endl;
					}
				}
			}

			LookupWords(*dummy_rule_, dummy_ants_, features, NULL, NULL);

		} else { // rules DO produce <s> ... </s>
                  cerr << "ERROR: JLM does not yet support rules with <s> and </s>" << endl;
                  abort();
			double p = 0;
			if (!GetFlag(state, HAS_EOS_ON_RIGHT)) {
				p -= 100;
			} // TODO: Tune another feature for this?
			if (UnscoredSize(state) > 0) { // are there unscored words
				if (jSOS_ != IthUnscoredWord(0, state)) {
					// TODO: Handle SOS?
				}
			}
			features->set_value(fid_, p);
		}
	}

	// if this is not a class-based LM, returns w untransformed,
	// otherwise returns a word class mapping of w,
	// returns TD::Convert("<unk>") if there is no mapping for w
	WordID ClassifyWordIfNecessary(WordID w) const {
		if (word2class_map_.empty())
			return w;
		if (w >= word2class_map_.size())
			return CDEC_UNK_;
		else
			return word2class_map_[w];
	}

public:
	JLanguageModelImpl(const string& filename, 
			   const string& mapfile,
			   bool explicit_markers,
			   int fid,
			   const string& featname,
                           const vector<int>& conjoin_with_fids,
			   const vector<int>& conjoined_fids,
			   const vector<boost::shared_ptr<FeatureMapper> >& feat_mappers) :
			CDEC_UNK_(TD::Convert("<unk>")),
                        add_sos_eos_(!explicit_markers),
                        fid_(fid),
			feat_mappers_(feat_mappers)
  {
		LoadDiscLM(filename);

		// TODO: Replace with a vector<bool>, dynamic_bitset<bool>, or use on-the-fly shifts
		conjoin_with_fids_ = conjoin_with_fids;
		conjoined_fids_ = conjoined_fids;
		conj_vec_size_ = conjoined_fids.size() * sizeof(conj_fid);
		state_size_ = (order_ - 1) * sizeof(WordID) // RemnantLM state
			+ 2 // unscored_size and is_complete
			+ (order_ - 1) * sizeof(WordID) // unscored_words: words that might participate in context in the future
                        + (order_ - 1) * conj_vec_size_; // vectors (one per unscored word) of conjoined fids that receive this LM score
		unscored_size_offset_ = (order_ - 1) * sizeof(WordID);
		is_complete_offset_ = unscored_size_offset_ + 1;
		unscored_words_offset_ = is_complete_offset_ + 1;
		conjoined_feats_offset_ = unscored_words_offset_
				+ (order_ - 1) * sizeof(lm::WordIndex);
		assert(
				conjoined_feats_offset_ + (order_ - 1) * conj_vec_size_ == state_size_);

		cerr << "LM state size is " << state_size_ << " bytes" << endl;

		// special handling of beginning / ending sentence markers
		dummy_state_ = new char[state_size_];
		memset(dummy_state_, 0, state_size_);
		// this will get written with the BOS LM state
		dummy_ants_.push_back(dummy_state_);
		// this will get overwritten for each true antecedent
		dummy_ants_.push_back(NULL);
		dummy_rule_.reset(
				new TRule(
						"[DUMMY] ||| [BOS] [DUMMY] ||| [1] [2] </s> ||| X=0"));
		jSOS_ = TD::Convert("<s>");
		assert(jSOS_ > 0);
		jEOS_ = TD::Convert("</s>");

		// handle class-based LMs (unambiguous word->class mapping reqd.)
		if (mapfile.size())
			LoadWordClasses(mapfile);
	}

	void LoadDiscLM(const string& filename) {
	  // 1) mmap the file
	  util::scoped_fd file(util::OpenReadOrThrow(filename.c_str()));
	  uint64_t size = util::SizeFile(file.get());
	  MapRead(util::POPULATE_OR_READ, file.get(), 0, size, table_mem_);
          disc_feats_.reset(new jlm::Table(table_mem_.get(), table_mem_.size()));

	  // 2) Load feat, vocab, and order
          cerr << "  Loading discriminative LM metadata from " << file << " ..." << endl;
	  ReadFile rf(filename + ".meta");
	  istream& in = *rf.stream();
	  string line;
	  order_ = 0;
	  while (in) {
	    getline(in, line);
	    if (!in)
	      continue;

            // XXX: I'm a terrible person for using const_cast
            if(line.find("ORDER: ") == 0) {
              order_ = boost::lexical_cast<int>(line.substr(7));
            } else if(line.find("WORD: ") == 0) {
              char* col = strtok(const_cast<char*>(line.c_str()), " ");
              int cdec_wid = TD::Convert(string(strtok(NULL, " ")));
              int jlm_wid = atoi(strtok(NULL, " "));
              wid2jlm_.insert(pair<int,int>(cdec_wid, jlm_wid));

              // TODO: Make sure all lookups convert WIDS
            } else if(line.find("FEAT: ") == 0) {
              char* col = strtok(const_cast<char*>(line.c_str()), " ");
              int cdec_fid = FD::Convert(string(strtok(NULL, " ")));
              int jlm_fid = atoi(strtok(NULL, " "));
              jlm2fid_.insert(pair<int,int>(jlm_fid,cdec_fid));

              // TODO: Make sure all lookups convert FIDS
            } else {
              cerr << "ERROR: Invalid line: " << line << endl;
              abort();
            }
	  }
	  cerr << "JLM: Loaded " << jlm2fid_.size() << " features; Max order is " << order_ << endl;
	  
	  for(int i=0; i<feat_mappers_.size(); ++i) {
	    feat_mappers_.at(i)->InitFeats(all_feats_, order_);
	  }
	}

	void LoadWordClasses(const string& file) {
		ReadFile rf(file);
		istream& in = *rf.stream();
		string line;
		vector<WordID> dummy;
		int lc = 0;
		cerr << "  Loading word classes from " << file << " ...\n";
		AddWordToClassMapping_(TD::Convert("<s>"), TD::Convert("<s>"));
		AddWordToClassMapping_(TD::Convert("</s>"), TD::Convert("</s>"));
		while (in) {
			getline(in, line);
			if (!in)
				continue;
			dummy.clear();
			TD::ConvertSentence(line, &dummy);
			++lc;
			if (dummy.size() != 2) {
				cerr << "    Format error in " << file << ", line " << lc
						<< ": " << line << endl;
				abort();
			}
			AddWordToClassMapping_(dummy[0], dummy[1]);
		}
	}

	void AddWordToClassMapping_(WordID word, WordID cls) {
		cerr << "Unimplemented" << endl;
		abort();
//		if (word2class_map_.size() <= word) {
//			word2class_map_.resize((word + 10) * 1.1, kCDEC_UNK);
//			assert(word2class_map_.size() > word);
//		}
//		if (word2class_map_[word] != kCDEC_UNK) {
//			cerr << "Multiple classes for symbol " << TD::Convert(word) << endl;
//			abort();
//		}
//		word2class_map_[word] = cls;
	}

	~JLanguageModelImpl() {
		delete[] dummy_state_;
	}

	int ReserveStateSize() const {
		return state_size_;
	}

private:
	WordID CDEC_UNK_;
	WordID jSOS_; // <s> - requires special handling.
	WordID jEOS_; // </s>
	const bool add_sos_eos_; // flag indicating whether the hypergraph produces <s> and </s>
	// if this is true, FinalTransitionFeatures will "add" <s> and </s>
	// if false, FinalTransitionFeatures will score anything with the
	// markers in the right place (i.e., the beginning and end of
	// the sentence) with 0, and anything else with -100

	int order_;
	int state_size_;
	int unscored_size_offset_;
	int is_complete_offset_;
	int unscored_words_offset_;
	int conjoined_feats_offset_;
	char* dummy_state_;
	vector<const void*> dummy_ants_;
	vector<WordID> word2class_map_; // if this is a class-based LM, this is the word->class mapping
	TRulePtr dummy_rule_;
	int fid_;

	vector<int> conjoin_with_fids_;
	vector<int> conjoined_fids_;
	int conj_vec_size_;

  util::scoped_memory table_mem_;
  boost::scoped_ptr<jlm::Table> disc_feats_;
  map<int,int> jlm2fid_;
  map<int,int> wid2jlm_;

  set<int> all_feats_;

  vector<boost::shared_ptr<FeatureMapper> > feat_mappers_;

  /*
	// features that can be annotated on individual target words
	// vector is indexed by word's position in the target side of the rule/phrase (including non-terms)
	// map key is original feature id annotated on the rule/phrase
	// map value is the feature to be fired and/or conjoined at the specified target word index
	vector<boost::unordered_map<int, int> > word_index_feats_;
  */
	// for getting word-level confidences based on the translation model
};

JLanguageModel::JLanguageModel(const string& param) {
	string filename, mapfile, featname;
	bool explicit_markers;
	vector<int> conjoin_with_fids;
	vector<int> conjoined_fids;
        bool use_phrase_bounds;
	// confidence feature mapper config:
        bool use_confidence;
	string f2e_lex_file;
	string stopwords_file;
	float min_prob;
	int min_link_count;
	if (!ParseLMArgs(param, &filename, &mapfile, &explicit_markers, &featname,
                         &conjoin_with_fids, &conjoined_fids, &use_phrase_bounds, &use_confidence,
			 &f2e_lex_file, &stopwords_file, &min_prob, &min_link_count)) {
		abort();
	}

	fid_ = FD::Convert(featname);

	// TODO: Create feat_mappers based on use_phrase_bounds and use of lex confidence
	vector<boost::shared_ptr<FeatureMapper> > feat_mappers;
	if(use_phrase_bounds) {
	  feat_mappers.push_back(boost::shared_ptr<FeatureMapper>(new PhraseBoundaryFeatureMapper));
	}
	if(use_confidence) {
	  assert(f2e_lex_file != "");
	  assert(stopwords_file != "");
	  feat_mappers.push_back(boost::shared_ptr<FeatureMapper>(
            new AnchorFeatureMapper(f2e_lex_file, stopwords_file, min_prob, min_link_count)));
	}

	// just throw any exceptions
	pimpl_ = new JLanguageModelImpl(filename, mapfile,
			explicit_markers, fid_, featname, conjoin_with_fids,
                                        conjoined_fids, feat_mappers);

	SetStateSize(pimpl_->ReserveStateSize());
}

Features JLanguageModel::features() const {
	return single_feature(fid_);
}

JLanguageModel::~JLanguageModel() {
	delete pimpl_;
}

void JLanguageModel::TraversalFeaturesImpl(const SentenceMetadata& smeta,
		const Hypergraph::Edge& edge, const vector<const void*>& ant_states,
		SparseVector<double>* features,
		SparseVector<double>* estimated_features, void* state) const {
	double oovs = 0;
	double est_oovs = 0;
	pimpl_->LookupWords(*edge.rule_, ant_states, features, estimated_features, state);

	// don't conjoin the OOVs... for now
//  if (oov_fid_) {
//    if (oovs) features->set_value(oov_fid_, oovs);
//    if (est_oovs) estimated_features->set_value(oov_fid_ est_oovs);
//  }
}

void JLanguageModel::FinalTraversalFeatures(const void* ant_state, SparseVector<double>* features) const {
	pimpl_->FinalTraversalCost(ant_state, features);
}

boost::shared_ptr<FeatureFunction> CreateModel(const std::string &param) {
	JLanguageModel *ret = new JLanguageModel(param);
	ret->Init();
	return boost::shared_ptr<FeatureFunction>(ret);
}

boost::shared_ptr<FeatureFunction> JLanguageModelFactory::Create(std::string param) const {
	return CreateModel(param);
}

std::string JLanguageModelFactory::usage(bool params, bool verbose) const {
	return JLanguageModel::usage(params, verbose);
}

void JLanguageModel::PrepareForInput(const SentenceMetadata& smeta) {
	pimpl_->PrepareForInput(smeta);
}
