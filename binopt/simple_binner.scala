#!/bin/sh
exec scala -nowarn "$0" "$@"
!#

import System._
import collection._
import io._
import annotation._

val DEBUG = false
val maxBins = args(0).toInt

// comma-separated list of overlap modes
// 0 means non-overlapping; otherwise, the number of bins to extend into
val overlaps: Seq[Int] = args(1).split(",").toList.map(_.toInt)

val plotFormat: Boolean = if (args.length > 2) {
  args(2) == "--plot"
} else {
  false
}
if (plotFormat) err.println("Using plot format...")

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

class Bin(val contents: Seq[FeatInfo], val overlap: Int) {
  assert(contents.size > 0, "contents are empty")
  val name: String = contents.head.name
  val beginVal: Float = contents.head.value
  val endVal: Float = contents.last.value
  val origCount: Int = contents.map(_.origCount).sum
  val adjCount: Int = contents.map(_.adjCount).sum
  override def toString() = {
    val range: String = if (contents.size == 1) "%f".format(beginVal) else "%f - %f".format(beginVal, endVal)
    "%s count=%d adjCount=%d L=%d R=%d".format(range, origCount, adjCount, contents.head.adjCount, contents.last.adjCount)
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

  if (!DEBUG) {
    err.print(".")
    err.flush()
  }

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
  if (DEBUG) err.println("Binning feature %s".format(name))

  // 1) Iteratively reduce the size of overly large bins such that they fill exactly one uniformly sized bin
  //    (this is the real magic)
  if (!DEBUG) err.print("%s: Adjusting bin sizes".format(name))
  val (featInfo: Seq[FeatInfo], effectiveBinCount: Int) = adjust(rawFeatInfo)
  if (!DEBUG) err.println()

  // 2) Form bins by adding data points until they're full
  val totalCount = featInfo.map(_.adjCount).sum
  val uniformBinSize: Int = math.ceil(totalCount.toFloat / effectiveBinCount.toFloat).toInt

  if (DEBUG) err.println("TotalCount: %d".format(totalCount))
  if (DEBUG) err.println("Uniform bin size: %d".format(uniformBinSize))
  if (DEBUG) err.println("%d * %d = %d".format(uniformBinSize, effectiveBinCount, uniformBinSize * effectiveBinCount))

  var curCount = 0
  var desiredCount = 0
  var remainingItems: Seq[FeatInfo] = featInfo
  if (!DEBUG) err.print("%s: Binning".format(name))
  val bins: Seq[Bin] = for (i <- 0 until effectiveBinCount) yield {
    desiredCount += uniformBinSize

    if (!DEBUG) {
      err.print(".")
      err.flush()
    }

    
    if (DEBUG) err.println("Filling bin %d; desired (cumulative) count is %d; current count is %d".format(i, desiredCount, curCount))
    val remainingBins = effectiveBinCount - i - 1
    val bin = new mutable.ArrayBuffer[FeatInfo](100)
    def addItemToBin() {
      val item = remainingItems.head
      remainingItems = remainingItems.drop(1)
      bin += item
      curCount += item.adjCount
      if (DEBUG) err.println("Remaining items = %d; bins = %d".format(remainingItems.size, remainingBins))
    }
    while (bin.isEmpty || 
           (!remainingItems.isEmpty && curCount + remainingItems.head.adjCount < desiredCount && remainingItems.size > remainingBins)) {
      //var curCount = 0
      //while (curCount < uniformBinSize && remainingItems.size > remainingBins && remainingItems.size > 0) {
      addItemToBin()
    }
    // special case for items that will straddle bin boundaries:
    // put it on whichever side of the boundary will cause it to violate the bin boundary *less*
    // i.e. the excess should be less than 50%
    if (!remainingItems.isEmpty && remainingItems.size > remainingBins) {
      val nextItemCount = remainingItems.head.adjCount
      val nextItemExcessCount: Int = (curCount + nextItemCount) - desiredCount
      val nextItemExcessPct: Float = nextItemExcessCount.toFloat / nextItemCount.toFloat
      if (DEBUG) err.println("Next item count: %d; excess count = %d (%f pct)".format(nextItemCount, nextItemExcessCount, nextItemExcessPct))
      if (nextItemExcessPct < 0.5) {
        addItemToBin()
      }
    }
    if (remainingItems.isEmpty && bin.isEmpty) {
      throw new RuntimeException("Ran out of elements")
    }
    if (DEBUG) err.println("Completed bin. curCount: %d; desiredCount: %d".format(curCount, desiredCount))
    new Bin(bin, overlap=0)
  }
  if (!DEBUG) err.println()

  bins
}

err.println("Outut format:")
err.println("FeatureName lowestValInclusive [highestValInclusive] supportingRuleCount")
err.println(" * Parentheses indicate that a range should be extended infinitely to either the left or right when generalizing to unseen data")
err.println(" * Highest bin value in tuning data is shown in square brackets, but is irrelevant for actual bin ranges")

// read the input from stdin (see above for format)
err.println("Reading input from stdin...")
val allFeatInfo: Seq[FeatInfo] = Source.stdin.getLines.toList.map { line =>
  try {
    val Array(f,v,c) = line.trim.split(" ")
    new FeatInfo(f, v.toFloat, c.toInt, c.toInt)
  } catch {
    case e: Exception => {
      err.println("ERROR: Could not parse line: " + line)
      err.println("Exception: %s".format(e.getMessage))
      System.exit(1)
      throw e
    }
  }
}

def dedupe(seq: Seq[Int]): Seq[Int] = {
  val seen = new mutable.HashSet[Int]
  val result = new mutable.ArrayBuffer[Int](seq.size)
  for (i <- 0 until seq.size) {
    val x = seq(i)
    if (!seen(x)) {
      result += x
      seen += x
    }
  }
  result
}

def makeOverlaps(bins: Seq[Bin], overlaps: Seq[Int]): Seq[Bin] = {
  // if an overlap request is greater than the number of bins,
  // round it down to cover all the bins
  // then, remove any duplicate overlap requests
  val mungedOverlaps: Seq[Int] = if (bins.size <= 2) {
    // any overlapping will result in no signal
    // (assuming all rules get some value of this feature)
    Seq(0)
  } else {
    dedupe(
      overlaps.map { i: Int => math.min(i + 1, bins.size) - 1 }
    )
  }

  mungedOverlaps.flatMap { i: Int =>
    if (i == 0) {
      bins
    } else {
      bins.sliding(i+1).map { window: Seq[Bin] =>
        val combinedContents: Seq[FeatInfo] = window.map(_.contents).flatten
        new Bin(combinedContents, overlap=i)
      }
    }
  }
}

// Do this for each feature
allFeatInfo.groupBy(_.name).foreach { case (name, list) =>
  val binned: Seq[Bin] = makeOverlaps(induceBins(name, list), overlaps)
  assert(binned.size > 0, "zero bins?!")
  binned.zipWithIndex.foreach { case (bin: Bin, i: Int) =>
    // leave a comment about how these bins should be interpreted when generalizing to unseen test sets
    val condition = {
      val isFirstBin = i == 0
      val isLastBin = i == (binned.size - 1)
      if (isFirstBin && isLastBin) {
        "-inf <= x < inf"
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

    @tailrec def formatRange(a: Float, b: Float, prec: Int = 0): String = {
      if (bin.beginVal == bin.endVal) {
        "%.1f".format(bin.beginVal)
      } else {
        val formatStr = "%."+prec+"f"
        val begin = formatStr.format(bin.beginVal)
        val end = formatStr.format(bin.endVal)
        if (begin == end) {
          formatRange(a, b, prec+1)
        } else {
          "%s_%s".format(begin, end)
        }
      }
    }

    val origFeatName = bin.name
    if (plotFormat) {
      val range = formatRange(bin.beginVal, bin.endVal)
      println("%s %s %d".format(origFeatName, range, bin.origCount))
    } else {

      val operator = "bin" // as opposed to "conjoin"
      val destFeatName = {
        val overlapSpec = "_Overlap%d".format(bin.overlap)
        val range = if (bin.beginVal == bin.endVal) {
          "%f".format(bin.beginVal)
        } else {
          "%f_%f".format(bin.beginVal, bin.endVal)
        }
        "%s_%s%s".format(bin.name, range, overlapSpec)
      }
      println("%s %s %s %s [ %s ]".format(operator, origFeatName, destFeatName, condition, bin.toString))
    }
  }
  //println()
}

// TODO: Output overlap mode so that feature can be named properly during optimization
// TODO: Now allow extending bins for overlap (multi-layer overlap?)
// TODO: Allow bins to be specified in bits
// TODO: Stump learners (keep values)
// TODO: Be more intelligent at bin boundaries -- or make second pass if *really* necessary
