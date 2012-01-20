#include "ff_jlm.h"

#include <cstring>
#include <iostream>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>

#include "filelib.h"
#include "stringlib.h"
#include "hg.h"
#include "tdict.h"
#include "lm/model.hh"
#include "lm/enumerate_vocab.hh"
#include "murmur_hash.h"
#include "vector_util.h"

#define JLM_ONE_FEAT 1

using namespace std;

static const unsigned char HAS_FULL_CONTEXT = 1;
static const unsigned char HAS_EOS_ON_RIGHT = 2;
static const unsigned char MASK = 7;

typedef unsigned short conj_fid;
#ifdef JLM_ONE_FEAT
typedef boost::unordered_map<uint64_t, pair<int,int> > FeatMap;
#else
typedef boost::unordered_map<uint64_t, pair<vector<int> , vector<int> > FeatMap;
#endif

static const conj_fid END_OF_CONJ_VEC = 0;

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

// -x : rules include <s> and </s>
// -n NAME : feature id is NAME
// -C conjoin_with_name : conjoined features must be *rule* features
// -C@file : specify file with list of names to conjoin with
// -d : Discriminative LM mode
bool ParseLMArgs(string const& in, string* filename, string* mapfile,
		bool* explicit_markers, string* featname,
		vector<int>* conjoin_with_fids, vector<int>* conjoined_fids) {
	vector<string> const& argv = SplitOnWhitespace(in);
	*explicit_markers = false;
	*featname = "JLanguageModel";
	*mapfile = "";
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

struct VMapper: public lm::EnumerateVocab {
	VMapper(vector<lm::WordIndex>* out) :
			out_(out), kLM_UNKNOWN_TOKEN(0) {
		out_->clear();
	}
	void Add(lm::WordIndex index, const StringPiece &str) {
		const WordID cdec_id = TD::Convert(str.as_string());
		if (cdec_id >= out_->size())
			out_->resize(cdec_id + 1, kLM_UNKNOWN_TOKEN);
		(*out_)[cdec_id] = index;
	}
	vector<lm::WordIndex>* out_;
	const lm::WordIndex kLM_UNKNOWN_TOKEN;
};

class JLanguageModelImpl {

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

		const vector<WordID>& e = rule.e();
		bool context_complete = false;
		for (int j = 0; j < e.size(); ++j) {

			///cerr << "Target terminal number " << j << endl;
			if (e[j] < 1) { // handle non-terminal substitution
				const void* astate = (ant_states[-e[j]]);

				// score unscored items (always on the left, since they
				// might not have seen their full context yet)
				int unscored_ant_len = UnscoredSize(astate);
				for (int k = 0; k < unscored_ant_len; ++k) {
					const int cur_word = IthUnscoredWord(k, astate);
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
						FireLMFeats(features, ngram);
						FireConjFeats(features, conj_fid_vec, false);
					} else {
						FireLMFeats(est_features, ngram);
						if (remnant) {
						  //cerr << "Storing (nonterm) Word "<<cur_word<<"in remnant at " << num_estimated << endl;
							SetIthUnscoredWord(num_estimated, cur_word,
									remnant);
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
					FireLMFeats(features, ngram);
					FireConjFeats(features, conj_fid_vec, false);
				} else {
					if (remnant) {
						SetIthUnscoredWord(num_estimated, cur_word, remnant);
						//cerr << "Storing (term) IthConjVec in remnant at " << num_estimated << endl;
						SetIthConjVec(num_estimated, conj_fid_vec, remnant);
					}
					++num_estimated;
					//cerr << "Est (in nonterm)" << endl;
					FireLMFeats(est_features, ngram);
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

	inline void FireLMFeats(SparseVector<double>* feats, const vector<WordID>& ngram) {
	  //cerr << "Scoring ngram: " << ngram << endl;
		assert(ngram.size() <= order_);
		bool found_match = false;
		for(int n=ngram.size(); n>0; --n) {
			uint64_t hash = Hash(ngram);
			FeatMap::const_iterator feat_vec_match = disc_feats_.find(hash);
			if(feat_vec_match != disc_feats_.end()) {
				found_match = true;
#ifdef JLM_ONE_FEAT
				const int feat = feat_vec_match->second.first;
				feats->set_value(feat, 1);
#else
				const vector<int>& feat_vec = feat_vec_match->second.first;
				for(int i=0; i<feat_vec.size(); ++i) {
				   //cerr << ngram << " :: Feat"<<n<<": " << i << "/" << feat_vec.size() << ": " << feat_vec.at(i) << endl;
				   feats->set_value(feat_vec.at(i), 1);
				}
#endif
			} else {
				uint64_t backoff_hash = Hash(ngram, ngram.size()-n, ngram.size());
				FeatMap::const_iterator backoff_feat_vec_match = disc_feats_.find(backoff_hash);
				if(backoff_feat_vec_match != disc_feats_.end()) {
#ifdef JLM_ONE_FEAT
				const int backoff_feat = backoff_feat_vec_match->second.second;
				if(backoff_feat != -1) {
					feats->set_value(backoff_feat, 1);
				}
#else
					const vector<int>& backoff_feat_vec = backoff_feat_vec_match->second.second;
					for(int i=0; i<backoff_feat_vec.size(); ++i) {
					  //cerr << ngram << " :: Backoff Feat"<<n<<": " << i << "/" << backoff_feat_vec.size() << ": " << backoff_feat_vec.at(i) << endl;
						feats->set_value(backoff_feat_vec.at(i), 1);
					}
#endif
				}
			}
		}
		if(!found_match) {
			// TODO: Precache this
			vector<WordID> unk_ngram;
			unk_ngram.push_back(jCDEC_UNK_);
			uint64_t hash = Hash(unk_ngram);
			FeatMap::const_iterator feat_vec_match = disc_feats_.find(hash);
			if(feat_vec_match != disc_feats_.end()) {
#ifdef JLM_ONE_FEAT
				const int feat = feat_vec_match->second.first;
				feats->set_value(feat, 1);
#else
				const vector<int>& feat_vec = feat_vec_match->second.first;
				found_match = true;
				for(int i=0; i<feat_vec.size(); ++i) {
					feats->set_value(feat_vec.at(i), 1);
				}
#endif
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
			return jCDEC_UNK_;
		else
			return word2class_map_[w];
	}

public:
	JLanguageModelImpl(const string& filename, const string& mapfile,
			bool explicit_markers, int fid, const string& featname,
			const vector<int>& conjoin_with_fids, const vector<int>& conjoined_fids) :
			jCDEC_UNK_(TD::Convert("<unk>")), add_sos_eos_(!explicit_markers), fid_(fid) {

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
		// KenLM invariant

		// handle class-based LMs (unambiguous word->class mapping reqd.)
		if (mapfile.size())
			LoadWordClasses(mapfile);
	}

	inline uint64_t Hash(const vector<WordID>& ngram) const {
		return Hash(ngram, 0, ngram.size());
	}

	// begin is the index of the first token to be scored
	// end is the index just beyond the last token to be scored
	inline uint64_t Hash(const vector<WordID>& ngram, int iBegin, int iEnd) const {
	  const char* begin = reinterpret_cast<const char*>(&*(ngram.begin() + iBegin));
	  int len = sizeof(WordID) * (iEnd - iBegin);
	  return MurmurHash64(begin, len);
	}

	void LoadDiscLM(const string& file) {
		ReadFile rf(file);
		istream& in = *rf.stream();
		string line;
		cerr << "  Loading discriminative LM features from " << file << " ...\n";
		order_ = 0;
		int entries = 0;
		int beforeFid = FD::NumFeats();
		while (in) {
			getline(in, line);
			if (!in)
				continue;

			++entries;

			// XXX: I'm a terrible person for using const_cast
			char* ngram = strtok(const_cast<char*>(line.c_str()), "\t");
			char* feats = strtok(NULL, "\t");
			char* backoff_feats = strtok(NULL, "\t");

			//cerr << "Got ngram: " << ngram << endl;

			char* tok = strtok(ngram, " ");
			vector<WordID> toks;
			while (tok != NULL) {
				int wid = TD::Convert(string(tok)); // ugh, copy
				//cerr << "Token: " << tok << " -> " << wid << endl;
				toks.push_back(wid);
				tok = strtok(NULL, " ");
			}
			uint64_t hash = Hash(toks);
			order_ = max(order_, (int) toks.size());

#ifdef JLM_ONE_FEAT
			pair<int, int>& feat_val = disc_feats_[hash];

			char* feat = strtok(feats, " ");
			feat_val.first = FD::Convert(string(feat)); // ugh, copy
			if(strtok(NULL, " ") != NULL) {
				cerr << "ERROR: JLM: Expeted only one feature" << endl;
				abort();
			}

			if(backoff_feats != NULL) {
				feat = strtok(backoff_feats, " ");
				feat_val.second = FD::Convert(string(feat)); // ugh, copy
				if(strtok(NULL, " ") != NULL) {
					cerr << "ERROR: JLM: Expeted only one feature" << endl;
					abort();
				}
			} else {
				feat_val.second = -1;
			}
#else
			pair<vector<int> , vector<int> >& feat_vec = disc_feats_[hash];

			char* feat = strtok(feats, " ");
			while (feat != NULL) {
				int fid = FD::Convert(string(feat)); // ugh, copy
				//cerr << "Feat: " << feat << " -> " << fid << endl;
				feat_vec.first.push_back(fid);
				feat = strtok(NULL, " ");
			}

			if(backoff_feats != NULL) {
				feat = strtok(backoff_feats, " ");
				while (feat != NULL) {
					int fid = FD::Convert(string(feat)); // ugh, copy
					//cerr << "Backoff Feat: " << feat << " -> " << fid << endl;
					feats_vec.second.push_back(fid);
					feat = strtok(NULL, " ");
				}
			}

			//cerr << "Loaded ngram " << ngram << "; toks = "<< toks << "; hash = " << hash << "; feat count = " << feat_vec.size() << "; backoff feat count = " << backoff_feats_vec.size() << endl;
#endif
 		}
		int lm_feat_count = FD::NumFeats() - beforeFid;
		cerr << "JLM: Loaded " << entries << " entries with " << lm_feat_count << " features; Max order is " << order_ << endl;
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
	WordID jCDEC_UNK_;
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

	FeatMap disc_feats_;
};

JLanguageModel::JLanguageModel(const string& param) {
	string filename, mapfile, featname;
	bool explicit_markers;
	vector<int> conjoin_with_fids;
	vector<int> conjoined_fids;
	if (!ParseLMArgs(param, &filename, &mapfile, &explicit_markers, &featname,
			&conjoin_with_fids, &conjoined_fids)) {
		abort();
	}

	fid_ = FD::Convert(featname);

	// just throw any exceptions
	pimpl_ = new JLanguageModelImpl(filename, mapfile,
			explicit_markers, fid_, featname, conjoin_with_fids,
			conjoined_fids);

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
//    if (est_oovs) estimated_features->set_value(oov_fid_, est_oovs);
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
