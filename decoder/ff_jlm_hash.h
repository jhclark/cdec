#ifndef _JLM_HASH_FF_H_
#define _JLM_HASH_FF_H_

#include "util/probing_hash_table.hh"

#include "murmur_hash.h"

namespace jlm {

  struct Entry {
    typedef uint64_t Key;
    Key GetKey() const {
      return key;
    }
    
    Key key;
    int match_feat;
    int miss_feat;
  };


  typedef util::ProbingHashTable<jlm::Entry, util::IdentityHash> Table;
  typedef int WordID;
  
  // begin is the index of the first token to be scored
  // end is the index just beyond the last token to be scored
  uint64_t Hash(const vector<WordID>& ngram, int iBegin, int iEnd) {
    const char* begin = reinterpret_cast<const char*>(&*(ngram.begin() + iBegin));
    int len = sizeof(WordID) * (iEnd - iBegin);
    return MurmurHash64(begin, len);
  }

  uint64_t Hash(const vector<WordID>& ngram) {
    return Hash(ngram, 0, ngram.size());
  }
}

#endif
