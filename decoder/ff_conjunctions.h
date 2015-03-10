#ifndef _FF_CONJUNCTIONS_H_
#define _FF_CONJUNCTIONS_H_

#include "ff.h"
#include "classmapper.h"

#include <tr1/unordered_set>
#include <boost/algorithm/string.hpp>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>

class ConjoinedWordSet : public FeatureFunction {
 public:
// we depend on the order of the initializer list
// to call member constructurs in the proper order
// modify this carefully!
//
// Usage: "ConjoinedWordSet -N name -s src_vocab.txt -t tgt_vocab.txt -m class_map.txt [--aligned] [--abs|--positive|--negative] [--invert_src] [--invert_tgt]"
 ConjoinedWordSet(const std::string& param)
  {
    std::string srcVocabFile;
    std::string tgtVocabFile;
    std::string featName;
    std::string mapFileF;
    std::string mapFileE;
    emit_lex_ = true;
    parseArgs(param, &featName, &mapFileF, &mapFileE, &alignedOnly_, &absValue_, &positive_, &negative_, &lexicalized_, &srcVocabFile, &tgtVocabFile, &invertSrc_, &invertTgt_);

    emit_classes_ = (mapFileF != "") && (mapFileE != "");
    emit_mixed_ = emit_classes_;

    featName_ = featName;
    fid_ = FD::Convert(featName);

    std::ostringstream alignedName;
    alignedName << featName << "Aligned";
    alignedName_ = alignedName.str();

    std::ostringstream alignedClassName;
    alignedClassName << featName << "AlignedClass";
    alignedClassName_ = alignedClassName.str();
    
    std::ostringstream alignedMixedName;
    alignedMixedName << featName << "AlignedMixed";
    alignedMixedName_ = alignedMixedName.str();

    std::ostringstream acrossName;
    acrossName << featName << "AcrossPhrase";
    acrossName_ = acrossName.str();

    std::ostringstream acrossClassName;
    acrossClassName << featName << "AcrossClass";
    acrossClassName_ = acrossClassName.str();
    
    std::ostringstream acrossMixedName;
    acrossMixedName << featName << "AcrossMixed";
    acrossMixedName_ = acrossMixedName.str();

    if (srcVocabFile == "NULL" || srcVocabFile == "null" || srcVocabFile == "") {
      std::cerr << "Using NULL vocab for src vocab of " << featName << std::endl;
    } else {
      std::cerr << "Loading src vocab for " << param << " from " << srcVocabFile << " for " << featName << std::endl;
      loadVocab(srcVocabFile, &srcVocab_);
    }

    if (tgtVocabFile == "NULL" || tgtVocabFile == "null" || tgtVocabFile == "") {
      std::cerr << "Using NULL vocab for tgt vocab of " << featName << std::endl;
    } else {
      std::cerr << "Loading tgt vocab for " << param << " from " << tgtVocabFile << " for " << featName << std::endl;
      loadVocab(tgtVocabFile, &tgtVocab_);
    }

    if (mapFileF != "")
      class_map_f_.LoadWordClasses(mapFileF);
    if (mapFileE != "")
      class_map_e_.LoadWordClasses(mapFileE);
  }

  ~ConjoinedWordSet() {
  }

  Features features() const { return single_feature(fid_); }

 protected:
  virtual void TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                     const Hypergraph::Edge& edge,
                                     const std::vector<const void*>& ant_contexts,
                                     SparseVector<double>* features,
                                     SparseVector<double>* estimated_features,
                                     void* context) const;
 private:

  static void loadVocab(const std::string& vocabFile, std::tr1::unordered_set<WordID>* vocab) {

      std::ifstream file;
      std::string line;

      file.open(vocabFile.c_str(), std::fstream::in);
      if (file.is_open()) {
	unsigned lineNum = 0;
	while (!file.eof()) {
	  ++lineNum;
	  getline(file, line);
	  boost::trim(line);
	  if(line.empty()) {
	    continue;
	  }
	  
	  WordID vocabId = TD::Convert(line);
	  vocab->insert(vocabId);
	}
	file.close();
      } else {
	std::cerr << "Unable to open file: " << vocabFile; 
	exit(1);
      }
  }

  static void parseArgs(const std::string& args, std::string* featName, std::string* mapFileF, std::string* mapFileE,
			bool* alignedOnly, bool* absValue, bool* positive, bool* negative, bool* lexicalized,
			std::string* srcVocabFile, std::string* tgtVocabFile,
                        bool* invertSrc, bool* invertTgt) {

    std::vector<std::string> toks(10);
    boost::split(toks, args, boost::is_any_of(" "));

    *alignedOnly = false;
    *absValue = false;
    *positive = false;
    *negative = false;
    *lexicalized = false;
    *invertSrc = false;
    *invertTgt = false;

    // skip initial feature name
    for(std::vector<std::string>::const_iterator it = toks.begin(); it != toks.end(); ++it) {
      if(*it == "-s") {
	*srcVocabFile = *++it; // copy

      } else if(*it == "-t") {
	*tgtVocabFile = *++it; // copy

      } else if(*it == "-N") {
	*featName = *++it;

      } else if(*it == "-mf") {
	*mapFileF = *++it;

      } else if(*it == "-me") {
	*mapFileE = *++it;

      } else if(*it == "--lexicalized") {
	*lexicalized = true;

      } else if(*it == "--aligned") {
	*alignedOnly = true;

      } else if(*it == "--abs") {
	*absValue = true;

      } else if(*it == "--positive") {
	*positive = true;

      } else if(*it == "--negative") {
	*negative = true;

      } else if(*it == "--invert_src") {
	*invertSrc = true;

      } else if(*it == "--invert_tgt") {
	*invertTgt = true;

      } else {
	std::cerr << "Unrecognized argument: " << *it << std::endl;
	exit(1);
      }
    }

    if(*featName == "") {
      std::cerr << "featName (-N) not specified for ConjoinedWordSet" << std::endl;
      exit(1);
    }
/*
    if(*srcVocabFile == "") {
      std::cerr << "srcVocabFile (-s) not specified for ConjoinedWordSet" << std::endl;
      exit(1);
    }
    if(*tgtVocabFile == "") {
      std::cerr << "tgtVocabFile (-t) not specified for ConjoinedWordSet" << std::endl;
      exit(1);
    }
*/
    // TODO: validate boolean options
  }
  
  std::string featName_;
  int fid_;
  bool alignedOnly_;
  bool absValue_;
  bool positive_;
  bool negative_;
  bool lexicalized_;
  bool invertSrc_;
  bool invertTgt_;
  std::tr1::unordered_set<WordID> srcVocab_;
  std::tr1::unordered_set<WordID> tgtVocab_;
  
  std::string alignedName_;
  std::string acrossName_;

  // class-based stuff
  bool emit_classes_;
  bool emit_lex_;
  bool emit_mixed_;
  ClassMapper class_map_f_;
  ClassMapper class_map_e_;
  std::string alignedClassName_;
  std::string alignedMixedName_;
  std::string acrossClassName_;
  std::string acrossMixedName_;
};

#endif
