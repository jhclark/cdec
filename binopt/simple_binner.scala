#!/bin/sh
export JAVA_OPTS="-Xmx8g"
exec scala -nowarn -nocompdaemon -savecompiled "$0" "$@"
!#

import System._
import collection._
import io._
import annotation._

val DEBUG = false
val PROFILE = false
val maxBins = args(0).toInt

err.println("Heap size: " + Runtime.getRuntime.maxMemory/1000/1000 + "MB")

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

// even though we store bins in a Seq, neighboring bins may not eventually be sequential
// due to overlapping, etc.
// so we store neighbors here
class Bin(val contents: Seq[FeatInfo], val overlap: Int,
          // values to be used when generalizing to new data -- filled in by generalizeBins()
          val effectiveVals: Option[(Float,Float)] = None,
          val prevBin: Option[Bin] = None, val nextBin: Option[Bin] = None) {
  assert(contents.size > 0, "contents are empty")
  val name: String = contents.head.name

  val beginVal: Float = contents.head.value
  val endVal: Float = contents.last.value

  effectiveVals match {
    case Some( (begin, end) ) => {
      assert(begin <= end, "Invalid effective vals. Wanted begin %f <= end %f".format(begin, end))
      assert(begin <= beginVal, "Invalid effective vals. Wanted begin %f <= beginVal %f".format(begin, beginVal))
      assert(end >= endVal, "Invalid effective vals. Wanted end %f >= endVal %f".format(end, endVal))
    }
    case None => ;
  }

  // throws if effectiveVals is None
  def effectiveBegin: Float = effectiveVals.get._1
  def effectiveEnd: Float = effectiveVals.get._2

  lazy val condition: Option[String] = effectiveVals match {
    case Some( (begin, end) ) => {
      val (strBegin, strEnd) = formatConditionRange(this)
      Some("%s <= x < %s".format(strBegin, strEnd))
    }
    case None => None
  }

  val origCount: Int = contents.map(_.origCount).sum
  val adjCount: Int = contents.map(_.adjCount).sum

  def withEffectiveRange(begin: Float, end: Float): Bin = {
    new Bin(contents, overlap, Some(begin, end), prevBin, nextBin)
  }

  def withNeighbors(prev: Option[Bin], next: Option[Bin]): Bin = {
    new Bin(contents, overlap, effectiveVals, prev, next)
  }

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

def time[A](task: String, alwaysShow: Boolean = false)(block: => A): A = {
  val t0 = System.nanoTime
  val result: A = block
  val t1 = System.nanoTime
  val timeMillis = (t1 - t0) / 1000 / 1000
  if (PROFILE || alwaysShow) err.println("%s took %dms".format(task, timeMillis))
  result
}

def induceBins(name: String, rawFeatInfo: Seq[FeatInfo]): Seq[Bin] = {
  if (DEBUG) err.println("Binning feature %s".format(name))

  // 1) Iteratively reduce the size of overly large bins such that they fill exactly one uniformly sized bin
  //    (this is the real magic)
  if (!DEBUG) err.print("%s: Adjusting bin sizes".format(name))
  val (featInfo: Seq[FeatInfo], effectiveBinCount: Int) = time("Adjusting bin sizes", alwaysShow=true) {
    adjust(rawFeatInfo)
  }
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
  if (!DEBUG) err.println("%s: Binning".format(name))
  val bins: Seq[Bin] = time("Inducing bins", alwaysShow=true) {
    for (i <- 0 until effectiveBinCount) yield {
      time("Bin %d/%d".format(i+1, effectiveBinCount)) {
        desiredCount += uniformBinSize
        
        if (DEBUG) err.println("Filling bin %d; desired (cumulative) count is %d; current count is %d".format(i, desiredCount, curCount))
        val remainingBins = effectiveBinCount - i - 1
        var countToAdd: Int = 0
        var countRemaining: Int = remainingItems.size
        def addItemToBin() {
          val item = remainingItems(countToAdd)
          countToAdd += 1
          countRemaining -= 1
          curCount += item.adjCount
          if (DEBUG) err.println("Remaining items = %d; bins = %d".format(countRemaining, remainingBins))
        }
        
        time("Finding boundary") {
          while (countToAdd == 0 || 
                 (countRemaining > 0 &&
                  curCount + remainingItems(countToAdd).adjCount < desiredCount &&
                  countRemaining > remainingBins)) {
                 //var curCount = 0
                 //while (curCount < uniformBinSize && remainingItems.size > remainingBins && remainingItems.size > 0) {
            addItemToBin()
          }
        }

        // special case for items that will straddle bin boundaries:
        // put it on whichever side of the boundary will cause it to violate the bin boundary *less*
        // i.e. the excess should be less than 50%
        if (countRemaining > 0 && countRemaining > remainingBins) {
          val nextItemCount = remainingItems(countToAdd).adjCount
          val nextItemExcessCount: Int = (curCount + nextItemCount) - desiredCount
          val nextItemExcessPct: Float = nextItemExcessCount.toFloat / nextItemCount.toFloat
          if (DEBUG) err.println("Next item count: %d; excess count = %d (%f pct)".format(nextItemCount, nextItemExcessCount, nextItemExcessPct))
          if (nextItemExcessPct < 0.5) {
            addItemToBin()
          }
        }
        
        val bin: Seq[FeatInfo] = time("Copying items") {
          val result = remainingItems.take(countToAdd)
          remainingItems = remainingItems.drop(countToAdd)
          result
        }
        
        if (remainingItems.isEmpty && bin.isEmpty) {
          throw new RuntimeException("Ran out of elements")
        }
        
        if (DEBUG || PROFILE) err.println("Completed bin %d/%d. curCount: %d; desiredCount: %d".format(i+1, effectiveBinCount, curCount, desiredCount))
        //    if (!DEBUG) {
        //      err.print("%d/%d ".format(i+1,effectiveBinCount))
        //      err.flush()
        //    }
        
        new Bin(bin, overlap=0)
      }
    }
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
val allFeatInfo: Seq[FeatInfo] = time("Reading input", alwaysShow=true) { 
  Source.stdin.getLines.toList.map { line =>
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

// generalize bin boundaries by expanding them to touch neighboring bins
def generalizeBins(bins: Seq[Bin]): Seq[Bin] = {
  bins.zipWithIndex.map { case (bin: Bin, i: Int) =>
    assert(bin.overlap == 0)
    val isFirstBin = i == 0
    val isLastBin = i == (bins.size - 1)

    val myBeginVal: Float = if (isFirstBin) {
      Float.NegativeInfinity
    } else {
      bin.beginVal
    }
    val nextBeginVal: Float = if (isLastBin) {
      Float.PositiveInfinity
    } else {
      bins(i+1).beginVal
    }
    bin.withEffectiveRange(myBeginVal, nextBeginVal)
  }
}

// inform bins about their neighbors' effective ranges
// this allows us to serialize bins with appropriate precision
// note: this has nothing to do with neighbor regularization
//       it is for serialization only
def propagateNeighbors(bins: Seq[Bin]): Seq[Bin] = {
  bins.zipWithIndex.map { case (bin: Bin, i: Int) =>
    assert(bin.overlap == 0)
    val isFirstBin = i == 0
    val isLastBin = i == (bins.size - 1)
    val prevBin: Option[Bin] = if (isFirstBin) None else Some(bins(i-1))
    val nextBin: Option[Bin] = if (isLastBin) None else Some(bins(i+1))
    bin.withNeighbors(prevBin, nextBin)
  }  
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
        val effectiveVals: Option[(Float, Float)] = Some( (window.head.effectiveBegin, window.last.effectiveEnd) )
        new Bin(combinedContents, overlap=i, effectiveVals=effectiveVals)
      }
    }
  }
}

// formats value with the minimal precision necessary to ensure that its string format
// is not equivalent to either of its neighboring values
@tailrec def formatRangeValue(prevOpt: Option[Float], value: Float, nextOpt: Option[Float], prec: Int = 0): String = {
  val formatStr = "%."+prec+"f"
  val strPrev = prevOpt match {
    case None => ""
    case Some(prev) => formatStr.format(prev)
  }
  val strValue = formatStr.format(value)
  val strNext = nextOpt match {
    case None => ""
    case Some(next) => formatStr.format(next)
  }
  if (strPrev == strValue || strValue == strNext) {
    if (prec > 100)
      throw new RuntimeException("Really? You need precision > 100? That's probably a bug. ('%s','%s','%s')".format(strPrev, strValue, strNext))
    formatRangeValue(prevOpt, value, nextOpt, prec+1)
  } else {
    strValue
  }
}

def formatFeatureRange(bin: Bin): String = {
  if (bin.beginVal == bin.endVal) {
    formatRangeValue(bin.prevBin.map(_.effectiveBegin), bin.effectiveBegin, bin.nextBin.map(_.effectiveBegin))
  } else {
    val strLow = formatRangeValue(bin.prevBin.map(_.effectiveBegin), bin.effectiveBegin, bin.nextBin.map(_.effectiveBegin))
    val strHigh = formatRangeValue(bin.prevBin.map(_.effectiveEnd), bin.effectiveEnd, bin.nextBin.map(_.effectiveEnd))
    "%s_%s".format(strLow, strHigh)
  }
}

def formatConditionRange(bin: Bin): (String, String) = {
  val strLow = formatRangeValue(bin.prevBin.map(_.effectiveBegin), bin.effectiveBegin, bin.nextBin.map(_.effectiveBegin))
  val strHigh = formatRangeValue(bin.prevBin.map(_.effectiveEnd), bin.effectiveEnd, bin.nextBin.map(_.effectiveEnd))
  (strLow, strHigh)
}

// Do this for each feature
allFeatInfo.groupBy(_.name).foreach { case (name, list) =>
  val binned: Seq[Bin] = makeOverlaps(
                           propagateNeighbors(
                             generalizeBins(
                               induceBins(name, list))), overlaps)
  assert(binned.size > 0, "zero bins?!")

  binned.foreach { bin: Bin =>
    val origFeatName = bin.name
    if (plotFormat) {
      val range: String = formatFeatureRange(bin)
      println("%s %s %d".format(origFeatName, range, bin.origCount))
    } else {

      val operator = "bin" // as opposed to "conjoin"
      val destFeatName = {
        val overlapSpec = "_Overlap%d".format(bin.overlap)
        val range: String = formatFeatureRange(bin)
        "%s_%s%s".format(bin.name, range, overlapSpec)
      }
      println("%s %s %s %s [ %s ]".format(operator, origFeatName, destFeatName, bin.condition.get, bin.toString))
    }
  }
  //println()
}
