require "benchmark/ips"
require "json"
require "date"
require "oj"

Oj.default_options = Oj.default_options.merge(mode: :compat)

if ENV["ONLY"]
  RUN = ENV["ONLY"].split(/[,: ]/).map{|x| [x.to_sym, true] }.to_h
  RUN.default = false
elsif ENV["EXCEPT"]
  RUN = ENV["EXCEPT"].split(/[,: ]/).map{|x| [x.to_sym, false] }.to_h
  RUN.default = true
else
  RUN = Hash.new(true)
end

def implementations(ruby_obj)
  state = JSON::State.new(JSON.dump_default_options)
  {
    json: ["json", proc { JSON.generate(ruby_obj) }],
    oj: ["oj", proc { Oj.dump(ruby_obj) }],
  }
end

def benchmark_encoding(benchmark_name, ruby_obj, check_expected: true, except: [])
  json_output = JSON.dump(ruby_obj)
  puts "== Encoding #{benchmark_name} (#{json_output.bytesize} bytes)"

  impls = implementations(ruby_obj).select { |name| RUN[name] }
  except.each { |i| impls.delete(i) }

  Benchmark.ips do |x|
    expected = ::JSON.dump(ruby_obj) if check_expected
    impls.values.each do |name, block|
      begin
        result = block.call
        if check_expected && expected != result
          puts "#{name} does not match expected output. Skipping"
          puts "Expected:" + '-' * 40
          puts expected
          puts "Actual:" + '-' * 40
          puts result
          puts '-' * 40
          next
        end
      rescue => error
        puts "#{name} unsupported (#{error})"
        next
      end
      x.report(name, &block)
    end
    x.compare!(order: :baseline)
  end
  puts
end

benchmark_encoding "long string", (["this is a test of the emergency broadcast system."*5]*500)