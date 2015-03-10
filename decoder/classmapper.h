#ifndef _CLASSMAP_H_
#define _CLASSMAP_H_

#include "filelib.h"

#include <vector>
#include <string>

class ClassMapper {
public:
  explicit ClassMapper() : kCDEC_UNK(TD::Convert("<unk>")) {}

  // copied from ff_klm.cc; TODO: Dedupe
  void LoadWordClasses(const std::string& file) {
    ReadFile rf(file);
    std::istream& in = *rf.stream();
    std::string line;
    std::vector<WordID> dummy;
    int lc = 0;
    std::cerr << "  Loading word classes from " << file << " ...\n";
    AddWordToClassMapping_(TD::Convert("<s>"), TD::Convert("<s>"));
    AddWordToClassMapping_(TD::Convert("</s>"), TD::Convert("</s>"));
    while(in) {
      getline(in, line);
      if (!in) continue;
      dummy.clear();
      TD::ConvertSentence(line, &dummy);
      ++lc;
      if (dummy.size() != 2 && dummy.size() != 3) {
        std::cerr << "    Format error in " << file << ", line " << lc << ": " << line << std::endl;
        abort();
      }
      if (dummy.size() == 2) { // old format
        AddWordToClassMapping_(dummy[0], dummy[1]);
      } else { // paths format
        AddWordToClassMapping_(dummy[1], dummy[0]);
      }
    }
  }

  void AddWordToClassMapping_(WordID word, WordID cls) {
    if (word2class_map_.size() <= word) {
      word2class_map_.resize((word + 10) * 1.1, kCDEC_UNK);
    }
    assert(word2class_map_.size() > word);
    if(word2class_map_[word] != kCDEC_UNK) {
      std::cerr << "Multiple classes for symbol " << TD::Convert(word) << std::endl;
      abort();
    }
    word2class_map_[word] = cls;
  }

  WordID ClassifyWord(WordID w) const {
    if (w >= word2class_map_.size())
      return kCDEC_UNK;
    else
      return word2class_map_[w];
  }

  bool Empty() const {
    return word2class_map_.empty();
  }

private:
  std::vector<WordID> word2class_map_;
  WordID kCDEC_UNK;
};

#endif
