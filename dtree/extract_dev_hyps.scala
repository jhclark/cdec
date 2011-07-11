import System._
import sys.process._

// K=1000; #0: 0; dir=164 step=-0.97086; weights: Glue=-0.538922 LanguageModel=-0.292076 PassThrough=-20.2897 PhraseModel_0=-0.421356 PhraseModel_1=0.036835 PhraseModel_2=-0.764953 WordPenalty=-1.3378
val hgDir = args(0)
val PAT = """K=([0-9]+); #([0-9]+): ([0-9 ]+); dir=([0-9]+) step=([0-9-.]+); weights: (.+)""".r

val translations = new Array[String](1000)
for(line <- io.Source.stdin.getLines) {
  line match {
    case PAT(k, i, strSents, dir, step, weights) => {
      val weightsFormatted = weights.replace(' ',';')
      val sents = List.fromString(strSents, ' ')
      for(sent <- sents) {
        val cmd = "extract_topbest -w '%s' -i %s/%s.json.gz".format(weightsFormatted, hgDir, sent)
        err.println(cmd)
        val result: String = cmd !! ;
        val lastLine = List.fromString(result, '\n').map(_.trim()).filter(_.startsWith("Viterbi:"))(1)

        val iSent = sent.toInt
        val best = lastLine.replaceFirst("Viterbi: ", "")
        err.println("%d: %s".format(iSent, best))
        translations(iSent) = best
      }
    }
    case _ => ;
  }
}

for(trans <- translations) {
  println(trans)
}
