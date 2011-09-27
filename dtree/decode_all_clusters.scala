import System._
import sys.process._
import collection._
import java.io._

if(args.length == 0 || args.length % 2 != 0) {
  println("""
Usage: 

-i input source sentences
-o output directory
-d decoder command
-c decoder config
""")
  exit(1)
}

val opts: Map[String,String] = { for(i <- 0 until args.length by 2) yield (args(i), args(i+1)) }.toMap
val sents = new File(opts("-i"))
val outdir = opts("-o")
val decodercmd = opts("-d")
val decoderconfig = opts("-c")

// K=1000; #0: 0; dir=164 step=-0.97086; weights: Glue=-0.538922 LanguageModel=-0.292076 PassThrough=-20.2897 PhraseModel_0=-0.421356 PhraseModel_1=0.036835 PhraseModel_2=-0.764953 WordPenalty=-1.3378
val PAT = """K=([0-9]+); #([0-9]+): ([0-9 ]+); dir=([0-9]+) step=([0-9-.]+); weights: (.+)""".r

def write(file: File, str: String) = {
  val fw = new FileWriter(file)
  fw.write(str)
  fw.close()    
}

err.println("Reading clusters from stdin...")
val q = new mutable.ListBuffer[()=>Unit]
for(line <- io.Source.stdin.getLines) {
  line match {
    case PAT(k, i, strSents, dir, step, weights) => {
      val weightsFormatted = weights.replace(' ', '\n').replace('=',' ')
      val weightsfile = new File("%s/k%s-i%s.weights".format(outdir, k,i))
      write(weightsfile, weightsFormatted)
      
      val cmd = "%s -c %s -w %s -k1".format(decodercmd, decoderconfig, weightsfile)
      val outfile = new File("%s/k%s-i%s.1best".format(outdir, k,i))
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
