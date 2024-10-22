source 'https://rubygems.org'

if ENV['JSON'] == 'pure'
  gemspec name: 'json_pure'
else
  gemspec name: 'json'
end

group :development do
  gem "rake"
  gem "rake-compiler"
  gem "test-unit"
  gem "test-unit-ruby-core"
  gem "all_images", "~> 0" unless RUBY_PLATFORM =~ /java/

  if ENV['BENCHMARK']
    gem "benchmark-ips"
    unless RUBY_PLATFORM =~ /java/
      gem "oj"
      gem "rapidjson"
    end
  end
end
