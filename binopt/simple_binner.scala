#!/bin/sh
exec scala "$0" "$@"
!#

import System._
import collection._
import io._
import annotation._

val DEBUG = true
val maxBins = args(0).toInt

// Note: We give each rule equal weight since they are the basic unit
// of sentence composition that we wish to allow the optimizer to select among

// Use feats2histogram.sh to convert into a histogram format
// that this script expects on stdin:
//   CountEF 1.0 677
//   CountEF 2.0 103
//   CountEF 3.0 46
//   CountEF 4.0 24

class FeatInfo(val name: String, val value: Float, val origCount: Int, val adjCount: Int) {
  override def toString() = "%s %f %d [adj=%d]".format(name, value, origCount, adjCount)
}

class Bin(val contents: Seq[FeatInfo]) {
  assert(contents.size > 0, "contents are empty")
  val name: String = contents.head.name
  val beginVal: Float = contents.head.value
  val endVal: Float = contents.last.value
  val origCount: Int = contents.map(_.origCount).sum
  val adjCount: Int = contents.map(_.adjCount).sum
  override def toString() = {
    if (contents.size == 1) {
      "%s".format(contents.head.toString)
    } else {
      "%s %s - %s %d [adj=%d]".format(name, beginVal, endVal, origCount, adjCount)
    }
  }
}

// 1) Determine maximum size any bin can have
//    also adjust number of bins if there are less unique data points than requested bins
// returns (adjustedBins, effectiveBinCount)
@tailrec def adjust(featInfo: Seq[FeatInfo]): (Seq[FeatInfo], Int) = {
  val totalCount = featInfo.map(_.adjCount).sum
  val maxCount = featInfo.map(_.adjCount).max
  val effectiveBinCount: Int = math.min(featInfo.size, maxBins)
  if (DEBUG) err.println("Effective bin count: %d".format(effectiveBinCount))
  val uniformBinSize: Int = math.ceil(totalCount.toFloat / effectiveBinCount.toFloat).toInt

  if (DEBUG) err.println("Adjusting bin sizes (Total count: %d, Max count: %d, Uniform bin size: %d)".
    format(totalCount, maxCount, uniformBinSize))
  if (DEBUG) err.println(featInfo.mkString("\n"))

  var changed = false
  val adjusted: Seq[FeatInfo] = featInfo.map { f =>
    if (f.adjCount > uniformBinSize) {
      changed = true
      new FeatInfo(f.name, f.value, origCount=f.origCount, adjCount=uniformBinSize)
    } else {
      f
    }
  }

  if (DEBUG) err.println(adjusted.mkString("\n"))

  if (changed) {
    adjust(adjusted)
  } else {
    (adjusted, effectiveBinCount)
  }
}

def induceBins(name: String, rawFeatInfo: Seq[FeatInfo]): Seq[Bin] = {
  err.println("Binning feature %s".format(name))

  // 1) Iteratively reduce the size of overly large bins such that they fill exactly one uniformly sized bin
  //    (this is the real magic)
  val (featInfo: Seq[FeatInfo], effectiveBinCount: Int) = adjust(rawFeatInfo)

  // 2) Form bins by adding data points until they're full
  val totalCount = featInfo.map(_.adjCount).sum
  val uniformBinSize: Int = math.ceil(totalCount.toFloat / effectiveBinCount.toFloat).toInt

  if (DEBUG) err.println("TotalCount: %d".format(totalCount))
  if (DEBUG) err.println("Uniform bin size: %d".format(uniformBinSize))
  if (DEBUG) err.println("%d * %d = %d".format(uniformBinSize, effectiveBinCount, uniformBinSize * effectiveBinCount))

  var curCount = 0
  var desiredCount = 0
  var remainingItems: Seq[FeatInfo] = featInfo
  val bins: Seq[Bin] = for (i <- 0 until effectiveBinCount) yield {
    desiredCount += uniformBinSize
    
    if (DEBUG) err.println("Filling bin %d; desired (cumulative) count is %d; current count is %d".format(i, desiredCount, curCount))
    val remainingBins = effectiveBinCount - i - 1
    val bin = new mutable.ArrayBuffer[FeatInfo]
    while (bin.isEmpty || 
           (!remainingItems.isEmpty && curCount + remainingItems.head.adjCount < desiredCount && remainingItems.size > remainingBins)) {
      //var curCount = 0
      //while (curCount < uniformBinSize && remainingItems.size > remainingBins && remainingItems.size > 0) {
        val item = remainingItems.head
      remainingItems = remainingItems.drop(1)
      bin += item
      curCount += item.adjCount
      if (DEBUG) err.println("Remaining items = %d; bins = %d".format(remainingItems.size, remainingBins))
    }
    if (remainingItems.isEmpty && bin.isEmpty) {
      throw new RuntimeException("Ran out of elements")
    }
    new Bin(bin)
  }

  bins
}

err.println("Outut format:")
err.println("FeatureName lowestValInclusive [highestValInclusive] supportingRuleCount")
err.println(" * Parentheses indicate that a range should be extended infinitely to either the left or right when generalizing to unseen data")
err.println(" * Highest bin value in tuning data is shown in square brackets, but is irrelevant for actual bin ranges")

// read the input from stdin (see above for format)
err.println("Reading input from stdin...")
val allFeatInfo: Seq[FeatInfo] = Source.stdin.getLines.toList.map { line =>
  val Array(f,v,c) = line.trim.split(" ")
  new FeatInfo(f, v.toFloat, c.toInt, c.toInt)
}

// Do this for each feature
allFeatInfo.groupBy(_.name).foreach { case (name, list) =>
  val binned: Seq[Bin] = induceBins(name, list)
  assert(binned.size > 0, "zero bins?!")
  binned.zipWithIndex.foreach { case (bin: Bin, i: Int) =>
    // we use parentheses to show how these bins should be interpreted
    // when generalizing from the tuning to the test data)
    val comment = {
      val isFirstBin = i == 0
      val isLastBin = i == (binned.size - 1)
      if (isFirstBin && isLastBin) {
        "x forall x"
      } else if (isLastBin) {
        "%f <= x < inf".format(bin.beginVal)
      } else {
        val nextBeginVal = binned(i+1).beginVal
        if (isFirstBin) {
          "-inf <= x < %f".format(nextBeginVal)
        } else {
          "%f <= x < %f".format(bin.beginVal, nextBeginVal)
        }
      }
    }
    println("%s (%s)".format(bin, comment))
  }
}

// TODO: Now allow extending bins for overlap (multi-layer overlap?)
// TODO: Allow bins to be specified in bits
// TODO: Stump learners (keep values)
