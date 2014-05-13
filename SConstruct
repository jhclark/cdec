#!/usr/bin/python
import check_boost

# EXPERIMENTAL and HACKY version of cdec build in scons

# TODO: Persist these so that you don't have to specify flags every time
# http://www.scons.org/wiki/SavingVariablesToAFile
AddOption('--prefix', dest='prefix', type='string', nargs=1, action='store', metavar='DIR', 
		      help='installation prefix')
AddOption('--with-boost', dest='boost', type='string', nargs=1, action='store', metavar='DIR',
                  help='boost installation directory (if in a non-standard location)')
AddOption('--with-glc', dest='glc', type='string', nargs=1, action='store', metavar='DIR',
                  help='path to Global Lexical Coherence package (optional)')
AddOption('--with-mpi', dest='mpi', action='store_true',
                  help='build tools that use Message Passing Interface? (optional)')
AddOption('--efence', dest='efence', action='store_true',
                  help='use electric fence for debugging memory corruptions')
AddOption('--no-opt', dest='debug', action='store_true',
          help='turn off optimization so that we dont remove useful debugging information')

# Use user's environment?
import os

platform = ARGUMENTS.get('OS', Platform())
# Need vest for dtree
include = Split('decoder utils klm mteval training vest .') #dtree
env = Environment(ENV = os.environ, # Not very scons-like... just import the whole environment
                  PREFIX=GetOption('prefix'),
                  PLATFORM = platform,
#                 BINDIR = bin,
#                 INCDIR = include,
#                 LIBDIR = lib,
                  CPPPATH = include,
                  LIBPATH = [],
                  LIBS = Split('boost_program_options boost_serialization boost_thread z'),
#                 LINKFLAGS = "--static",
		  CCFLAGS=Split('-g -DHAVE_SCONS --static -DJLM_REAL_VALUES'),
		  CXXFLAGS=Split('-std=c++11'))

# ---- check for environment variables
#if 'PATH' in os.environ:
#    env.Replace(PATH = os.environ['PATH'])
#    print(">> Using custom path " + os.environ['PATH'])

#if 'LD_LIBRARY_PATH' in os.environ:
#    env.Replace(LD_LIBRARY_PATH = os.environ['LD_LIBRARY_PATH'])
#    print(">> Using custom library path " + os.environ['LD_LIBRARY_PATH'])

#if 'CC' in os.environ:
#    env.Replace(CC = os.environ['CC'])
#    print(">> Using C compiler " + os.environ['CC'])

#if 'CCFLAGS' in os.environ:
#    env.Append(CCFLAGS = os.environ['CCFLAGS'])
#    print(">> Appending custom CCFLAGS " + os.environ['CCFLAGS'])

#if 'CXX' in os.environ:
#    env.Replace(CXX = os.environ['CXX'])
#    print(">> Using C++ compiler " + os.environ['CXX'])

#if 'CXXFLAGS' in os.environ:
#    env.Append(CCFLAGS = os.environ['CXXFLAGS'])
#    print(">> Appending custom CXX flags: " + os.environ['CXXFLAGS'])
    
#if 'LDFLAGS' in os.environ:
#    env.Append(LINKFLAGS = os.environ['LDFLAGS'])
#    print(">> Appending custom link flags: " + os.environ['LDFLAGS'])

## HACK for Trestles
#env['CC'] = '/opt/gnu/gcc/bin/gcc'
#env['CXX'] = '/opt/gnu/gcc/bin/g++'

import os.path
import shutil
if not os.path.exists('config.h'):
    shutil.copy('config.h.scons', 'config.h')

if GetOption('debug'):
    env.Append(CCFLAGS=Split('-O0'),
               LIBS=Split("SegFault"))
else:
    env.Append(CCFLAGS=Split('-O3'))

import os
if os.path.exists('colorgcc.pl'):
    path = os.path.abspath('colorgcc.pl')
    print('Found colorgcc at ' + path)
    env['CC'] = path
    env['CXX'] = path
else:
    print('colorgcc not found')

# Do some autoconf-like sanity checks (http://www.scons.org/wiki/SconsAutoconf)
conf = Configure(env, custom_tests = {'CheckBoost' : check_boost.CheckBoost})
print('Checking if the environment is sane...')
if not conf.CheckCXX():
    print('!! Your compiler and/or environment is not correctly configured (CXX not found).')
    print('CXX is ' + env['CXX'])
    #Exit(1)
if not conf.CheckFunc('printf'):
    print('!! Your compiler and/or environment is not correctly configured (printf unusable).')
    #Exit(1)
#env = conf.Finish()

boost = GetOption('boost')
if boost:
   print 'Using Boost at {0}'.format(boost)
   env.Append(CCFLAGS='-DHAVE_BOOST',
              LINKFLAGS=('-Wl,-rpath '+boost+'/lib').split(),
              CPPPATH=boost+'/include',
	      LIBPATH=boost+'/lib')

# Check boost version (older versions have problems with program options)
if not conf.CheckBoost('1.46'):
    print('Boost version >= 1.46 needed')
    #Exit(1)

if not conf.CheckLib('boost_program_options'):
   print "Boost library 'boost_program_options' not found"
   #Exit(1)
#if not conf.CheckHeader('boost/math/special_functions/digamma.hpp'):
#   print "Boost header 'digamma.hpp' not found"
#   Exit(1)

mpi = GetOption('mpi')
if mpi:
   if not conf.CheckHeader('mpi.h'):
      print "MPI header 'mpi.h' not found"
      Exit(1)   

if GetOption('efence'):
   env.Append(LIBS=Split('efence')) #Segfault (as a lib)

print('Environment is sane.')
print

srcs = []

# TODO: Get rid of config.h

glc = GetOption('glc')
if glc:
   print 'Using Global Lexical Coherence package at {0}'.format(glc)
   env.Append(CCFLAGS='-DHAVE_GLC',
	      CPPPATH=[glc, glc+'/cdec'])
   srcs.append(glc+'/string_util.cc')
   srcs.append(glc+'/sys_util.cc')
   srcs.append(glc+'/debug.cc')
   srcs.append(glc+'/feature-factory.cc')
   srcs.append(glc+'/cdec/ff_glc.cc')

# Run LEX *before* detecting .cc files
# see http://www.scons.org/doc/0.96.90/HTML/scons-user/a5264.html (CFile)
# see http://www.scons.org/doc/1.2.0/HTML/scons-user/a4774.html (LEXFLAGS)
print 'Ruing LEX'
env.Append(LEXFLAGS="-s -CF -8")
#flex -s -CF -8 -o decoder/rule_lexer.cc decoder/rule_lexer.l
env.CFile(target='decoder/rule_lexer.cc', source="decoder/rule_lexer.l")

for pattern in ['decoder/*.cc', 'decoder/*.c', 'klm/*/*.cc', 'utils/*.cc', 'mteval/*.cc', 'dpmert/*.cc']: # 'dtree/*.cc', 
    srcs.extend([ file for file in Glob(pattern)
    		       if not 'test' in str(file)
		       	  and 'build_binary.cc' not in str(file)
                          and 'atools.cc' not in str(file)
			  and 'ngram_query.cc' not in str(file)
			  and 'mbr_kbest.cc' not in str(file)
			  and 'sri.cc' not in str(file)
			  and 'fast_score.cc' not in str(file)
                          and 'cdec.cc' not in str(file)
                          and 'mr_' not in str(file)
                          and 'extract_topbest.cc' not in str(file)
                          and 'utils/ts.cc' != str(file)
                          and 'utils/phmt.cc' != str(file)
                          and 'utils/reconstruct_weights.cc' != str(file)
		]) #                           and 'dtree.cc' not in str(file)
srcs.append('training/optimize.cc')

print 'Found {0} source files'.format(len(srcs))
def comb(cc, srcs):
   x = [cc]
   x.extend(srcs)
   return x

env.Program(target='decoder/cdec', source=comb('decoder/cdec.cc', srcs))

# TODO: The various decoder tests
# TODO: extools
env.Program(target='klm/lm/build_binary', source=comb('klm/lm/build_binary.cc', srcs))
# TODO: klm ngram_query and tests
env.Program(target='mteval/fast_score', source=comb('mteval/fast_score.cc', srcs))
env.Program(target='mteval/mbr_kbest', source=comb('mteval/mbr_kbest.cc', srcs))
#env.Program(target='mteval/scorer_test', source=comb('mteval/fast_score.cc', srcs))
# TODO: phrasinator

# PRO Trainer
env.Program(target='pro-train/mr_pro_map', source=comb('pro-train/mr_pro_map.cc', srcs))
env.Program(target='pro-train/mr_pro_reduce', source=comb('pro-train/mr_pro_reduce.cc', srcs))

# TODO: Various training binaries
env.Program(target='training/model1', source=comb('training/model1.cc', srcs))
env.Program(target='training/augment_grammar', source=comb('training/augment_grammar.cc', srcs))
env.Program(target='training/grammar_convert', source=comb('training/grammar_convert.cc', srcs))
#env.Program(target='training/optimize_test', source=comb('training/optimize_test.cc', srcs))
env.Program(target='training/collapse_weights', source=comb('training/collapse_weights.cc', srcs))
#env.Program(target='training/lbfgs_test', source=comb('training/lbfgs_test.cc', srcs))
#env.Program(target='training/mr_optimize_reduce', source=comb('training/mr_optimize_reduce.cc', srcs))
env.Program(target='training/mr_em_map_adapter', source=comb('training/mr_em_map_adapter.cc', srcs))
env.Program(target='training/mr_reduce_to_weights', source=comb('training/mr_reduce_to_weights.cc', srcs))
env.Program(target='training/mr_em_adapted_reduce', source=comb('training/mr_em_adapted_reduce.cc', srcs))

# LINKFLAGS='-all-static' appears to be gone as of gcc 4.6
# It was only ever a libtool option, in truth
env.Program(target='dpmert/sentserver', source=['dpmert/sentserver.c'], LIBS=['pthread'])
env.Program(target='dpmert/sentclient', source=['dpmert/sentclient.c'], LIBS=['pthread'])
env.Program(target='dpmert/mr_dpmert_generate_mapper_input', source=comb('dpmert/mr_dpmert_generate_mapper_input.cc', srcs))
env.Program(target='dpmert/mr_dpmert_map', source=comb('dpmert/mr_dpmert_map.cc', srcs))
env.Program(target='dpmert/mr_dpmert_reduce', source=comb('dpmert/mr_dpmert_reduce.cc', srcs))

env.Program(target='utils/atools', source=comb('utils/atools.cc', srcs))

# Decision tree stuffs
#env.Program(target='dtree/dtree', source=comb('dtree/dtree.cc', srcs))
#env.Program(target='dtree/extract_topbest', source=comb('dtree/extract_topbest.cc', srcs))
#env.Program(target='dpmert/lo_test', source=comb('dpmert/lo_test.cc', srcs))
# TODO: util tests

if mpi:
   env.Program(target='training/mpi_online_optimize', source=comb('training/mpi_online_optimize.cc', srcs))
   env.Program(target='training/mpi_batch_optimize', source=comb('training/mpi_batch_optimize.cc', srcs))
   env.Program(target='training/compute_cllh', source=comb('training/compute_cllh.cc', srcs))
   env.Program(target='training/cllh_filter_grammar', source=comb('training/cllh_filter_grammar.cc', srcs))

