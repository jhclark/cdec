#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;

#include "vector_util.h"

struct _DPState {
  float pathCost;
  float estCost;
  int curBinStart;
  int prevBin;
  int backpointer;
};
typedef struct _DPState State;

bool state_cmp(const State& a, const State& b) {
  return a.pathCost > b.pathCost;
}

inline float avg(const vector<float>& weights, int a, int b) {
    float sum = 0.0;
    for(int i=a; i<=b; ++i) {
      sum += weights[i];
    }
    float av = sum / (b-a);
    return av;
}

float cost(const vector<float>& weights, int a, int b) {
  if(a == b) {
    return 0;
  } else {
    float av = avg(weights, a, b);
    
    // compute squared distance
    float result = 0.0;
    for(int i=a; i<=b; ++i) {
      float w = weights[i];
      float x = (av-w);
      result += x * x;
    }
    return result;
  }
}

void search(const int start,
	    const int end,
	    const vector<float>& weights,
	    const int MAX_BINS,
	    const int K,
	    vector<vector<vector<State> > >& dp) {

  for(int i = 0; i < weights.size(); ++i) {
    cerr << "Searching at weight " << i << endl;
    if(i == 0) {
      dp.at(0).push_back(vector<State>(1));
      dp.at(0).at(0).push_back(State());
      State& initState = dp.at(0).at(0).at(0);
      initState.pathCost = 0;
      initState.estCost = 0;
      initState.curBinStart = 0;
      initState.prevBin = -1;
      initState.backpointer = -1;
    } else {
      for(int binCount = 0; binCount < dp.at(i-1).size(); ++binCount) {
	while(dp.at(i).size() <= binCount) {
	  dp.at(i).push_back(vector<State>());
	  dp.at(i).back().reserve(K);
	}
	for(int j=0; j<dp.at(i-1).at(binCount).size(); ++j) { // prev backpointer
	  State& prevState = dp.at(i-1).at(binCount).at(j);

	  // first, try making a new bin
	  if(binCount < MAX_BINS-1) {
	    while(dp.at(i).size() <= binCount+1) {
	      dp.at(i).push_back(vector<State>());
	      dp.at(i).back().reserve(K);
	    }
	    dp.at(i).at(binCount+1).push_back(State());
	    State& newState = dp.at(i).at(binCount+1).back();
	    if(i < weights.size()-1) {
              newState.pathCost = prevState.pathCost + prevState.estCost;
              newState.estCost = cost(weights, i, i);
            } else {
              newState.pathCost = prevState.pathCost + prevState.estCost + cost(weights, i, i);
              newState.estCost = 0;
            }
            newState.curBinStart = i;
            newState.prevBin = binCount;
            newState.backpointer = j;
          }

	  // now, try growing the current bin
	  {
	    dp.at(i).at(binCount).push_back(State());
	    State& extState = dp.at(i).at(binCount).back();
	    if(i < weights.size() - 1) {
              extState.pathCost = prevState.pathCost;
              extState.estCost = cost(weights, prevState.curBinStart, i);
            } else {
              extState.pathCost = prevState.pathCost + cost(weights, prevState.curBinStart, i);
              extState.estCost = 0;
            }
            extState.curBinStart = prevState.curBinStart;
            extState.prevBin = prevState.prevBin;
            extState.backpointer = prevState.backpointer;
	  }
	}
      }

      cerr << i << endl;
      for(int binCount=0; binCount < dp.at(i-1).size(); ++binCount) {
	// TODO: Switch to heap?
	//states[binCount] = sorted(states[binCount], key=lambda x: x[0]+x[1])[:K]
	vector<State>& statesForBin = dp.at(i-1).at(binCount);
	sort(statesForBin.begin(), statesForBin.end(), state_cmp);
	if(statesForBin.size() > K) {
	  statesForBin.resize(K);
	}
	cerr << "Hypothesized " << statesForBin.size() << " states for bin " << binCount << " (beamed to " << K << ") at " << i << endl;
      }
    }
  } // for i
}

// binnedWeights must be pre-sized
void backtrace(int i,
	       int binCount,
	       int backpointer,
	       const vector<float>& weights,
	       const vector<vector<vector<State> > >& dp,
	       vector<float>& binnedWeights) {
  const State& state = dp.at(i).at(binCount).at(backpointer);
  float binWeight = avg(weights, state.curBinStart, i+1);
  // TODO: XXX: In the optimizer, this will actually come out to the SUM!!!
  for(int j=state.curBinStart; j<i+1; ++j) {
    binnedWeights[j] = binWeight;
  }
  if(state.curBinStart != 0) {
    backtrace(state.curBinStart-1, state.prevBin/*Count*/, state.backpointer, weights, dp, binnedWeights);
  }
}

/*
def backtrace(i, binCount, backpointer):
    (pathCost, costEst, curBinStart, nextBP) = dp[i][binCount][backpointer]
    #print 'backtrace', i, backpointer, '--', binCount, pathCost, costEst, curBinStart, nextBinBP, nextIdxBP
    binSpan = i - curBinStart + 1
    origBin = weights[curBinStart:i+1]
    binWeight = avg(origBin)
    #print 'span/weight', binSpan, binWeight, weights[curBinStart:i+1]
    print >>sys.stderr, 'Bin:',curBinStart, i, '--', origBin[:10], '->', binWeight
    if curBinStart == 0:
        return [binWeight]*binSpan
    else:
        (nextBinCount, nextIdx) = nextBP
        return backtrace(curBinStart-1, nextBinCount, nextIdx) + [binWeight]*binSpan
*/


// let's write a dynamic program for re-binning weights
int main(int argc, char** argv) {
  vector<float> weights;
  weights.push_back(0.1);
  weights.push_back(0.2);
  weights.push_back(0.1);
  weights.push_back(0.1);
  weights.push_back(0.16);
  weights.push_back(0.2);
  weights.push_back(0.25);
  weights.push_back(0.25);
  weights.push_back(0.3);
  weights.push_back(0.25);

  if(argc != 3) {
    cerr << "Usage: " << argv[0] <<" k max_bins" << endl;
    exit(1);
  }

  int K = atoi(argv[1]); // 2
  int maxBins = atoi(argv[2]); // 10

  cerr << "K=" << K << endl;
  cerr << "MAX_BINS=" << maxBins << endl;
  cerr << "Orig Weights: " << weights << endl;

  // has [I][binCount]->(pathCost, estCost, curBinStart, backpointer)
  // dimensions: [weights.size()][MAX_BINS][K];
  vector<vector<vector<State> > > dp;
  dp.reserve(weights.size());
  for(int i=0; i<weights.size(); ++i) {
    dp.push_back(vector<vector<State> >());
    dp.back().reserve(maxBins);
  }
  
  search(0, weights.size(), weights, maxBins, K, dp);

  for(int binCount=0; binCount < dp.back().size(); ++binCount) {
    // TODO: Sort by path cost
    // TODO: This loop actually only selects the best
    //for(int selfpointer=0; selfpointer<dp.back().size(); ++selfpointer) {
    {
      int selfpointer=0;
      const State& state = dp.back().at(binCount).at(selfpointer);
      cerr << "Bin Count: " << binCount << "; " << state.pathCost << endl;
      
      vector<float> binnedWeights;
      binnedWeights.reserve(weights.size());
      binnedWeights.resize(weights.size());
      backtrace(dp.size()-1, binCount, selfpointer, weights, dp, binnedWeights);
      if(binCount == maxBins-1) {
	for(int i=0; i<weights.size(); ++i) {
	  cout << /*feats.at(i) <<*/ "XXX," << binnedWeights.at(i) << endl;
	}
      }
    }
  }

  /*
  for binCount in xrange(MAX_BINS):
    for ((pathCost, costEst, curBinStart, backpointer), selfpointer) in sorted(zip(dp[-1][binCount], range(len(dp[-1]))))[:1]:
        print >>sys.stderr, "Bin Count: {0};  Final cost: {1}".format(binCount, pathCost)
        bt = backtrace(len(dp)-1, binCount, selfpointer)
        #print "Weights: {0}".format(bt)
        print >>sys.stderr
        if binCount == MAX_BINS-1:
            binsOnly = True
            if not binsOnly:
                # Print a line for each original point
                for (feat, weight) in zip(feats, bt):
                    print "%s,%f"%(feat,weight)
            else:
                bins = []
                prevWeight = None
                for (feat, weight) in zip(feats, bt):
                    if weight != prevWeight:
                        bins.append( (feat, weight) )
                        prevWeight = weight
                # Just print the bins
                for (feat, weight) in bins:
                    print "%s,%f"%(feat,weight)
  */

  
  return 0;
}
