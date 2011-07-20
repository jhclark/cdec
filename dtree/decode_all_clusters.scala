import System._
import sys.process._
import collection.mutable._
import java.io._

// K=1000; #0: 0; dir=164 step=-0.97086; weights: Glue=-0.538922 LanguageModel=-0.292076 PassThrough=-20.2897 PhraseModel_0=-0.421356 PhraseModel_1=0.036835 PhraseModel_2=-0.764953 WordPenalty=-1.3378
val PAT = """K=([0-9]+); #([0-9]+): ([0-9 ]+); dir=([0-9]+) step=([0-9-.]+); weights: (.+)""".r

def write(file: File, str: String) = {
  val fw = new FileWriter(file)
  fw.write(str)
  fw.close()    
}

err.println("Reading clusters from stdin...")
val q = new ListBuffer[()=>Unit]
for(line <- io.Source.stdin.getLines) {
  line match {
    case PAT(k, i, strSents, dir, step, weights) => {
      val weightsFormatted = weights.replace(' ', '\n').replace('=',' ')
      val weightsfile = new File("/home/jhclark/emnlp/de-en-acl/test/k%s-i%s.weights".format(k,i))
      write(weightsfile, weightsFormatted)
      
      val cmd = "/home/jhclark/workspace/cdec/decoder/cdec -c /home/jhclark/emnlp/de-en-acl/test/cdec.ini -w %s -k1".format(weightsfile)
      val sents = new File("/home/jhclark/emnlp/de-en-acl/test/test2010.lc.de")
      val outfile = new File("/home/jhclark/emnlp/de-en-acl/test/k%s-i%s.1best".format(k,i))
      err.println("Queuing: %s < %s > %s".format(cmd, sents, outfile))
      
      val func = () => {
        err.println("Running: %s < %s > %s".format(cmd, sents, outfile))
        val eCode: Int = cmd #< sents #> outfile ! ;
        if(eCode != 0) throw new RuntimeException("Command returned non-zero: %s < %s > %s".format(cmd, sents, outfile))
      }
      q += func
    }
    case _ => ;
  }
}

err.println("Queued %d tasks. Running them...".format(q.size))
q.par.foreach(func => func())
err.println("Completed all")

// TODO: Interleave 1-best lists and use MultEval to determine oracle scores
