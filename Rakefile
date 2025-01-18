begin
  require 'rubygems/package_task'
rescue LoadError
end

require 'rbconfig'
include RbConfig

require 'rake/clean'
CLOBBER.include 'doc', 'Gemfile.lock'
CLEAN.include FileList['diagrams/*.*'], 'doc', 'coverage', 'tmp',
  FileList["ext/**/{Makefile,mkmf.log}"], 'build', 'dist', FileList['**/*.rbc'],
  FileList["{ext,lib}/**/*.{so,bundle,#{CONFIG['DLEXT']},o,obj,pdb,lib,manifest,exp,def,jar,class,dSYM}"],
  FileList['java/src/**/*.class']

require 'rake/testtask'
class UndocumentedTestTask < Rake::TestTask
  def desc(*) end
end

which = lambda { |c|
  w = `which #{c}`
  break w.chomp unless w.empty?
}

MAKE   = ENV['MAKE']   || %w[gmake make].find(&which)
BUNDLE = ENV['BUNDLE'] || %w[bundle].find(&which)

PKG_VERSION       = File.foreach(File.join(__dir__, "lib/json/version.rb")) do |line|
  /^\s*VERSION\s*=\s*'(.*)'/ =~ line and break $1
end rescue nil

EXT_ROOT_DIR      = 'ext/json/ext'
EXT_PARSER_DIR    = "#{EXT_ROOT_DIR}/parser"
EXT_PARSER_DL     = "#{EXT_PARSER_DIR}/parser.#{CONFIG['DLEXT']}"
RAGEL_PATH        = "#{EXT_PARSER_DIR}/parser.rl"
EXT_PARSER_SRC    = "#{EXT_PARSER_DIR}/parser.c"
EXT_GENERATOR_DIR = "#{EXT_ROOT_DIR}/generator"
EXT_GENERATOR_DL  = "#{EXT_GENERATOR_DIR}/generator.#{CONFIG['DLEXT']}"
EXT_GENERATOR_SRC = "#{EXT_GENERATOR_DIR}/generator.c"

JAVA_DIR            = "java/src/json/ext"
JAVA_RAGEL_PATH     = "#{JAVA_DIR}/ParserConfig.rl"
JAVA_PARSER_SRC     = "#{JAVA_DIR}/ParserConfig.java"
JAVA_SOURCES        = FileList["#{JAVA_DIR}/*.java"]
JAVA_CLASSES        = []
JRUBY_PARSER_JAR    = File.expand_path("lib/json/ext/parser.jar")
JRUBY_GENERATOR_JAR = File.expand_path("lib/json/ext/generator.jar")

if RUBY_PLATFORM =~ /mingw|mswin/
  # cleans up Windows CI output
  RAGEL_CODEGEN     = %w[ragel].find(&which)
  RAGEL_DOTGEN      = %w[ragel].find(&which)
else
  RAGEL_CODEGEN     = %w[rlcodegen rlgen-cd ragel].find(&which)
  RAGEL_DOTGEN      = %w[rlgen-dot rlgen-cd ragel].find(&which)
end

desc "Installing library (extension)"
task :install => [ :compile ] do
  sitearchdir = CONFIG["sitearchdir"]
  cd 'ext' do
    for file in Dir["json/ext/*.#{CONFIG['DLEXT']}"]
      d = File.join(sitearchdir, file)
      mkdir_p File.dirname(d)
      install(file, d)
    end
    warn " *** Installed EXT ruby library."
  end
end

namespace :gems do
  desc 'Install all development gems'
  task :install do
    sh "#{BUNDLE}"
  end
end

file EXT_PARSER_DL => EXT_PARSER_SRC do
  cd EXT_PARSER_DIR do
    ruby 'extconf.rb'
    sh MAKE
  end
  cp "#{EXT_PARSER_DIR}/parser.#{CONFIG['DLEXT']}", EXT_ROOT_DIR
end

file EXT_GENERATOR_DL => EXT_GENERATOR_SRC do
  cd EXT_GENERATOR_DIR do
    ruby "extconf.rb #{ENV['JSON_GENERATOR_CONFIGURE_OPTS']}"
    sh MAKE
  end
  cp "#{EXT_GENERATOR_DIR}/generator.#{CONFIG['DLEXT']}", EXT_ROOT_DIR
end

file JAVA_PARSER_SRC => JAVA_RAGEL_PATH do
  cd JAVA_DIR do
    if RAGEL_CODEGEN == 'ragel'
      sh "ragel ParserConfig.rl -J -o ParserConfig.java"
    else
      sh "ragel -x ParserConfig.rl | #{RAGEL_CODEGEN} -J"
    end
  end
end

desc "Generate parser with ragel"
task :ragel => [EXT_PARSER_SRC, JAVA_PARSER_SRC]

desc "Delete the ragel generated C source"
task :ragel_clean do
  rm_rf EXT_PARSER_SRC
  rm_rf JAVA_PARSER_SRC
end

if defined?(RUBY_ENGINE) and RUBY_ENGINE == 'jruby'
  ENV['JAVA_HOME'] ||= [
    '/usr/local/java/jdk',
    '/usr/lib/jvm/java-6-openjdk',
    '/Library/Java/Home',
  ].find { |c| File.directory?(c) }
  if ENV['JAVA_HOME']
    warn " *** JAVA_HOME is set to #{ENV['JAVA_HOME'].inspect}"
    ENV['PATH'] = ENV['PATH'].split(/:/).unshift(java_path = "#{ENV['JAVA_HOME']}/bin") * ':'
    warn " *** java binaries are assumed to be in #{java_path.inspect}"
  else
    warn " *** JAVA_HOME was not set or could not be guessed!"
    exit 1
  end

  JRUBY_JAR = File.join(CONFIG["libdir"], "jruby.jar")
  if File.exist?(JRUBY_JAR)
    JAVA_SOURCES.each do |src|
      classpath = (Dir['java/lib/*.jar'] << 'java/src' << JRUBY_JAR) * ':'
      obj = src.sub(/\.java\Z/, '.class')
      file obj => src do
        sh 'javac', '-classpath', classpath, '-source', '1.8', '-target', '1.8', src
      end
      JAVA_CLASSES << obj
    end
  else
    warn "WARNING: Cannot find jruby in path => Cannot build jruby extension!"
  end

  desc "Compiling jruby extension"
  task :compile => [:ragel] + JAVA_CLASSES

  desc "Package the jruby gem"
  task :jruby_gem => :create_jar do
    mkdir_p 'pkg'
    sh "gem build -o pkg/json-#{PKG_VERSION}-java.gem json.gemspec"
  end

  UndocumentedTestTask.new do |t|
    t.name = :test
    t.test_files = FileList['test/json/*_test.rb']
    t.verbose = true
    t.options = '-v'
  end
  desc "Testing library (jruby)"
  task :test => [:create_jar ]

  file JRUBY_PARSER_JAR => :compile do
    cd 'java/src' do
      parser_classes = FileList[
        "json/ext/ByteList*.class",
        "json/ext/OptionsReader*.class",
        "json/ext/Parser*.class",
        "json/ext/RuntimeInfo*.class",
        "json/ext/StringDecoder*.class",
        "json/ext/Utils*.class"
      ]
      sh 'jar', 'cf', File.basename(JRUBY_PARSER_JAR), *parser_classes
      mv File.basename(JRUBY_PARSER_JAR), File.dirname(JRUBY_PARSER_JAR)
    end
  end

  desc "Create parser jar"
  task :create_parser_jar => JRUBY_PARSER_JAR

  file JRUBY_GENERATOR_JAR => :compile do
    cd 'java/src' do
      generator_classes = FileList[
        "json/ext/ByteList*.class",
        "json/ext/OptionsReader*.class",
        "json/ext/Generator*.class",
        "json/ext/RuntimeInfo*.class",
        "json/ext/StringEncoder*.class",
        "json/ext/Utils*.class"
      ]
      sh 'jar', 'cf', File.basename(JRUBY_GENERATOR_JAR), *generator_classes
      mv File.basename(JRUBY_GENERATOR_JAR), File.dirname(JRUBY_GENERATOR_JAR)
    end
  end

  desc "Create generator jar"
  task :create_generator_jar => JRUBY_GENERATOR_JAR

  desc "Create parser and generator jars"
  task :create_jar => [ :create_parser_jar, :create_generator_jar ]

  desc "Build all gems and archives for a new release of the jruby extension."
  task :build => [ :clean, :jruby_gem ]

  task :release => :build
else
  desc "Compiling extension"
  if RUBY_ENGINE == 'truffleruby'
    task :compile => [ :ragel, EXT_PARSER_DL ]
  else
    task :compile => [ :ragel, EXT_PARSER_DL, EXT_GENERATOR_DL ]
  end

  UndocumentedTestTask.new do |t|
    t.name = :test
    t.test_files = FileList['test/json/*_test.rb']
    t.verbose = true
    t.options = '-v'
  end

  desc "Testing library (extension)"
  task :test => [ :compile ]

  begin
    require "ruby_memcheck"
    RubyMemcheck::TestTask.new(valgrind: [ :compile, :test ]) do |t|
      t.test_files = FileList['test/json/*_test.rb']
      t.verbose = true
      t.options = '-v'
    end
  rescue LoadError
  end

  desc "Update the tags file"
  task :tags do
    system 'ctags', *Dir['**/*.{rb,c,h,java}']
  end

  file EXT_PARSER_SRC => RAGEL_PATH do
    cd EXT_PARSER_DIR do
      if RAGEL_CODEGEN == 'ragel'
        sh "ragel parser.rl -G2 -o parser.c"
      else
        sh "ragel -x parser.rl | #{RAGEL_CODEGEN} -G2"
      end
      src = File.read("parser.c").gsub(/[ \t]+$/, '')
      src.gsub!(/^static const int (JSON_.*=.*);$/, 'enum {\1};')
      src.gsub!(/^(static const char) (_JSON(?:_\w+)?_nfa_\w+)(?=\[\] =)/, '\1 MAYBE_UNUSED(\2)')
      src.gsub!(/0 <= ([\( ]+\*[\( ]*p\)+) && \1 <= 31/, "0 <= (signed char)(*(p)) && (*(p)) <= 31")
      src[0, 0] = "/* This file is automatically generated from parser.rl by using ragel */"
      File.open("parser.c", "w") {|f| f.print src}
    end
  end

  desc "Generate diagrams of ragel parser (ps)"
  task :ragel_dot_ps do
    root = 'diagrams'
    specs = []
    File.new(RAGEL_PATH).grep(/^\s*machine\s*(\S+);\s*$/) { specs << $1 }
    for s in specs
      if RAGEL_DOTGEN == 'ragel'
        sh "ragel #{RAGEL_PATH} -S#{s} -p -V | dot -Tps -o#{root}/#{s}.ps"
      else
        sh "ragel -x #{RAGEL_PATH} -S#{s} | #{RAGEL_DOTGEN} -p|dot -Tps -o#{root}/#{s}.ps"
      end
    end
  end

  desc "Generate diagrams of ragel parser (png)"
  task :ragel_dot_png do
    root = 'diagrams'
    specs = []
    File.new(RAGEL_PATH).grep(/^\s*machine\s*(\S+);\s*$/) { specs << $1 }
    for s in specs
      if RAGEL_DOTGEN == 'ragel'
        sh "ragel #{RAGEL_PATH} -S#{s} -p -V | dot -Tpng -o#{root}/#{s}.png"
      else
        sh "ragel -x #{RAGEL_PATH} -S#{s} | #{RAGEL_DOTGEN} -p|dot -Tpng -o#{root}/#{s}.png"
      end
    end
  end

  desc "Generate diagrams of ragel parser"
  task :ragel_dot => [ :ragel_dot_png, :ragel_dot_ps ]

  desc "Create the gem packages"
  task :package do
    sh "gem build json.gemspec"
    mkdir_p 'pkg'
    mv "json-#{PKG_VERSION}.gem", 'pkg'
  end

  desc "Build all gems and archives for a new release of json"
  task :build => [ :clean, :package ]

  task :release => :build
end

desc "Compile in the the source directory"
task :default => [ :clean, :test ]
